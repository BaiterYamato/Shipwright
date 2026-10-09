#include <cstring>
#include "shiplua/native/ship_native_abi.h"
#include "oot_engine.h"
#include "oot_layout_id.h"
#include "z64.h"

static_assert(sizeof(void*) == 8, "Windows x64 requerido");

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!instance) return SHIP_NATIVE_FAILURE;
    *instance = nullptr;
    if (!runtime || runtime->size < sizeof(ShipNativeRuntime) || !runtime->get_service)
        return SHIP_NATIVE_UNSUPPORTED;
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION,
        sizeof(ShipOotEngineV1)));
    if (!engine || engine->size < sizeof(ShipOotEngineV1) || !engine->layout_id ||
        std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) != 0 ||
        engine->play_state_size != sizeof(PlayState) || engine->player_size != sizeof(Player) ||
        engine->save_context_size != sizeof(SaveContext)) return SHIP_NATIVE_UNSUPPORTED;
    // A prova é somente consultar o contrato. Nenhum ponteiro de gameplay é retido.
    return SHIP_NATIVE_OK;
}

static void SHIP_NATIVE_CALL Shutdown(void*) {}

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor = {
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, SHIP_NATIVE_ABI_MINOR, Init, Shutdown
    };
    return &descriptor;
}
