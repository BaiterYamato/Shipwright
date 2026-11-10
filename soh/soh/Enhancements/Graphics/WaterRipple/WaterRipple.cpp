#include "WaterRipple.h"
#include "water_ripple_field.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>

extern "C" {
#include "z64.h"
#include "macros.h"
}

namespace {

constexpr int StepsPerTick = 3;     // 20 Hz de jogo, passo fixo de 1/60 s
constexpr int QuietTicks = 20 * 12; // sem eventos por 12 s: o campo já decaiu
constexpr float PlaneTolerance = 2.0f;
constexpr float Strength = 1.0f;

std::atomic<bool> wanted{ false };
std::mutex mutex;
std::unique_ptr<water::Field> field;
std::vector<uint16_t> packed;
WaterRipple::Frame current{};
uint64_t currentVersion = 0;
float planeY = 0.0f;
bool hasPlane = false;
int quiet = QuietTicks;
int lastScene = -1;
const void* lastPlay = nullptr;

void Publish(bool active) {
    const auto uv = field->WorldToUV();
    current.worldToUV = { uv[0], uv[1], uv[2], uv[3] };
    current.info = { planeY, Strength, 1.0f / WaterRipple::Side, active ? 1.0f : 0.0f };
    if (active) {
        field->PackTextureRGBA16(packed);
    } else {
        std::fill(packed.begin(), packed.end(), uint16_t{ 0 });
    }
    ++currentVersion;
}

// Chamada com o mutex tomado.
bool Prepare(PlayState* play, float y) {
    if (!play) {
        return false;
    }
    if (!field) {
        water::Config config;
        config.side = WaterRipple::Side;
        field = std::make_unique<water::Field>(config);
        packed.assign(size_t{ WaterRipple::Side } * WaterRipple::Side * 4, 0);
    }
    if (!hasPlane || std::fabs(y - planeY) > PlaneTolerance) {
        field->Reset();
        planeY = y;
        hasPlane = true;
    }
    quiet = 0;
    return true;
}

} // namespace

namespace WaterRipple {

void SetWanted(bool value) {
    wanted.store(value);
    if (!value) {
        std::lock_guard lock(mutex);
        field.reset();
        packed.clear();
        current = {};
        hasPlane = false;
        quiet = QuietTicks;
        ++currentVersion;
    }
}

bool Read(uint64_t& version, std::vector<uint16_t>& texels, Frame& frame) {
    std::lock_guard lock(mutex);
    frame = current;
    if (version == currentVersion || packed.empty()) {
        return false;
    }
    texels = packed;
    version = currentVersion;
    return true;
}

} // namespace WaterRipple

extern "C" void WaterRipple_OnRipple(PlayState* play, const float* pos, short radius, short radiusMax) {
    if (!wanted.load(std::memory_order_relaxed) || !pos) {
        return;
    }
    std::lock_guard lock(mutex);
    if (!Prepare(play, pos[1])) {
        return;
    }
    const float size = static_cast<float>(std::max(radius, radiusMax));
    field->Inject(pos[0], pos[2], std::clamp(size * 0.06f, 20.0f, 64.0f),
                  -std::clamp(40.0f + size * 0.12f, 40.0f, 160.0f));
}

extern "C" void WaterRipple_OnSplash(PlayState* play, const float* pos, short scale) {
    if (!wanted.load(std::memory_order_relaxed) || !pos) {
        return;
    }
    std::lock_guard lock(mutex);
    if (!Prepare(play, pos[1])) {
        return;
    }
    const float size = static_cast<float>(std::max<short>(scale, 0));
    field->Inject(pos[0], pos[2], std::clamp(24.0f + size * 0.05f, 24.0f, 80.0f),
                  -std::clamp(120.0f + size * 0.1f, 120.0f, 220.0f));
}

extern "C" void WaterRipple_Tick(PlayState* play) {
    if (!wanted.load(std::memory_order_relaxed) || !play) {
        return;
    }
    std::lock_guard lock(mutex);
    if (!field) {
        return;
    }
    if (play != lastPlay || play->sceneNum != lastScene) {
        lastPlay = play;
        lastScene = play->sceneNum;
        field->Reset();
        hasPlane = false;
        quiet = QuietTicks;
        Publish(false);
        return;
    }
    if (quiet >= QuietTicks || play->pauseCtx.state != 0) {
        return;
    }
    if (const Player* player = GET_PLAYER(play)) {
        field->Follow(player->actor.world.pos.x, player->actor.world.pos.z);
    }
    for (int i = 0; i < StepsPerTick; ++i) {
        field->Step();
    }
    if (++quiet >= QuietTicks) {
        field->Reset();
        Publish(false);
    } else {
        Publish(true);
    }
}
