#ifndef LINKSPAN_OOT_RENDER_H
#define LINKSPAN_OOT_RENDER_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_RENDER_SERVICE "linkspan.oot.render"
#define LINKSPAN_OOT_RENDER_VERSION 1u
#define LINKSPAN_OOT_RENDER_MAX_PATH 256u
#define LINKSPAN_OOT_RENDER_MAX_DEPTH 16u

#define LINKSPAN_OOT_RENDER_OPAQUE 0u
#define LINKSPAN_OOT_RENDER_TRANSLUCENT 1u

/* Desenho no mundo com a matriz corrente do jogo, só dentro de um escopo de draw:
 * draw de tipo de ator de mod (linkspan.oot.actors), hook oot.actor.draw e hook
 * oot.player.limb_draw. Fora deles tudo retorna UNSUPPORTED.
 *
 * As operações de matriz aplicam sobre a matriz corrente (a do ator ou do limb).
 * O host desfaz no fim do escopo os push sem pop, até LINKSPAN_OOT_RENDER_MAX_DEPTH.
 * draw_display_list usa um display list do resource manager, caminho sem __OTR__;
 * caminho inexistente retorna FAILURE sem desenhar. */
typedef struct ShipOotRenderV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* draw_display_list)(void* play_state, const char* path, uint8_t layer);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_push)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_pop)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_translate)(float x, float y, float z);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_scale)(float x, float y, float z);
    /* Ângulos binários do jogo (0x8000 = 180 graus), ordem Z, Y, X. */
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_rotate_zyx)(int16_t x, int16_t y, int16_t z);
} ShipOotRenderV1;

#endif
