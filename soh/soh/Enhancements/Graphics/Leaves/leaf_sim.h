#pragma once

// R050: unidade pura; unidades do mundo/segundo, sem headers do jogo ou heap.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace leaves {
constexpr std::size_t Capacity = 128;
constexpr float TickSeconds = 0.05f;
constexpr float Gravity = 90.0f;
constexpr float TerminalSpeed = 30.0f;
constexpr float GroundSeconds = 6.0f;
constexpr float WaterSeconds = 8.0f;
constexpr float FadeSeconds = 1.5f;
constexpr float AirSeconds = 20.0f;
constexpr float Skin = 0.35f;
constexpr float Pi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
inline bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vec3 Unit(Vec3 v) {
    if (!Finite(v))
        return { 0, 1, 0 };
    const float n = std::hypot(v.x, v.y, v.z);
    return n > 0.001f ? Vec3{ v.x / n, v.y / n, v.z / n } : Vec3{ 0, 1, 0 };
}
enum class State : std::uint8_t { Free, Falling, Ground, Water, Fading };
enum class Kind : std::uint8_t { Bush, Green, Yellow };
struct Surface {
    bool ground = false;
    float groundY = 0;
    Vec3 normal{ 0, 1, 0 };
    bool water = false;
    float waterY = 0;
};
// O teto evita que uma folha abaixo de uma ponte salte para o piso de cima.
struct World {
    void* context = nullptr;
    Surface (*sample)(void*, Vec3 position, float ceilingY) = nullptr;
};
struct Spawn {
    Vec3 position{}, velocity{};
    float yaw = 0, phase = 0, spin = 0.4f, scale = 0.04f;
    Kind kind = Kind::Bush;
};
struct Leaf {
    Vec3 position{}, velocity{}, normal{ 0, 1, 0 };
    float yaw = 0, tilt = 0, phase = 0, spin = 0;
    float age = 0, stateAge = 0, scale = 0, size = 1;
    Kind kind = Kind::Bush;
    State state = State::Free, fadeFrom = State::Free;
    bool touchedWater = false;
    std::uint32_t generation = 0;
};
struct Touch {
    Vec3 position{};
    std::uint32_t generation = 0;
};
struct Events {
    std::array<Touch, Capacity> water{};
    std::size_t count = 0;
};
using Winds = std::array<Vec3, Capacity>;

