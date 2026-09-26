/* Inventário estendido do fork NEI (NEI-003) como unidade de compilação da DLL.
 *
 * O extended_inventory.c é quem sabe quantas páginas existem, qual slot visual corresponde a qual
 * slot real (0..23 vanilla, 24..71 no gNeiSave), qual ícone e qual nome cada item mostra e quais
 * slots estão bloqueados por idade ou por forma. O kaleido extraído (extracted/z_kaleido_item.c)
 * chama tudo isso; antes do NEI-003 essas funções eram stub de "recurso ausente".
 *
 * Fica numa unidade própria, e não colado no player_unit.c, porque o arquivo do fork já é uma TU
 * completa com os próprios includes — o z_player.c do fork nunca o inclui. */
#include "mods/extended_inventory.c"

/* Instrumento de prova do NEI-003. As quatro de baixo existem para o teste em jogo poder encher a
 * página do NEI, esvaziá-la e ler o estado sem depender do get-item do fork, que é o NEI-008.
 * Enchem e leem pelas mesmas funções do fork que o kaleido usa, então o que aparece no menu é o
 * mesmo que aparece aqui. As do fork são `static inline` no header e não têm símbolo próprio. */
void NeiInv_FillNeiPage(void) {
    ExtInv_InitializePage2Items();
}

void NeiInv_ClearNeiPage(void) {
    ExtInv_ClearPage2Items();
}

/* Slots ocupados fora da página vanilla, isto é, de 24 até o fim das páginas do NEI. */
uint32_t NeiInv_OccupiedSlots(void) {
    uint32_t total = 0;
    const int fim = ExtInv_GetMaxPages() * 24;
    for (int slot = 24; slot < fim; ++slot) {
        if (ExtInv_GetSlotItem(slot) != ITEM_NONE) {
            ++total;
        }
    }
    return total;
}

/* Posse na página do NEI, pelo mesmo caminho que o Item_Give do fork usa (ExtInv_SetItemById). A célula do Roc's
 * Feather é progressiva: o Cape a toma, e o Feather dado depois do Cape não volta para ela. Item sem célula
 * própria (0xFF) não tem o que pôr aqui. */
u8 Cane_GiveSkill(u8 skill); /* item_cane_of_somaria.c, no player_unit.c */

void NeiInv_PlaceItem(uint8_t logicalId) {
    const uint8_t slot = ExtInv_GetItemSlot(logicalId);
    if (slot == 0xFF || slot < 24) {
        return;
    }
    /* A Cane é seis skills numa célula só, e a posse é o bit da skill: o Cane_GiveSkill da primeira é que põe a
     * Cane na célula. Sem skill, a Cane na célula não lança nada. */
    if (logicalId == ITEM_CANE_OF_SOMARIA) {
        if (!Nei_CaneOwned()) {
            Cane_GiveSkill(0);
        }
        return;
    }
    const uint16_t current = ExtInv_GetSlotItem(slot);
    if (current == logicalId || (current == ITEM_ROCS_CAPE && logicalId == ITEM_ROCS_FEATHER_SKIJER)) {
        return;
    }
    ExtInv_SetSlotItem(slot, logicalId);
}

/* Cada get-item do registro (callback `received`). É o RG_CANE_OF_SOMARIA do randomizer do fork: cada cópia
 * recebida acende a próxima skill, alternando as duas canes (Statue, Flip, Block, Stone, Platform, Ultrahand). */
void NeiInv_ReceiveItem(uint8_t logicalId) {
    if (logicalId == ITEM_CANE_OF_SOMARIA) {
        static const u8 kCaneOrder[6] = { 0, 3, 1, 4, 2, 5 };
        for (int i = 0; i < 6; i++) {
            if (Cane_GiveSkill(kCaneOrder[i])) {
                break;
            }
        }
        return;
    }
    NeiInv_PlaceItem(logicalId);
}

void NeiInv_RemoveItem(uint8_t logicalId) {
    const uint8_t slot = ExtInv_GetItemSlot(logicalId);
    if (slot != 0xFF && slot >= 24 && ExtInv_GetSlotItem(slot) == logicalId) {
        ExtInv_SetSlotItem(slot, ITEM_NONE);
    }
}

int NeiInv_HasItem(uint8_t logicalId) {
    const uint8_t slot = ExtInv_GetItemSlot(logicalId);
    return slot != 0xFF && slot >= 24 && ExtInv_GetSlotItem(slot) == logicalId;
}

void NeiInv_ReadState(int32_t* pages, int32_t* currentPage, uint32_t* occupied) {
    *pages = ExtInv_GetMaxPages();
    *currentPage = ExtInv_GetCurrentPage();
    *occupied = NeiInv_OccupiedSlots();
}

/* Instrumento de prova das ondas de itens (NEI-008..011): os rods e vários itens do fork gastam magia, e o save de
 * teste é de antes da Grande Fada. Só em memória, pelo mesmo caminho do RG_MAGIC_SINGLE do randomizer; o .sav não
 * muda enquanto o jogo não salvar. Enche também a vida: os inimigos da prova de dano (NeiTest_SpawnEnemy) revidam,
 * e o save de teste tem poucos corações. */
