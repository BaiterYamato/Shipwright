// Liga linkspan.oot.lights (OotNativeLights.cpp) ao jogo: um LightInfo fixo por vaga na lista play->lightCtx.
#include "OotNativeLights.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

// Endereço fixo: o mod reconhece a luz por ele (node->info e oot.light.point_color).
struct HostLight {
    LightInfo info;
    LightNode* node;
};
HostLight gLights[LINKSPAN_OOT_LIGHTS_MAX];

void* Gameplay() {
    return gPlayState;
}

// Pelo mesmo caminho das luzes do jogo: Lights_PointSetColorAndRadius dispara oot.light.point_color.
void SetInfo(LightInfo* info, const ShipOotPointLightV1& light) {
    Lights_PointSetInfo(info, light.position[0], light.position[1], light.position[2], light.color[0], light.color[1],
                        light.color[2], light.radius, light.glow ? LIGHT_POINT_GLOW : LIGHT_POINT_NOGLOW);
}

bool Insert(void* state, uint32_t slot, const ShipOotPointLightV1& light) {
    auto* play = static_cast<PlayState*>(state);
    HostLight& host = gLights[slot];
    SetInfo(&host.info, light);
    host.node = LightContext_InsertLight(play, &play->lightCtx, &host.info);
    return host.node != nullptr;
}

void Update(uint32_t slot, const ShipOotPointLightV1& light) {
    SetInfo(&gLights[slot].info, light);
}

void Remove(void* state, uint32_t slot) {
    auto* play = static_cast<PlayState*>(state);
    HostLight& host = gLights[slot];
    if (play && host.node) {
        LightContext_RemoveLight(play, &play->lightCtx, host.node);
    }
    host.node = nullptr;
}

const void* Info(uint32_t slot) {
    return &gLights[slot].info;
}

} // namespace

namespace ShipLuaHost {

void RegisterOotLightsGameHooks() {
    static bool registered = false;
    if (registered || !GameInteractor::Instance) {
        return;
    }
    registered = true;
    OotLightsBridge bridge;
    bridge.gameplay = Gameplay;
    bridge.insert = Insert;
    bridge.update = Update;
    bridge.remove = Remove;
    bridge.info = Info;
    SetOotLightsBridge(bridge);
    // No início de Play_Destroy a lista de luzes ainda é desta cena.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDestroy>(
        []() { ShipLuaHost::ReleaseOotSceneLights(); });
}

} // namespace ShipLuaHost
