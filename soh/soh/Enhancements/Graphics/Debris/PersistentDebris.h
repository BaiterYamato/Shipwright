#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct PlayState;
// Vetores são três floats contíguos (Vec3f). Chamadas somente no thread do jogo.
bool PersistentDebris_TrySpawnShard(struct PlayState* play, const float* position, const float* velocity, int16_t flags,
                                    int16_t scale, int16_t colorIdx, int16_t objectId, void* displayList);
void PersistentDebris_OnExplosion(struct PlayState* play, const float* position);
void PersistentDebris_Tick(struct PlayState* play);
void PersistentDebris_Draw(struct PlayState* play);
void PersistentDebris_Reset(void);
#ifdef __cplusplus
}
#endif
