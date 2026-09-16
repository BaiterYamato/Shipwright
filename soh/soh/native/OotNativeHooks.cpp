#include "OotNativeHooks.h"

#include "oot_hooks.h"

namespace ShipLuaHost {
namespace {
void (*hookLogger)(const std::string&) = nullptr;
std::shared_ptr<ShipLua::NativeHookRegistry> registry;
OotHookPoints points;

uint64_t Declare(const char* name, uint32_t payloadSize,
                 uint32_t modes = SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_REPLACE) {
    const ShipNativeHookPointSpec spec{sizeof(ShipNativeHookPointSpec), name, LINKSPAN_OOT_HOOKS_VERSION, payloadSize,
                                       modes};
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
    points.saveLoaded = Declare(LINKSPAN_OOT_HOOK_SAVE_LOADED, sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.saveSaving = Declare(LINKSPAN_OOT_HOOK_SAVE_SAVING, sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.saveDeleted = Declare(LINKSPAN_OOT_HOOK_SAVE_DELETED, sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.saveCopied = Declare(LINKSPAN_OOT_HOOK_SAVE_COPIED, sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.playerLimbDraw =
        Declare(LINKSPAN_OOT_HOOK_PLAYER_LIMB_DRAW, sizeof(ShipOotPlayerLimbHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    return registry;
}

ShipLua::NativeHookRegistry* GetOotHookRegistry() {
    return registry.get();
}

const OotHookPoints& GetOotHookPoints() {
    return points;
}

void DispatchOotSaveHook(uint64_t point, int32_t slot, int32_t otherSlot) {
    if (!registry || !registry->HasHooks(point)) {
        return;
    }
    ShipOotSaveHookV1 payload{sizeof(ShipOotSaveHookV1), slot, otherSlot};
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
}

void ResetOotHooks() {
    registry.reset();
    points = {};
}
} // namespace ShipLuaHost
