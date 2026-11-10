#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ShaderPacks {

// Sem DirectX: a mesma política é usada pelo host e pelo teste de persistência.
class PackHistory {
  public:
    void Reset() {
        valid = false;
        initialized = false;
        read = 0;
    }
    void Begin(uint16_t scene, unsigned width, unsigned height, bool enabled, double seconds) {
        const bool reset = !initialized || scene != lastScene || width != lastWidth || height != lastHeight ||
                           enabled != lastEnabled || !std::isfinite(seconds) || seconds < lastTime;
        if (reset) {
            valid = false;
            read = 0;
        }
        delta = reset ? 0.0f : static_cast<float>(std::clamp(seconds - lastTime, 0.0, 0.25));
        lastScene = scene;
        lastWidth = width;
        lastHeight = height;
        lastEnabled = enabled;
        lastTime = std::isfinite(seconds) ? seconds : 0.0;
        initialized = std::isfinite(seconds);
    }
    bool Valid() const {
        return valid && lastEnabled && initialized;
    }
    float Delta() const {
        return delta;
    }
    unsigned ReadIndex() const {
        return read;
    }
    unsigned WriteIndex() const {
        return 1u - read;
    }
    void Commit() {
        read = WriteIndex();
        valid = lastEnabled && initialized;
    }

  private:
    bool initialized = false, valid = false, lastEnabled = false;
    uint16_t lastScene = 0;
    unsigned lastWidth = 0, lastHeight = 0, read = 0;
    double lastTime = 0.0;
    float delta = 0.0f;
};

inline constexpr int HistoryInputBase = -16;
inline constexpr int HistoryInput(unsigned index) {
    return HistoryInputBase - static_cast<int>(index);
}
inline constexpr bool IsHistoryInput(int input) {
    return input <= HistoryInputBase;
}
inline constexpr unsigned HistoryInputIndex(int input) {
    return static_cast<unsigned>(HistoryInputBase - input);
}

inline float SafeLogLuminance(float luminance) {
    return std::log2(std::clamp(std::isfinite(luminance) ? luminance : 0.0f, 0.0001f, 65504.0f));
}

// Espelho da equação HLSL; tauBright < tauDark dá adaptação mais rápida ao claro.
inline float AdaptLogLuminance(float previous, float target, float delta, bool valid, float tauBright = 0.45f,
                               float tauDark = 1.6f) {
    if (!std::isfinite(target))
        target = SafeLogLuminance(0.0f);
    if (!valid || !std::isfinite(previous))
        return target;
    if (!std::isfinite(delta))
        delta = 0.0f;
    float tau = target > previous ? tauBright : tauDark;
    if (!std::isfinite(tau))
        tau = 0.001f;
    const float alpha = -std::expm1(-std::clamp(delta, 0.0f, 0.25f) / std::max(tau, 0.001f));
    return previous + (target - previous) * alpha;
}

} // namespace ShaderPacks
