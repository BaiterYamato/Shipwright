#ifndef LINKSPAN_OOT_RENDER_H
#define LINKSPAN_OOT_RENDER_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_RENDER_SERVICE "linkspan.oot.render"
#define LINKSPAN_OOT_RENDER_VERSION 1u
#define LINKSPAN_OOT_RENDER_VERSION_2 2u
#define LINKSPAN_OOT_RENDER_VERSION_3 3u
#define LINKSPAN_OOT_RENDER_MAX_PATH 256u
#define LINKSPAN_OOT_RENDER_MAX_DEPTH 16u
/* Filhos de interpolação abertos por escopo de draw (V3). */
#define LINKSPAN_OOT_RENDER_MAX_INTERPOLATION 16u

#define LINKSPAN_OOT_RENDER_OPAQUE 0u
#define LINKSPAN_OOT_RENDER_TRANSLUCENT 1u

#define LINKSPAN_OOT_RENDER_FEATURE_TOON_ACTORS 0x00000001u
#define LINKSPAN_OOT_RENDER_FEATURE_SUPPRESS_VANILLA_SHADOWS 0x00000002u
#define LINKSPAN_OOT_RENDER_FEATURE_HIDE_VANILLA_POINT_GLOW 0x00000004u
#define LINKSPAN_OOT_RENDER_MAX_SHADOW_RECEIVERS 256u

/* Nomes estáveis das chaves; V2 as recebe como a máscara FEATURE equivalente. */
#define LINKSPAN_OOT_RENDER_KEY_TOON_ACTORS "toon_actors"
#define LINKSPAN_OOT_RENDER_KEY_SUPPRESS_VANILLA_SHADOWS "suppress_vanilla_shadows"
#define LINKSPAN_OOT_RENDER_KEY_HIDE_VANILLA_POINT_GLOW "hide_vanilla_point_glow"

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

/* V2 conserva V1 no início. Um estado pertence a um mod, é removido no unload
 * pelo host e pode ligar três chaves genéricas. `receiver_actor_ids` é uma lista
 * de ids de ator, sem whitelist do jogo: o host faz a união dos estados ativos.
 * Todas as emissões exigem um escopo de draw ativo e `layer` OPA ou XLU. */
typedef struct ShipOotRenderV2 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* draw_display_list)(void* play_state, const char* path, uint8_t layer);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_push)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_pop)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_translate)(float x, float y, float z);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_scale)(float x, float y, float z);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_rotate_zyx)(int16_t x, int16_t y, int16_t z);
    ShipNativeStatus(SHIP_NATIVE_CALL* acquire_state)(const char* owner, uint64_t* state);
    ShipNativeStatus(SHIP_NATIVE_CALL* release_state)(uint64_t state);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_state)(uint64_t state, uint32_t features,
                                                   const int16_t* receiver_actor_ids, uint32_t receiver_count);
    ShipNativeStatus(SHIP_NATIVE_CALL* emit_toon_key)(uint8_t layer, int8_t dx, int8_t dy, int8_t dz,
                                                       uint8_t r, uint8_t g, uint8_t b);
    ShipNativeStatus(SHIP_NATIVE_CALL* emit_stencil)(uint8_t layer, uint8_t mode);
    ShipNativeStatus(SHIP_NATIVE_CALL* emit_toon_shadow)(uint8_t layer, int16_t feet_clamp_y, float size);
    ShipNativeStatus(SHIP_NATIVE_CALL* flush_toon_shadows)(uint8_t layer);
    /* Ajuste global do visual do transporte, sem política: rampa do toon (centro, suavidade, intensidade do
     * lado iluminado e da sombra; debug_bands desenha branco/preto por lado da rampa) e aparência da sombra de
     * ator (opacidade, elevação mínima da luz, faixa do volume abaixo/acima dos pés, anéis de penumbra 0..2,
     * volume visível). Último escritor vale; o host volta ao padrão do renderer quando o último estado de
     * render é liberado. Fora do escopo de draw, na thread do jogo. */
    ShipNativeStatus(SHIP_NATIVE_CALL* set_toon_ramp)(float center, float softness, float highlight, float shadow,
                                                       uint8_t debug_bands);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_toon_shadow_params)(float opacity, float min_elevation, float slab_depth,
                                                                float slab_rise, int32_t edge_softness,
                                                                uint8_t show_volume);
} ShipOotRenderV2;

/* Dados do frame para animação e geometria própria. `frame` conta os frames do game state atual (recomeça na
 * troca de cena); `camera_epoch` muda num corte de câmera, e um filho de interpolação com esse número não
 * interpola através do corte; `delta_seconds` é o tempo de jogo de um frame (R_UPDATE_RATE / 60, 0,05 s a 20 fps
 * de jogo); `aspect_ratio` é o da janela. */
