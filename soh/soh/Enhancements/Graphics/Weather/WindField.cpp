#include "WindField.h"

#include <mutex>
#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
}

namespace {
WindField::State state;
WindField::Frame published{};
std::mutex mutex;
int loggedScene = -1;
} // namespace

namespace WindField {
Frame Read() {
    std::lock_guard lock(mutex);
    return published;
}
} // namespace WindField

extern "C" void WindField_Tick(PlayState* play) {
    WindField::Input in;
    if (play) {
        in.session = reinterpret_cast<std::uintptr_t>(play);
        in.scene = play->sceneNum;
        in.tick = play->gameplayFrames;
        in.paused = play->pauseCtx.state != 0 || play->pauseCtx.debugState != 0;
        in.enabled = CVarGetInteger("gDeveloperTools.WindField", 1) != 0;
        const auto& direction = play->envCtx.windDirection;
        in.direction = { static_cast<float>(direction.x), static_cast<float>(direction.y),
                         static_cast<float>(direction.z) };
        const bool hasSky = WindField::AmbientSky(play->skyboxId);
        const int ambient = CVarGetInteger("gDeveloperTools.WindAmbient", 40);
        in.speed = WindField::EffectiveSpeed(play->envCtx.windSpeed, hasSky, static_cast<float>(ambient));
        // Só para teste: força a velocidade (0..255) sem mexer no envCtx do jogo.
        const int override = CVarGetInteger("gDeveloperTools.WindSpeedOverride", -1);
        if (override >= 0)
            in.speed = static_cast<float>(override);
        // Uma linha por cena: o vento real da cena, para calibrar e provar.
        if (in.scene != loggedScene) {
            loggedScene = in.scene;
            SPDLOG_INFO("WindField: cena 0x{:02X} skybox {} direcao ({}, {}, {}) velocidade {} efetiva {} "
                        "(ambiente {}, override {})",
                        in.scene, play->skyboxId, direction.x, direction.y, direction.z, play->envCtx.windSpeed,
                        in.speed, ambient, override);
        }
    }
    std::lock_guard lock(mutex);
    published = state.Step(in);
}

extern "C" void WindField_Sample(const float* pos, float* out) {
    WindField::Frame frame;
    {
        std::lock_guard lock(mutex);
        frame = published;
    }
    const auto velocity = WindField::VelocityAt(frame, pos[0], pos[2]);
    out[0] = velocity[0];
    out[1] = velocity[1];
    out[2] = velocity[2];
}
