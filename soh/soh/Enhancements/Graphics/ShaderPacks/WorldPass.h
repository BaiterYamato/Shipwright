#pragma once

#include <stdint.h>

struct GraphicsContext;

#ifdef __cplusplus
#include <span>
#include <string>
#include <string_view>

namespace ShaderPacks {
struct PassSource;
bool ActivateWorldPass(std::span<const PassSource> sources, std::string_view name, std::string& error);
void DisableWorldPass();
// Vetor 11 do LinkSpanIris (RFC 0035): parâmetros do provider, como a altura das nuvens de um pack Iris.
void SetProviderParameters(const float* values, uint32_t count);
} // namespace ShaderPacks

extern "C" {
#endif

// Comando legado combinado, para chamadores sem efeito entre as fases.
void ShaderPacks_DrawWorldPass(struct GraphicsContext* gfxCtx);
// Zero indica ausência de pack/snapshot. Compose recebe o ticket retornado.
uint32_t ShaderPacks_PrepareWorldPass(struct GraphicsContext* gfxCtx);
void ShaderPacks_ComposeWorldPass(struct GraphicsContext* gfxCtx, uint32_t ticket);
void ShaderPacks_CaptureWorldGeometry(struct GraphicsContext* gfxCtx, int enable);
// Apenas Scene_Draw/Room_Draw; nunca delimitar Actor_DrawAll com isto.
void ShaderPacks_CaptureWaterSurface(struct GraphicsContext* gfxCtx, int enable);
// Não zero com pack de passe do mundo ativo: elementos 2D de tela que o jogo põe no
// OVERLAY antes da composição (cartão de título) devem ir para depois dela.
int ShaderPacks_WorldPassActive(void);
// Ganho (0..1) da luz ambiente nativa em interior com pack e céu automático; 1 fora disso.
float ShaderPacks_NativeAmbientScale(void);

#ifdef __cplusplus
}
#endif
