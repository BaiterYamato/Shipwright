#ifndef LINKSPAN_OOT_LIGHTS_H
#define LINKSPAN_OOT_LIGHTS_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_LIGHTS_SERVICE "linkspan.oot.lights"
#define LINKSPAN_OOT_LIGHTS_VERSION 1u
/* Luzes de mod ao mesmo tempo, somando todos os mods. Cada uma ocupa também uma das 32 vagas do buffer de luzes
 * do jogo, que os atores (tochas, fadas, fogo) dividem. */
#define LINKSPAN_OOT_LIGHTS_MAX 8u
#define LINKSPAN_OOT_LIGHTS_MAX_OWNER 128u

/* Luz pontual na semântica de Lights_PointSetInfo (z_lights.c): posição em unidades do mundo, raio (0 apaga),
 * cor e `glow` = 0 para LIGHT_POINT_NOGLOW ou 1 para LIGHT_POINT_GLOW (o brilho vanilla desenhado no ponto). */
typedef struct ShipOotPointLightV1 {
    uint32_t size;
    float position[3];
    int16_t radius;
    uint8_t color[3];
    uint8_t glow;
} ShipOotPointLightV1;

/* Luzes pontuais de um mod na cena atual, com LightInfo e nó do host na lista play->lightCtx. Na thread do jogo.
 *
 * - `create_point_light` só vale no gameplay (UNSUPPORTED fora dele) e devolve LIMIT sem vaga, seja no teto do
 *   host, seja no buffer de luzes do jogo.
 * - `update_point_light` passa pelo mesmo caminho do jogo (Lights_PointSetInfo), então o hook
 *   oot.light.point_color vê a luz como vê uma tocha.
 * - A troca de cena e o unload do mod apagam as luzes. O handle antigo passa a dar UNSUPPORTED no update, e o mod
 *   cria outra.
 * - `get_point_light_info` devolve a identidade da luz, o LightInfo que o host inseriu (o mesmo ponteiro de
 *   node->info na lista e do campo `light` de oot.light.point_color), para o mod reconhecer a própria luz. Só
 *   compare; o host escreve nela. */
typedef struct ShipOotLightsV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* create_point_light)(const char* owner, const ShipOotPointLightV1* light,
                                                            uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* update_point_light)(uint64_t handle, const ShipOotPointLightV1* light);
    ShipNativeStatus(SHIP_NATIVE_CALL* destroy_point_light)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_point_light_info)(uint64_t handle, const void** light_info);
} ShipOotLightsV1;

#endif
