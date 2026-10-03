#ifndef LINKSPAN_OOT_ACTOR_MODELS_H
#define LINKSPAN_OOT_ACTOR_MODELS_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_ACTOR_MODELS_SERVICE "linkspan.oot.actor-models"
#define LINKSPAN_OOT_ACTOR_MODELS_VERSION 1u
#define LINKSPAN_OOT_ACTOR_MODELS_ID_BASE 0x1000

typedef struct ShipOotActorModelSegmentV1 {
    uint8_t segment;
    const char* texture;
} ShipOotActorModelSegmentV1;

/* Perfil genérico de ator estático/NPC. O host copia strings e listas; não guarda
 * ponteiros do mod. Os caminhos aceitam __OTR__. As juntas usam root = 1. */
typedef struct ShipOotActorModelSpecV1 {
    uint32_t size;
    const char* owner;
    const char* name;
    const char* display_name;
    const char* skeleton;
    const char* animation;
    const char* display_list;
    uint8_t hold_frame;
    uint8_t translucent;
    float frame;
    float speed;
    float scale;
    float y_offset;
    float shadow;
    float cull_radius;
    float draw_distance;
    const ShipOotActorModelSegmentV1* segments;
    uint32_t segment_count;
    const int32_t* hide_limbs;
    uint32_t hide_limb_count;
    int16_t radius;
    int16_t height;
    int16_t y_shift;
    uint8_t talks;
    uint16_t message;
    float talk_range;
    uint8_t looks;
    int32_t look_limb;
    float look_pivot;
    float look_range;
    float turn_axis[3];
    float nod_axis[3];
} ShipOotActorModelSpecV1;

typedef void(SHIP_NATIVE_CALL* ShipOotActorModelNameFn)(void* user, const char* name, int16_t id);

/* Operações na thread do jogo, fora dos callbacks de atores. Registrar durante
 * gameplay é recusado: o ActorDB usa um vector e um spawn conserva seu endereço.
 * remove_owner destrói os recursos de cada instância antes de matar o ator.
 * list_types inclui os nomes dos atores do jogo e dos mods ativos. */
typedef struct ShipOotActorModelsV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* register_type)(const ShipOotActorModelSpecV1* spec, int16_t* actor_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* remove_owner)(const char* owner);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_types)(ShipOotActorModelNameFn callback, void* user);
} ShipOotActorModelsV1;

#endif
