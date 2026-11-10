#pragma once

// Ondulação interativa da água (R034). O jogo alimenta um height field com os
// mesmos eventos que geram anéis e respingos; um shaderpack DX11 pode ler o
// resultado. Sem pack que declare LinkSpanWaterRipple, nada é alocado nem simulado.

#ifdef __cplusplus
#include <array>
#include <cstdint>
#include <vector>

namespace WaterRipple {

constexpr unsigned Side = 128;

struct Frame {
    std::array<float, 4> worldToUV{}; // uv = worldXZ * xy + zw
    std::array<float, 4> info{};      // planeY, força, 1/Side, ativo (0 ou 1)
};

void SetWanted(bool wanted);
// Sempre preenche frame. Devolve true quando texels (Side*Side*4 halfs, RGBA16F)
// foi atualizado; version guarda a última versão lida por quem chama.
bool Read(uint64_t& version, std::vector<uint16_t>& texels, Frame& frame);

} // namespace WaterRipple

extern "C" {
#endif

struct PlayState;
// pos aponta para três floats (Vec3f).
void WaterRipple_OnRipple(struct PlayState* play, const float* pos, short radius, short radiusMax);
void WaterRipple_OnSplash(struct PlayState* play, const float* pos, short scale);
void WaterRipple_Tick(struct PlayState* play);

#ifdef __cplusplus
}
#endif
