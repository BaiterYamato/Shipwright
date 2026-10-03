// Ajustes à mão do fork NEI ao host Link-Span. Incluído por /FI depois do nei_compat.h.
#pragma once

extern PlayState* gPlayState;

// O fork pôs lensFromLantern no primeiro byte do padding unk_04 do ActorContext; o host mantém o padding.
#define lensFromLantern unk_04[0]

// A tabela de BgActors do host cresce sob demanda (OOT-CORE-007); o fork percorre o tamanho fixo de 50.
#define BG_ACTOR_MAX (gPlayState->colCtx.dyna.bgActorMax)

// Nomes que o upstream 78dc6d970 trocou (mesma assinatura).
#define Audio_PlaySoundGeneral Audio_PlaySfxGeneral
#define Camera_ChangeMode Camera_RequestMode
#define EffectBlure_ChangeType EffectBlureShip_ChangeType

// resource_guard.c: o fork pôs guarda de nulo nesta função no host dele e testa existência de recurso com ela; a do
// host derruba o jogo quando o recurso falta.
#define ResourceMgr_LoadTexOrDListByName NeiResource_LoadTexOrDListByName
// O cache de existência do host é criado antes da montagem dos .o2r do NEI.
// Consulte o VFS atual para modelos, animações e texturas do fork.
#define ResourceMgr_FileExists NeiResource_FileExists

// resource_guard.c + text.cpp: o fork monta alguns textos por hook C++ na hora de abrir a caixa (Time Gate, Lantern,
// Pictobox); o desvio grava o texto do id no host antes de abrir.
#define Message_StartTextbox NeiMessage_StartTextbox
#define Message_ContinueTextbox NeiMessage_ContinueTextbox
struct Actor;
void NeiMessage_StartTextbox(PlayState* play, u16 textId, struct Actor* actor);
void NeiMessage_ContinueTextbox(PlayState* play, u16 textId);

// fork_items.c: id runtime do linkspan.oot.items no botão -> id do item no fork (patch 0002).
u8 NeiFork_ToLogicalItem(u8 runtimeId);
u8 NeiFork_BButtonItem(u8 storedId);
// fork_items.c: o contrário, para o kaleido do fork gravar no botão C o id que o host entende (extracted-fixes.txt).
u8 NeiFork_ToRuntimeItem(u16 logicalId);
u16 NeiFork_ExtendedItem(u8 runtimeId);
// fork_items.c: id lógico de item do fork registrado (fica fora dos hooks de item de mod do host).
u8 NeiFork_IsForkItem(s32 item);
u8 NeiFork_IsForkButtonItem(s32 item, s32 button);
u8 NeiFork_IsForeignRuntimeButton(s32 button);

// sItemActions, sItemActionUpdateFuncs e sItemActionInitFuncs, que o extended_player.c do fork declara extern, são
// as tabelas do próprio fork copiadas para a DLL (overlay.py, NEI-HOST-001): no host são static, e as funções do host
// que as leem também foram copiadas e desviadas. O sActionModelGroups é global no host e fica o dele.
#define sActionModelGroups (*nei_host_sActionModelGroups)

// actor_guard.c (NEI-006): o sync.py reescreve as trocas de update/draw/destroy, os Actor_Spawn e os testes
// `p->update == NULL` do código do fork para passar por aqui.
struct Actor;
struct Actor* NeiActor_Spawned(struct Actor* actor);
void NeiActor_SetUpdate(struct Actor* actor, void (*fn)(struct Actor*, struct PlayState*));
void NeiActor_SetDraw(struct Actor* actor, void (*fn)(struct Actor*, struct PlayState*));
void NeiActor_SetDestroy(struct Actor* actor, void (*fn)(struct Actor*, struct PlayState*));
int NeiActor_IsAlive(const struct Actor* actor);
