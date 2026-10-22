#ifndef LINKSPAN_OOT_PLAYER_BODY_H
#define LINKSPAN_OOT_PLAYER_BODY_H

#include <shiplua/native/ship_native_abi.h>
#include "oot_skeletons.h"

/* Corpo único Lua/nativo por owner; ponteiros emprestados só na chamada.
 * Controle separado da ação; callbacks são drenados antes do unload. */
#define LINKSPAN_OOT_PLAYER_BODY_SERVICE "linkspan.oot.player-body"
#define LINKSPAN_OOT_PLAYER_BODY_VERSION 1u

typedef struct ShipOotBodyAnimationV1 {
    uint32_t size;
    uint32_t reserved_align;
    const char* name;
    const char* path;
    uint8_t reverse_default;
    uint8_t reserved[7];
} ShipOotBodyAnimationV1;

typedef struct ShipOotBodySegmentV1 {
    uint32_t size;
    uint32_t segment; /* 1..15, conforme a API Lua atual */
    const char* resource_path;
} ShipOotBodySegmentV1;

typedef struct ShipOotBodySpecV1 {
    uint32_t size;
    uint32_t reserved_align;
    ShipOotSkeletonSpecV2 skeleton;
    const char* default_animation; /* nome lógico */
    const ShipOotBodyAnimationV1* animations;
    uint32_t animation_count;
    uint32_t reserved_animations;
    const ShipOotBodySegmentV1* segments;
    uint32_t segment_count;
    float ground_offset; /* ajuste visual, não teleporte do Player */
} ShipOotBodySpecV1;

typedef struct ShipOotBodyStateV1 {
    uint32_t size;
    uint32_t reserved_align;
    uint64_t body_generation;
    uint64_t scene_epoch;
    uint8_t active;
    uint8_t controlled;
    uint8_t reserved[6];
    ShipOotAnimationStateV2 animation;
} ShipOotBodyStateV1;

typedef struct ShipOotBodyTickV1 {
    uint32_t size;
    uint32_t reserved_align;
    uint64_t control;
    void* play_state;
    void* player;
    ShipOotBodyStateV1 state; /* snapshot antes do avanço deste tick */
} ShipOotBodyTickV1;

typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotBodyTickFnV1)(void* user, const ShipOotBodyTickV1* tick);

typedef struct ShipOotBodyDrawV1 {
    uint32_t size;
    uint32_t reserved_align;
    uint64_t control;
    void* play_state;
    void* player;
    void* actor;
    ShipOotBodyStateV1 state;
} ShipOotBodyDrawV1;

typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotBodyDrawFnV1)(void* user, const ShipOotBodyDrawV1* draw,
                                                                uint8_t* draw_default);

typedef struct ShipOotBodyControllerV1 {
    uint32_t size;
    uint32_t reserved_align;
    ShipOotBodyTickFnV1 tick;
    ShipOotSkeletonLimbFnV2 post_limb;
    ShipOotBodyDrawFnV1 draw; /* opcional, matriz do corpo e escopo render ativos */
    void* user;
} ShipOotBodyControllerV1;

typedef struct ShipOotPlayerBodyV1 {
    uint32_t size;
    const char* layout_id;
    ShipNativeStatus(SHIP_NATIVE_CALL* set_body)(const char* owner,
                                                 const ShipOotBodySpecV1* spec); /* NULL desativa o corpo do owner */
    ShipNativeStatus(SHIP_NATIVE_CALL* get_state)(ShipOotBodyStateV1* state);
    ShipNativeStatus(SHIP_NATIVE_CALL* acquire_control)(const char* owner, uint64_t expected_body_generation,
                                                        const ShipOotBodyControllerV1* controller, uint64_t* control);
    ShipNativeStatus(SHIP_NATIVE_CALL* release_control)(uint64_t control);
    ShipNativeStatus(SHIP_NATIVE_CALL* start_animation)(uint64_t control, const char* name,
                                                        const ShipOotAnimationPlayV2* play, uint32_t lane);
    ShipNativeStatus(SHIP_NATIVE_CALL* animation_info)(uint64_t control, const char* name,
                                                       ShipOotAnimationInfoV2* info);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_segment)(uint64_t control, uint32_t segment,
                                                    const char* resource_path); /* NULL remove */
    ShipNativeStatus(SHIP_NATIVE_CALL* set_root_motion)(uint64_t control, uint64_t action_token, uint32_t mode);
} ShipOotPlayerBodyV1;

#endif
