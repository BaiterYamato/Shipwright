#pragma once

struct PlayState;

#ifdef __cplusplus
#include "wind_field.h"
namespace WindField {
// Snapshot protegido entre tick e thread de renderização; sem alocação.
Frame Read();
} // namespace WindField
extern "C" {
#endif

// Uma vez por quadro de jogo, junto de WetSurface_Tick. Congela na pausa; zera na troca de cena.
void WindField_Tick(struct PlayState* play);
// Velocidade do vento (u/s, com rajada) num ponto do mundo; pos e out apontam para três floats.
// Sem vento ou com gDeveloperTools.WindField = 0, devolve zero.
void WindField_Sample(const float* pos, float* out);

#ifdef __cplusplus
}
#endif
