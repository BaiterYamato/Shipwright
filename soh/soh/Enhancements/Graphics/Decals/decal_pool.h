#pragma once

// Unidade pura C++20: sem jogo, GPU, relógio de parede ou alocação dinâmica.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace decals {
inline constexpr std::size_t Capacity = 32;
inline constexpr std::size_t ConstantVectors = 1 + Capacity * 3;
using Vec3 = std::array<float, 3>;
enum class Kind : unsigned { Sword = 0, Scorch = 1, Dust = 2 };

struct Mark {
    Vec3 position{};
    Vec3 normal{};
    float radius = 0;
    Kind kind = Kind::Sword;
    float age = 0;
    float lifetime = 0;
    float fadeSeconds = 0;
    std::uint64_t serial = 0;

    bool Alive() const {
        return radius > 0 && age < lifetime;
    }
    float Opacity() const {
        if (!Alive())
            return 0;
        return std::clamp((lifetime - age) / fadeSeconds, 0.0f, 1.0f);
    }
};

// ABI b7: um único float4[97], compatível com CheckVectorArrayBuffer.
struct alignas(16) Frame {
    std::array<std::array<float, 4>, ConstantVectors> data{};
};
static_assert(sizeof(Frame) == ConstantVectors * 16);
static_assert(std::is_trivially_copyable_v<Frame>);

class Pool {
  public:
    void Reset() {
        marks_ = {};
        cursor_ = 0;
        serial_ = 0;
    }

    // Antes de QUALQUER evento: não apaga o primeiro impacto da cena no Tick.
    bool Synchronize(std::uintptr_t session, int scene, int room) {
        if (hasScene_ && session_ == session && scene_ == scene && room_ == room)
            return false;
        Reset();
        session_ = session;
        scene_ = scene;
        room_ = room;
        hasScene_ = true;
        return true;
    }

    bool Insert(Vec3 position, Vec3 normal, float radius, Kind kind, float lifetime, float fadeSeconds,
                bool paused = false) {
        if (paused || !Finite(position) || !Finite(normal) || !std::isfinite(radius) || radius <= 0 ||
            !std::isfinite(lifetime) || lifetime <= 0 || !std::isfinite(fadeSeconds) || fadeSeconds <= 0 ||
            static_cast<unsigned>(kind) > static_cast<unsigned>(Kind::Dust))
            return false;
        const float lengthSq = Dot(normal, normal);
        if (!std::isfinite(lengthSq) || lengthSq < 0.000001f)
            return false;
        const float inverseLength = 1.0f / std::sqrt(lengthSq);
        for (float& component : normal)
            component *= inverseLength;

        // Reaproveita uma vaga a partir do cursor do anel; cheio: menor serial.
        std::size_t slot = cursor_;
        bool free = false;
        for (std::size_t i = 0; i < Capacity; ++i) {
            const auto candidate = (cursor_ + i) % Capacity;
            if (!marks_[candidate].Alive()) {
                slot = candidate;
                free = true;
                break;
            }
        }
        if (!free) {
            for (std::size_t i = 0; i < Capacity; ++i) {
                if (marks_[i].serial < marks_[slot].serial)
                    slot = i;
            }
        }
        marks_[slot] = { position, normal, radius, kind, 0, lifetime, std::min(fadeSeconds, lifetime), ++serial_ };
        cursor_ = (slot + 1) % Capacity;
        return true;
    }

    void Step(float seconds, bool paused = false) {
        if (paused || !std::isfinite(seconds) || seconds <= 0)
            return;
        for (auto& mark : marks_) {
            if (mark.Alive())
                mark.age = std::min(mark.lifetime, mark.age + seconds);
        }
    }

    bool Recent(Vec3 position, Vec3 normal, Kind kind, float distance, float seconds) const {
        for (const auto& mark : marks_) {
            Vec3 delta{ position[0] - mark.position[0], position[1] - mark.position[1],
                        position[2] - mark.position[2] };
            if (mark.Alive() && mark.kind == kind && mark.age < seconds && Dot(normal, mark.normal) > 0.95f &&
                Dot(delta, delta) < distance * distance)
                return true;
        }
        return false;
    }

    std::size_t Count() const {
        std::size_t count = 0;
        for (const auto& mark : marks_)
            count += mark.Alive();
        return count;
    }
    const auto& Entries() const {
        return marks_;
    }

    Frame Publish() const {
        Frame frame{};
        std::size_t count = 0;
        // Ordem cronológica, inclusive depois de reutilizar vagas expiradas.
        std::array<const Mark*, Capacity> ordered{};
        for (const auto& mark : marks_)
            if (mark.Alive())
                ordered[count++] = &mark;
        std::sort(ordered.begin(), ordered.begin() + count,
                  [](const Mark* a, const Mark* b) { return a->serial < b->serial; });
        frame.data[0] = { static_cast<float>(count), 1.0f, 0, 0 }; // quantidade, versão ABI
        for (std::size_t i = 0; i < count; ++i) {
            const auto& mark = *ordered[i];
            const auto offset = 1 + i * 3;
            frame.data[offset] = { mark.position[0], mark.position[1], mark.position[2], mark.radius };
            frame.data[offset + 1] = { mark.normal[0], mark.normal[1], mark.normal[2], static_cast<float>(mark.kind) };
            frame.data[offset + 2] = { mark.age, mark.Opacity(), static_cast<float>(mark.serial % 1024),
                                       std::clamp(mark.radius * 0.06f, 1.5f, 4.0f) };
        }
        return frame;
    }

  private:
    static bool Finite(Vec3 value) {
        return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
    }
    static float Dot(Vec3 a, Vec3 b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }
    std::array<Mark, Capacity> marks_{};
    std::size_t cursor_ = 0;
    std::uint64_t serial_ = 0;
    std::uintptr_t session_ = 0;
    int scene_ = -1;
    int room_ = -1;
    bool hasScene_ = false;
};
} // namespace decals
