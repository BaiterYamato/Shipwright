// Demo da proteção de boot (COREEXT-009): o init escreve num ponteiro nulo. O crash log deve
// apontar este mod, e o próximo início deve pular o mod com o motivo em mods/.shiplua-disabled.
#include <shiplua/native/ship_native_abi.h>

// A variante core_extension (linkspan_crash_coremod) declara ABI 1.2: o host só aceita core extension a partir
// da 1.1. O init é o mesmo.
#ifndef CRASH_DEMO_ABI_MINOR
#define CRASH_DEMO_ABI_MINOR 0u
#endif

namespace {

void Crash() {
    volatile int* target = nullptr;
    *target = 42;
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || !instance) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    Crash();
    return SHIP_NATIVE_FAILURE;
}

void SHIP_NATIVE_CALL Shutdown(void*) {
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR,
                                                 CRASH_DEMO_ABI_MINOR, Init, Shutdown};
    return &descriptor;
}
