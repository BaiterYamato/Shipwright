#pragma once
// Mola angular de dois graus de liberdade. Unidades: u, segundos, kg; sem jogo/heap.
#include <algorithm>
#include <array>
#include <cmath>

namespace hat {
constexpr float Tick = 1.0f / 20.0f;
constexpr float Pi = 3.14159265358979323846f;
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline Vec3 operator-(Vec3 a) {
    return { -a.x, -a.y, -a.z };
}
inline Vec3 operator*(Vec3 a, float b) {
    return { a.x * b, a.y * b, a.z * b };
}
inline Vec3 operator/(Vec3 a, float b) {
    return a * (1.0f / b);
}
inline float Dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 Cross(Vec3 a, Vec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline float Length(Vec3 a) {
    return std::sqrt(Dot(a, a));
}
inline bool Finite(Vec3 a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
inline Vec3 Unit(Vec3 a, Vec3 fallback = { 0, 1, 0 }) {
    float n = Length(a);
    return n > 1e-7f ? a / n : fallback;
}
inline Vec3 Limited(Vec3 a, float limit) {
    float n = Length(a);
    return n > limit ? a * (limit / n) : a;
}
struct Quat {
    float w = 1, x = 0, y = 0, z = 0;
};
inline Quat Unit(Quat q) {
    float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return n > 1e-7f ? Quat{ q.w / n, q.x / n, q.y / n, q.z / n } : Quat{};
}
inline Quat Conjugate(Quat q) {
    return { q.w, -q.x, -q.y, -q.z };
}
inline Vec3 Rotate(Quat q, Vec3 v) {
    Vec3 u{ q.x, q.y, q.z };
    return v + Cross(u, Cross(u, v) + v * q.w) * 2;
}
inline Quat Mix(Quat a, Quat b, float t) {
    if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0)
        b = { -b.w, -b.x, -b.y, -b.z };
    return Unit(Quat{ a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t });
}
inline Quat AxisAngle(Vec3 axis, float angle) {
    axis = Unit(axis);
    float s = std::sin(angle * .5f);
    return { std::cos(angle * .5f), axis.x * s, axis.y * s, axis.z * s };
}
inline Quat Between(Vec3 a, Vec3 b) {
    float d = Dot(a, b);
    Vec3 c = Cross(a, b);
    if (d < -.9999f)
        return AxisAngle(Unit(Cross(a, { 1, 0, 0 }), { 0, 0, 1 }), Pi);
    return Unit(Quat{ 1 + d, c.x, c.y, c.z });
}
struct Pose {
    Vec3 position{};
    Quat orientation{};
};
inline Vec3 World(Pose p, Vec3 v) {
    return p.position + Rotate(p.orientation, v);
}
inline Vec3 Local(Pose p, Vec3 v) {
    return Rotate(Conjugate(p.orientation), v - p.position);
}
struct Sphere {
    Vec3 center{};
    float radius = 0;
};
struct Capsule {
    Vec3 a{}, b{};
    float radius = 0;
};
struct Colliders {
    std::array<Sphere, 3> spheres{};
    Capsule torso{};
};
inline Vec3 Closest(Capsule c, Vec3 p) {
    Vec3 ab = c.b - c.a;
    float n = Dot(ab, ab);
    return c.a + ab * (n > 1e-8f ? std::clamp(Dot(p - c.a, ab) / n, 0.0f, 1.0f) : 0);
}
// Distância assinada, incluindo a espessura da ponta.
inline float Clearance(const Colliders& c, Vec3 p, float thickness) {
    float result = 1e30f;
    for (const auto& s : c.spheres)
        if (s.radius > 0)
            result = std::min(result, Length(p - s.center) - s.radius - thickness);
    if (c.torso.radius > 0)
        result = std::min(result, Length(p - Closest(c.torso, p)) - c.torso.radius - thickness);
    return result;
}
struct Config {
    float length = 15, mass = .055f, spring = 5.5f, damping = .23f, airDrag = .07f;
    float cone = .61086524f; // 35 graus, < pi/2.
    float thickness = .6f, teleport = 100;
    int substeps = 8;
};
inline Config ForAge(bool adult) {
    Config c;
    if (!adult) {
        c.length = 11;
        c.mass = .035f;
        c.spring = 4.9f;
        c.damping = .18f;
        c.thickness = .45f;
    }
    return c;
}
struct Input {
    Pose root{};                       // Pose ANIMADA, antes da correção.
    Vec3 gravity{ 0, -80, 0 }, wind{}; // Vento = velocidade, não força.
    Colliders colliders{};             // Centros/eixos no espaço local de root, em u.
    bool paused = false, discontinuity = false, underwater = false;
};
struct Output {
    Quat correction{}; // Pós-multiplicar na pose animada; zero twist.
    Vec3 tip{};
    bool feasible = true; // Falso se a busca limitada não encontrar direção livre.
};
class Chain {
  public:
    explicit Chain(Config config = {}) : config_(config) {
    }
    const Output& Read() const {
        return out_;
    }
    void Clear() {
        initialized_ = false;
        velocity_ = {};
        out_ = {};
    }
    void Reset(const Input& in) {
        previous_ = in.root;
        previous_.orientation = Unit(previous_.orientation);
        velocity_ = {};
        initialized_ = true;
        Publish(in, { 0, 1, 0 });
    }
    const Output& Step(const Input& in, float dt = Tick) {
        // Pausa é um no-op inclusive para teleporte/reset.
        if (in.paused)
            return out_;
        if (!Finite(in.root.position) || !Finite(in.gravity) || !Finite(in.wind) || !std::isfinite(dt) || dt <= 0)
            return out_;
        if (!initialized_ || in.discontinuity || Length(in.root.position - previous_.position) > config_.teleport) {
            Reset(in);
            return out_;
        }
        // Um tick fixo; nunca faz catch-up após draw omitido.
        dt = Tick;
        const Pose current{ in.root.position, Unit(in.root.orientation) };
        const Vec3 target = World(current, { 0, config_.length, 0 });
        const Vec3 oldTarget = World(previous_, { 0, config_.length, 0 });
        const Vec3 targetVelocity = Limited((target - oldTarget) / dt, 600);
        Vec3 point = out_.tip;
        const int count = std::clamp(config_.substeps, 1, 32);
        const float h = dt / static_cast<float>(count);
        for (int i = 0; i < count; ++i) {
            const float t = static_cast<float>(i + 1) / static_cast<float>(count);
            Pose root{ previous_.position + (current.position - previous_.position) * t,
                       Mix(previous_.orientation, current.orientation, t) };
            Vec3 rest = Rotate(root.orientation, { 0, 1, 0 });
            Vec3 goal = root.position + rest * config_.length;
            // Pré-carga cancela o torque estático da gravidade na pose animada.
            // Fora dela a gravidade muda o torque restaurador; sem deriva em repouso.
            Vec3 preload = in.gravity - rest * Dot(in.gravity, rest);
            float drag = in.underwater ? .85f : config_.airDrag;
            Vec3 mediumVelocity = in.underwater ? Vec3{} : in.wind; // Água parada; vento não é correnteza.
            Vec3 force = (goal - point) * config_.spring + (targetVelocity - velocity_) * config_.damping +
                         (mediumVelocity - velocity_) * drag;
            Vec3 acceleration = force / config_.mass + in.gravity - preload;
            if (in.underwater)
                acceleration = acceleration - in.gravity * .12f;
            velocity_ = Limited(velocity_ + acceleration * h, 600);
            Vec3 next = point + velocity_ * h;
            Vec3 direction = Constrain(Local(root, next) / config_.length, in.colliders);
            next = World(root, direction * config_.length);
            velocity_ = Limited((next - point) / h, 600);
            point = next;
        }
        previous_ = current;
        Publish(in, Local(current, point) / config_.length);
        return out_;
    }

  private:
    Config config_;
    Pose previous_{};
    Vec3 velocity_{};
    Output out_{};
    bool initialized_ = false;
    Vec3 Cone(Vec3 n) const {
        n = Unit(n);
        const float c = std::cos(config_.cone);
        if (n.y >= c)
            return n;
        Vec3 tangent = Unit(Vec3{ n.x, 0, n.z }, { 1, 0, 0 });
        return tangent * std::sin(config_.cone) + Vec3{ 0, c, 0 };
    }
    bool Safe(Vec3 n, const Colliders& c) const {
        return Clearance(c, n * config_.length, config_.thickness) >= .001f;
    }
    Vec3 Constrain(Vec3 desired, const Colliders& c) {
        desired = Cone(desired);
        if (Safe(desired, c)) {
            out_.feasible = true;
            return desired;
        }
        Vec3 safe{ 0, 1, 0 };
        if (!Safe(safe, c)) {
            // Poses extremas: busca determinística limitada, sem alocação.
            bool found = false;
            for (int ring = 1; ring <= 12 && !found; ++ring) {
                const float a = config_.cone * static_cast<float>(ring) / 12;
                for (int j = 0; j < 48; ++j) {
                    float az = 2 * Pi * static_cast<float>(j) / 48;
                    Vec3 n{ std::sin(a) * std::cos(az), std::cos(a), std::sin(a) * std::sin(az) };
                    if (Safe(n, c)) {
                        safe = n;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                out_.feasible = false;
                return { 0, 1, 0 };
            }
        }
        // Mantém um extremo comprovadamente livre. Aplicar cone/comprimento
        // depois de projetar colisores poderia reintroduzir penetração.
        Vec3 blocked = desired;
        for (int i = 0; i < 20; ++i) {
            Vec3 middle = Unit(safe + blocked);
            if (Safe(middle, c))
                safe = middle;
            else
                blocked = middle;
        }
        out_.feasible = true;
        return safe;
    }
    void Publish(const Input& in, Vec3 direction) {
        direction = Constrain(direction, in.colliders);
        out_.correction = Between({ 0, 1, 0 }, direction);
        Pose root{ in.root.position, Unit(in.root.orientation) };
        out_.tip = World(root, direction * config_.length);
    }
};
} // namespace hat