class Pool {
  public:
    const std::array<Leaf, Capacity>& Items() const {
        return items;
    }
    std::size_t Count() const {
        std::size_t n = 0;
        for (const auto& leaf : items)
            n += leaf.state != State::Free;
        return n;
    }
    // Não reinicia a geração: Reset não pode interpolar com a cena anterior.
    void Reset() {
        items = {};
    }
    std::size_t Insert(const Spawn& spawn) {
        if (!Finite(spawn.position) || !Finite(spawn.velocity) || !std::isfinite(spawn.yaw) ||
            !std::isfinite(spawn.phase) || !std::isfinite(spawn.spin) || !std::isfinite(spawn.scale) ||
            spawn.scale <= 0)
            return Capacity;
        std::size_t slot = 0;
        bool free = false;
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (items[i].state == State::Free) {
                slot = i;
                free = true;
                break;
            }
            if (items[i].age > items[slot].age)
                slot = i;
        }
        if (!free) {
            for (std::size_t i = 0; i < Capacity; ++i)
                if (items[i].age == items[slot].age && items[i].generation < items[slot].generation)
                    slot = i;
        }
        Leaf leaf;
        leaf.position = spawn.position;
        leaf.velocity = { std::clamp(spawn.velocity.x, -120.0f, 120.0f),
                          std::clamp(spawn.velocity.y, -TerminalSpeed, 120.0f),
                          std::clamp(spawn.velocity.z, -120.0f, 120.0f) };
        leaf.yaw = std::remainder(spawn.yaw, 2 * Pi);
        leaf.phase = std::remainder(spawn.phase, 2 * Pi);
        leaf.spin = std::clamp(spawn.spin, -4.0f, 4.0f);
        leaf.scale = std::clamp(spawn.scale, 0.001f, 0.2f);
        leaf.kind = spawn.kind;
        leaf.state = State::Falling;
        // Compatível com o int usado por FrameInterpolation_RecordOpenChild.
        serial = serial == 0x7fffffffU ? 1 : serial + 1;
        leaf.generation = serial;
        items[slot] = leaf;
        return slot;
    }

    // Uma chamada = 20 Hz. Vento por folha em u/s, já com a rajada compartilhada.
    Events Step(const World& world, const Winds& winds, bool paused = false) {
        Events events;
        if (paused)
            return events;
        constexpr float dt = TickSeconds / 4;
        for (std::size_t i = 0; i < Capacity; ++i) {
            auto& leaf = items[i];
            if (leaf.state == State::Free)
                continue;
            Vec3 wind = Finite(winds[i]) ? winds[i] : Vec3{};
            wind.x = std::clamp(wind.x, -200.0f, 200.0f);
            wind.z = std::clamp(wind.z, -200.0f, 200.0f);
            // Rajada vertical limitada, para não eliminar a queda.
            wind.y = std::clamp(wind.y, -10.0f, 10.0f);
            for (int sub = 0; sub < 4 && leaf.state != State::Free; ++sub) {
                leaf.age += dt;
                leaf.stateAge += dt;
                leaf.phase = std::remainder(leaf.phase + 5.5f * dt, 2 * Pi);
                if (leaf.state == State::Falling) {
                    leaf.tilt = 0.8f * std::sin(leaf.phase);
                    leaf.yaw += leaf.spin * dt;
                    const float glide = 22.0f * std::sin(leaf.tilt);
                    const float drag = 1 - std::exp(-4.0f * dt);
                    leaf.velocity.x += (wind.x + glide * std::cos(leaf.yaw) - leaf.velocity.x) * drag;
                    leaf.velocity.z += (wind.z + glide * std::sin(leaf.yaw) - leaf.velocity.z) * drag;
                    leaf.velocity.y =
                        std::max(-TerminalSpeed, (leaf.velocity.y - wind.y) * std::exp(-3.0f * dt) + wind.y -
                                                     (Gravity / 3.0f) * (1 - std::exp(-3.0f * dt)));
                    const float oldY = leaf.position.y;
                    Move(leaf, dt);
                    const auto surface = Sample(world, leaf, std::max(oldY, leaf.position.y) + 2);
                    const bool waterAbove =
                        surface.water && (!surface.ground || surface.waterY > surface.groundY + Skin);
                    if (waterAbove && oldY >= surface.waterY - Skin && leaf.position.y <= surface.waterY + Skin) {
                        Rest(leaf, State::Water);
                        leaf.position.y = surface.waterY + Skin;
                        if (!leaf.touchedWater) {
                            events.water[events.count++] = { { leaf.position.x, surface.waterY, leaf.position.z },
                                                             leaf.generation };
                            leaf.touchedWater = true;
                        }
                    } else if (surface.ground && leaf.position.y <= surface.groundY + Skin) {
                        Rest(leaf, State::Ground);
                        leaf.normal = Unit(surface.normal);
                        leaf.position.y = surface.groundY + Skin;
                    } else if (leaf.age >= AirSeconds) {
                        Fade(leaf);
                    }
                } else {
                    const State support = leaf.state == State::Fading ? leaf.fadeFrom : leaf.state;
                    if (support == State::Water) {
                        const float drag = 1 - std::exp(-1.6f * dt);
                        leaf.velocity.x += (0.55f * wind.x - leaf.velocity.x) * drag;
                        leaf.velocity.z += (0.55f * wind.z - leaf.velocity.z) * drag;
                        leaf.yaw += leaf.spin * 0.15f * dt;
                        leaf.tilt = 0.07f * std::sin(leaf.phase);
                        Move(leaf, dt);
                        const auto surface = Sample(world, leaf, leaf.position.y + 2);
                        if (surface.ground && (!surface.water || surface.groundY + Skin >= surface.waterY) &&
                            leaf.position.y <= surface.groundY + Skin) {
                            leaf.position.y = surface.groundY + Skin;
                            if (leaf.state == State::Fading)
                                leaf.fadeFrom = State::Ground;
                            else
                                Rest(leaf, State::Ground);
                            leaf.normal = Unit(surface.normal);
                            leaf.velocity = {};
                            leaf.tilt = 0;
                        } else if (surface.water) {
                            leaf.position.y = surface.waterY + Skin + 0.18f * std::sin(leaf.phase);
                        } else if (leaf.state != State::Fading) {
                            Rest(leaf, State::Falling);
                        }
                    }
                    if ((leaf.state == State::Ground && leaf.stateAge >= GroundSeconds) ||
                        (leaf.state == State::Water && leaf.stateAge >= WaterSeconds))
                        Fade(leaf);
                    if (leaf.state == State::Fading) {
                        leaf.size = std::max(0.0f, 1 - leaf.stateAge / FadeSeconds);
                        if (leaf.stateAge >= FadeSeconds)
                            leaf.state = State::Free;
                    }
                }
                leaf.yaw = std::remainder(leaf.yaw, 2 * Pi);
                if (!Finite(leaf.position) || !Finite(leaf.velocity))
                    leaf.state = State::Free;
            }
        }
        return events;
    }

  private:
    std::array<Leaf, Capacity> items{};
    std::uint32_t serial = 0;
    static Surface Sample(const World& world, const Leaf& leaf, float ceiling) {
        Surface surface = world.sample ? world.sample(world.context, leaf.position, ceiling) : Surface{};
        surface.ground = surface.ground && std::isfinite(surface.groundY) && surface.groundY <= ceiling &&
                         Finite(surface.normal) && surface.normal.y > 0.01f;
        surface.water = surface.water && std::isfinite(surface.waterY);
        return surface;
    }
    static void Move(Leaf& leaf, float dt) {
        leaf.position.x += leaf.velocity.x * dt;
        leaf.position.y += leaf.velocity.y * dt;
        leaf.position.z += leaf.velocity.z * dt;
    }
    static void Rest(Leaf& leaf, State state) {
        leaf.state = state;
        leaf.stateAge = 0;
        leaf.velocity = {};
        leaf.normal = { 0, 1, 0 };
        leaf.tilt = 0;
    }
    static void Fade(Leaf& leaf) {
        leaf.fadeFrom = leaf.state;
        leaf.state = State::Fading;
        leaf.stateAge = 0;
    }
};
} // namespace leaves
