// Demo do serviço linkspan.oot.save: a cada arquivo carregado lê {"loads":N}, grava N+1 e marca o
// namespace como obrigatório. O valor chega ao disco no próximo save do jogo.
#include <cstdio>
#include <cstring>
#include <new>

#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_layout_id.h"
#include "oot_save.h"

namespace {

constexpr const char* NAMESPACE = "linkspan-demo.save";
constexpr uint32_t SCHEMA_VERSION = 1;

struct Demo {
    const ShipOotSaveV1* save = nullptr;
    uint64_t handle = 0;
    char status[160] = "idle";
    uint32_t events = 0;
};

ShipNativeStatus SHIP_NATIVE_CALL OnLoaded(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    const auto* payload = static_cast<const ShipOotSaveHookV1*>(call->payload);
    char text[64] = {};
    uint32_t size = 0;
    unsigned loads = 0;
    uint32_t stored = 0;
    const ShipNativeStatus read = demo.save->read(demo.handle, text, sizeof(text) - 1, &size);
    if (read == SHIP_NATIVE_OK) {
        text[size] = '\0';
        std::sscanf(text, "{\"loads\":%u}", &loads);
    }
    demo.save->get_stored_version(demo.handle, &stored);
    char next[32];
    const int length = std::snprintf(next, sizeof(next), "{\"loads\":%u}", loads + 1);
    const ShipNativeStatus written = demo.save->write(demo.handle, next, static_cast<uint32_t>(length));
    demo.save->set_required(demo.handle, 1);
    ++demo.events;
    std::snprintf(demo.status, sizeof(demo.status), "loaded slot=%d read=%s stored_version=%u loads=%u write=%u",
                  payload->slot, read == SHIP_NATIVE_OK ? "ok" : "vazio", stored, loads + 1, written);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnSaving(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    const auto* payload = static_cast<const ShipOotSaveHookV1*>(call->payload);
    ++demo.events;
    std::snprintf(demo.status, sizeof(demo.status), "saving slot=%d get_slot=%d", payload->slot,
                  demo.save->get_slot());
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Status(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                         void* writer) {
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& demo = *static_cast<Demo*>(user);
    char text[200];
    const int count = std::snprintf(text, sizeof(text), "%u %s", demo.events, demo.status);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(text)) {
        return SHIP_NATIVE_FAILURE;
    }
    return write(writer, text, static_cast<uint32_t>(count));
}

ShipNativeStatus Observe(const ShipNativeRuntime* runtime, Demo* demo, const char* point, ShipNativeHookFn callback) {
    const ShipNativeHookSpec spec{sizeof(ShipNativeHookSpec), point, LINKSPAN_OOT_HOOKS_VERSION,
                                  sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_BEFORE,
                                  0, callback, demo};
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
    const auto* save = static_cast<const ShipOotSaveV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_SAVE_SERVICE, LINKSPAN_OOT_SAVE_VERSION, sizeof(ShipOotSaveV1)));
    if (!engine || !engine->layout_id || std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) || !save) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* demo = new (std::nothrow) Demo;
    if (!demo) {
        return SHIP_NATIVE_FAILURE;
    }
    *instance = demo;
    demo->save = save;
    ShipNativeStatus status = save->open_namespace(NAMESPACE, SCHEMA_VERSION, &demo->handle);
    if (status == SHIP_NATIVE_OK) {
        status = Observe(runtime, demo, LINKSPAN_OOT_HOOK_SAVE_LOADED, OnLoaded);
    }
    if (status == SHIP_NATIVE_OK) {
        status = Observe(runtime, demo, LINKSPAN_OOT_HOOK_SAVE_SAVING, OnSaving);
    }
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    return runtime->register_function(runtime->context, "status", Status, demo);
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    delete static_cast<Demo*>(instance);
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                 Shutdown};
    return &descriptor;
}
