#include <stdio.h>
#include <stdlib.h>

#include <shiplua/native/ship_native_abi.h>

/* Prova do catálogo público (RFC 0029): chama Math_SinS, lê gItemIcons e confere três recusas. Só headers do SDK;
 * as assinaturas vêm de soh/include/functions.h, aqui repetidas no tipo do ponteiro. */
typedef float (*MathSinSFn)(short angle);
typedef struct ProbeState {
    char report[256];
} ProbeState;

static const char* Name(ShipNativeStatus status) {
    return status == SHIP_NATIVE_OK ? "ok" : status == SHIP_NATIVE_UNSUPPORTED ? "unsupported" : "error";
}

static ShipNativeStatus SHIP_NATIVE_CALL Report(void* user, const char* request, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    ProbeState* state = (ProbeState*)user;
    uint32_t count = 0;
    (void)request;
    (void)length;
    while (state->report[count])
        ++count;
    return write(writer, state->report, count);
}

static ShipNativeStatus Resolve(const ShipNativeRuntime* runtime, const char* name, uintptr_t* address) {
    *address = 0xDEAD;
    return runtime->resolve_symbol(runtime->context, name, address);
}

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    ProbeState* state;
    uintptr_t sin, icons, extra, qualified, missing;
    ShipNativeStatus sSin, sIcons, sExtra, sQualified, sMissing;
    float value = -1.0f;
    if (!runtime || !output || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_minor < 4)
        return SHIP_NATIVE_UNSUPPORTED;
    state = (ProbeState*)calloc(1, sizeof(ProbeState));
    if (!state)
        return SHIP_NATIVE_FAILURE;
    *output = state;
    sSin = Resolve(runtime, "Math_SinS", &sin);
    if (sSin == SHIP_NATIVE_OK)
        value = ((MathSinSFn)sin)(0x4000);
    sIcons = Resolve(runtime, "gItemIcons", &icons);
    /* Player_ZeroSpeedXZ só existe na retenção privada (host-keep-extra.txt): sem hash, nunca resolve. */
    sExtra = Resolve(runtime, "Player_ZeroSpeedXZ", &extra);
    sQualified = Resolve(runtime, "z_lib.c!Math_SinS", &qualified);
    sMissing = Resolve(runtime, "LinkSpan_NoSuchSymbol", &missing);
    snprintf(state->report, sizeof(state->report),
             "public-probe sin=%s:%.3f icons=%s:%d extra=%s:%d qual=%s:%d miss=%s:%d", Name(sSin), value, Name(sIcons),
             sIcons == SHIP_NATIVE_OK && *(void* const*)icons != NULL, Name(sExtra), extra != 0, Name(sQualified),
             qualified != 0, Name(sMissing), missing != 0);
    return runtime->register_function(runtime->context, "report", Report, state);
}

static void SHIP_NATIVE_CALL Shutdown(void* instance) {
    free(instance);
}

SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor = { sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 4u, Init,
                                                     Shutdown };
    return &descriptor;
}
