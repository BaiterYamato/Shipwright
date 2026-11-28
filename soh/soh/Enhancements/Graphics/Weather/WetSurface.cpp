#include "WetSurface.h"

#include <cmath>
#include <mutex>
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64.h"
extern u8 gWeatherMode;
// Global de z_kankyo.c, apesar do prefixo s; valor real do flash desenhado.
extern s16 sLightningFlashAlpha;
}

namespace {
WetSurface::State state;
WetSurface::Frame published{};
std::mutex mutex;

bool ExposedSky(int skybox) {
    switch (skybox) {
        case SKYBOX_NORMAL_SKY:
        case SKYBOX_OVERCAST_SUNSET:
        case SKYBOX_MARKET_ADULT:
        case SKYBOX_CUTSCENE_MAP:
        case SKYBOX_MARKET_CHILD_DAY:
        case SKYBOX_MARKET_CHILD_NIGHT:
            return true;
        default:
            return false;
    }
}
} // namespace

namespace WetSurface {
Frame Read() {
    std::lock_guard lock(mutex);
    // O interruptor tambem vale quando o jogo nao esta produzindo ticks.
    if (!CVarGetInteger("gDeveloperTools.Wetness", 1))
        return {};
    return published;
}
} // namespace WetSurface

extern "C" void WetSurface_Tick(PlayState* play) {
    WetSurface::Input in;
    if (play) {
        in.session = reinterpret_cast<std::uintptr_t>(play);
        in.scene = play->sceneNum;
        in.skybox = play->skyboxId;
        in.tick = play->gameplayFrames;
        in.outdoor = ExposedSky(in.skybox);
        in.paused = play->pauseCtx.state != 0 || play->pauseCtx.debugState != 0;
        in.enabled = CVarGetInteger("gDeveloperTools.Wetness", 1) != 0;
        in.rain = WetSurface::RainIntensity(play->envCtx.unk_EE[1], play->envCtx.unk_EE[2], gWeatherMode);
        in.flash = static_cast<float>(sLightningFlashAlpha) / 200.0f;
        const auto& direction = play->envCtx.windDirection;
        const float x = direction.x, y = direction.y, z = direction.z;
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length > 0.0f) {
            const float speed = play->envCtx.windSpeed / length;
            in.wind = { x * speed, y * speed, z * speed };
        }
    }
    std::lock_guard lock(mutex);
    published = state.Step(in);
}
