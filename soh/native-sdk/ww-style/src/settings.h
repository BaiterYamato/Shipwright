// Configuração do Wind Waker Style. O main.lua guarda os valores no ship.storage do mod e manda um snapshot
// "chave=valor" por linha pelo ship.native.call("configure", ...); a DLL nunca lê CVar do host.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace WWStyle {

struct Settings {
    // Cel Shading.
    bool celEnabled = true;
    float rampCenter = 0.5f;
    float rampSoftness = 0.02f;
    float highlightIntensity = 0.6f;
    float shadowIntensity = 0.6f;
    float pointLightRange = 1.5f;
    bool useNaviLight = true;
    float transitionTime = 1.0f;
    bool debugLightSources = false;
    bool debugHighlightBands = false;

    // Lights.
    bool hideVanillaGlow = true;
    bool improveFlameFlicker = true;
    float flickerSpeed = 1.0f;
    float naviSaturation = 0.2f;
    bool lightCasting = false;
    bool wwDefaultMovement = true;
    float rotationSpeed = 1.0f;
    float sizeFlicker = 1.0f;
    float sphereSize = 0.5f;
    float lightIntensity = 0.2f;
    bool naviLightCasting = true;
    float naviSphereSize = 0.75f;
    float naviIntensity = 0.2f;
    bool otherFairyLights = false;
    float wildFairySphereSize = 0.75f;
    float wildFairyIntensity = 0.2f;
    bool debugLightSpheres = false;
    bool dekuStickLight = true;
    float dekuStickSphereSize = 0.5f;

    // Actor Shadows.
    bool shadowsEnabled = false;
    bool suppressVanillaShadows = true;
    float shadowOpacity = 0.2f;
    int32_t shadowEdgeSoftness = 0;
    float shadowLength = 0.2f;
    float shadowSlabDepth = 8.0f;
    float shadowSlabRise = 8.0f;
    int32_t shadowMaxDistance = 550;
    bool debugShadowVolume = false;

    // Sky.
    bool skyEnabled = false;
    float horizonHeight = -408.0f;
    float horizonParallax = 0.75f;
    bool gradientEnabled = true;
    float gradientBrightness = 1.0f;
    bool cloudsEnabled = true;
    float cloudsOpacity = 0.85f;
    float cloudsCoverage = 0.3f;
    float cloudsDriftSpeed = 1.0f;
    bool starsEnabled = true;
    int32_t starCount = 1000;
    float starBrightness = 1.0f;
    float starTwinkleSpeed = 1.0f;
    bool wispsEnabled = true;
    float wispAmount = 1.0f;
    float wispSpeed = 1.0f;
    bool debugSkySplit = false;
};

struct ParseResult {
    uint32_t applied = 0;
    uint32_t unknown = 0;
    uint32_t invalid = 0;
};

// Lê o snapshot sobre `out`: chave desconhecida é ignorada (Lua mais novo que a DLL), valor fora da faixa é
// limitado a ela e valor malformado mantém o anterior. Linhas vazias e as começadas por '#' não contam.
ParseResult ParseSettings(std::string_view text, Settings& out);

} // namespace WWStyle
