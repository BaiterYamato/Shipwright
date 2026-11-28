#pragma once
// R057/R058: unidade pura. Distâncias do jogo, segundos, radianos; massa relativa.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace rigid_debris {
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const {
        return { x + b.x, y + b.y, z + b.z };
    }
    Vec3 operator-(Vec3 b) const {
        return { x - b.x, y - b.y, z - b.z };
    }
    Vec3 operator-() const {
        return { -x, -y, -z };
    }
    Vec3 operator*(float s) const {
        return { x * s, y * s, z * s };
    }
    Vec3 operator/(float s) const {
        return *this * (1 / s);
    }
    Vec3& operator+=(Vec3 b) {
        return *this = *this + b;
    }
    Vec3& operator-=(Vec3 b) {
        return *this = *this - b;
    }
};
inline float Dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 Cross(Vec3 a, Vec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline float Length(Vec3 a) {
    return std::sqrt(Dot(a, a));
}
inline Vec3 Unit(Vec3 a, Vec3 fallback = { 0, 1, 0 }) {
    float l = Length(a);
    return l > 1e-6f ? a / l : fallback;
}
inline bool Finite(Vec3 a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
inline Vec3 Limited(Vec3 a, float max) {
    float l = Length(a);
    return l > max ? a * (max / l) : a;
}
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    Quat Conjugate() const {
        return { -x, -y, -z, w };
    }
    Quat operator*(Quat b) const {
        return { w * b.x + x * b.w + y * b.z - z * b.y, w * b.y - x * b.z + y * b.w + z * b.x,
                 w * b.z + x * b.y - y * b.x + z * b.w, w * b.w - x * b.x - y * b.y - z * b.z };
    }
    Quat Normalized() const {
        float n = std::sqrt(x * x + y * y + z * z + w * w);
        return n > 1e-6f ? Quat{ x / n, y / n, z / n, w / n } : Quat{};
    }
    Vec3 Rotate(Vec3 a) const {
        Vec3 q{ x, y, z };
        return a + Cross(q, Cross(q, a) + a * w) * 2;
    }
    static Quat AxisAngle(Vec3 axis, float angle) {
        axis = Unit(axis);
        float s = std::sin(angle * .5f);
        return { axis.x * s, axis.y * s, axis.z * s, std::cos(angle * .5f) };
    }
};
enum class State : std::uint8_t { Flying, Sleeping, Floating };
struct Spawn {
    Vec3 position{}, velocity{}, angularVelocity{}, halfExtents{ 2, 1, 2 };
    Quat orientation{};
    float mass = 1, restitution = .25f, friction = .65f;
};
struct Body : Spawn {
    Vec3 inertia{}, inverseInertia{};
    float inverseMass = 1, radius = 2, density = 1; // mass / (volume * waterDensity).
    std::uint64_t age = 0, serial = 0;
    unsigned quietTicks = 0;
    State state = State::Flying;
    bool active = false;
};
struct Ground {
    float height = 0;
    Vec3 normal{ 0, 1, 0 };
};
struct Wall {
    Vec3 push{}, normal{};
};
struct World {
    void* context = nullptr;
    bool (*floor)(void*, Vec3, Ground&) = nullptr;
    bool (*wall)(void*, Vec3, Vec3, float, Wall&) = nullptr;
    bool (*water)(void*, Vec3, float&) = nullptr;
};
struct Config {
    Vec3 gravity{ 0, -400, 0 };
    float slop = .02f, contactMargin = .04f;
    float sleepLinear = .65f, sleepAngular = .12f;
    unsigned sleepTicks = 30;
    float bounceThreshold = 15, waterDrag = 6, angularWaterDrag = 4, waterDensity = .01f;
    static constexpr float tick = .05f, dt = tick / 4;
    static constexpr unsigned substeps = 4, iterations = 12;
};
class Pool {
  public:
    static constexpr std::size_t capacity = 64, invalid = capacity;
    Config config{};
    const std::array<Body, capacity>& Bodies() const {
        return bodies;
    }
    const Body& Get(std::size_t i) const {
        return bodies[i];
    }
    std::size_t Count() const {
        std::size_t n = 0;
        for (const auto& b : bodies)
            n += b.active;
        return n;
    }
    void Reset() {
        bodies = {};
    } // serial não volta; outro corpo nunca herda a chave de interpolação.
    void Remove(std::size_t i) {
        if (i < capacity)
            bodies[i].active = false;
    }
    std::size_t Insert(const Spawn& in) {
        const auto& q = in.orientation;
        if (!Finite(in.position) || !Finite(in.velocity) || !Finite(in.angularVelocity) || !Finite(in.halfExtents) ||
            !std::isfinite(q.x + q.y + q.z + q.w) || !std::isfinite(in.mass) || !std::isfinite(in.restitution) ||
            !std::isfinite(in.friction) || in.mass <= 0 ||
            std::min({ in.halfExtents.x, in.halfExtents.y, in.halfExtents.z }) < .05f ||
            std::max({ in.halfExtents.x, in.halfExtents.y, in.halfExtents.z }) > 100)
            return invalid;
        std::size_t slot = invalid;
        for (std::size_t i = 0; i < capacity; ++i)
            if (!bodies[i].active) {
                slot = i;
                break;
            }
        if (slot == invalid)
            for (std::size_t i = 0; i < capacity; ++i) {
                if (slot == invalid || (bodies[i].state == State::Sleeping && bodies[slot].state != State::Sleeping) ||
                    ((bodies[i].state == State::Sleeping) == (bodies[slot].state == State::Sleeping) &&
                     bodies[i].serial < bodies[slot].serial))
                    slot = i;
            }
        Body b;
        static_cast<Spawn&>(b) = in;
        b.orientation = q.Normalized();
        b.velocity = Limited(b.velocity, 1500);
        b.angularVelocity = Limited(b.angularVelocity, 80);
        b.restitution = std::clamp(b.restitution, 0.0f, 1.0f);
        b.friction = std::clamp(b.friction, 0.0f, 2.0f);
        b.mass = std::clamp(b.mass, .000001f, 10000.0f);
        b.inverseMass = 1 / b.mass;
        const Vec3 h = b.halfExtents;
        b.density = b.mass / (8 * h.x * h.y * h.z * std::max(config.waterDensity, .000001f));
        b.inertia = { b.mass * (h.y * h.y + h.z * h.z) / 3, b.mass * (h.x * h.x + h.z * h.z) / 3,
                      b.mass * (h.x * h.x + h.y * h.y) / 3 };
        b.inverseInertia = { 1 / b.inertia.x, 1 / b.inertia.y, 1 / b.inertia.z };
        b.radius = std::max({ h.x, h.y, h.z });
        b.active = true;
        b.serial = ++serial;
        bodies[slot] = b;
        return slot;
    }
    void Impulse(std::size_t i, Vec3 impulse, Vec3 point) {
        if (i >= capacity || !bodies[i].active || !Finite(impulse) || !Finite(point) || Length(impulse) < 1e-6f)
            return;
        Wake(bodies[i]);
        Apply(bodies[i], impulse, point - bodies[i].position);
    }
    void Explosion(Vec3 center, float radius = 100, float speed = 220) {
        if (!Finite(center) || !std::isfinite(radius) || !std::isfinite(speed) || radius <= 0 || speed <= 0)
            return;
        for (std::size_t i = 0; i < capacity; ++i)
            if (bodies[i].active) {
                auto& b = bodies[i];
                Vec3 delta = b.position - center;
                float d = Length(delta);
                if (d >= radius)
                    continue;
                Vec3 n = Unit(delta + Vec3{ 0, radius * .12f, 0 });
                Impulse(i, n * (b.mass * speed * (1 - d / radius)), b.position + Vec3{ 0, -b.halfExtents.y * .5f, 0 });
            }
    }
    // Esfera cinemática. Sobreposição estática não cria energia; aproximação acorda o corpo.
    void PushSphere(Vec3 position, Vec3 velocity, float radius) {
        if (!Finite(position) || !Finite(velocity) || !std::isfinite(radius) || radius <= 0)
            return;
        for (std::size_t i = 0; i < capacity; ++i)
            if (bodies[i].active) {
                auto& b = bodies[i];
                Vec3 delta = b.position - position;
                float d = Length(delta);
                if (d >= radius + b.radius)
                    continue;
                Vec3 n = Unit(delta, Unit(velocity, { 1, 0, 0 }));
                float approach = Dot(velocity - b.velocity, n);
                if (approach <= 0)
                    continue;
                Vec3 r = -n * b.radius;
                float j = std::min(approach, 200.0f) / Effective(b, r, n);
                Impulse(i, n * j, b.position + r);
            }
    }
    void Step(const World& world) {
        std::array<bool, capacity> supported{}, wet{};
        for (auto& b : bodies)
            if (b.active)
                ++b.age;
        struct Pose {
            Vec3 position;
            Quat orientation;
            float energy;
        };
        std::array<Pose, capacity> before{};
        for (unsigned sub = 0; sub < Config::substeps; ++sub) {
            for (std::size_t i = 0; i < capacity; ++i)
                if (bodies[i].active && bodies[i].state != State::Sleeping) {
                    const auto& b = bodies[i];
                    before[i] = { b.position, b.orientation,
                                  KineticEnergy(b) - b.mass * Dot(config.gravity, b.position) };
                }
            for (std::size_t i = 0; i < capacity; ++i) {
                auto& b = bodies[i];
                if (!b.active || b.state == State::Sleeping)
                    continue;
                const Vec3 prev = b.position;
                Vec3 acceleration = config.gravity;
                float surface = 0, immersion = 0;
                if (world.water && world.water(world.context, b.position, surface) && std::isfinite(surface)) {
                    float halfHeight = 0;
                    for (unsigned k = 0; k < 8; ++k)
                        halfHeight = std::max(halfHeight, std::abs(Corner(b, k).y));
                    immersion = std::clamp((surface - b.position.y + halfHeight) / (2 * halfHeight), 0.0f, 1.0f);
                    acceleration -= config.gravity * (immersion / b.density);
                }
                wet[i] = immersion > 0;
                b.velocity += acceleration * Config::dt;
                b.velocity = b.velocity * std::exp(-config.waterDrag * immersion * Config::dt);
                b.angularVelocity = b.angularVelocity * std::exp(-config.angularWaterDrag * immersion * Config::dt);
                b.position += b.velocity * Config::dt;
                b.state = immersion > 0 && b.density < 1 ? State::Floating : State::Flying;
                Wall wall;
                if (world.wall && world.wall(world.context, prev, b.position, b.radius, wall) && Finite(wall.push)) {
                    b.position += Limited(wall.push, 2 * b.radius);
                    Vec3 n = Unit(wall.normal, Unit(wall.push)), r = -n * b.radius;
                    float vn = Dot(b.velocity + Cross(b.angularVelocity, r), n);
                    if (vn < 0)
                        Apply(b, n * (-(1 + b.restitution) * vn / Effective(b, r, n)), r);
                }
            }
            MakeContacts(world, supported);
            for (unsigned iter = 0; iter < Config::iterations; ++iter) {
                for (std::size_t k = 0; k < worldCount; ++k)
                    Solve(worldContacts[k]);
                for (std::size_t k = 0; k < pairCount; ++k)
                    Solve(pairContacts[k]);
            }
            for (auto& b : bodies)
                if (b.active && b.state != State::Sleeping) {
                    float angle = Length(b.angularVelocity) * Config::dt;
                    if (angle > 1e-7f)
                        b.orientation = (Quat::AxisAngle(b.angularVelocity, angle) * b.orientation).Normalized();
                }
            // Correção posicional separada (Baumgarte leve), sem bias de velocidade/energia cinética.
            for (unsigned iter = 0; iter < 4; ++iter) {
                for (std::size_t k = 0; k < pairCount; ++k)
                    CorrectPair(pairContacts[k]);
                for (std::size_t i = 0; i < capacity; ++i)
                    if (bodies[i].active && bodies[i].state != State::Sleeping)
                        CorrectFloor(i);
            }
            // O split de penetracao pode elevar o COM. Desconta esse trabalho da energia
            // cinetica em contato isolado passivo; agua e pares sao fontes/trocas externas ao corpo.
            std::array<bool, capacity> paired{};
            for (std::size_t k = 0; k < pairCount; ++k) {
                paired[pairContacts[k].a] = true;
                paired[pairContacts[k].b] = true;
            }
            for (std::size_t i = 0; i < capacity; ++i) {
                auto& b = bodies[i];
                if (!b.active || b.state == State::Sleeping || wet[i] || paired[i])
                    continue;
                float budget = before[i].energy + b.mass * Dot(config.gravity, b.position);
                const float kinetic = KineticEnergy(b);
                if (budget < 0) {
                    // Sem energia para subir a barreira geometrica: conserva a pose anterior.
                    b.position = before[i].position;
                    b.orientation = before[i].orientation;
                    b.velocity = {};
                    b.angularVelocity = {};
                } else if (kinetic > budget && kinetic > 0) {
                    float scale = std::sqrt(budget / kinetic);
                    b.velocity = b.velocity * scale;
                    b.angularVelocity = b.angularVelocity * scale;
                }
            }
        }
        for (std::size_t i = 0; i < capacity; ++i) {
            auto& b = bodies[i];
            if (!b.active || b.state == State::Sleeping)
                continue;
            b.velocity = Limited(b.velocity, 1500);
            b.angularVelocity = Limited(b.angularVelocity, 80);
            if ((!wet[i] || b.density >= 1) && supported[i] && Length(b.velocity) < config.sleepLinear &&
                Length(b.angularVelocity) < config.sleepAngular) {
                if (++b.quietTicks >= config.sleepTicks) {
                    b.state = State::Sleeping;
                    b.velocity = {};
                    b.angularVelocity = {};
                }
            } else
                b.quietTicks = 0;
        }
    }
    static Vec3 Corner(const Body& b, unsigned k) {
        return b.orientation.Rotate({ (k & 1) ? b.halfExtents.x : -b.halfExtents.x,
                                      (k & 2) ? b.halfExtents.y : -b.halfExtents.y,
                                      (k & 4) ? b.halfExtents.z : -b.halfExtents.z });
    }
    static float KineticEnergy(const Body& b) {
        Vec3 w = b.orientation.Conjugate().Rotate(b.angularVelocity);
        return .5f * (b.mass * Dot(b.velocity, b.velocity) + b.inertia.x * w.x * w.x + b.inertia.y * w.y * w.y +
                      b.inertia.z * w.z * w.z);
    }

  private:
    struct Contact {
        std::size_t a = 0, b = invalid;
        Vec3 ra{}, rb{}, normal{}, tangentImpulse{};
        float normalImpulse = 0, target = 0, friction = 0;
    };
    std::array<Body, capacity> bodies{};
    std::uint64_t serial = 0;
    std::array<Contact, capacity * 8> worldContacts{};
    std::array<Contact, capacity*(capacity - 1) / 2> pairContacts{};
    std::size_t worldCount = 0, pairCount = 0;
    struct Plane {
        Vec3 point{}, normal{};
        bool valid = false;
    };
    std::array<std::array<Plane, 8>, capacity> floorPlanes{};
    static void Wake(Body& b) {
        b.state = State::Flying;
        b.quietTicks = 0;
    }
    static Vec3 InverseI(const Body& b, Vec3 v) {
        v = b.orientation.Conjugate().Rotate(v);
        return b.orientation.Rotate({ v.x * b.inverseInertia.x, v.y * b.inverseInertia.y, v.z * b.inverseInertia.z });
    }
    static float Effective(const Body& b, Vec3 r, Vec3 n) {
        return b.inverseMass + Dot(n, Cross(InverseI(b, Cross(r, n)), r));
    }
    static void Apply(Body& b, Vec3 j, Vec3 r) {
        b.velocity += j * b.inverseMass;
        b.angularVelocity += InverseI(b, Cross(r, j));
    }
    void MakeContacts(const World& world, std::array<bool, capacity>& supported) {
        worldCount = pairCount = 0;
        for (std::size_t i = 0; i < capacity; ++i) {
            const auto& b = bodies[i];
            floorPlanes[i] = {};
            if (!b.active || b.state == State::Sleeping || !world.floor)
                continue;
            unsigned touching = 0;
            Vec3 averageNormal{};
            for (unsigned k = 0; k < 8; ++k) {
                Vec3 r = Corner(b, k), p = b.position + r;
                Ground g;
                if (!world.floor(world.context, p, g) || !std::isfinite(g.height) || !Finite(g.normal))
                    continue;
                Vec3 n = Unit(g.normal);
                if (n.y <= .05f)
                    continue;
                floorPlanes[i][k] = { { p.x, g.height, p.z }, n, true };
                if ((p.y - g.height) * n.y > config.contactMargin)
                    continue;
                Contact c;
                c.a = i;
                c.ra = r;
                c.normal = n;
                c.friction = b.friction;
                float vn = Dot(b.velocity + Cross(b.angularVelocity, r), n);
                c.target = vn < -config.bounceThreshold ? -b.restitution * vn : 0;
                worldContacts[worldCount++] = c;
                ++touching;
                averageNormal += n;
            }
            Vec3 local = b.orientation.Conjugate().Rotate(Unit(averageNormal));
            supported[i] =
                touching >= 3 && std::max({ std::abs(local.x), std::abs(local.y), std::abs(local.z) }) > .995f;
        }
        // Grade uniforme esparsa. Célula >= diâmetro máximo: bastam as 27 vizinhas.
        std::array<int, 128> heads;
        heads.fill(-1);
        std::array<int, capacity> next{}, cx{}, cy{}, cz{};
        float cell = .1f;
        for (const auto& b : bodies)
            if (b.active)
                cell = std::max(cell, 2 * b.radius);
        auto coord = [cell](float f) {
            return static_cast<int>(std::floor(std::clamp(f / cell, -1000000.0f, 1000000.0f)));
        };
        auto hash = [](int x, int y, int z) {
            return (static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u ^
                    static_cast<unsigned>(z) * 83492791u) &
                   127u;
        };
        for (std::size_t i = 0; i < capacity; ++i)
            if (bodies[i].active) {
                cx[i] = coord(bodies[i].position.x);
                cy[i] = coord(bodies[i].position.y);
                cz[i] = coord(bodies[i].position.z);
                unsigned h = hash(cx[i], cy[i], cz[i]);
                next[i] = heads[h];
                heads[h] = static_cast<int>(i);
            }
        for (std::size_t i = 0; i < capacity; ++i)
            if (bodies[i].active)
                for (int x = -1; x <= 1; ++x)
                    for (int y = -1; y <= 1; ++y)
                        for (int z = -1; z <= 1; ++z) {
                            int xx = cx[i] + x, yy = cy[i] + y, zz = cz[i] + z;
                            for (int j = heads[hash(xx, yy, zz)]; j >= 0; j = next[static_cast<std::size_t>(j)]) {
                                std::size_t other = static_cast<std::size_t>(j);
                                if (other <= i || cx[other] != xx || cy[other] != yy || cz[other] != zz)
                                    continue;
                                auto& a = bodies[i];
                                auto& b = bodies[other];
                                if (a.state == State::Sleeping && b.state == State::Sleeping)
                                    continue;
                                Vec3 delta = a.position - b.position;
                                float d = Length(delta), reach = a.radius + b.radius;
                                if (d > reach + config.contactMargin)
                                    continue;
                                Vec3 n = Unit(delta, { 1, 0, 0 });
                                float closing = Dot(a.velocity - b.velocity, n);
                                if (a.state == State::Sleeping && (closing < -1 || d < reach - .25f))
                                    Wake(a);
                                if (b.state == State::Sleeping && (closing < -1 || d < reach - .25f))
                                    Wake(b);
                                Contact c;
                                c.a = i;
                                c.b = other;
                                c.normal = n;
                                c.ra = -n * a.radius;
                                c.rb = n * b.radius;
                                c.friction = std::sqrt(a.friction * b.friction);
                                c.target = closing < -config.bounceThreshold
                                               ? -std::min(a.restitution, b.restitution) * closing
                                               : 0;
                                pairContacts[pairCount++] = c;
                                if (n.y > .1f)
                                    supported[i] = true;
                                if (n.y < -.1f)
                                    supported[other] = true;
                            }
                        }
    }
    void Solve(Contact& c) {
        auto& a = bodies[c.a];
        Body* b = c.b < capacity ? &bodies[c.b] : nullptr;
        const bool movableA = a.state != State::Sleeping, movableB = b && b->state != State::Sleeping;
        auto effective = [&](Vec3 n) {
            return (movableA ? Effective(a, c.ra, n) : 0) + (movableB ? Effective(*b, c.rb, n) : 0);
        };
        auto relative = [&]() {
            return a.velocity + Cross(a.angularVelocity, c.ra) -
                   (b ? b->velocity + Cross(b->angularVelocity, c.rb) : Vec3{});
        };
        auto apply = [&](Vec3 j) {
            if (movableA)
                Apply(a, j, c.ra);
            if (movableB)
                Apply(*b, -j, c.rb);
        };
        float denominator = effective(c.normal);
        if (denominator < 1e-8f)
            return;
        float old = c.normalImpulse;
        c.normalImpulse = std::max(0.0f, old + (c.target - Dot(relative(), c.normal)) / denominator);
        apply(c.normal * (c.normalImpulse - old));
        Vec3 v = relative(), tangent = v - c.normal * Dot(v, c.normal);
        float speed = Length(tangent);
        if (speed > 1e-6f) {
            Vec3 t = tangent / speed, previous = c.tangentImpulse;
            c.tangentImpulse = Limited(previous - t * (speed / effective(t)), c.friction * c.normalImpulse);
            apply(c.tangentImpulse - previous);
        }
    }
    void CorrectPair(const Contact& c) {
        auto& a = bodies[c.a];
        auto& b = bodies[c.b];
        Vec3 delta = a.position - b.position;
        float d = Length(delta);
        float penetration = a.radius + b.radius - d - config.slop;
        if (penetration <= 0)
            return;
        float wa = a.state == State::Sleeping ? 0 : a.inverseMass, wb = b.state == State::Sleeping ? 0 : b.inverseMass;
        if (wa + wb <= 0)
            return;
        Vec3 correction = Unit(delta, c.normal) * (.6f * penetration / (wa + wb));
        a.position += correction * wa;
        b.position -= correction * wb;
    }
    void CorrectFloor(std::size_t i) {
        auto& b = bodies[i];
        float deepest = 0;
        Vec3 n{};
        // Reutiliza o plano local dos cantos consultados neste subpasso: so 8 raycasts/corpo.
        for (unsigned k = 0; k < 8; ++k) {
            const auto& plane = floorPlanes[i][k];
            if (!plane.valid)
                continue;
            Vec3 p = b.position + Corner(b, k);
            float depth = Dot(plane.point - p, plane.normal) - config.slop;
            if (depth > deepest) {
                deepest = depth;
                n = plane.normal;
            }
        }
        b.position += n * (deepest * .8f);
    }
};
} // namespace rigid_debris
