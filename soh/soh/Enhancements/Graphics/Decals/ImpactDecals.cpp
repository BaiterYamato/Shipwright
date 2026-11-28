#include "ImpactDecals.h"

#include <mutex>
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64.h"
}
#include "functions.h"

namespace {
std::mutex mutex;
decals::Pool pool;
ImpactDecals::Frame published{};
bool wanted = false;
bool hasTick = false;
unsigned lastTick = 0;

bool Enabled() {
    return CVarGetInteger("gDeveloperTools.ImpactDecals", 1) != 0;
}
bool Paused(const PlayState* play) {
    return play->pauseCtx.state != 0 || play->pauseCtx.debugState != 0;
}

// Mutex tomado. Sincroniza antes dos eventos e ignora múltiplos draws do mesmo tick.
bool Prepare(PlayState* play) {
    if (!play || !wanted || !Enabled())
        return false;
    const unsigned tick = play->gameplayFrames;
    const bool changed =
        pool.Synchronize(reinterpret_cast<std::uintptr_t>(play), play->sceneNum, play->roomCtx.curRoom.num);
    if (changed || (hasTick && tick < lastTick)) {
        if (!changed)
            pool.Reset();
        published = {};
        lastTick = tick;
        hasTick = true;
    }
    if (!hasTick) {
        lastTick = tick;
        hasTick = true;
    }
    if (tick != lastTick) {
        // OoT: 20 ticks/s. Sem catch-up depois de pausa ou draw omitido.
        pool.Step(1.0f / 20.0f, Paused(play));
        lastTick = tick;
    }
    return !Paused(play);
}

Vec3f Vector(const float* value) {
    return { value[0], value[1], value[2] };
}
bool Finite(const float* value) {
    return value && std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

// Só recebe a colisão real; sem normal padrão/floor inventado e sem ponteiro persistido.
void Trace(PlayState* play, Vec3f start, Vec3f end, decals::Kind kind, float radius) {
    Vec3f point{};
    CollisionPoly* poly = nullptr;
    s32 bgId = BGCHECK_SCENE;
    if (!BgCheck_EntityLineTest1(&play->colCtx, &start, &end, &point, &poly, true, true, true, true, &bgId) || !poly ||
        bgId != BGCHECK_SCENE || SurfaceType_IsIgnoredByEntities(&play->colCtx, poly, bgId) ||
        SurfaceType_GetFloorType(&play->colCtx, poly, bgId) == 6)
        return;
    // Mesma conversão de z_bgcheck.c:231; a função auxiliar não é pública em functions.h.
    const Vec3f normal{ COLPOLY_GET_NORMAL(poly->normal.x), COLPOLY_GET_NORMAL(poly->normal.y),
                        COLPOLY_GET_NORMAL(poly->normal.z) };
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (!std::isfinite(length) || length < 0.001f)
        return;
    const decals::Vec3 n{ normal.x / length, normal.y / length, normal.z / length };
    const decals::Vec3 p{ point.x, point.y, point.z };
    if (pool.Recent(p, n, kind, radius * 0.5f, 0.18f))
        return;
    const bool scorch = kind == decals::Kind::Scorch;
    pool.Insert(p, n, radius, kind, scorch ? 45.0f : 25.0f, scorch ? 12.0f : 8.0f);
}
} // namespace

namespace ImpactDecals {
void SetWanted(bool value) {
    std::lock_guard lock(mutex);
    if (wanted == value)
        return;
    wanted = value;
    pool.Reset();
    published = {};
    hasTick = false;
}

Frame Read() {
    std::lock_guard lock(mutex);
    return wanted && Enabled() ? published : Frame{};
}
} // namespace ImpactDecals

extern "C" void ImpactDecals_Reset(void) {
    std::lock_guard lock(mutex);
    pool.Reset();
    published = {};
    hasTick = false;
}

extern "C" void ImpactDecals_OnSwordSweep(PlayState* play, const float* start, const float* end) {
    if (!Finite(start) || !Finite(end))
        return;
    std::lock_guard lock(mutex);
    if (!Prepare(play))
        return;
    Trace(play, Vector(start), Vector(end), decals::Kind::Sword, 12.0f);
    published = pool.Publish();
}

extern "C" void ImpactDecals_OnExplosion(PlayState* play, const float* position) {
    if (!Finite(position))
        return;
    std::lock_guard lock(mutex);
    if (!Prepare(play))
        return;
    const Vec3f origin = Vector(position);
    constexpr float reach = 80.0f;
    // Seis sondas limitadas por evento, não por quadro. Bomba no ar sem superfície não deixa marca.
    for (const auto& direction : std::array<decals::Vec3, 6>{
             { { 0, -1, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 0, 1 } } }) {
        Vec3f end{ origin.x + reach * direction[0], origin.y + reach * direction[1], origin.z + reach * direction[2] };
        Trace(play, origin, end, decals::Kind::Scorch, 44.0f);
    }
    published = pool.Publish();
}

extern "C" void ImpactDecals_Tick(PlayState* play) {
    std::lock_guard lock(mutex);
    if (!play || !wanted || !Enabled()) {
        pool.Reset();
        published = {};
        hasTick = false;
        return;
    }
    Prepare(play);
    published = pool.Publish();
}
