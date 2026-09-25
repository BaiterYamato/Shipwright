/* Atores do fork NEI dentro da DLL (NEI-006).
 *
 * O fork não tem tipo de ator próprio: cria um ator vanilla de "portador" (EnLightbox, ObjLift, EnHorse...) e
 * troca update/draw/destroy por funções dele, ou troca essas funções num inimigo vivo (Stasis, Ultrahand).
 * Dentro da DLL isso deixa ponteiros para a imagem da DLL em atores que o jogo gerencia, e o fork decide se um
 * ator guardado ainda existe lendo `ator->update == NULL` — o que, depois de o ator ser liberado, lê memória
 * liberada.
 *
 * O sync.py reescreve o código do fork que entra na DLL para passar por aqui:
 *   - `x->update = f;` (e draw/destroy) vira NeiActor_SetUpdate(x, f): a tabela guarda o ator, o id e o ponteiro
 *     original de cada campo antes da primeira troca;
 *   - `Actor_Spawn(...)` vira NeiActor_Spawned(Actor_Spawn(...)): o ator entra na tabela como criado pelo fork;
 *   - `p->update == NULL` vira !NeiActor_IsAlive(p): o ponteiro só é lido depois de achado nas listas do
 *     actorCtx.
 *
 * A cada frame (NeiActor_Frame) sai da tabela quem não está mais nas listas; os contadores provam que criar e
 * destruir não vaza. No descarregamento (NeiActor_Unload), todo ator com ponteiro para dentro da DLL volta ao
 * ponteiro original, e o que o fork criou morre — depois disso nenhum ator aponta para a DLL. */
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include <stdio.h>
#include <string.h>

#define NEI_ACTOR_SLOTS 3 /* update, draw, destroy */
#define NEI_ACTOR_TABLE 512

typedef struct {
    Actor* actor;
    s16 id;
    u8 used;
    u8 spawned;
    ActorFunc original[NEI_ACTOR_SLOTS];
} NeiTrackedActor;

static NeiTrackedActor sTable[NEI_ACTOR_TABLE];
static u32 sCount;
static u32 sSpawned;   /* criados pelo fork */
static u32 sOverrides; /* atores de outro dono com função trocada pelo fork */
static u32 sReleased;  /* saíram da tabela porque o jogo liberou o ator */
static u32 sDropped;   /* tabela cheia */
static u32 sLastFrames;
static u32 sUnloadRestored;
static u32 sUnloadKilled;

/* A imagem da DLL, pelo cabeçalho PE que o linker põe em __ImageBase. */
extern const unsigned char __ImageBase[];

static int NeiActor_InModule(const void* fn) {
    const unsigned char* base = __ImageBase;
    const u32 ntOffset = *(const u32*)(base + 0x3C);
    const u32 imageSize = *(const u32*)(base + ntOffset + 0x50); /* OptionalHeader.SizeOfImage (PE32+) */
    const unsigned char* p = (const unsigned char*)fn;
    return fn != NULL && p >= base && p < base + imageSize;
}

static ActorFunc* NeiActor_Field(Actor* actor, int slot) {
    return slot == 0 ? &actor->update : slot == 1 ? &actor->draw : &actor->destroy;
}

static int NeiActor_InLists(const Actor* actor) {
    if (actor == NULL || gPlayState == NULL) {
        return 0;
    }
    for (s32 cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* it = gPlayState->actorCtx.actorLists[cat].head; it != NULL; it = it->next) {
            if (it == actor) {
                return 1;
            }
        }
    }
    return 0;
}

int NeiActor_IsAlive(const Actor* actor) {
    if (actor == NULL) {
        return 0;
    }
    if (gPlayState == NULL) {
        return 0;
    }
    return NeiActor_InLists(actor) && actor->update != NULL;
}

static NeiTrackedActor* NeiActor_Find(const Actor* actor) {
    for (u32 i = 0; i < NEI_ACTOR_TABLE; i++) {
        if (sTable[i].used && sTable[i].actor == actor) {
            return &sTable[i];
        }
    }
    return NULL;
}

static NeiTrackedActor* NeiActor_Track(Actor* actor, u8 spawned) {
    NeiTrackedActor* entry = NeiActor_Find(actor);
    if (entry != NULL && entry->id != actor->id) {
        /* Mesmo endereço, outro ator: o anterior foi liberado entre dois frames. */
        entry->used = 0;
        sCount--;
        sReleased++;
        entry = NULL;
    }
    if (entry != NULL) {
        return entry;
    }
    for (u32 i = 0; i < NEI_ACTOR_TABLE; i++) {
        if (!sTable[i].used) {
            entry = &sTable[i];
            entry->actor = actor;
            entry->id = actor->id;
            entry->used = 1;
            entry->spawned = spawned;
            for (int slot = 0; slot < NEI_ACTOR_SLOTS; slot++) {
                entry->original[slot] = *NeiActor_Field(actor, slot);
            }
            sCount++;
            if (!spawned) {
                sOverrides++;
            }
            return entry;
        }
    }
    sDropped++;
    return NULL;
}

Actor* NeiActor_Spawned(Actor* actor) {
    if (actor != NULL) {
        sSpawned++;
        NeiActor_Track(actor, 1);
    }
    return actor;
}

