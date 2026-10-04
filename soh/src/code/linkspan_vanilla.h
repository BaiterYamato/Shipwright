#ifndef LINKSPAN_VANILLA_H
#define LINKSPAN_VANILLA_H

// SOH [Link-Span] OOT-VANILLA-001. Interno ao host: fica fora de soh/include para não mudar o layout id.
// Sem mod carregado no boot, o jogo volta ao comportamento do upstream nos pontos que o substrato Unbound alargou.
// A chave é travada em ShipLuaBootstrap.cpp antes do Heaps_Alloc e não muda durante a sessão.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Definida em soh/soh/ShipLuaBootstrap.cpp; 1 quando algum mod carregou no boot.
extern uint8_t gLinkSpanEngineExtended;

static inline uint8_t LinkSpan_EngineExtended(void) {
    return gLinkSpanEngineExtended;
}

// Gravação num campo que era s16 no upstream e virou float (colliders, luzes). Sem mod, converte como o MSVC
// convertia na atribuição a s16 (trunca para 32 bits e fica com os 16 de baixo); com mod, só passa para float.
// Recebe double para a expressão em double não ser arredondada para float antes de truncar.
static inline float LinkSpan_S16F(double value) {
    if (gLinkSpanEngineExtended) {
        return (float)value;
    }
    return (float)(int16_t)(int32_t)value;
}

// Piso ausente das consultas de chão: o substrato Unbound trocou -32000 por -2^31 em z64bgcheck.h, que não pode
// mudar sem mudar o layout id. O host usa este valor no lugar de BGCHECK_Y_MIN; sem mod, é o do upstream.
#define LINKSPAN_BGCHECK_Y_MIN (gLinkSpanEngineExtended ? BGCHECK_Y_MIN : -32000.0f)
// Mesmo caso: limite de coordenada das consultas de colisão (era 32760).
#define LINKSPAN_BGCHECK_XYZ_ABSMAX (gLinkSpanEngineExtended ? BGCHECK_XYZ_ABSMAX : 32760.0f)

// Limites do upstream que o substrato alargou em headers do layout id; valem sem mod.
#define LINKSPAN_VANILLA_BG_ACTOR_MAX 50
#define LINKSPAN_VANILLA_ACTOR_NUMBER_MAX 2000

#ifdef __cplusplus
}
#endif

#endif
