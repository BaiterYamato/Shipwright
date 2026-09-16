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
 * chame call_original para os demais. */
#define LINKSPAN_OOT_HOOK_PLAY_UPDATE "oot.play.update"
#define LINKSPAN_OOT_HOOK_ACTOR_UPDATE "oot.actor.update"
#define LINKSPAN_OOT_HOOK_ACTOR_DRAW "oot.actor.draw"
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

#endif
