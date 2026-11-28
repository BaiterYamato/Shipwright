#pragma once

#ifdef __cplusplus
#include "decal_pool.h"
namespace ImpactDecals {
using Frame = decals::Frame;
void SetWanted(bool wanted);
Frame Read();
} // namespace ImpactDecals
extern "C" {
#endif

struct PlayState;
// Vetores apontam para três floats (Vec3f). Chamadas só no thread do jogo.
void ImpactDecals_OnSwordSweep(struct PlayState* play, const float* start, const float* end);
void ImpactDecals_OnExplosion(struct PlayState* play, const float* position);
void ImpactDecals_Tick(struct PlayState* play);
void ImpactDecals_Reset(void);

#ifdef __cplusplus
}
#endif