static void NeiActor_Set(Actor* actor, int slot, ActorFunc fn) {
    if (actor == NULL) {
        return;
    }
    if (NeiActor_InModule((const void*)fn) || NeiActor_InModule((const void*)*NeiActor_Field(actor, slot))) {
        NeiActor_Track(actor, 0);
    }
    *NeiActor_Field(actor, slot) = fn;
}

void NeiActor_SetUpdate(Actor* actor, ActorFunc fn) {
    NeiActor_Set(actor, 0, fn);
}

void NeiActor_SetDraw(Actor* actor, ActorFunc fn) {
    NeiActor_Set(actor, 1, fn);
}

void NeiActor_SetDestroy(Actor* actor, ActorFunc fn) {
    NeiActor_Set(actor, 2, fn);
}

static void NeiActor_Clear(void) {
    for (u32 i = 0; i < NEI_ACTOR_TABLE; i++) {
        if (sTable[i].used) {
            sTable[i].used = 0;
            sReleased++;
        }
    }
    sCount = 0;
}

/* Chamado a cada frame de gameplay, antes do update do fork. */
void NeiActor_Frame(void) {
    if (gPlayState == NULL) {
        NeiActor_Clear();
        return;
    }
    /* PlayState novo (troca de cena, reset): os atores antigos já foram destruídos pelo Play_Destroy. */
    const u32 frames = gPlayState->gameplayFrames;
    if (frames < sLastFrames) {
        NeiActor_Clear();
    }
    sLastFrames = frames;
    for (u32 i = 0; i < NEI_ACTOR_TABLE; i++) {
        NeiTrackedActor* entry = &sTable[i];
        if (entry->used && (!NeiActor_InLists(entry->actor) || entry->actor->id != entry->id)) {
            entry->used = 0;
            sCount--;
            sReleased++;
        }
    }
}

/* Descarregamento da DLL: nenhum ator do jogo pode continuar apontando para ela. Com `dryRun`, só conta o que
 * seria restaurado ou morto (prova em jogo: a DLL só é descarregada ao fechar o jogo). */
void NeiActor_Unload(int dryRun) {
    sUnloadRestored = sUnloadKilled = 0;
    if (gPlayState == NULL) {
        if (!dryRun) {
            NeiActor_Clear();
        }
        return;
    }
    for (s32 cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* actor = gPlayState->actorCtx.actorLists[cat].head; actor != NULL; actor = actor->next) {
            NeiTrackedActor* entry = NeiActor_Find(actor);
            if (entry != NULL && entry->id != actor->id) {
                entry = NULL;
            }
            int foreign = 0;
            for (int slot = 0; slot < NEI_ACTOR_SLOTS; slot++) {
                ActorFunc* field = NeiActor_Field(actor, slot);
                if (!NeiActor_InModule((const void*)*field)) {
                    continue;
                }
                foreign = 1;
                if (dryRun) {
                    continue;
                }
                if (entry != NULL && !NeiActor_InModule((const void*)entry->original[slot])) {
                    *field = entry->original[slot];
                } else {
                    *field = NULL;
                }
            }
            if (!foreign) {
                continue;
            }
            if (cat != ACTORCAT_PLAYER && (entry == NULL || entry->spawned)) {
                if (!dryRun) {
                    Actor_Kill(actor);
                }
                sUnloadKilled++;
            } else {
                sUnloadRestored++;
            }
        }
    }
    if (!dryRun) {
        NeiActor_Clear();
    }
}

/* "vivos=N criados=M liberados=K sobrepostos=S" para o stats do coremod. */
int NeiActor_Stats(char* out, int capacity) {
    return snprintf(out, (size_t)capacity, "vivos=%u criados=%u liberados=%u sobrepostos=%u%s", sCount, sSpawned,
                    sReleased, sOverrides, sDropped ? " TABELA-CHEIA" : "");
}

int NeiActor_UnloadStats(char* out, int capacity) {
    return snprintf(out, (size_t)capacity, "restaurados=%u mortos=%u", sUnloadRestored, sUnloadKilled);
}

/* Instrumento de prova do NEI-006: cria uma de cada invocação da Cane of Somaria na frente do Link, ou mata
 * todas, pelas mesmas funções do fork que o item usa. */
#include "mods/actors/somaria_cubes.h"

int NeiActor_TestSomaria(int spawn) {
    if (gPlayState == NULL || GET_PLAYER(gPlayState) == NULL) {
        return -1;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (!spawn) {
        CaneSummon_KillAll(gPlayState);
        return 0;
    }
    int created = 0;
    for (int kind = 0; kind < CANE_SUMMON_MAX; kind++) {
        Vec3f pos = player->actor.world.pos;
        const f32 distance = 90.0f + 70.0f * kind;
        pos.x += Math_SinS(player->actor.shape.rot.y) * distance;
        pos.z += Math_CosS(player->actor.shape.rot.y) * distance;
        if (CaneSummon_Spawn(gPlayState, (CaneSummonKind)kind, &pos, player->actor.shape.rot.y) != NULL) {
            created++;
        }
    }
    return created;
}
