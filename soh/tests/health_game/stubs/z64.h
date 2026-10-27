#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CollisionCheckInfo {
    uint8_t health;
    uint8_t damage;
} CollisionCheckInfo;
enum { ACTOR_EN_IK = 0x113, ACTOR_EN_DH = 0xA4, ACTOR_EN_ZF = 0x25 };
typedef struct Actor {
    int16_t id;
    uint8_t category;
    CollisionCheckInfo colChkInfo;
} Actor;
typedef struct Player {
    uint32_t ivanDamageMultiplier;
} Player;
typedef struct PlayState {
    Player* player;
} PlayState;
enum { ACTORCAT_PLAYER = 2 };
typedef struct SaveContext {
    int16_t health;
    int16_t healthCapacity;
} SaveContext;
extern SaveContext gSaveContext;
#define GET_PLAYER(play) ((play)->player)
#ifdef __cplusplus
}
#endif
