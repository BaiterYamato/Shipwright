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
    const ShipNativeHookPointSpec spec{ sizeof(ShipNativeHookPointSpec), name, version, payloadSize, modes };
    uint64_t id = 0;
    return registry->DeclarePoint("", spec, &id) == SHIP_NATIVE_OK ? id : 0;
}

// Operação do jogo para call_original. `allowed` é o valor do jogo, não o do payload: um OBSERVE que escreva no
// payload não liga a ação nem a máscara.
struct GameOperation {
    void (*run)(void* context);
    void* context;
    uint8_t allowed;
};

ShipNativeStatus SHIP_NATIVE_CALL RunAction(const ShipNativeHookCall* call) {
    const auto* operation = static_cast<const GameOperation*>(call->original_context);
    if (operation->allowed && operation->run) {
        operation->run(operation->context);
        static_cast<ShipOotPlayerActionHookV1*>(call->payload)->executed = 1;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL RunMaskDraw(const ShipNativeHookCall* call) {
    const auto* operation = static_cast<const GameOperation*>(call->original_context);
    if (operation->allowed && operation->run)
        operation->run(operation->context);
    return SHIP_NATIVE_OK;
}
} // namespace

void SetOotHookLogger(void (*logger)(const std::string& message)) {
    hookLogger = logger;
}

std::shared_ptr<ShipLua::NativeHookRegistry> CreateOotHookRegistry() {
    registry = std::make_shared<ShipLua::NativeHookRegistry>([](const std::string& message) {
        if (hookLogger)
            hookLogger(message);
    });
    points.playUpdate = Declare(LINKSPAN_OOT_HOOK_PLAY_UPDATE, sizeof(ShipOotPlayHookV1));
    points.actorInit = Declare(LINKSPAN_OOT_HOOK_ACTOR_INIT, sizeof(ShipOotActorHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.playDrawEnd =
        Declare(LINKSPAN_OOT_HOOK_PLAY_DRAW_END, sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.actorUpdate = Declare(LINKSPAN_OOT_HOOK_ACTOR_UPDATE, sizeof(ShipOotActorHookV1));
    points.actorDraw = Declare(LINKSPAN_OOT_HOOK_ACTOR_DRAW, sizeof(ShipOotActorHookV1));
    points.saveLoaded = Declare(LINKSPAN_OOT_HOOK_SAVE_LOADED, sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.saveSaving = Declare(LINKSPAN_OOT_HOOK_SAVE_SAVING, sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.saveDeleted = Declare(LINKSPAN_OOT_HOOK_SAVE_DELETED, sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.saveCopied = Declare(LINKSPAN_OOT_HOOK_SAVE_COPIED, sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.playerLimbDraw =
        Declare(LINKSPAN_OOT_HOOK_PLAYER_LIMB_DRAW, sizeof(ShipOotPlayerLimbHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.roomActors =
        Declare(LINKSPAN_OOT_HOOK_ROOM_ACTORS, sizeof(ShipOotRoomActorsHookV2),
                SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_TRANSFORM, LINKSPAN_OOT_HOOK_ROOM_ACTORS_VERSION);
    points.renderActorDraw =
        Declare(LINKSPAN_OOT_HOOK_RENDER_ACTOR_DRAW, sizeof(ShipOotRenderActorDrawHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.renderWorldLights =
        Declare(LINKSPAN_OOT_HOOK_RENDER_WORLD_LIGHTS, sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.renderSkyGradient =
        Declare(LINKSPAN_OOT_HOOK_RENDER_SKY_GRADIENT, sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.renderSky = Declare(LINKSPAN_OOT_HOOK_RENDER_SKY, sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.renderSkyClouds =
        Declare(LINKSPAN_OOT_HOOK_RENDER_SKY_CLOUDS, sizeof(ShipOotRenderPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.renderFileSelectSky = Declare(LINKSPAN_OOT_HOOK_RENDER_FILE_SELECT_SKY,
                                         sizeof(ShipOotRenderFileSelectSkyHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.anchorState = Declare(LINKSPAN_OOT_HOOK_ANCHOR_STATE, sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.pointLightColor =
        Declare(LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR, sizeof(ShipOotPointLightColorHookV2),
                SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_TRANSFORM, LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR_VERSION);
    points.fairyLights =
        Declare(LINKSPAN_OOT_HOOK_LIGHT_FAIRY, sizeof(ShipOotFairyLightHookV1),
                SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_TRANSFORM, LINKSPAN_OOT_HOOK_LIGHT_FAIRY_VERSION);
    // HOST-SLIM-001 (RFC 0029).
    constexpr uint32_t kObserveTransform = SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_TRANSFORM;
    points.playerVoiceSfx =
        Declare(LINKSPAN_OOT_HOOK_PLAYER_VOICE_SFX, sizeof(ShipOotPlayerVoiceHookV1), kObserveTransform);
    points.playerRollAllowed =
        Declare(LINKSPAN_OOT_HOOK_PLAYER_ROLL_ALLOWED, sizeof(ShipOotPlayerGateHookV1), kObserveTransform);
    points.playerLedgeGrabAllowed =
        Declare(LINKSPAN_OOT_HOOK_PLAYER_LEDGE_GRAB_ALLOWED, sizeof(ShipOotPlayerGateHookV1), kObserveTransform);
    points.playerAction =
        Declare(LINKSPAN_OOT_HOOK_PLAYER_ACTION, sizeof(ShipOotPlayerActionHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.playerUpdateEnd =
        Declare(LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END, sizeof(ShipOotPlayerTickHookV1), SHIP_NATIVE_HOOK_OBSERVE);
    points.playerMaskDraw = Declare(LINKSPAN_OOT_HOOK_PLAYER_MASK_DRAW, sizeof(ShipOotPlayerMaskDrawHookV1));
    points.playDestroy = Declare(LINKSPAN_OOT_HOOK_PLAY_DESTROY, sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE);
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
    ShipOotSaveHookV1 payload{ sizeof(ShipOotSaveHookV1), slot, otherSlot };
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
}

uint32_t DispatchOotRoomActors(void* play, int32_t sceneId, int32_t room, int32_t layer, const char* roomPath,
                               ShipOotActorEntryV2* entries, uint32_t count, uint32_t capacity) {
    if (!registry || !registry->HasHooks(points.roomActors) || !entries || count > capacity) {
        return count;
    }
    ShipOotRoomActorsHookV2 payload{
        sizeof(ShipOotRoomActorsHookV2), play, sceneId, room, layer, roomPath, entries, count, capacity
    };
    registry->Dispatch(points.roomActors, &payload, sizeof(payload), nullptr, nullptr);
    if (payload.entries != entries || payload.capacity != capacity || payload.count > capacity) {
        if (hookLogger) {
            hookLogger("oot.room.actors: payload inválido depois dos hooks; lista original mantida");
        }
        return count;
    }
    return payload.count;
}

void DispatchOotPointLightColor(ShipOotPointLightColorHookV2* payload) {
    if (!payload || !registry || !registry->HasHooks(points.pointLightColor))
        return;
    registry->Dispatch(points.pointLightColor, payload, sizeof(*payload), nullptr, nullptr);
}

bool HasOotPointLightColorHooks() {
    return registry && registry->HasHooks(points.pointLightColor);
}

bool HasOotHooks(uint64_t point) {
    return registry && point && registry->HasHooks(point);
}

uint8_t DispatchOotPlayerVoice(void* play, void* player, uint16_t* sfx) {
    if (!sfx || !HasOotHooks(points.playerVoiceSfx))
        return 0;
    ShipOotPlayerVoiceHookV1 payload{ sizeof(payload), play, player, *sfx, *sfx, 0, {} };
    registry->Dispatch(points.playerVoiceSfx, &payload, sizeof(payload), nullptr, nullptr);
    if (payload.consumed)
        return 1;
    *sfx = payload.sfx;
    return 0;
}

uint8_t DispatchOotPlayerGate(uint64_t point, void* play, void* player, uint8_t allowed) {
    const uint8_t input = allowed != 0;
    if (!HasOotHooks(point))
        return input;
    ShipOotPlayerGateHookV1 payload{ sizeof(payload), play, player, input, {} };
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
    return input && payload.allowed != 0;
}

uint8_t DispatchOotPlayerAction(void* play, void* player, void* input, uint8_t vanillaAllowed,
                                void (*action)(void* context), void* context) {
    const GameOperation operation{ action, context, uint8_t(vanillaAllowed != 0) };
    if (!HasOotHooks(points.playerAction)) {
        if (!operation.allowed || !action)
            return 0;
        action(context);
        return 1;
    }
    ShipOotPlayerActionHookV1 payload{ sizeof(payload), play, player, input, operation.allowed, 0, {} };
    registry->Dispatch(points.playerAction, &payload, sizeof(payload), RunAction,
                       const_cast<GameOperation*>(&operation));
    return payload.executed;
}

void DispatchOotPlayerTick(uint64_t point, void* play, void* player) {
    if (!HasOotHooks(point))
        return;
    ShipOotPlayerTickHookV1 payload{ sizeof(payload), play, player };
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
}

void DispatchOotPlayerMaskDraw(void* play, void* player, int32_t mask, uint8_t vanillaAllowed,
                               void (*drawVanilla)(void* context), void* context) {
    const GameOperation operation{ drawVanilla, context, uint8_t(vanillaAllowed != 0) };
    if (!HasOotHooks(points.playerMaskDraw)) {
        if (operation.allowed && drawVanilla)
            drawVanilla(context);
        return;
    }
    ShipOotPlayerMaskDrawHookV1 payload{ sizeof(payload), play, player, mask, operation.allowed, {} };
    registry->Dispatch(points.playerMaskDraw, &payload, sizeof(payload), RunMaskDraw,
                       const_cast<GameOperation*>(&operation));
}

void DispatchOotPlayDestroy(void* play) {
    if (!HasOotHooks(points.playDestroy))
        return;
    ShipOotPlayHookV1 payload{ sizeof(payload), play };
    registry->Dispatch(points.playDestroy, &payload, sizeof(payload), nullptr, nullptr);
}

void ResetOotHooks() {
    registry.reset();
    points = {};
}
} // namespace ShipLuaHost
