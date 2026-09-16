#ifndef LINKSPAN_OOT_WORLD_H
#define LINKSPAN_OOT_WORLD_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_WORLD_SERVICE "linkspan.oot.world"
#define LINKSPAN_OOT_WORLD_VERSION 1u

/* Superfícies aceitas por line_test. */
#define LINKSPAN_OOT_WORLD_LINE_WALL (1u << 0)
#define LINKSPAN_OOT_WORLD_LINE_FLOOR (1u << 1)
#define LINKSPAN_OOT_WORLD_LINE_CEILING (1u << 2)

/* Resultado de uma consulta. `hit` 0 deixa os demais campos zerados. `normal` é
 * unitária; `bg_id` é BGCHECK_SCENE (50) para a cena ou o índice do DynaPoly;
 * `floor_type` segue SurfaceType_GetFloorType. */
typedef struct ShipOotWorldHitV1 {
    uint32_t size;
    uint8_t hit;
    float pos[3];
    float normal[3];
    int32_t bg_id;
    uint32_t floor_type;
} ShipOotWorldHitV1;

/* Consultas à colisão da cena atual, só na thread do jogo e em gameplay (fora
 * dela, UNSUPPORTED). Nenhuma altera o jogo.
 *
 * raycast_floor: primeiro chão abaixo de (x, y, z); sem chão, hit = 0 e OK.
 * line_test: primeira superfície entre `from` e `to` nas superfícies pedidas.
 * wall_check: esfera de `radius` indo de `from` para `to`, à altura `height`;
 *   `hit` indica parede e `pos` a posição corrigida (igual a `to` sem parede).
 * water_surface: altura da água em (x, z); FAILURE fora de água. */
typedef struct ShipOotWorldV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* raycast_floor)(float x, float y, float z, ShipOotWorldHitV1* hit);
    ShipNativeStatus(SHIP_NATIVE_CALL* line_test)(const float* from, const float* to, uint32_t surfaces,
                                                  ShipOotWorldHitV1* hit);
    ShipNativeStatus(SHIP_NATIVE_CALL* wall_check)(const float* from, const float* to, float radius, float height,
                                                   ShipOotWorldHitV1* hit);
    ShipNativeStatus(SHIP_NATIVE_CALL* water_surface)(float x, float z, float* y);
} ShipOotWorldV1;

#endif
