// Demo dos hooks nativos (ABI 1.2): conta frames e updates de ator por observe e
// pisca o Link substituindo o draw do ator Player.
#include <cstdio>
#include <cstring>
#include <new>

#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_layout_id.h"
#include "z64.h"

namespace {

// Link some por BLINK_FRAMES frames e volta pelo mesmo tempo.
constexpr uint32_t BLINK_FRAMES = 10;

struct Demo {
    const ShipNativeRuntime* runtime = nullptr;
    uint32_t frames = 0;
    uint32_t updates = 0;
    uint32_t draws = 0;
    uint32_t hidden = 0;
};

ShipNativeStatus SHIP_NATIVE_CALL OnFrame(void* user, const ShipNativeHookCall*) {
    ++static_cast<Demo*>(user)->frames;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnActorUpdate(void* user, const ShipNativeHookCall*) {
    ++static_cast<Demo*>(user)->updates;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL DrawActor(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    const auto* payload = static_cast<const ShipOotActorHookV1*>(call->payload);
    ++demo.draws;
    if (payload->actor_id == ACTOR_PLAYER && (demo.frames / BLINK_FRAMES) % 2 == 1) {
        ++demo.hidden;
        return SHIP_NATIVE_OK;
    }
    return call->call_original(call);
}

ShipNativeStatus Register(const ShipNativeRuntime* runtime, Demo* demo, const char* point, uint32_t size,
                          uint32_t mode, uint32_t phase, ShipNativeHookFn callback) {
    const ShipNativeHookSpec spec{sizeof(ShipNativeHookSpec), point, LINKSPAN_OOT_HOOKS_VERSION, size, mode, phase,
                                  0, callback, demo};
    uint64_t handle = 0;
    return runtime->register_hook(runtime->context, &spec, &handle);
}

ShipNativeStatus SHIP_NATIVE_CALL Stats(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                        void* writer) {
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& demo = *static_cast<Demo*>(user);
    char text[96];
    const int count = std::snprintf(text, sizeof(text), "frames=%u updates=%u draws=%u hidden=%u", demo.frames,
                                    demo.updates, demo.draws, demo.hidden);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(text)) {
        return SHIP_NATIVE_FAILURE;
    }
    return write(writer, text, static_cast<uint32_t>(count));
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
    auto* demo = new (std::nothrow) Demo;
    if (!demo) {
        return SHIP_NATIVE_FAILURE;
    }
    *instance = demo;
    demo->runtime = runtime;
    ShipNativeStatus status = Register(runtime, demo, LINKSPAN_OOT_HOOK_PLAY_UPDATE, sizeof(ShipOotPlayHookV1),
                                       SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_AFTER, OnFrame);
    if (status == SHIP_NATIVE_OK) {
        status = Register(runtime, demo, LINKSPAN_OOT_HOOK_ACTOR_UPDATE, sizeof(ShipOotActorHookV1),
                          SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_BEFORE, OnActorUpdate);
    }
    if (status == SHIP_NATIVE_OK) {
        status = Register(runtime, demo, LINKSPAN_OOT_HOOK_ACTOR_DRAW, sizeof(ShipOotActorHookV1),
                          SHIP_NATIVE_HOOK_REPLACE, 0, DrawActor);
    }
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    return runtime->register_function(runtime->context, "stats", Stats, demo);
}

// Os hooks já saíram quando o host chama shutdown.
void SHIP_NATIVE_CALL Shutdown(void* instance) {
    delete static_cast<Demo*>(instance);
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                 Shutdown};
    return &descriptor;
}
