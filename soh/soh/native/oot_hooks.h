#ifndef LINKSPAN_OOT_HOOKS_H
#define LINKSPAN_OOT_HOOKS_H

#include <shiplua/native/ship_native_abi.h>

/* Pontos de hook do host OoT (ABI 1.2, register_hook). Todos na thread do jogo.
 * Ponteiros do payload valem só durante a chamada; converta com os headers do
 * host depois de conferir o layout_id em linkspan.oot.engine.
 *
 * oot.play.update v1: um frame de gameplay (Play_Update). OBSERVE ou REPLACE.
 * oot.actor.update v1: update de um ator vivo, já filtrado pelo culling e pelo
 *   GameInteractor. OBSERVE ou REPLACE; o original chama actor->update.
 * oot.actor.draw v1: draw de um ator, entre as matrizes e segmentos do ator e a
 *   sombra. OBSERVE ou REPLACE; o original chama actor->draw.
 *
 * REPLACE é exclusivo por ponto e vale para todos os atores: filtre por id e
 * chame call_original para os demais.
 *
 * Pontos de save, só OBSERVE, payload ShipOotSaveHookV1 (ver oot_save.h):
 * oot.save.loaded v1: depois de carregar um arquivo; `slot` é o arquivo.
 * oot.save.saving v1: antes do jogo gravar; última chance de escrever blocos.
 * oot.save.deleted v1: arquivo `slot` apagado.
 * oot.save.copied v1: arquivo `other_slot` copiado para `slot`.
 * oot.anchor.state v1: o estado do time do Anchor substituiu namespaces compartilhados (linkspan.oot.anchor) do
 *   arquivo `slot`; releia o que o mod guarda neles.
 *
 * oot.player.limb_draw v1: só OBSERVE, payload ShipOotPlayerLimbHookV1. Um limb do
 *   Link (ou do Dark Link, que usa o mesmo desenho) acabou de ser desenhado; a
 *   matriz corrente é a do limb. É escopo de draw de linkspan.oot.render, e o host
 *   restaura a matriz depois dos hooks. `limb` segue PLAYER_LIMB_* de z64player.h.
 *
 * oot.room.actors v2 (LINKSPAN_OOT_HOOK_ROOM_ACTORS_VERSION): TRANSFORM ou OBSERVE, payload
 *   ShipOotRoomActorsHookV2. A lista
 *   de atores de uma sala acabou de ser lida (comando de cena da sala, antes do
 *   spawn). `entries` é uma cópia do host com `capacity` posições; o TRANSFORM pode
 *   editar, reordenar, remover ou acrescentar entradas e ajustar `count` (até
 *   `capacity`). Os atores nascem na ordem final. Um TRANSFORM que falha precisa
 *   deixar `entries` intacto: o host só restaura os campos do payload. */
#define LINKSPAN_OOT_HOOK_PLAY_UPDATE "oot.play.update"
#define LINKSPAN_OOT_HOOK_ACTOR_INIT "oot.actor.init"
#define LINKSPAN_OOT_HOOK_PLAY_DRAW_END "oot.play.draw_end"
/* OBSERVE somente: ator depois de init completo, inclusive carga tardia de
 * objeto; draw_end depois do desenho normal
 * da cena, no escopo de render. */
