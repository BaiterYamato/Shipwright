// Demo do escape hatch (ABI 1.3): core extension que resolve funções do soh.exe por nome e desvia
// Interface_Draw para o HUD piscar. Só funciona no executável listado em host_fingerprints.
#include <cstdio>
#include <cstring>
#include <new>

#include <shiplua/native/ship_native_abi.h>

struct PlayState;

namespace {

// HUD some por BLINK_FRAMES chamadas e volta pelo mesmo tanto.
constexpr unsigned BLINK_FRAMES = 20;

using InterfaceDrawFn = void (*)(PlayState* play);

struct Demo {
    char status[256] = "init";
    InterfaceDrawFn original = nullptr;
    unsigned calls = 0;
    unsigned skipped = 0;
};

Demo* gDemo = nullptr;

void DrawHud(PlayState* play) {
    auto& demo = *gDemo;
    ++demo.calls;
    if ((demo.calls / BLINK_FRAMES) % 2 == 1) {
        ++demo.skipped;
        return;
    }
    demo.original(play);
}

ShipNativeStatus SHIP_NATIVE_CALL Status(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                         void* writer) {
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& demo = *static_cast<Demo*>(user);
    char text[384];
    const int count =
        std::snprintf(text, sizeof(text), "%s hud_calls=%u skipped=%u", demo.status, demo.calls, demo.skipped);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(text)) {
        return SHIP_NATIVE_FAILURE;
    }
    return write(writer, text, static_cast<uint32_t>(count));
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || !instance || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_minor < 3) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* demo = new (std::nothrow) Demo;
    if (!demo) {
        return SHIP_NATIVE_FAILURE;
    }
    *instance = demo;
    gDemo = demo;

    char fingerprint[65] = {};
    uint32_t size = 0;
    const auto fp = runtime->get_host_fingerprint(runtime->context, fingerprint, 64, &size);
    uintptr_t roll = 0;
    const auto rollStatus = runtime->resolve_symbol(runtime->context, "z_player.c!Player_Action_Roll", &roll);
    uintptr_t draw = 0;
    const auto drawStatus = runtime->resolve_symbol(runtime->context, "Interface_Draw", &draw);
    void* original = nullptr;
    uint64_t patch = 0;
    ShipNativeStatus patchStatus = SHIP_NATIVE_UNSUPPORTED;
    if (drawStatus == SHIP_NATIVE_OK) {
        patchStatus = runtime->install_patch(runtime->context, draw, reinterpret_cast<void*>(&DrawHud), &original,
                                             &patch);
        demo->original = static_cast<InterfaceDrawFn>(original);
    }
    std::snprintf(demo->status, sizeof(demo->status),
                  "fp=%u:%.12s roll=%u@%llx draw=%u@%llx patch=%u", fp, fingerprint, rollStatus,
                  static_cast<unsigned long long>(roll), drawStatus, static_cast<unsigned long long>(draw),
                  patchStatus);
    return runtime->register_function(runtime->context, "status", Status, demo);
}

// Os patches já saíram quando o host chama shutdown.
void SHIP_NATIVE_CALL Shutdown(void* instance) {
    gDemo = nullptr;
    delete static_cast<Demo*>(instance);
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 3u, Init,
                                                 Shutdown};
    return &descriptor;
}
