#pragma once

// Ondulação interativa da água (R034). O jogo alimenta um height field com os
// mesmos eventos que geram anéis e respingos; um shaderpack DX11 pode ler o
// resultado. Sem pack que declare LinkSpanWaterRipple, não há grade persistente
// nem simulação. Configure valida usando uma instância temporária.

#ifdef __cplusplus
#include <array>
#include <cstdint>
#include <vector>
#include "water_ripple_field.h"

namespace WaterRipple {

constexpr unsigned Side = 128;

struct Frame {
    std::array<float, 4> worldToUV{}; // uv = worldXZ * xy + zw
    std::array<float, 4> info{};      // planeY, força, 1/Side, inativo (0), legado (1), fluxo XZ M05 (2)
};

void SetWanted(bool wanted);
// Configuração explícita, fora do tick. Mantém padrão R034 e A=0.
// Exige side=Side e stepSeconds=1/60 s; valida antes de substituir o estado.
// Retorna false se inválida; sucesso limpa ondas e aguarda novo evento.
// Pode alocar; Read também pode realocar o vetor do consumidor, como antes.
bool Configure(const water::Config& config, bool publishFlow = false,
               water::FlowComponent component = water::FlowComponent::X);
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
