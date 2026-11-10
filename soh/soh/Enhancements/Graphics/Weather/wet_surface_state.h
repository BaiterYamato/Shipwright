#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Unidade pura: nenhuma dependencia do jogo, GPU, relogio de parede ou heap.
namespace WetSurface {
constexpr float TickSeconds = 1.0f / 20.0f;
constexpr float WetPerSecond = 0.08f;
constexpr float DryPerSecond = 0.01f;

struct Frame {
    // chuva [0,1], molhamento [0,1], segundos simulados, flash [0,1]
    std::array<float, 4> weather{};
    // vento nas unidades nativas do jogo (xyz), exposicao (interior/desligado = 0)
    std::array<float, 4> windExposure{};
};
static_assert(sizeof(Frame) == 32);

struct Input {
    std::uintptr_t session = 0;
    int scene = -1;
    int skybox = 0;
    std::uint32_t tick = 0;
    bool outdoor = false;
    bool paused = false;
    bool enabled = true;
    float rain = 0.0f;
    float flash = 0.0f;
    std::array<float, 3> wind{};
};

inline float Unit(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

// Os modos 4/5 configuram chuva; 3 configura neve. O contador atual
// (precipitation[1], chamado unk_EE[1] neste host) sempre prevalece sobre o alvo.
inline float RainIntensity(unsigned drops, unsigned snow, unsigned weatherMode) {
    if (snow != 0)
        return 0.0f;
    const float fullRain = weatherMode == 4 ? 20.0f : 30.0f;
    return Unit(static_cast<float>(drops) / fullRain);
}

class State {
  public:
    Frame Step(const Input& in) {
        if (in.session == 0) {
            *this = {};
            return frame;
        }
        const bool reset = !initialized || in.session != session || in.scene != scene || in.skybox != skybox ||
                           in.outdoor != outdoor || static_cast<std::uint32_t>(in.tick - tick) > 0x7fffffffu;
        const bool advance = !reset && in.tick != tick;
        if (reset) {
            frame = {};
            seconds = 0.0;
        }
        initialized = true;
        session = in.session;
        scene = in.scene;
        skybox = in.skybox;
        outdoor = in.outdoor;
        tick = in.tick;

        // Desligar/entrar num interior limpa inclusive durante a pausa.
        if (!in.enabled || !in.outdoor) {
            frame = {};
            seconds = 0.0;
            return frame;
        }
        if (in.paused)
            return frame;

        const float rain = Unit(in.rain);
        if (advance) {
            // Uma chamada por tick; nunca recupera tempo de renderizacao perdido.
            seconds += static_cast<double>(TickSeconds);
            const float rate = rain > 0.0f ? rain * WetPerSecond : -DryPerSecond;
            frame.weather[1] = Unit(frame.weather[1] + rate * TickSeconds);
        }
        frame.weather[0] = rain;
        frame.weather[2] = static_cast<float>(seconds);
        frame.weather[3] = Unit(in.flash);
        for (unsigned i = 0; i < 3; ++i) {
            frame.windExposure[i] = std::isfinite(in.wind[i]) ? in.wind[i] : 0.0f;
        }
        frame.windExposure[3] = 1.0f;
        return frame;
    }

  private:
    Frame frame{};
    double seconds = 0.0;
    std::uintptr_t session = 0;
    std::uint32_t tick = 0;
    int scene = -1;
    int skybox = 0;
    bool initialized = false;
    bool outdoor = false;
};
} // namespace WetSurface