typedef struct ShipOotRenderFrameInfoV1 {
    uint32_t size;
    uint32_t frame;
    uint32_t camera_epoch;
    float delta_seconds;
    float aspect_ratio;
} ShipOotRenderFrameInfoV1;

/* V3 conserva V2 no início e acrescenta desenho de geometria própria, com a matriz e a interpolação do jogo.
 *
 * - `set_actor_toon_enabled`: só no hook oot.render.actor_draw. Com 0, o ator sai do colchete toon do laço de
 *   atores e desenha com a luz vanilla; o host religa o colchete antes do próximo ator. Sem colchete neste frame
 *   (nenhum estado com FEATURE_TOON_ACTORS) não faz nada. Toda borda do colchete invalida a chave toon: emita
 *   `emit_toon_key` em cada ator em vez de repetir a última.
 * - `matrix_translate_new` troca a matriz corrente por uma translação (o MTXMODE_NEW do jogo) e exige um
 *   `matrix_push` do mod no escopo, para a matriz do host voltar no fim dele. `matrix_rotate_axis` gira em volta
 *   de um eixo qualquer (normalizado pelo host), em radianos.
 * - `export_current_matrix` grava a matriz corrente num Mtx do frame (Matrix_NewMtx, que também a registra para a
 *   interpolação) e devolve o ponteiro para um gSPMatrix da lista do mod.
 * - `draw_native_display_list` chama, na camada OPA ou XLU, uma display list montada pelo mod com os tipos Gfx e
 *   Vtx do layout. Use os macros puros (__gSPVertex, __gSPDisplayList, gDP*, gSPMatrix): gSPVertex e
 *   gSPDisplayList são funções do host. A lista termina em gSPEndDisplayList, precisa valer até o fim do frame e
 *   deixa mudado o estado de RDP que mudar, como o Gfx_SetupDL do jogo.
 * - `get_frame_info` vale fora de escopo, na thread do jogo.
 * - `interpolation_begin`/`interpolation_end` abrem e fecham um filho de interpolação de frame
 *   (FrameInterpolation_RecordOpenChild/CloseChild). `key` só identifica o filho entre frames: o endereço de um
 *   objeto do mod, ou nulo com `child` = camera_epoch para uma geometria presa à câmera. O host fecha no fim do
 *   escopo o que o mod deixar aberto.
 *
 * Tudo exige escopo de draw, exceto `get_frame_info`. */
typedef struct ShipOotRenderV3 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* draw_display_list)(void* play_state, const char* path, uint8_t layer);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_push)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_pop)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_translate)(float x, float y, float z);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_scale)(float x, float y, float z);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_rotate_zyx)(int16_t x, int16_t y, int16_t z);
    ShipNativeStatus(SHIP_NATIVE_CALL* acquire_state)(const char* owner, uint64_t* state);
    ShipNativeStatus(SHIP_NATIVE_CALL* release_state)(uint64_t state);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_state)(uint64_t state, uint32_t features,
                                                   const int16_t* receiver_actor_ids, uint32_t receiver_count);
    ShipNativeStatus(SHIP_NATIVE_CALL* emit_toon_key)(uint8_t layer, int8_t dx, int8_t dy, int8_t dz,
                                                       uint8_t r, uint8_t g, uint8_t b);
    ShipNativeStatus(SHIP_NATIVE_CALL* emit_stencil)(uint8_t layer, uint8_t mode);
    ShipNativeStatus(SHIP_NATIVE_CALL* emit_toon_shadow)(uint8_t layer, int16_t feet_clamp_y, float size);
    ShipNativeStatus(SHIP_NATIVE_CALL* flush_toon_shadows)(uint8_t layer);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_toon_ramp)(float center, float softness, float highlight, float shadow,
                                                       uint8_t debug_bands);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_toon_shadow_params)(float opacity, float min_elevation, float slab_depth,
                                                                float slab_rise, int32_t edge_softness,
                                                                uint8_t show_volume);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_actor_toon_enabled)(uint8_t enabled);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_translate_new)(float x, float y, float z);
    ShipNativeStatus(SHIP_NATIVE_CALL* matrix_rotate_axis)(float radians, float x, float y, float z);
    ShipNativeStatus(SHIP_NATIVE_CALL* export_current_matrix)(const void** mtx);
    ShipNativeStatus(SHIP_NATIVE_CALL* draw_native_display_list)(const void* display_list, uint8_t layer);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_frame_info)(ShipOotRenderFrameInfoV1* info);
    ShipNativeStatus(SHIP_NATIVE_CALL* interpolation_begin)(const void* key, int32_t child);
    ShipNativeStatus(SHIP_NATIVE_CALL* interpolation_end)(void);
} ShipOotRenderV3;

#endif
