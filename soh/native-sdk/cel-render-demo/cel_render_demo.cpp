// Fixture CEL-003/CEL-004: exercita o serviço de render, as luzes de mod e todos os hooks novos sem carregar
// política do Wind Waker. Y alterna tudo; desligada não emite opcode nem mexe em luz.
//
// CEL-004 (0.2.0): um losango translúcido gira acima do Link por uma display list da própria DLL (render V3:
// matriz NEW, rotação por eixo, Mtx do frame e filho de interpolação); uma luz azul do linkspan.oot.lights segue o
// Link; a Navi fica vermelha pelo oot.light.fairy, e as fadas soltas ganham raio como no fork; no modo 3 o Link
// sai do colchete toon.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>

#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_layout_id.h"
#include "oot_lights.h"
#include "oot_render.h"
#include "z64.h"
#include "macros.h"

namespace {
constexpr const char* kOwner = "linkspan.cel_render_demo";
constexpr uint32_t kFeatures = LINKSPAN_OOT_RENDER_FEATURE_TOON_ACTORS |
                               LINKSPAN_OOT_RENDER_FEATURE_SUPPRESS_VANILLA_SHADOWS |
                               LINKSPAN_OOT_RENDER_FEATURE_HIDE_VANILLA_POINT_GLOW;
constexpr uint32_t kModes = 4;

struct Demo {
    const ShipNativeRuntime* runtime = nullptr;
    const ShipOotRenderV3* render = nullptr;
    const ShipOotLightsV1* lights = nullptr;
    uint64_t renderState = 0;
    uint64_t light = 0;
    bool enabled = true;
    // U alterna o que a fixture liga, para isolar cada parte do transporte: 0 = tudo, 1 = só toon, 2 = só sombra,
    // 3 = tudo com o Link fora do colchete toon.
    uint32_t mode = 0;
    uint32_t actorDraws = 0;
    uint32_t worldLights = 0;
    uint32_t skyGradient = 0;
    uint32_t sky = 0;
    uint32_t skyClouds = 0;
    uint32_t fileSelectSky = 0;
    uint32_t pointColors = 0;
    // CEL-004.
    uint32_t nativeDraws = 0;
    uint32_t nativeFailures = 0;
    uint32_t ownLightColors = 0;
    uint32_t lightFailures = 0;
    uint32_t fairies = 0;
    uint32_t naviLights = 0;
    uint32_t toonOptOuts = 0;
    float spin = 0.0f;
    // Display list e vértices do losango: memória da DLL, reescrita uma vez por frame de jogo.
    Gfx dl[16]{};
    Vtx vtx[4]{};
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

void SetVertex(Vtx& v, int16_t x, int16_t y, int16_t z) {
    v = Vtx{};
    v.v.ob[0] = x;
    v.v.ob[1] = y;
    v.v.ob[2] = z;
    v.v.cn[0] = v.v.cn[1] = v.v.cn[2] = v.v.cn[3] = 255;
}

// Losango de 40 unidades no plano XY local, girando em Y, 90 unidades acima do Link.
void DrawMarker(Demo& demo, const Actor* player) {
    auto* render = demo.render;
    ShipOotRenderFrameInfoV1 info{sizeof(info)};
    if (render->get_frame_info(&info) == SHIP_NATIVE_OK) demo.spin += 3.0f * info.delta_seconds;
    const void* mtx = nullptr;
    if (render->interpolation_begin(&demo, 0) != SHIP_NATIVE_OK) {
        ++demo.nativeFailures;
        return;
    }
    ShipNativeStatus status = render->matrix_push();
    if (status == SHIP_NATIVE_OK) {
        status = render->matrix_translate_new(player->world.pos.x, player->world.pos.y + 90.0f, player->world.pos.z);
        if (status == SHIP_NATIVE_OK) status = render->matrix_rotate_axis(demo.spin, 0.0f, 1.0f, 0.0f);
        if (status == SHIP_NATIVE_OK) status = render->export_current_matrix(&mtx);
        render->matrix_pop();
    }
    if (status == SHIP_NATIVE_OK) {
        SetVertex(demo.vtx[0], 0, 20, 0);
        SetVertex(demo.vtx[1], 20, 0, 0);
        SetVertex(demo.vtx[2], 0, -20, 0);
        SetVertex(demo.vtx[3], -20, 0, 0);
        Gfx* g = demo.dl;
        gDPPipeSync(g++);
        gDPSetCycleType(g++, G_CYC_1CYCLE);
        gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetCombineMode(g++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetPrimColor(g++, 0, 0, 80, 255, 160, 170);
        gDPSetRenderMode(g++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
        gSPClearGeometryMode(g++, G_CULL_BOTH | G_LIGHTING | G_FOG | G_TEXTURE_GEN);
        gSPSetGeometryMode(g++, G_ZBUFFER | G_SHADE);
        gSPMatrix(g++, const_cast<void*>(mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        __gSPVertex(g++, reinterpret_cast<uintptr_t>(demo.vtx), 4, 0);
        gSP2Triangles(g++, 0, 1, 2, 0, 0, 2, 3, 0);
        gDPPipeSync(g++);
        gSPEndDisplayList(g++);
        status = render->draw_native_display_list(demo.dl, LINKSPAN_OOT_RENDER_TRANSLUCENT);
    }
    render->interpolation_end();
    if (status == SHIP_NATIVE_OK) ++demo.nativeDraws;
    else ++demo.nativeFailures;
}

ShipNativeStatus SHIP_NATIVE_CALL OnActorDraw(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    ++demo.actorDraws;
    if (!demo.enabled) return SHIP_NATIVE_OK;
    const auto* payload = static_cast<const ShipOotRenderActorDrawHookV1*>(call->payload);
    // Toda borda do colchete invalida a chave toon: a chave vai depois do opt-out e em todo ator.
    if (demo.mode == 3 && payload->actor_id == ACTOR_PLAYER &&
        demo.render->set_actor_toon_enabled(0) == SHIP_NATIVE_OK)
        ++demo.toonOptOuts;
    // Luz-chave quente vinda de cima e sombra de ator armada: a sombra é desenhada com os volumes de stencil
    // do transporte, então aparecer no chão prova toon e stencil. O modo de stencil cru fica em Off.
    if (demo.mode != 2) demo.render->emit_toon_key(LINKSPAN_OOT_RENDER_OPAQUE, -48, -96, 48, 255, 210, 120);
    demo.render->emit_stencil(LINKSPAN_OOT_RENDER_OPAQUE, 0);
    if (demo.mode != 1) demo.render->emit_toon_shadow(LINKSPAN_OOT_RENDER_OPAQUE, -32768, 1.0f);
    if (payload->actor_id == ACTOR_PLAYER) DrawMarker(demo, static_cast<const Actor*>(payload->actor));
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
    auto* light = static_cast<ShipOotPointLightColorHookV2*>(call->payload);
    // A luz da própria fixture passa por aqui como uma tocha, e a identidade a separa das outras.
    const void* own = nullptr;
    if (demo.light && demo.lights->get_point_light_info(demo.light, &own) == SHIP_NATIVE_OK && own == light->light) {
        ++demo.ownLightColors;
        return SHIP_NATIVE_OK;
    }
    light->r = static_cast<uint8_t>(light->r / 2);
    light->g = static_cast<uint8_t>(light->g / 2);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL TransformFairy(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    ++demo.fairies;
    if (!demo.enabled) return SHIP_NATIVE_OK;
    auto* fairy = static_cast<ShipOotFairyLightHookV1*>(call->payload);
    if (fairy->params == LINKSPAN_OOT_FAIRY_NAVI) {
        ++demo.naviLights;
        for (auto* light : { &fairy->no_glow, &fairy->glow }) {
            light->color[0] = 255;
            light->color[1] = 64;
            light->color[2] = 64;
        }
    } else if (fairy->params == LINKSPAN_OOT_FAIRY_KOKIRI || fairy->params == LINKSPAN_OOT_FAIRY_HEAL ||
               fairy->params == LINKSPAN_OOT_FAIRY_HEAL_BIG || fairy->params == LINKSPAN_OOT_FAIRY_HEAL_TIMED) {
        const auto* actor = static_cast<const Actor*>(fairy->actor);
        fairy->no_glow.position[0] = actor->world.pos.x;
        fairy->no_glow.position[1] = actor->world.pos.y;
        fairy->no_glow.position[2] = actor->world.pos.z;
        fairy->no_glow.radius = (fairy->fairy_flags & LINKSPAN_OOT_FAIRY_FLAG_BIG) ? 150 : 100;
        fairy->no_glow.color[0] = fairy->no_glow.color[1] = fairy->no_glow.color[2] = 255;
    }
    return SHIP_NATIVE_OK;
}

// Luz azul 60 unidades acima do Link enquanto a fixture está ligada; a troca de cena apaga e o frame seguinte cria.
ShipNativeStatus SHIP_NATIVE_CALL OnPlayUpdate(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    if (!demo.enabled) return SHIP_NATIVE_OK;
    auto* play = static_cast<PlayState*>(static_cast<const ShipOotPlayHookV1*>(call->payload)->play_state);
    const Player* player = play ? GET_PLAYER(play) : nullptr;
    if (!player) return SHIP_NATIVE_OK;
    ShipOotPointLightV1 light{sizeof(light),
                              {player->actor.world.pos.x, player->actor.world.pos.y + 60.0f, player->actor.world.pos.z},
                              300, {60, 140, 255}, 0};
    if (demo.light && demo.lights->update_point_light(demo.light, &light) == SHIP_NATIVE_OK) return SHIP_NATIVE_OK;
    demo.light = 0;
    if (demo.lights->create_point_light(kOwner, &light, &demo.light) != SHIP_NATIVE_OK) ++demo.lightFailures;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Register(const ShipNativeRuntime* runtime, Demo* demo, const char* point, uint32_t version,
                          uint32_t size, uint32_t mode, ShipNativeHookFn callback) {
    const ShipNativeHookSpec spec{sizeof(ShipNativeHookSpec), point, version, size, mode,
                                  SHIP_NATIVE_HOOK_BEFORE, 0, callback, demo};
    uint64_t handle = 0;
    return runtime->register_hook(runtime->context, &spec, &handle);
}

void DropLight(Demo& demo) {
    if (demo.light) demo.lights->destroy_point_light(demo.light);
    demo.light = 0;
}

ShipNativeStatus SHIP_NATIVE_CALL Toggle(void* user, const char*, uint32_t length, ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& demo = *static_cast<Demo*>(user);
    demo.enabled = !demo.enabled;
    if (!demo.enabled) DropLight(demo);
    auto status = Apply(demo);
    if (status == SHIP_NATIVE_OK) status = ApplyLook(demo);
    if (status != SHIP_NATIVE_OK) return status;
    const char* text = demo.enabled ? "CEL fixture ligada" : "CEL fixture desligada";
    return write(writer, text, static_cast<uint32_t>(std::strlen(text)));
}

ShipNativeStatus SHIP_NATIVE_CALL Mode(void* user, const char*, uint32_t length, ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& demo = *static_cast<Demo*>(user);
    demo.mode = (demo.mode + 1) % kModes;
    const auto status = Apply(demo);
    if (status != SHIP_NATIVE_OK) return status;
    static const char* const names[] = { "CEL modo: tudo", "CEL modo: so toon", "CEL modo: so sombra",
                                         "CEL modo: tudo, Link fora do toon" };
    return write(writer, names[demo.mode], static_cast<uint32_t>(std::strlen(names[demo.mode])));
}

ShipNativeStatus SHIP_NATIVE_CALL Stats(void* user, const char*, uint32_t length, ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    const auto& d = *static_cast<Demo*>(user);
    ShipOotRenderFrameInfoV1 info{sizeof(info)};
    if (d.render->get_frame_info(&info) != SHIP_NATIVE_OK) info = ShipOotRenderFrameInfoV1{};
    char text[480];
    const int count = std::snprintf(text, sizeof(text),
        "on=%u mode=%u actor=%u world=%u gradient=%u sky=%u clouds=%u file=%u point=%u | native=%u/%u own=%u "
        "light=%s/%u fairy=%u navi=%u optout=%u frame=%u epoch=%u dt=%.3f ar=%.2f",
        d.enabled ? 1u : 0u, d.mode, d.actorDraws, d.worldLights, d.skyGradient, d.sky, d.skyClouds, d.fileSelectSky,
        d.pointColors, d.nativeDraws, d.nativeFailures, d.ownLightColors, d.light ? "sim" : "nao", d.lightFailures,
        d.fairies, d.naviLights, d.toonOptOuts, info.frame, info.camera_epoch, info.delta_seconds, info.aspect_ratio);
    return count < 0 || static_cast<size_t>(count) >= sizeof(text) ? SHIP_NATIVE_FAILURE
        : write(writer, text, static_cast<uint32_t>(count));
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
    if (!engine || !render || !lights || !engine->layout_id || std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID))
        return SHIP_NATIVE_UNSUPPORTED;
    auto* demo = new (std::nothrow) Demo;
    if (!demo) return SHIP_NATIVE_FAILURE;
    demo->runtime = runtime;
    demo->render = render;
    demo->lights = lights;
    ShipNativeStatus status = render->acquire_state(kOwner, &demo->renderState);
    if (status == SHIP_NATIVE_OK) status = Apply(*demo);
    if (status == SHIP_NATIVE_OK) status = ApplyLook(*demo);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_ACTOR_DRAW, 1u,
        sizeof(ShipOotRenderActorDrawHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnActorDraw);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_WORLD_LIGHTS, 1u,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountWorldLights);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_SKY_GRADIENT, 1u,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountSkyGradient);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_SKY, 1u,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountSky);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_SKY_CLOUDS, 1u,
        sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountSkyClouds);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_RENDER_FILE_SELECT_SKY, 1u,
        sizeof(ShipOotRenderFileSelectSkyHookV1), SHIP_NATIVE_HOOK_OBSERVE, CountFileSelectSky);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR,
        LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR_VERSION, sizeof(ShipOotPointLightColorHookV2), SHIP_NATIVE_HOOK_TRANSFORM,
        TransformPointColor);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_LIGHT_FAIRY,
        LINKSPAN_OOT_HOOK_LIGHT_FAIRY_VERSION, sizeof(ShipOotFairyLightHookV1), SHIP_NATIVE_HOOK_TRANSFORM,
        TransformFairy);
    if (status == SHIP_NATIVE_OK) status = Register(runtime, demo, LINKSPAN_OOT_HOOK_PLAY_UPDATE, 1u,
        sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, OnPlayUpdate);
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
    if (demo && demo->lights) DropLight(*demo);
    delete demo;
}
} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init, Shutdown};
    return &descriptor;
}