#define LINKSPAN_OOT_HOOK_ACTOR_UPDATE "oot.actor.update"
#define LINKSPAN_OOT_HOOK_ACTOR_DRAW "oot.actor.draw"
#define LINKSPAN_OOT_HOOK_SAVE_LOADED "oot.save.loaded"
#define LINKSPAN_OOT_HOOK_SAVE_SAVING "oot.save.saving"
#define LINKSPAN_OOT_HOOK_SAVE_DELETED "oot.save.deleted"
#define LINKSPAN_OOT_HOOK_SAVE_COPIED "oot.save.copied"
#define LINKSPAN_OOT_HOOK_ANCHOR_STATE "oot.anchor.state"
#define LINKSPAN_OOT_HOOK_PLAYER_LIMB_DRAW "oot.player.limb_draw"
#define LINKSPAN_OOT_HOOK_ROOM_ACTORS "oot.room.actors"
#define LINKSPAN_OOT_HOOK_RENDER_ACTOR_DRAW "oot.render.actor_draw"
#define LINKSPAN_OOT_HOOK_RENDER_WORLD_LIGHTS "oot.render.world_lights"
#define LINKSPAN_OOT_HOOK_RENDER_SKY_GRADIENT "oot.render.sky_gradient"
#define LINKSPAN_OOT_HOOK_RENDER_SKY "oot.render.sky"
#define LINKSPAN_OOT_HOOK_RENDER_SKY_CLOUDS "oot.render.sky_clouds"
#define LINKSPAN_OOT_HOOK_RENDER_FILE_SELECT_SKY "oot.render.file_select_sky"
#define LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR "oot.light.point_color"
/* v2 (CEL-004): o campo `light` identifica a luz. A v1, sem identidade, saiu antes do release. */
#define LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR_VERSION 2u
#define LINKSPAN_OOT_HOOK_LIGHT_FAIRY "oot.light.fairy"
#define LINKSPAN_OOT_HOOK_LIGHT_FAIRY_VERSION 1u
/* v2: posições f32 (mundo amplo do OOT-CORE-007). A v1, com posições s16, saiu antes do release. */
#define LINKSPAN_OOT_HOOK_ROOM_ACTORS_VERSION 2u
/* Máximo de entradas por sala depois do TRANSFORM (teto de atores vivos). */
#define LINKSPAN_OOT_ROOM_ACTORS_MAX 8192u
#define LINKSPAN_OOT_HOOKS_VERSION 1u

typedef struct ShipOotPlayHookV1 {
    uint32_t size;
    void* play_state;
} ShipOotPlayHookV1;

typedef struct ShipOotActorHookV1 {
    uint32_t size;
    void* play_state;
    void* actor;
    int16_t actor_id;
    int16_t params;
} ShipOotActorHookV1;

typedef struct ShipOotSaveHookV1 {
    uint32_t size;
    int32_t slot;
    int32_t other_slot;
} ShipOotSaveHookV1;

typedef struct ShipOotPlayerLimbHookV1 {
    uint32_t size;
    void* play_state;
    void* actor;
    int32_t limb;
} ShipOotPlayerLimbHookV1;

/* Hooks de render CEL-003. São OBSERVE e cada payload vale somente no callback.
 * actor_draw acontece antes do draw real do ator, dentro do escopo de
 * linkspan.oot.render. Os quatro hooks de Play_Draw também possuem esse escopo. */
typedef struct ShipOotRenderActorDrawHookV1 {
    uint32_t size;
    void* play_state;
    void* actor;
    int16_t actor_id;
    int16_t params;
} ShipOotRenderActorDrawHookV1;

typedef struct ShipOotRenderPlayHookV1 {
    uint32_t size;
    void* play_state;
} ShipOotRenderPlayHookV1;

typedef struct ShipOotRenderFileSelectSkyHookV1 {
    uint32_t size;
    void* game_state;
    void* graphics_context;
    void* view;
} ShipOotRenderFileSelectSkyHookV1;

/* TRANSFORM: r/g/b são o resultado que Lights_PointSetColorAndRadius grava.
 * `light` é a identidade da luz: o LightInfo do jogo, o mesmo ponteiro de node->info na lista play->lightCtx,
 * estável enquanto a luz existir (chave de estado por luz, como o flicker de chama). Só compare; posição, raio e
 * tipo são de leitura. */
typedef struct ShipOotPointLightColorHookV2 {
    uint32_t size;
    float x;
    float y;
    float z;
    int16_t radius;
    uint8_t type;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    const void* light;
} ShipOotPointLightColorHookV2;

