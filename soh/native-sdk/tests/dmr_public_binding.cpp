// Instrumentação exclusiva de test_independent.py: o DMR inteiro é compilado aqui.
// O runtime e os resultados vêm do NativeProvider real; nenhum resolver é simulado.
#define ShipNative_Query DmrRealQuery
#include "../example/provider.cpp"
#undef ShipNative_Query

namespace {
ShipNativeRuntime observedRuntime{};
ShipNativeRuntime forwardedRuntime{}; // o DMR conserva este ponteiro até Shutdown
struct Resolution {
    ShipNativeStatus status = SHIP_NATIVE_FAILURE;
    uintptr_t address = 0;
    unsigned calls = 0;
};
Resolution iconResolution, geometryResolution;

ShipNativeStatus SHIP_NATIVE_CALL ObserveResolve(void* context, const char* name, uintptr_t* address) {
    // Sentinela prova que o core zera a saída também quando recusa o layout.
    *address = 0xDEAD;
    const auto status = observedRuntime.resolve_symbol(context, name, address);
    auto& result = std::strcmp(name, "gItemIcons") == 0 ? iconResolution : geometryResolution;
    result = { status, *address, result.calls + 1 };
    return status;
}

ShipNativeStatus SHIP_NATIVE_CALL BindingReport(void* instance, const char*, uint32_t, ShipNativeWriteFn write,
                                                void* writer) {
    const auto& mod = *static_cast<Mod*>(instance);
    char report[256];
    std::snprintf(report, sizeof(report), "icons=%d:%d:%u geometry=%d:%d:%u fallback=%d:%d", int(iconResolution.status),
                  iconResolution.address != 0, iconResolution.calls, int(geometryResolution.status),
                  geometryResolution.address != 0, geometryResolution.calls, mod.itemIcons == nullptr,
                  mod.cameraGeometry == nullptr);
    return Write(write, writer, report);
}

ShipNativeStatus SHIP_NATIVE_CALL ObserveInit(const ShipNativeRuntime* runtime, void** instance) {
    observedRuntime = *runtime;
    iconResolution = {};
    geometryResolution = {};
    const auto* real = DmrRealQuery();
    if (runtime->abi_minor != 4 || !runtime->resolve_symbol || real->abi_major != 1 || real->abi_minor != 4)
        return SHIP_NATIVE_UNSUPPORTED;
    forwardedRuntime = *runtime;
    forwardedRuntime.resolve_symbol = ObserveResolve;
    const auto result = real->init(&forwardedRuntime, instance);
    if (result != SHIP_NATIVE_OK)
        return result;
    return runtime->register_function(runtime->context, "test_public_binding", BindingReport, *instance);
}
} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{ sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 4u, ObserveInit,
                                                  DmrRealQuery()->shutdown };
    return &descriptor;
}
