#ifndef LINKSPAN_OOT_COLLIDERS_H
#define LINKSPAN_OOT_COLLIDERS_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_COLLIDERS_SERVICE "linkspan.oot.colliders"
#define LINKSPAN_OOT_COLLIDERS_VERSION 1u
#define LINKSPAN_OOT_COLLIDERS_MAX 256u

/* Campos na semântica de ColliderCylinderInit (z64collision_check.h): flags AT_*,
 * AC_*, OC1_*, OC2_*, TOUCH_*, BUMP_*, OCELEM_*, COLTYPE_* e ELEMTYPE_*. */
typedef struct ShipOotCylinderSpecV1 {
    uint32_t size;
    void* actor; /* Actor* dono; a posição segue actor->world.pos */
    uint8_t col_type;
    uint8_t at_flags;
    uint8_t ac_flags;
    uint8_t oc1_flags;
    uint8_t oc2_flags;
    uint8_t elem_type;
    uint32_t touch_dmg_flags;
    uint8_t touch_effect;
    uint8_t touch_damage;
    uint32_t bump_dmg_flags;
    uint8_t bump_effect;
    uint8_t bump_defense;
    uint8_t touch_flags;
    uint8_t bump_flags;
    uint8_t oc_elem_flags;
    int16_t radius;
    int16_t height;
    int16_t y_shift;
} ShipOotCylinderSpecV1;

/* Colisões registradas pelo jogo desde a última leitura. `*_actor` são Actor* do
 * outro lado, válidos só no frame; `ac_dmg_flags` são os dmgFlags de quem acertou. */
typedef struct ShipOotColliderHitsV1 {
    uint32_t size;
    uint8_t at_hit;
    uint8_t ac_hit;
    uint8_t oc_hit;
    void* at_actor;
    void* ac_actor;
    void* oc_actor;
    uint32_t ac_dmg_flags;
} ShipOotColliderHitsV1;

/* Colliders cilíndricos de mods, só na thread do jogo e em gameplay.
 *
 * O host guarda o collider. submit, chamado do update do ator dono a cada frame,
 * move o cilindro para o ator e o inscreve nas checagens AT/AC/OC habilitadas nas
 * flags. read_hits devolve e limpa os acertos do frame anterior. destroy libera
 * no frame seguinte, depois da checagem do frame atual; chame no destroy do ator.
 * Colliders de uma cena saem sozinhos na troca de cena e no reset do host.
 *
 * knockback_player: empurra o Link a partir de `source` (Actor*), com velocidade,
 * direção (ângulo binário), velocidade vertical e dano em pontos de vida (1/16 de
 * coração); `large` escolhe o empurrão grande. */
typedef struct ShipOotCollidersV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* create_cylinder)(const ShipOotCylinderSpecV1* spec, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* destroy)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* submit)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_hits)(uint64_t handle, ShipOotColliderHitsV1* hits);
    ShipNativeStatus(SHIP_NATIVE_CALL* knockback_player)(void* source, float speed, int16_t yaw,
                                                         float y_velocity, uint32_t damage, uint8_t large);
} ShipOotCollidersV1;

#endif
