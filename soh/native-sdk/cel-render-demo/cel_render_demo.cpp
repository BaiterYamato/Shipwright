// Fixture CEL-003: exercita o serviço de render e todos os hooks novos sem
// carregar política do Wind Waker. Y alterna tudo; desligada não emite opcode.
#include <cstdio>
#include <cstring>
#include <new>

#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_layout_id.h"
#include "oot_render.h"

namespace {
constexpr const char* kOwner = "linkspan.cel_render_demo";
constexpr uint32_t kFeatures = LINKSPAN_OOT_RENDER_FEATURE_TOON_ACTORS |
                               LINKSPAN_OOT_RENDER_FEATURE_SUPPRESS_VANILLA_SHADOWS |
                               LINKSPAN_OOT_RENDER_FEATURE_HIDE_VANILLA_POINT_GLOW;

struct Demo {
    const ShipNativeRuntime* runtime = nullptr;
    const ShipOotRenderV2* render = nullptr;
    uint64_t renderState = 0;
    bool enabled = true;
    // U alterna o que a fixture liga, para isolar cada parte do transporte: 0 = tudo, 1 = só toon, 2 = só sombra.
    uint32_t mode = 0;
    uint32_t actorDraws = 0;
    uint32_t worldLights = 0;
    uint32_t skyGradient = 0;
    uint32_t sky = 0;
    uint32_t skyClouds = 0;
    uint32_t fileSelectSky = 0;
    uint32_t pointColors = 0;
};

ShipNativeStatus ApplyLook(Demo& demo) {
    // Rampa dura e sombra mais escura que o padrão, para a diferença ficar óbvia na captura.
    if (!demo.enabled) return SHIP_NATIVE_OK;
    const auto ramp = demo.render->set_toon_ramp(0.5f, 0.02f, 1.1f, 1.0f, 0);
    return ramp != SHIP_NATIVE_OK ? ramp : demo.render->set_toon_shadow_params(0.7f, 0.6f, 40.0f, 10.0f, 1, 0);
}

ShipNativeStatus Apply(Demo& demo) {
    // A fixture escolhe dois pisos de cenas vanilla; a lista real é sempre
    // política do mod e pode ser completamente diferente.
    static const int16_t receivers[] = { 0x00F8, 0x0102 };
    const uint32_t features = demo.mode == 2 ? (kFeatures & ~LINKSPAN_OOT_RENDER_FEATURE_TOON_ACTORS) : kFeatures;
    return demo.render->set_state(demo.renderState, demo.enabled ? features : 0u,
                                  demo.enabled ? receivers : nullptr,
                                  demo.enabled ? static_cast<uint32_t>(sizeof(receivers) / sizeof(receivers[0])) : 0u);
}

ShipNativeStatus SHIP_NATIVE_CALL OnActorDraw(void* user, const ShipNativeHookCall*) {
    auto& demo = *static_cast<Demo*>(user);
    ++demo.actorDraws;
    if (!demo.enabled) return SHIP_NATIVE_OK;
    // Luz-chave quente vinda de cima e sombra de ator armada: a sombra é desenhada com os volumes de stencil
    // do transporte, então aparecer no chão prova toon e stencil. O modo de stencil cru fica em Off.
    if (demo.mode != 2) demo.render->emit_toon_key(LINKSPAN_OOT_RENDER_OPAQUE, -48, -96, 48, 255, 210, 120);
    demo.render->emit_stencil(LINKSPAN_OOT_RENDER_OPAQUE, 0);
    if (demo.mode != 1) demo.render->emit_toon_shadow(LINKSPAN_OOT_RENDER_OPAQUE, -32768, 1.0f);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL CountWorldLights(void* user, const ShipNativeHookCall*) {
    ++static_cast<Demo*>(user)->worldLights;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL CountSkyGradient(void* user, const ShipNativeHookCall*) {
    ++static_cast<Demo*>(user)->skyGradient;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL CountSky(void* user, const ShipNativeHookCall*) {
    ++static_cast<Demo*>(user)->sky;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL CountSkyClouds(void* user, const ShipNativeHookCall*) {
    ++static_cast<Demo*>(user)->skyClouds;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL CountFileSelectSky(void* user, const ShipNativeHookCall*) {
    ++static_cast<Demo*>(user)->fileSelectSky;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL TransformPointColor(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    ++demo.pointColors;
    if (!demo.enabled) return SHIP_NATIVE_OK;
    auto* light = static_cast<ShipOotPointLightColorHookV1*>(call->payload);
    light->r = static_cast<uint8_t>(light->r / 2);
    light->g = static_cast<uint8_t>(light->g / 2);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Register(const ShipNativeRuntime* runtime, Demo* demo, const char* point, uint32_t size,
                          uint32_t mode, ShipNativeHookFn callback) {
    const ShipNativeHookSpec spec{sizeof(ShipNativeHookSpec), point, 1u, size, mode,
                                  SHIP_NATIVE_HOOK_BEFORE, 0, callback, demo};
    uint64_t handle = 0;
    return runtime->register_hook(runtime->context, &spec, &handle);
}

ShipNativeStatus SHIP_NATIVE_CALL Toggle(void* user, const char*, uint32_t length, ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& demo = *static_cast<Demo*>(user);
    demo.enabled = !demo.enabled;
    auto status = Apply(demo);
    if (status == SHIP_NATIVE_OK) status = ApplyLook(demo);
    if (status != SHIP_NATIVE_OK) return status;
    const char* text = demo.enabled ? "CEL fixture ligada" : "CEL fixture desligada";
    return write(writer, text, static_cast<uint32_t>(std::strlen(text)));
}

ShipNativeStatus SHIP_NATIVE_CALL Mode(void* user, const char*, uint32_t length, ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& demo = *static_cast<Demo*>(user);
    demo.mode = (demo.mode + 1) % 3;
    const auto status = Apply(demo);
    if (status != SHIP_NATIVE_OK) return status;
    static const char* const names[] = { "CEL modo: tudo", "CEL modo: so toon", "CEL modo: so sombra" };
    return write(writer, names[demo.mode], static_cast<uint32_t>(std::strlen(names[demo.mode])));
}

ShipNativeStatus SHIP_NATIVE_CALL Stats(void* user, const char*, uint32_t length, ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    const auto& d = *static_cast<Demo*>(user);
    char text[224];
    const int count = std::snprintf(text, sizeof(text),
        "on=%u mode=%u actor=%u world=%u gradient=%u sky=%u clouds=%u file=%u point=%u",
        d.enabled ? 1u : 0u, d.mode, d.actorDraws, d.worldLights, d.skyGradient, d.sky, d.skyClouds, d.fileSelectSky, d.pointColors);
    return count < 0 || static_cast<size_t>(count) >= sizeof(text) ? SHIP_NATIVE_FAILURE
        : write(writer, text, static_cast<uint32_t>(count));
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || !instance || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_minor < 2 ||
        !runtime->get_service || !runtime->register_hook || !runtime->register_function) return SHIP_NATIVE_UNSUPPORTED;
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    const auto* render = static_cast<const ShipOotRenderV2*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RENDER_SERVICE, LINKSPAN_OOT_RENDER_VERSION_2, sizeof(ShipOotRenderV2)));
    if (!engine || !render || !engine->layout_id || std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID))
        return SHIP_NATIVE_UNSUPPORTED;
    auto* demo = new (std::nothrow) Demo;
    if (!demo) return SHIP_NATIVE_FAILURE;
    demo->runtime = runtime;
    demo->render = render;
    ShipNativeStatus status = render->acquire_state(kOwner, &demo->renderState);
    if (status == SHIP_NATIVE_OK) status = Apply(*demo);
    if (status == SHIP_NATIVE_OK) status = ApplyLook(*demo);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_ACTOR_DRAW,
        sizeof(ShipOotRenderActorDrawHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnActorDraw);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_WORLD_LIGHTS,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountWorldLights);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_SKY_GRADIENT,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountSkyGradient);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_SKY,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountSky);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_SKY_CLOUDS,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountSkyClouds);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_FILE_SELECT_SKY,
        sizeof(ShipOotRenderFileSelectSkyHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountFileSelectSky);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR,
        sizeof(ShipOotPointLightColorHookV1), SHIP_NATIVE_HOOK_TRANSFORM, TransformPointColor);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "toggle", Toggle, demo);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "stats", Stats, demo);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "mode", Mode, demo);
    if (status != SHIP_NATIVE_OK) {
        render->release_state(demo->renderState);
        delete demo;
        return status;
    }
    *instance = demo;
    return SHIP_NATIVE_OK;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* demo = static_cast<Demo*>(instance);
    if (demo && demo->render) demo->render->release_state(demo->renderState);
    delete demo;
}
} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init, Shutdown};
    return &descriptor;
}