/* oot.light.fairy v1: TRANSFORM ou OBSERVE, uma vez por update de toda fada (Navi, fadas de cura, da floresta
 * Kokiri, de garrafa). A Navi despacha no fim de EnElf_UpdateLights, porque o update dela não é o EnElf_Update
 * (func_80A053F0 e vizinhos); as outras, no fim de EnElf_Update. Cada fada tem duas luzes pontuais, sem brilho e
 * com brilho. O host copia as duas para o payload, chama os hooks e grava de volta posição, raio e cor; `light` e
 * `type` são de leitura (compare `light` com node->info da lista play->lightCtx). A cor pode trazer o que um hook
 * gravou num frame anterior, porque o ator só a refaz quando atualiza as luzes: grave valores absolutos. */
#define LINKSPAN_OOT_FAIRY_NAVI 0
#define LINKSPAN_OOT_FAIRY_REVIVE_BOTTLE 1
#define LINKSPAN_OOT_FAIRY_HEAL_TIMED 2
#define LINKSPAN_OOT_FAIRY_KOKIRI 3
#define LINKSPAN_OOT_FAIRY_SPAWNER 4
#define LINKSPAN_OOT_FAIRY_REVIVE_DEATH 5
#define LINKSPAN_OOT_FAIRY_HEAL 6
#define LINKSPAN_OOT_FAIRY_HEAL_BIG 7
/* Bit de fairy_flags da fada grande (FAIRY_FLAG_BIG de z_en_elf.c). */
#define LINKSPAN_OOT_FAIRY_FLAG_BIG 0x0200u

typedef struct ShipOotFairyLightV1 {
    const void* light;
    float position[3];
    int16_t radius;
    uint8_t color[3];
    uint8_t type;
} ShipOotFairyLightV1;

typedef struct ShipOotFairyLightHookV1 {
    uint32_t size;
    void* play_state;
    void* actor;
    int16_t actor_id;
    /* Tipo da fada (LINKSPAN_OOT_FAIRY_*), o params do ator. */
    int16_t params;
    uint16_t fairy_flags;
    uint16_t reserved;
    /* Cor da aura e da mira (EnElf::outerColor), 0..255. */
    float outer_color[3];
    ShipOotFairyLightV1 no_glow;
    ShipOotFairyLightV1 glow;
} ShipOotFairyLightHookV1;

/* Igual ao ActorEntry do jogo: `pos` em unidades do mundo (f32), `rot` em unidade
 * binária de ângulo e `params` no formato do tipo de ator. */
typedef struct ShipOotActorEntryV2 {
    int16_t id;
    float pos[3];
    int16_t rot[3];
    int16_t params;
} ShipOotActorEntryV2;

typedef struct ShipOotRoomActorsHookV2 {
    uint32_t size;
    void* play_state;
    int32_t scene_id;
    int32_t room;
    /* Camada da cena (gSaveContext.sceneLayer): 0 criança dia, 1 criança noite... */
    int32_t layer;
    /* Recurso da sala, sem __OTR__ ("scenes/shared/spot00_scene/spot00_room_0"). */
    const char* room_path;
    ShipOotActorEntryV2* entries;
    uint32_t count;
    uint32_t capacity;
} ShipOotRoomActorsHookV2;

