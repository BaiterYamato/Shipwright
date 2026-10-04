// OOT-VANILLA-001: a chave do motor (soh/src/code/linkspan_vanilla.h). Sem mod, as gravações em campos que eram
// s16 no upstream convertem como a atribuição a s16 convertia, e os limites voltam aos do upstream.

#include <cstdint>
#include <cstdlib>
#include <iostream>

// Valores alargados de z64bgcheck.h (substrato Unbound); o header do jogo puxa o mundo todo.
#define BGCHECK_Y_MIN (-2147483648.0f)
#define BGCHECK_XYZ_ABSMAX 1048576.0f

#include "src/code/linkspan_vanilla.h"

uint8_t gLinkSpanEngineExtended = 0;

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Atribuição a um campo s16, como o upstream gravava (MSVC: trunca para 32 bits e fica com os 16 de baixo).
float UpstreamS16Field(double value) {
    volatile int32_t wide = static_cast<int32_t>(value);
    int16_t field = static_cast<int16_t>(wide);
    return static_cast<float>(field);
}

void TestVanillaTruncatesLikeAnS16Field() {
    gLinkSpanEngineExtended = 0;
    const double values[] = { 0.0, 1.7, -1.7, 0.49, -0.49, 100.5, -100.5, 32767.9, -32768.9, 32768.0, 40000.25,
                              -40000.25, 65536.0 + 12.0 };
    for (double value : values) {
        Check(LinkSpan_S16F(value) == UpstreamS16Field(value), "S16F sem mod diverge da gravação em s16");
    }
    Check(LinkSpan_S16F(1.7) == 1.0f, "S16F sem mod não truncou para zero");
    Check(LinkSpan_S16F(-1.7) == -1.0f, "S16F sem mod não truncou negativo para zero");
    Check(LinkSpan_S16F(40000.0) == -25536.0f, "S16F sem mod não deu a volta como o s16");
}

void TestExtendedKeepsTheFloat() {
    gLinkSpanEngineExtended = 1;
    Check(LinkSpan_S16F(1.7) == 1.7f, "S16F com mod perdeu a fração");
    Check(LinkSpan_S16F(40000.25) == 40000.25f, "S16F com mod deu a volta");
    gLinkSpanEngineExtended = 0;
}

void TestLimitsFollowTheSwitch() {
    gLinkSpanEngineExtended = 0;
    Check(LINKSPAN_BGCHECK_Y_MIN == -32000.0f, "piso sem mod não é o do upstream");
    Check(LINKSPAN_BGCHECK_XYZ_ABSMAX == 32760.0f, "limite de coordenada sem mod não é o do upstream");
    Check(LinkSpan_EngineExtended() == 0, "chave ligada sem mod");

    gLinkSpanEngineExtended = 1;
    Check(LINKSPAN_BGCHECK_Y_MIN == BGCHECK_Y_MIN, "piso com mod não é o alargado");
    Check(LINKSPAN_BGCHECK_XYZ_ABSMAX == BGCHECK_XYZ_ABSMAX, "limite de coordenada com mod não é o alargado");
    Check(LinkSpan_EngineExtended() == 1, "chave desligada com mod");
    gLinkSpanEngineExtended = 0;

    Check(LINKSPAN_VANILLA_BG_ACTOR_MAX == 50, "BG_ACTOR_MAX do upstream mudou");
    Check(LINKSPAN_VANILLA_ACTOR_NUMBER_MAX == 2000, "ACTOR_NUMBER_MAX do upstream mudou");
}

} // namespace

int main() {
    TestVanillaTruncatesLikeAnS16Field();
    TestExtendedKeepsTheFloat();
    TestLimitsFollowTheSwitch();
    std::cout << "oot_vanilla_mode_tests: ok\n";
    return 0;
}
