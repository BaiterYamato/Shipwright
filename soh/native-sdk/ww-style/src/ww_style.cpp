// Wind Waker Style como mod Link-Span: o port do fork (cel shading, sombras de ator, luzes e céu) sobre os serviços
// do host OoT. O main.lua desenha o menu e manda a configuração; esta DLL só aplica política pelos hooks.
#include <cstdio>
#include <cstring>
#include <new>
#include <string_view>

#include "oot_layout_id.h"
#include "sky.h"
#include "ww_style.h"

namespace WWStyle {
namespace {

ShipNativeStatus SHIP_NATIVE_CALL OnPlayUpdate(void* user, const ShipNativeHookCall* call) {
    auto& mod = *static_cast<Mod*>(user);
    auto* play = static_cast<PlayState*>(static_cast<const ShipOotPlayHookV1*>(call->payload)->play_state);
    ToonFrame(mod, play);
    WorldFrame(mod, play);
    return SHIP_NATIVE_OK;
}

// Depois do Play_Update: o Link já atualizou neste frame.
ShipNativeStatus SHIP_NATIVE_CALL OnPlayUpdateAfter(void* user, const ShipNativeHookCall* call) {
    DekuStickLight(*static_cast<Mod*>(user),
                   static_cast<PlayState*>(static_cast<const ShipOotPlayHookV1*>(call->payload)->play_state));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnPointLightColor(void* user, const ShipNativeHookCall* call) {
    FlameFlicker(*static_cast<Mod*>(user), *static_cast<ShipOotPointLightColorHookV2*>(call->payload));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnWorldLights(void* user, const ShipNativeHookCall* call) {
    DrawWorldLights(*static_cast<Mod*>(user),
                    static_cast<PlayState*>(static_cast<const ShipOotRenderPlayHookV1*>(call->payload)->play_state));
    return SHIP_NATIVE_OK;
}

PlayState* RenderPlay(const ShipNativeHookCall* call) {
    return static_cast<PlayState*>(static_cast<const ShipOotRenderPlayHookV1*>(call->payload)->play_state);
}

// Céu: logo depois do skybox vanilla e antes do sol, da lua e do mundo, na ordem cúpula, estrelas, nuvens.
ShipNativeStatus SHIP_NATIVE_CALL OnSkyGradient(void* user, const ShipNativeHookCall* call) {
    DrawSkyGradient(*static_cast<Mod*>(user), RenderPlay(call));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnSky(void* user, const ShipNativeHookCall* call) {
    DrawNightSky(*static_cast<Mod*>(user), RenderPlay(call));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnSkyClouds(void* user, const ShipNativeHookCall* call) {
    auto& mod = *static_cast<Mod*>(user);
    PlayState* play = RenderPlay(call);
    DrawClouds(mod, play);
    DrawWindWisps(mod, play);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnFileSelectSky(void* user, const ShipNativeHookCall* call) {
    DrawFileSelectSky(*static_cast<Mod*>(user),
                      static_cast<View*>(static_cast<const ShipOotRenderFileSelectSkyHookV1*>(call->payload)->view));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnRenderActorDraw(void* user, const ShipNativeHookCall* call) {
    ToonActorDraw(*static_cast<Mod*>(user), *static_cast<const ShipOotRenderActorDrawHookV1*>(call->payload));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnFairyLights(void* user, const ShipNativeHookCall* call) {
    FairyLights(*static_cast<Mod*>(user), *static_cast<ShipOotFairyLightHookV1*>(call->payload));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Register(const ShipNativeRuntime* runtime, Mod* mod, const char* point, uint32_t version,
                          uint32_t size, uint32_t mode, ShipNativeHookFn callback,
                          uint32_t phase = SHIP_NATIVE_HOOK_BEFORE) {
    const ShipNativeHookSpec spec{sizeof(ShipNativeHookSpec), point, version, size, mode, phase, 0, callback, mod};
    uint64_t handle = 0;
    return runtime->register_hook(runtime->context, &spec, &handle);
}

ShipNativeStatus Reply(ShipNativeWriteFn write, void* writer, const char* text, int count, size_t capacity) {
    return count < 0 || static_cast<size_t>(count) >= capacity ? SHIP_NATIVE_FAILURE
                                                               : write(writer, text, static_cast<uint32_t>(count));
}

// "configure": snapshot "chave=valor" do main.lua. Responde quantas chaves entraram.
ShipNativeStatus SHIP_NATIVE_CALL Configure(void* user, const char* input, uint32_t length, ShipNativeWriteFn write,
                                            void* writer) {
    auto& mod = *static_cast<Mod*>(user);
    Settings next = mod.cfg;
    const ParseResult parsed = ParseSettings(std::string_view(input ? input : "", input ? length : 0), next);
    mod.cfg = next;
    const ShipNativeStatus status = ApplyRenderState(mod);
    if (status != SHIP_NATIVE_OK) return status;
    ++mod.stats.configures;
    char text[128];
    const int count = std::snprintf(text, sizeof(text), "ww-style: %u chaves, %u desconhecidas, %u invalidas",
                                    parsed.applied, parsed.unknown, parsed.invalid);
    return Reply(write, writer, text, count, sizeof(text));
}

ShipNativeStatus SHIP_NATIVE_CALL Stats(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                        void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    const auto& mod = *static_cast<const Mod*>(user);
    const auto& s = mod.stats;
    const auto& c = mod.cfg;
    char text[640];
    const int count = std::snprintf(
        text, sizeof(text),
        "cfg=%u cel=%u shadows=%u | draws=%u excl=%u toonoff=%u point=%u env=%u keyfail=%u armed=%u off=%u ray=%u "
        "states=%zu dbgrays=%u dbgskip=%u | fairy=%u navi=%u navi_skip=%u tint=%u wild=%u | flicker=%u/%zu "
        "pools=%u poolfail=%u lights=%zu wildlive=%zu stick=%u | sky=%u domes=%u stars=%u bands=%u clouds=%u "
        "wisps=%u texpack=%u skyfail=%u",
        s.configures, c.celEnabled ? 1u : 0u, c.shadowsEnabled ? 1u : 0u, s.actorDraws, s.excluded, s.toonOff,
        s.pointKeys, s.envKeys, s.keyFailures, s.shadowsArmed, s.shadowsOff, s.raycasts, mod.toon.keys.size(),
        s.debugRays, s.debugSkipped, s.fairyCalls, s.naviSeen, s.naviSkipped, s.naviTinted, s.wildFairyLit,
        s.flickered, mod.lighting.flicker.size(), s.pools, s.poolFailures, mod.lighting.lights.size(),
        mod.lighting.wildLights.size(), s.stickLights, c.skyEnabled ? 1u : 0u, s.skyDomes, s.skyStars, s.skyBands,
        s.skyClouds, s.skyWisps, s.skyTexOverrides, s.skyFailures);
    return Reply(write, writer, text, count, sizeof(text));
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || !instance || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_minor < 2 ||
        !runtime->get_service || !runtime->register_hook || !runtime->register_function) return SHIP_NATIVE_UNSUPPORTED;
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    const auto* render = static_cast<const ShipOotRenderV3*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RENDER_SERVICE, LINKSPAN_OOT_RENDER_VERSION_3, sizeof(ShipOotRenderV3)));
    const auto* lights = static_cast<const ShipOotLightsV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_LIGHTS_SERVICE, LINKSPAN_OOT_LIGHTS_VERSION, sizeof(ShipOotLightsV1)));
    const auto* world = static_cast<const ShipOotWorldV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_WORLD_SERVICE, LINKSPAN_OOT_WORLD_VERSION, sizeof(ShipOotWorldV1)));
    if (!engine || !render || !lights || !world || !engine->layout_id ||
        std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID))
        return SHIP_NATIVE_UNSUPPORTED;
    // Opcional: sem ele as nuvens ficam só com as texturas geradas.
    const auto* resources = static_cast<const ShipOotResourcesV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION, sizeof(ShipOotResourcesV1)));
    auto* mod = new (std::nothrow) Mod;
    if (!mod) return SHIP_NATIVE_FAILURE;
    mod->runtime = runtime;
    mod->engine = engine;
    mod->render = render;
    mod->lights = lights;
    mod->world = world;
    mod->resources = resources;
    // Até o primeiro configure vale o padrão do fork (cel ligado, sombras desligadas).
    ShipNativeStatus status = render->acquire_state(kOwner, &mod->renderState);
    if (status == SHIP_NATIVE_OK) status = ApplyRenderState(*mod);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_PLAY_UPDATE, 1u,
        sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnPlayUpdate);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_RENDER_ACTOR_DRAW, 1u,
        sizeof(ShipOotRenderActorDrawHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnRenderActorDraw);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_LIGHT_FAIRY,
        LINKSPAN_OOT_HOOK_LIGHT_FAIRY_VERSION, sizeof(ShipOotFairyLightHookV1), SHIP_NATIVE_HOOK_TRANSFORM,
        OnFairyLights);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_PLAY_UPDATE, 1u,
        sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnPlayUpdateAfter, SHIP_NATIVE_HOOK_AFTER);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR,
        LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR_VERSION, sizeof(ShipOotPointLightColorHookV2), SHIP_NATIVE_HOOK_TRANSFORM,
        OnPointLightColor);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_RENDER_WORLD_LIGHTS, 1u,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnWorldLights);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_RENDER_SKY_GRADIENT, 1u,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnSkyGradient);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_RENDER_SKY, 1u,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnSky);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_RENDER_SKY_CLOUDS, 1u,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnSkyClouds);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, mod, LINKSPAN_OOT_HOOK_RENDER_FILE_SELECT_SKY, 1u,
        sizeof(ShipOotRenderFileSelectSkyHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnFileSelectSky);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "configure", Configure, mod);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "stats", Stats, mod);
    if (status != SHIP_NATIVE_OK) {
        if (mod->renderState) render->release_state(mod->renderState);
        delete mod;
        return status;
    }
    *instance = mod;
    return SHIP_NATIVE_OK;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* mod = static_cast<Mod*>(instance);
    if (mod && mod->render && mod->renderState) mod->render->release_state(mod->renderState);
    delete mod;
}

} // namespace
} // namespace WWStyle

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u,
                                                 WWStyle::Init, WWStyle::Shutdown};
    return &descriptor;
}