/* HOST-SLIM-001 (RFC 0029). Versão 1 (LINKSPAN_OOT_HOOKS_VERSION), thread do jogo.
 *
 * oot.player.voice_sfx: TRANSFORM ou OBSERVE, ShipOotPlayerVoiceHookV1. Player_PlayVoiceSfx vai tocar `sfx` (antes
 *   do ajuste de idade). O TRANSFORM troca `sfx` ou põe `consumed = 1` para o jogo não tocar nada.
 * oot.player.roll_allowed, oot.player.ledge_grab_allowed: TRANSFORM ou OBSERVE, ShipOotPlayerGateHookV1. O Link vai
 *   rolar ou agarrar a borda. `allowed = 0` veta; o host faz E com o valor que entrou, então um hook só veta o jogo.
 *   Cada TRANSFORM vê o resultado do anterior: escreva só 0, porque gravar 1 desfaz o veto de um hook anterior.
 *   roll_allowed cobre a rolagem do A em movimento (Player_TryRoll), como o veto legado; a do Z-target e a da
 *   aterrissagem chamam Player_SetupRoll direto e não passam pelo ponto.
 * oot.player.action: só OBSERVE, ShipOotPlayerActionHookV1, em volta da função de ação do Link no Player_Update.
 *   BEFORE vê `executed = 0`; AFTER vê se a ação rodou. Tomar a ação é do linkspan.oot.movement V3.
 * oot.player.update_end: só OBSERVE, ShipOotPlayerTickHookV1, no fim do Player_Update.
 * oot.player.mask_draw: REPLACE ou OBSERVE, ShipOotPlayerMaskDrawHookV1, no lugar do desenho da máscara, com a
 *   matriz e os segmentos do desenho do jogo e no escopo de render de linkspan.oot.render. call_original desenha a
 *   máscara do jogo (só se `vanilla_allowed`); um REPLACE que não chama omite a máscara ou desenha outra.
 * oot.play.destroy: só OBSERVE, ShipOotPlayHookV1, no começo do Play_Destroy, com o PlayState ainda válido. */

#define LINKSPAN_OOT_HOOK_PLAYER_VOICE_SFX "oot.player.voice_sfx"
#define LINKSPAN_OOT_HOOK_PLAYER_ROLL_ALLOWED "oot.player.roll_allowed"
#define LINKSPAN_OOT_HOOK_PLAYER_LEDGE_GRAB_ALLOWED "oot.player.ledge_grab_allowed"
#define LINKSPAN_OOT_HOOK_PLAYER_ACTION "oot.player.action"
#define LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END "oot.player.update_end"
#define LINKSPAN_OOT_HOOK_PLAYER_MASK_DRAW "oot.player.mask_draw"
#define LINKSPAN_OOT_HOOK_PLAY_DESTROY "oot.play.destroy"

typedef struct ShipOotPlayerVoiceHookV1 {
    uint32_t size;
    void* play_state;
    void* player;
    uint16_t requested_sfx; /* o que o jogo pediu; só leitura */
    uint16_t sfx;           /* o que vai tocar */
    uint8_t consumed;       /* 1 = o mod tratou; o jogo não toca */
    uint8_t reserved[3];
} ShipOotPlayerVoiceHookV1;

typedef struct ShipOotPlayerGateHookV1 {
    uint32_t size;
    void* play_state;
    void* player;
    uint8_t allowed;
    uint8_t reserved[3];
} ShipOotPlayerGateHookV1;

typedef struct ShipOotPlayerActionHookV1 {
    uint32_t size;
    void* play_state;
    void* player;
    void* input;
    uint8_t vanilla_allowed; /* o jogo ia rodar a ação (sem PAUSE_ACTION_FUNC e sem veto do GameInteractor) */
    uint8_t executed;        /* AFTER: a função de ação rodou */
    uint8_t reserved[2];
} ShipOotPlayerActionHookV1;

typedef struct ShipOotPlayerTickHookV1 {
    uint32_t size;
    void* play_state;
    void* player;
} ShipOotPlayerTickHookV1;

typedef struct ShipOotPlayerMaskDrawHookV1 {
    uint32_t size;
    void* play_state;
    void* player;
    int32_t mask;            /* PLAYER_MASK_* de z64player.h */
    uint8_t vanilla_allowed; /* VB_DRAW_PLAYER_MASK do GameInteractor */
    uint8_t reserved[3];
} ShipOotPlayerMaskDrawHookV1;

#endif