void NeiTest_GrantMagic(void) {
    if (gPlayState == NULL) {
        return;
    }
    gSaveContext.health = gSaveContext.healthCapacity;
    gSaveContext.isMagicAcquired = true;
    gSaveContext.magicFillTarget = MAGIC_NORMAL_METER;
    Magic_Fill(gPlayState);
}

/* Instrumento de prova da onda B (NEI-009): dano, collider e magia precisam de um inimigo perto do Link, e as cenas
 * do save de teste não têm nenhum. Carrega o objeto do inimigo se a cena não o tiver e o põe 120 unidades à frente
 * do Link, virado para ele. kind 0 é a Deku Baba (cabeça alta, para golpe e arremesso), 1 o Tektite vermelho e 2 o
 * Baby Dodongo, rentes ao chão, na altura dos projéteis dos rods; 3 o Wolfos (vida 8: a Kokiri tira 1, a Master 2 e a
 * Biggoron 4, para os upgrades de espada); 4 a estátua de Armos, que o Dominion Rod possui. O pipeline_probe.c lê a
 * vida e a distância ao Link no stats. */
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);

static Actor* sNeiTestTarget = NULL;

int NeiTest_SpawnEnemy(int kind) {
    static const s16 kActors[] = { ACTOR_EN_DEKUBABA, ACTOR_EN_TITE, ACTOR_EN_DODOJR, ACTOR_EN_WF, ACTOR_EN_AM };
    static const s16 kObjects[] = { OBJECT_DEKUBABA, OBJECT_TITE, OBJECT_DODOJR, OBJECT_WF, OBJECT_AM };
    static const s16 kParams[] = { 0, -1 /* TEKTITE_RED */, 0, 0 /* WOLFOS_NORMAL */, 0 /* ARMOS_STATUE */ };
    Player* player;
    s32 bank;
    f32 x;
    f32 z;

    if (gPlayState == NULL) {
        return -1;
    }
    if (kind < 0 || kind >= (int)ARRAY_COUNT(kActors)) {
        return -4;
    }
    player = GET_PLAYER(gPlayState);
    bank = Object_GetIndex(&gPlayState->objectCtx, kObjects[kind]);
    if (bank < 0) {
        bank = Object_Spawn(&gPlayState->objectCtx, kObjects[kind]);
    }
    if (bank < 0) {
        return -2;
    }
    x = player->actor.world.pos.x + Math_SinS(player->actor.shape.rot.y) * 120.0f;
    z = player->actor.world.pos.z + Math_CosS(player->actor.shape.rot.y) * 120.0f;
    sNeiTestTarget = Actor_Spawn(&gPlayState->actorCtx, gPlayState, kActors[kind], x, player->actor.world.pos.y, z, 0,
                                 (s16)(player->actor.shape.rot.y + 0x8000), 0, kParams[kind]);
    return sNeiTestTarget != NULL ? 0 : -3;
}

/* O alvo, se ainda estiver na lista de inimigos do actorCtx; NULL depois que morre e é liberado. */
Actor* NeiTest_Target(void) {
    Actor* actor;

    if (gPlayState == NULL || sNeiTestTarget == NULL) {
        return NULL;
    }
    // A estátua de Armos não fica na categoria de inimigo: procura em todas.
    for (s32 category = 0; category < ACTORCAT_MAX; category++) {
        for (actor = gPlayState->actorCtx.actorLists[category].head; actor != NULL; actor = actor->next) {
            if (actor == sNeiTestTarget) {
                return actor;
            }
        }
    }
    sNeiTestTarget = NULL;
    return NULL;
}

/* Instrumento da onda D (NEI-011): os quatro itens de id u16 do fork (Sheikah Slate, Phantom Hourglass, Shadow
 * Crystal, Rod of Seasons) não entram no registro do linkspan.oot.items, que só conhece ids u8. O fork os equipa
 * pelo marcador ITEM_EXT_BUTTON no botão e pela tabela do kaleido_shim.c; aqui a posse vai pelo mesmo caminho do
 * get-item do fork (a runa ou estação 0 entrega o item) e o id vai para o C esquerdo. Só em memória. */
void ExtButton_SetItem(s32 btn, u16 extId);

int NeiTest_EquipExt(int which) {
    static const u16 kIds[] = { EXT_ITEM_SHEIKAH_SLATE, EXT_ITEM_PHANTOM_HOURGLASS, EXT_ITEM_SHADOW_CRYSTAL,
                                EXT_ITEM_ROD_OF_SEASONS };
    static const u8 kSlots[] = { SLOT_SHEIKAH_SLATE, SLOT_PHANTOM_HOURGLASS, SLOT_SHADOW_CRYSTAL,
                                 SLOT_ROD_OF_SEASONS };

    if (gPlayState == NULL) {
        return -1;
    }
    if (which < 0 || which >= (int)ARRAY_COUNT(kIds)) {
        return -4;
    }
    if (which == 0) {
        Slate_GrantRune(0);
    } else if (which == 3) {
        Seasons_GrantSeason(0);
    } else {
        ExtInv_GiveItem(kSlots[which], kIds[which]);
    }
    ExtButton_SetItem(1, kIds[which]);
    Interface_LoadItemIcon1(gPlayState, 1);
    return 0;
}
