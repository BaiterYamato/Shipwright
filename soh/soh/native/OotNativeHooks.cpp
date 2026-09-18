#include "OotNativeHooks.h"

#include "oot_hooks.h"

namespace ShipLuaHost {
namespace {
void (*hookLogger)(const std::string&) = nullptr;
std::shared_ptr<ShipLua::NativeHookRegistry> registry;
OotHookPoints points;

uint64_t Declare(const char* name, uint32_t payloadSize,
                 uint32_t modes = SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_REPLACE,
                 uint32_t version = LINKSPAN_OOT_HOOKS_VERSION) {
    const ShipNativeHookPointSpec spec{sizeof(ShipNativeHookPointSpec), name, version, payloadSize, modes};
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
    points.roomActors = Declare(LINKSPAN_OOT_HOOK_ROOM_ACTORS, sizeof(ShipOotRoomActorsHookV2),
                                SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_TRANSFORM,
                                LINKSPAN_OOT_HOOK_ROOM_ACTORS_VERSION);
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

uint32_t DispatchOotRoomActors(void* play, int32_t sceneId, int32_t room, int32_t layer, const char* roomPath,
                               ShipOotActorEntryV2* entries, uint32_t count, uint32_t capacity) {
    if (!registry || !registry->HasHooks(points.roomActors) || !entries || count > capacity) {
        return count;
    }
    ShipOotRoomActorsHookV2 payload{sizeof(ShipOotRoomActorsHookV2), play, sceneId, room, layer, roomPath, entries,
                                    count, capacity};
    registry->Dispatch(points.roomActors, &payload, sizeof(payload), nullptr, nullptr);
    if (payload.entries != entries || payload.capacity != capacity || payload.count > capacity) {
        if (hookLogger) {
            hookLogger("oot.room.actors: payload inválido depois dos hooks; lista original mantida");
        }
        return count;
    }
    return payload.count;
}

void ResetOotHooks() {
    registry.reset();
    points = {};
}
} // namespace ShipLuaHost
