#pragma once

struct PlayState;

#ifdef __cplusplus
#include "wet_surface_state.h"
namespace WetSurface {
// Snapshot protegido entre tick e thread de renderizacao; sem alocacao.
Frame Read();
} // namespace WetSurface
extern "C" {
#endif

void WetSurface_Tick(struct PlayState* play);

#ifdef __cplusplus
}
#endif
