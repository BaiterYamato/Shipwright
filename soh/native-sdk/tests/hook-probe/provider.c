#include <stdio.h>
#include <stdlib.h>

#include <shiplua/native/ship_native_abi.h>
#include "oot_hooks.h"

typedef struct HookProbe {
    unsigned counts[7];
    char report[256];
} HookProbe;

static const char* const kPoints[7] = { LINKSPAN_OOT_HOOK_PLAYER_VOICE_SFX,
                                        LINKSPAN_OOT_HOOK_PLAYER_ROLL_ALLOWED,
                                        LINKSPAN_OOT_HOOK_PLAYER_LEDGE_GRAB_ALLOWED,
                                        LINKSPAN_OOT_HOOK_PLAYER_ACTION,
                                        LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END,
                                        LINKSPAN_OOT_HOOK_PLAYER_MASK_DRAW,
                                        LINKSPAN_OOT_HOOK_PLAY_DESTROY };
static const uint32_t kSizes[7] = { sizeof(ShipOotPlayerVoiceHookV1), sizeof(ShipOotPlayerGateHookV1),
                                    sizeof(ShipOotPlayerGateHookV1),  sizeof(ShipOotPlayerActionHookV1),
                                    sizeof(ShipOotPlayerTickHookV1),  sizeof(ShipOotPlayerMaskDrawHookV1),
                                    sizeof(ShipOotPlayHookV1) };

static ShipNativeStatus SHIP_NATIVE_CALL Count(void* user, const ShipNativeHookCall* call) {
    ++*(unsigned*)user;
    (void)call;
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Report(void* user, const char* request, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    HookProbe* probe = (HookProbe*)user;
    int count = snprintf(probe->report, sizeof(probe->report),
                         "hook-probe voice=%u roll=%u ledge=%u action=%u update_end=%u mask=%u destroy=%u",
                         probe->counts[0], probe->counts[1], probe->counts[2], probe->counts[3], probe->counts[4],
                         probe->counts[5], probe->counts[6]);
    (void)request;
    (void)length;
    return write(writer, probe->report, (uint32_t)count);
}

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    HookProbe* probe = (HookProbe*)calloc(1, sizeof(HookProbe));
    int index;
    if (!probe)
        return SHIP_NATIVE_FAILURE;
    *output = probe;
    for (index = 0; index < 7; ++index) {
        ShipNativeHookSpec spec = {
            sizeof(spec), kPoints[index],       1, kSizes[index], SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_AFTER, 0,
            Count,        &probe->counts[index]
        };
        uint64_t handle = 0;
        ShipNativeStatus status = runtime->register_hook(runtime->context, &spec, &handle);
        if (status != SHIP_NATIVE_OK)
            return status;
    }
    return runtime->register_function(runtime->context, "report", Report, probe);
}

static void SHIP_NATIVE_CALL Shutdown(void* instance) {
    free(instance);
}

SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor = { sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                     Shutdown };
    return &descriptor;
}
