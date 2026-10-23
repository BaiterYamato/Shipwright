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
    ShipNativeStatus(SHIP_NATIVE_CALL* knockback_player)(void* source, float speed, int16_t yaw, float y_velocity,
                                                         uint32_t damage, uint8_t large);
} ShipOotCollidersV1;

/* Quads reais do Player por owner/action_token; prefixo V1 literal.
 * ATTACK reserva ambos os slots; staging por tick e snapshot após resolução. */
#define LINKSPAN_OOT_COLLIDERS_VERSION_2 2u
#define SHIP_OOT_QUAD_ATTACK_0 0u
#define SHIP_OOT_QUAD_ATTACK_1 1u
#define SHIP_OOT_QUAD_DEFENSE 2u
#define SHIP_OOT_QUAD_MASK_ATTACK 0x03u
#define SHIP_OOT_QUAD_MASK_DEFENSE 0x04u

typedef struct ShipOotPlayerQuadSpecV2 {
    uint32_t size;
    float vertices[4][3]; /* mundo, mesma ordem de Collider_SetQuadVertices(a,b,c,d) */
    uint8_t col_type;
    uint8_t at_flags;
    uint8_t ac_flags;
    uint8_t elem_type;
    uint32_t touch_dmg_flags;
    uint32_t bump_dmg_flags;
    uint8_t touch_effect;
    uint8_t touch_damage;
    uint8_t touch_flags;
    uint8_t bump_effect;
    uint8_t bump_defense;
    uint8_t bump_flags;
    uint8_t defending; /* só para o slot DEFENSE: estado de defesa do Player */
    uint8_t reserved;
    float defense_matrix[16]; /* só DEFENSE: MtxF, m[coluna][linha], sem perspectiva */
} ShipOotPlayerQuadSpecV2;

typedef struct ShipOotPlayerQuadHitsV2 {
    uint32_t size;
    uint32_t reserved_align;
    uint64_t scene_epoch;
    uint64_t resolved_tick;
    ShipOotColliderHitsV1 hits; /* snapshot; OC sempre zero */
} ShipOotPlayerQuadHitsV2;

typedef struct ShipOotCollidersV2 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* create_cylinder)(const ShipOotCylinderSpecV1* spec, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* destroy)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* submit)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_hits)(uint64_t handle, ShipOotColliderHitsV1* hits);
    ShipNativeStatus(SHIP_NATIVE_CALL* knockback_player)(void* source, float speed, int16_t yaw, float y_velocity,
                                                         uint32_t damage, uint8_t large);
    const char* layout_id;
    ShipNativeStatus(SHIP_NATIVE_CALL* acquire_player_quads)(const char* owner, uint64_t action_token,
                                                             uint32_t slot_mask, uint64_t* lease);
    ShipNativeStatus(SHIP_NATIVE_CALL* submit_player_quad)(uint64_t lease, uint32_t slot,
                                                           const ShipOotPlayerQuadSpecV2* spec);
    ShipNativeStatus(SHIP_NATIVE_CALL* disable_player_quad)(uint64_t lease, uint32_t slot);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_player_quad_hits)(uint64_t lease, uint32_t slot,
                                                              ShipOotPlayerQuadHitsV2* hits);
    ShipNativeStatus(SHIP_NATIVE_CALL* release_player_quads)(uint64_t lease);
} ShipOotCollidersV2;

#endif
