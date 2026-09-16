#include "OotNativeHooks.h"

#include "oot_hooks.h"

namespace ShipLuaHost {
namespace {
void (*hookLogger)(const std::string&) = nullptr;
std::shared_ptr<ShipLua::NativeHookRegistry> registry;
OotHookPoints points;

uint64_t Declare(const char* name, uint32_t payloadSize) {
    const ShipNativeHookPointSpec spec{sizeof(ShipNativeHookPointSpec), name, LINKSPAN_OOT_HOOKS_VERSION, payloadSize,
                                       SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_REPLACE};
    uint64_t id = 0;
    return registry->DeclarePoint("", spec, &id) == SHIP_NATIVE_OK ? id : 0;
}
} // namespace

void SetOotHookLogger(void (*logger)(const std::string& message)) {
    hookLogger = logger;
}

std::shared_ptr<ShipLua::NativeHookRegistry> CreateOotHookRegistry() {
    registry = std::make_shared<ShipLua::NativeHookRegistry>([](const std::string& message) {
        if (hookLogger) hookLogger(message);
    });
    points.playUpdate = Declare(LINKSPAN_OOT_HOOK_PLAY_UPDATE, sizeof(ShipOotPlayHookV1));
    points.actorUpdate = Declare(LINKSPAN_OOT_HOOK_ACTOR_UPDATE, sizeof(ShipOotActorHookV1));
    points.actorDraw = Declare(LINKSPAN_OOT_HOOK_ACTOR_DRAW, sizeof(ShipOotActorHookV1));
    return registry;
}

ShipLua::NativeHookRegistry* GetOotHookRegistry() {
    return registry.get();
}

const OotHookPoints& GetOotHookPoints() {
    return points;
}

void ResetOotHooks() {
    registry.reset();
    points = {};
}
} // namespace ShipLuaHost
