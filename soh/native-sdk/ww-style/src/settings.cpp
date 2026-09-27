#include "settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace WWStyle {
namespace {

enum class Kind { Bool, Float, Int };

// Uma chave do snapshot: o campo de Settings e a faixa do slider do fork (a mesma do menu do main.lua).
struct Field {
    const char* key;
    Kind kind;
    bool Settings::*b;
    float Settings::*f;
    int32_t Settings::*i;
    double min;
    double max;
};

constexpr Field B(const char* key, bool Settings::*member) {
    return {key, Kind::Bool, member, nullptr, nullptr, 0.0, 1.0};
}
constexpr Field F(const char* key, float Settings::*member, double min, double max) {
    return {key, Kind::Float, nullptr, member, nullptr, min, max};
}
constexpr Field I(const char* key, int32_t Settings::*member, double min, double max) {
    return {key, Kind::Int, nullptr, nullptr, member, min, max};
}

const Field kFields[] = {
    B("cel.enabled", &Settings::celEnabled),
    F("cel.ramp_center", &Settings::rampCenter, 0.0, 1.0),
    F("cel.ramp_softness", &Settings::rampSoftness, 0.01, 0.2),
    F("cel.highlight_intensity", &Settings::highlightIntensity, 0.0, 2.0),
    F("cel.shadow_intensity", &Settings::shadowIntensity, 0.0, 1.0),
    F("cel.point_light_range", &Settings::pointLightRange, 1.0, 4.0),
    B("cel.use_navi_light", &Settings::useNaviLight),
    F("cel.transition_time", &Settings::transitionTime, 0.1, 6.0),
    B("debug.cel_light_sources", &Settings::debugLightSources),
    B("debug.cel_highlight_bands", &Settings::debugHighlightBands),

    B("lights.hide_vanilla_glow", &Settings::hideVanillaGlow),
    B("lights.improve_flame_flicker", &Settings::improveFlameFlicker),
    F("lights.flicker_speed", &Settings::flickerSpeed, 0.1, 3.0),
    F("lights.navi_saturation", &Settings::naviSaturation, 0.0, 1.0),
    B("lights.enabled", &Settings::lightCasting),
    B("lights.ww_default_movement", &Settings::wwDefaultMovement),
    F("lights.rotation_speed", &Settings::rotationSpeed, 0.0, 3.0),
    F("lights.size_flicker", &Settings::sizeFlicker, 0.0, 3.0),
    F("lights.sphere_size", &Settings::sphereSize, 0.1, 4.0),
    F("lights.intensity", &Settings::lightIntensity, 0.0, 2.0),
    B("lights.use_navi_light", &Settings::naviLightCasting),
    F("lights.navi_sphere_size", &Settings::naviSphereSize, 0.1, 4.0),
    F("lights.navi_intensity", &Settings::naviIntensity, 0.0, 2.0),
    B("lights.other_fairy_lights", &Settings::otherFairyLights),
    F("lights.wild_fairy_sphere_size", &Settings::wildFairySphereSize, 0.1, 4.0),
    F("lights.wild_fairy_intensity", &Settings::wildFairyIntensity, 0.0, 2.0),
    B("debug.light_spheres", &Settings::debugLightSpheres),
    B("lights.deku_stick_light", &Settings::dekuStickLight),
    F("lights.deku_stick_sphere_size", &Settings::dekuStickSphereSize, 0.1, 4.0),

    B("shadows.enabled", &Settings::shadowsEnabled),
    B("shadows.suppress_vanilla", &Settings::suppressVanillaShadows),
    F("shadows.opacity", &Settings::shadowOpacity, 0.0, 1.0),
    I("shadows.edge_softness", &Settings::shadowEdgeSoftness, 0, 2),
    F("shadows.length", &Settings::shadowLength, 0.0, 1.0),
    F("shadows.slab_depth", &Settings::shadowSlabDepth, 5.0, 200.0),
    F("shadows.slab_rise", &Settings::shadowSlabRise, 0.0, 120.0),
    I("shadows.max_distance", &Settings::shadowMaxDistance, 300, 5000),
    B("debug.shadow_volume", &Settings::debugShadowVolume),

    B("sky.enabled", &Settings::skyEnabled),
    F("sky.horizon_height", &Settings::horizonHeight, -2000.0, 2000.0),
    F("sky.horizon_parallax", &Settings::horizonParallax, 0.0, 1.5),
    B("sky.gradient.enabled", &Settings::gradientEnabled),
    F("sky.gradient.brightness", &Settings::gradientBrightness, 0.5, 1.5),
    B("sky.clouds.enabled", &Settings::cloudsEnabled),
    F("sky.clouds.opacity", &Settings::cloudsOpacity, 0.0, 1.0),
    F("sky.clouds.coverage", &Settings::cloudsCoverage, 0.0, 1.0),
    F("sky.clouds.drift_speed", &Settings::cloudsDriftSpeed, 0.0, 4.0),
    B("sky.stars.enabled", &Settings::starsEnabled),
    I("sky.stars.count", &Settings::starCount, 50, 1000),
    F("sky.stars.brightness", &Settings::starBrightness, 0.0, 2.0),
    F("sky.stars.twinkle_speed", &Settings::starTwinkleSpeed, 0.1, 5.0),
    B("sky.wisps.enabled", &Settings::wispsEnabled),
    F("sky.wisps.amount", &Settings::wispAmount, 0.5, 10.0),
    F("sky.wisps.speed", &Settings::wispSpeed, 0.25, 1.5),
    B("debug.sky_split", &Settings::debugSkySplit),
};

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

bool ParseNumber(std::string_view text, double& value) {
    // from_chars não depende do locale: o SoH troca o locale para pt-BR e um sscanf leria "0,25".
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && std::isfinite(value);
}

bool Apply(const Field& field, std::string_view text, Settings& out) {
    if (field.kind == Kind::Bool) {
        if (text == "1" || text == "true") out.*field.b = true;
        else if (text == "0" || text == "false") out.*field.b = false;
        else return false;
        return true;
    }
    double value = 0.0;
    if (!ParseNumber(text, value)) return false;
    value = std::clamp(value, field.min, field.max);
    if (field.kind == Kind::Float) out.*field.f = static_cast<float>(value);
    else out.*field.i = static_cast<int32_t>(std::lround(value));
    return true;
}

} // namespace

ParseResult ParseSettings(std::string_view text, Settings& out) {
    ParseResult result;
    while (!text.empty()) {
        const size_t newline = text.find('\n');
        std::string_view line = Trim(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        if (line.empty() || line.front() == '#') continue;
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            ++result.invalid;
            continue;
        }
        const std::string_view key = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        const auto field = std::find_if(std::begin(kFields), std::end(kFields),
                                        [&](const Field& candidate) { return key == candidate.key; });
        if (field == std::end(kFields)) ++result.unknown;
        else if (Apply(*field, value, out)) ++result.applied;
        else ++result.invalid;
    }
    return result;
}

} // namespace WWStyle
