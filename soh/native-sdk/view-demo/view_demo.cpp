// Demo de linkspan.oot.camera/render e do hook oot.player.limb_draw (OOT-CORE-005): desenha o
// modelo do coração na mão direita do Link e, em gameplay, alterna 4 segundos de câmera própria
// orbitando o Link com 4 segundos da câmera do jogo.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "oot_camera.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_layout_id.h"
#include "oot_render.h"
#include "z64.h"

namespace {

constexpr const char* OWNER = "linkspan.view-demo";
constexpr const char* MODEL = "objects/gameplay_keep/gHeartPieceInteriorDL";
constexpr uint32_t HALF_CYCLE = 80; // frames (20 fps)
constexpr float RADIUS = 260.0f;

struct Demo {
    const ShipOotEngineV1* engine = nullptr;
    const ShipOotCameraV1* camera = nullptr;
    const ShipOotRenderV1* render = nullptr;
    uint64_t token = 0;
    uint32_t frames = 0;
    uint32_t handDraws = 0;
    uint32_t drawFailures = 0;
    uint32_t acquired = 0;
    uint32_t refused = 0;
    uint32_t released = 0;
    uint32_t lost = 0;
    uint32_t views = 0;
};

ShipNativeStatus SHIP_NATIVE_CALL OnLimb(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    const auto* payload = static_cast<const ShipOotPlayerLimbHookV1*>(call->payload);
    const auto* actor = static_cast<const Actor*>(payload->actor);
    if (payload->limb != PLAYER_LIMB_R_HAND || actor->id != ACTOR_PLAYER) {
        return SHIP_NATIVE_OK;
    }
    const auto& render = *demo.render;
    render.matrix_translate(500.0f, 0.0f, 0.0f);
    render.matrix_rotate_zyx(0, static_cast<int16_t>(demo.frames * 0x0600), 0);
    render.matrix_scale(1.5f, 1.5f, 1.5f);
    if (render.draw_display_list(payload->play_state, MODEL, LINKSPAN_OOT_RENDER_TRANSLUCENT) == SHIP_NATIVE_OK) {
        ++demo.handDraws;
    } else {
        ++demo.drawFailures;
    }
    return SHIP_NATIVE_OK;
}

void ReleaseCamera(Demo& demo) {
    if (demo.token) {
        demo.camera->release(demo.token);
        demo.token = 0;
        ++demo.released;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL OnFrame(void* user, const ShipNativeHookCall*) {
    auto& demo = *static_cast<Demo*>(user);
    auto* player = static_cast<Player*>(demo.engine->get_player());
    if (!player) {
        demo.token = 0;
        return SHIP_NATIVE_OK;
    }
    ++demo.frames;
    const bool wanted = (demo.frames / HALF_CYCLE) % 2 == 1;
    if (demo.token && !demo.camera->is_owned(demo.token)) {
        demo.token = 0;
        ++demo.lost;
    }
    if (!wanted) {
        ReleaseCamera(demo);
        return SHIP_NATIVE_OK;
    }
    if (!demo.token) {
        if (demo.camera->acquire(OWNER, &demo.token) != SHIP_NATIVE_OK) {
            demo.token = 0;
            ++demo.refused;
            return SHIP_NATIVE_OK;
        }
        ++demo.acquired;
    }
    const auto& pos = player->actor.world.pos;
    const double angle = demo.frames * 0.05;
    ShipOotCameraViewV1 view{ sizeof(ShipOotCameraViewV1) };
    view.eye[0] = pos.x + static_cast<float>(std::sin(angle) * RADIUS);
    view.eye[1] = pos.y + 120.0f;
    view.eye[2] = pos.z + static_cast<float>(std::cos(angle) * RADIUS);
    view.at[0] = pos.x;
    view.at[1] = pos.y + 40.0f;
    view.at[2] = pos.z;
    view.fov = 60.0f;
    if (demo.camera->set_view(demo.token, &view) == SHIP_NATIVE_OK) {
        ++demo.views;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Stats(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                        void* writer) {
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& demo = *static_cast<Demo*>(user);
    char text[256];
    const int count = std::snprintf(text, sizeof(text),
                                    "frames=%u owned=%u hand=%u drawfail=%u acquired=%u refused=%u released=%u "
                                    "lost=%u views=%u",
                                    demo.frames, demo.token ? 1u : 0u, demo.handDraws, demo.drawFailures,
                                    demo.acquired, demo.refused, demo.released, demo.lost, demo.views);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(text)) {
        return SHIP_NATIVE_FAILURE;
    }
    return write(writer, text, static_cast<uint32_t>(count));
}

ShipNativeStatus Register(const ShipNativeRuntime* runtime, Demo* demo, const char* point, uint32_t size,
                          uint32_t phase, ShipNativeHookFn callback) {
    const ShipNativeHookSpec spec{ sizeof(ShipNativeHookSpec),  point, LINKSPAN_OOT_HOOKS_VERSION, size,
                                   SHIP_NATIVE_HOOK_OBSERVE, phase, 0,     callback,                   demo };
    uint64_t handle = 0;
    return runtime->register_hook(runtime->context, &spec, &handle);
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || !instance || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_minor < 2 ||
        !runtime->get_service || !runtime->register_function || !runtime->register_hook) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    if (!engine || !engine->layout_id || std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID)) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto* camera = static_cast<const ShipOotCameraV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_CAMERA_SERVICE, LINKSPAN_OOT_CAMERA_VERSION, sizeof(ShipOotCameraV1)));
    const auto* render = static_cast<const ShipOotRenderV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RENDER_SERVICE, LINKSPAN_OOT_RENDER_VERSION, sizeof(ShipOotRenderV1)));
    if (!camera || !render) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* demo = new (std::nothrow) Demo;
    if (!demo) {
        return SHIP_NATIVE_FAILURE;
    }
    *instance = demo;
    demo->engine = engine;
    demo->camera = camera;
    demo->render = render;
    ShipNativeStatus status = Register(runtime, demo, LINKSPAN_OOT_HOOK_PLAYER_LIMB_DRAW,
                                       sizeof(ShipOotPlayerLimbHookV1), SHIP_NATIVE_HOOK_AFTER, OnLimb);
    if (status == SHIP_NATIVE_OK) {
        status = Register(runtime, demo, LINKSPAN_OOT_HOOK_PLAY_UPDATE, sizeof(ShipOotPlayHookV1),
                          SHIP_NATIVE_HOOK_AFTER, OnFrame);
    }
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    return runtime->register_function(runtime->context, "stats", Stats, demo);
}

// Os hooks já saíram; a câmera é do mod e sai aqui.
void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* demo = static_cast<Demo*>(instance);
    ReleaseCamera(*demo);
    delete demo;
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{ sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                  Shutdown };
    return &descriptor;
}
