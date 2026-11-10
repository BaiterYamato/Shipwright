#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// R054: campo de vento compartilhado. Unidade pura: sem jogo, GPU, relógio de parede ou heap.
// Base = vento da cena (windSettings: direção + velocidade 0..255, a mesma que a bandeira En_Hata lê).
// Rajada = frentes determinísticas que andam a favor do vento; a mesma fórmula existe em HLSL
// (shaderpack, lib/wind.hlsli), então CPU e GPU veem a mesma rajada no mesmo ponto e instante.
// Deslocamento = integral da base por tick, para quem advecta um padrão (poeira) sem saltar
// quando o vento muda: muda a velocidade, nunca a posição já acumulada.
namespace WindField {
constexpr float TickSeconds = 1.0f / 20.0f;
// Velocidade em unidades de mundo por segundo com windSpeed = 255. O Link corre a ~120 u/s.
constexpr float MaxSpeed = 60.0f;
constexpr float GustAmplitude = 0.6f;
constexpr float GustWavelength = 700.0f;
constexpr float GustFrontSpeed = 1.3f;

struct Frame {
    // velocidade base (u/s) xyz, relógio do vento em segundos (congela na pausa, zera na cena)
    std::array<float, 4> velocityClock{};
    // deslocamento acumulado xyz (unidades), amplitude da rajada
    std::array<float, 4> offsetGust{};
};
static_assert(sizeof(Frame) == 32);

struct Input {
    std::uintptr_t session = 0;
    int scene = -1;
    std::uint32_t tick = 0;
    bool paused = false;
    bool enabled = true;
    std::array<float, 3> direction{}; // envCtx.windDirection, qualquer escala
    float speed = 0.0f;               // envCtx.windSpeed, 0..255
};

inline float Finite(float value) {
    return std::isfinite(value) ? value : 0.0f;
}

// Só o plano: o padrão do jogo (80, 80, 80) é direção de efeito (a poeira do z_kankyo ainda soma +500 em y),
// não corrente ascendente; levar o y faria folhas e poeira subirem. Direção vertical pura = sem vento.
inline std::array<float, 3> BaseVelocity(const std::array<float, 3>& direction, float speed) {
    const float x = Finite(direction[0]), z = Finite(direction[2]);
    const float length = std::sqrt(x * x + z * z);
    float s = Finite(speed);
    s = s < 0.0f ? 0.0f : (s > 255.0f ? 255.0f : s);
    if (length < 0.0001f || s <= 0.0f)
        return {};
    const float scale = (s / 255.0f) * MaxSpeed / length;
    return { x * scale, 0.0f, z * scale };
}

// Skyboxes ao ar livre (valores do SkyboxId de z64.h): céu normal, nublado, mercados, mapa de cutscene e
// 0x1D (copa de Kokiri). Casas, lojas e bazar também têm skybox (fundo pré-renderizado), mas são interiores.
inline bool AmbientSky(int skybox) {
    switch (skybox) {
        case 0x01: // SKYBOX_NORMAL_SKY
        case 0x03: // SKYBOX_OVERCAST_SUNSET
        case 0x04: // SKYBOX_MARKET_ADULT
        case 0x05: // SKYBOX_CUTSCENE_MAP
        case 0x09: // SKYBOX_MARKET_CHILD_DAY
        case 0x0A: // SKYBOX_MARKET_CHILD_NIGHT
        case 0x1D: // SKYBOX_UNSET_1D (Kokiri)
            return true;
        default:
            return false;
    }
}

// Cena que declara vento 0 mas está ao ar livre ganha brisa ambiente; interior fica parado.
inline float EffectiveSpeed(float sceneSpeed, bool hasSky, float ambient) {
    const float s = Finite(sceneSpeed);
    if (s > 0.0f)
        return s;
    return hasSky ? Finite(ambient) : 0.0f;
}

// Multiplicador da rajada em (x, z, t). Média ~1, faixa [1 - A/2, 1 + 1,5A]. Sem vento horizontal = 1.
// Mantenha idêntica a WindGust() em lib/wind.hlsli.
inline float Gust(float x, float z, float seconds, const std::array<float, 3>& velocity) {
    const float speed = std::sqrt(velocity[0] * velocity[0] + velocity[2] * velocity[2]);
    if (speed < 0.001f)
        return 1.0f;
    const float dx = velocity[0] / speed, dz = velocity[2] / speed;
    const float along = x * dx + z * dz;
    const float across = -x * dz + z * dx;
    const float phase = (along - GustFrontSpeed * speed * seconds) / GustWavelength +
                        0.15f * std::sin(across / 450.0f + 0.31f * seconds);
    const float front = 0.5f + 0.5f * std::sin(6.2831853f * phase);
    const float envelope = 0.5f + 0.5f * std::sin(0.37f * seconds + 1.7f * std::sin(0.13f * seconds));
    return 1.0f + GustAmplitude * (2.0f * front * envelope - 0.5f);
}

inline std::array<float, 3> VelocityAt(const Frame& frame, float x, float z) {
    const std::array<float, 3> base = { frame.velocityClock[0], frame.velocityClock[1], frame.velocityClock[2] };
    const float g = Gust(x, z, frame.velocityClock[3], base);
    return { base[0] * g, base[1] * g, base[2] * g };
}

class State {
  public:
    Frame Step(const Input& in) {
        if (in.session == 0 || !in.enabled) {
            *this = {};
            return frame;
        }
        const bool reset = !initialized || in.session != session || in.scene != scene ||
                           static_cast<std::uint32_t>(in.tick - tick) > 0x7fffffffu;
        const bool advance = !reset && in.tick != tick;
        if (reset) {
            frame = {};
            seconds = 0.0;
            offset = {};
        }
        initialized = true;
        session = in.session;
        scene = in.scene;
        tick = in.tick;

        const auto velocity = BaseVelocity(in.direction, in.speed);
        if (advance && !in.paused) {
            // Um passo por tick novo; render repetido ou salto de contador não recupera tempo.
            seconds += static_cast<double>(TickSeconds);
            for (unsigned i = 0; i < 3; ++i)
                offset[i] += static_cast<double>(velocity[i]) * TickSeconds;
        }
        for (unsigned i = 0; i < 3; ++i) {
            frame.velocityClock[i] = velocity[i];
            frame.offsetGust[i] = static_cast<float>(offset[i]);
        }
        frame.velocityClock[3] = static_cast<float>(seconds);
        frame.offsetGust[3] = GustAmplitude;
        return frame;
    }

  private:
    Frame frame{};
    double seconds = 0.0;
    std::array<double, 3> offset{};
    std::uintptr_t session = 0;
    std::uint32_t tick = 0;
    int scene = -1;
    bool initialized = false;
};
} // namespace WindField
