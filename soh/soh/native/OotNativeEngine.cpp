#include "OotNativeEngine.h"
#include "OotNativeRegistry.h"
#include "oot_engine.h"
#include "oot_layout_id.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <thread>
#include "z64.h"
#include "macros.h"
#include "sfx.h"
#include "assets/objects/gameplay_keep/gameplay_keep.h"

extern "C" {
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
void Player_SetupRoll(Player* player, PlayState* play);
void Player_Action_Roll(Player* player, PlayState* play);
void func_80838940(Player* player, LinkAnimationHeader* anim, f32 velocity, PlayState* play, u16 sfxId);
// z_player.c (não static): uso de item e as ações comparadas por Player_CanUpdateItems.
void Player_UseItem(PlayState* play, Player* player, s32 item);
s8 Player_ItemToItemAction(s32 item);
s32 Player_UpperAction_ChangeHeldItem(Player* player, PlayState* play);
void Player_Action_WaitForPutAway(Player* player, PlayState* play);
uint8_t GameInteractor_PacifistModeActive();
}

namespace ShipLuaHost {
namespace {
std::thread::id gameThread;
OotNativeGamepadBridge gamepadBridge;
OotNativeResourceBridge resourceBridge;
bool OnGameThread() { return std::this_thread::get_id() == gameThread; }
bool ValidText(const char* value) { return value && *value && std::strlen(value) <= 4096; }
void* SHIP_NATIVE_CALL Play() { return OnGameThread() ? gPlayState : nullptr; }
void* SHIP_NATIVE_CALL CurrentPlayer() {
    return OnGameThread() && gPlayState ? GET_PLAYER(gPlayState) : nullptr;
}
void* SHIP_NATIVE_CALL Save() { return OnGameThread() ? &gSaveContext : nullptr; }
void* SHIP_NATIVE_CALL Spawn(int16_t id, float x, float y, float z,
                            int16_t rx, int16_t ry, int16_t rz, int16_t params) {
    if (!OnGameThread() || !gPlayState) return nullptr;
    return Actor_Spawn(&gPlayState->actorCtx, gPlayState, id, x, y, z, rx, ry, rz, params);
}
ShipNativeStatus SHIP_NATIVE_CALL Kill(void* target) {
    if (!OnGameThread() || !gPlayState || !target) return SHIP_NATIVE_INVALID_ARGUMENT;
    for (const auto& list : gPlayState->actorCtx.actorLists) {
        for (Actor* actor = list.head; actor; actor = actor->next) {
            if (actor == target) { Actor_Kill(actor); return SHIP_NATIVE_OK; }
        }
    }
    return SHIP_NATIVE_INVALID_ARGUMENT;
}
bool ValidPort(uint8_t port) { return port < 4; }
uint16_t SHIP_NATIVE_CALL InputCurrent(uint8_t port) {
    return OnGameThread() && gPlayState && ValidPort(port) ? gPlayState->state.input[port].cur.button : 0;
}
uint16_t SHIP_NATIVE_CALL InputPressed(uint8_t port) {
    return OnGameThread() && gPlayState && ValidPort(port) ? gPlayState->state.input[port].press.button : 0;
}
uint16_t SHIP_NATIVE_CALL InputReleased(uint8_t port) {
    return OnGameThread() && gPlayState && ValidPort(port) ? gPlayState->state.input[port].rel.button : 0;
}
int8_t SHIP_NATIVE_CALL StickX(uint8_t port) {
    return OnGameThread() && gPlayState && ValidPort(port) ? gPlayState->state.input[port].rel.stick_x : 0;
}
int8_t SHIP_NATIVE_CALL StickY(uint8_t port) {
    return OnGameThread() && gPlayState && ValidPort(port) ? gPlayState->state.input[port].rel.stick_y : 0;
}
int8_t SHIP_NATIVE_CALL RightStickX(uint8_t port) {
    return OnGameThread() && gPlayState && ValidPort(port) ? gPlayState->state.input[port].rel.right_stick_x : 0;
}
int8_t SHIP_NATIVE_CALL RightStickY(uint8_t port) {
    return OnGameThread() && gPlayState && ValidPort(port) ? gPlayState->state.input[port].rel.right_stick_y : 0;
}
uint8_t SHIP_NATIVE_CALL HasGamepad(uint8_t port) {
    return OnGameThread() && ValidPort(port) && gamepadBridge.hasGamepad ? gamepadBridge.hasGamepad(port) : 0;
}
uint32_t SHIP_NATIVE_CALL GamepadButtons(uint8_t port) {
    return OnGameThread() && ValidPort(port) && gamepadBridge.getButtons ? gamepadBridge.getButtons(port) : 0;
}
ShipNativeStatus SHIP_NATIVE_CALL ClearGamepadButtonBindings(uint8_t port, uint16_t virtualButton) {
    if (!OnGameThread() || !ValidPort(port) || !virtualButton || !gamepadBridge.clearButtonBindings)
        return SHIP_NATIVE_UNSUPPORTED;
    return gamepadBridge.clearButtonBindings(port, virtualButton);
}
ShipNativeStatus SHIP_NATIVE_CALL BindGamepadButton(uint8_t port, uint16_t virtualButton, uint8_t sdlButton) {
    if (!OnGameThread() || !ValidPort(port) || !virtualButton || !gamepadBridge.bindButton)
        return SHIP_NATIVE_UNSUPPORTED;
    return gamepadBridge.bindButton(port, virtualButton, sdlButton);
}
ShipNativeStatus SHIP_NATIVE_CALL ReloadGamepadMappings(uint8_t port) {
    if (!OnGameThread() || !ValidPort(port) || !gamepadBridge.reloadMappings) return SHIP_NATIVE_UNSUPPORTED;
    return gamepadBridge.reloadMappings(port);
}
int32_t SHIP_NATIVE_CALL GetSettingInt(const char* name, int32_t fallback) {
    return OnGameThread() && name && *name && gamepadBridge.getSettingInt
               ? gamepadBridge.getSettingInt(name, fallback)
               : fallback;
}
ShipNativeStatus SHIP_NATIVE_CALL SetSettingInt(const char* name, int32_t value) {
    if (!OnGameThread() || !name || !*name || !gamepadBridge.setSettingInt) return SHIP_NATIVE_UNSUPPORTED;
    return gamepadBridge.setSettingInt(name, value);
}
uint8_t SHIP_NATIVE_CALL Grounded() {
    auto* player = static_cast<Player*>(CurrentPlayer());
    return player && (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0;
}
uint8_t SHIP_NATIVE_CALL Rolling() {
    auto* player = static_cast<Player*>(CurrentPlayer());
    return player && player->actionFunc == Player_Action_Roll ? 1 : 0;
}
bool CanMove(Player* player) {
    return player && !(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE |
                                             PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_WATER));
}
ShipNativeStatus SHIP_NATIVE_CALL Jump(float velocity) {
    auto* player = static_cast<Player*>(CurrentPlayer());
    if (!std::isfinite(velocity) || velocity <= 0.0f || velocity > 30.0f) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!gPlayState || !CanMove(player) || !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND))
        return SHIP_NATIVE_UNSUPPORTED;
    const int16_t yawDifference = static_cast<int16_t>(player->yaw - player->actor.shape.rot.y);
    const bool runningForward = std::abs(static_cast<int32_t>(yawDifference)) < 0x1000 && player->linearVelocity > 4.0f;
    auto* animation = reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(
        runningForward ? gPlayerAnim_link_normal_run_jump : gPlayerAnim_link_normal_jump));
    func_80838940(player, animation, velocity, gPlayState, NA_SE_VO_LI_AUTO_JUMP);
    player->av2.actionVar2 = 1;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Roll() {
    auto* player = static_cast<Player*>(CurrentPlayer());
    if (!gPlayState || !CanMove(player) || !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND))
        return SHIP_NATIVE_UNSUPPORTED;
    if (player->actionFunc != Player_Action_Roll) Player_SetupRoll(player, gPlayState);
    return SHIP_NATIVE_OK;
}
int16_t SHIP_NATIVE_CALL GamepadAxis(uint8_t port, uint8_t axis) {
    return OnGameThread() && ValidPort(port) && gamepadBridge.getAxis ? gamepadBridge.getAxis(port, axis) : 0;
}
ShipNativeStatus SHIP_NATIVE_CALL BindGamepadAxis(uint8_t port, uint16_t virtualButton, uint8_t axis,
                                                  int8_t direction) {
    if (direction != 1 && direction != -1) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!OnGameThread() || !ValidPort(port) || !virtualButton || !gamepadBridge.bindAxis)
        return SHIP_NATIVE_UNSUPPORTED;
    return gamepadBridge.bindAxis(port, virtualButton, axis, direction);
}
// Nomes finais das CVars (prefixos de CMake/soh-cvars.cmake).
bool SettingEnabled(const char* name) {
    return gamepadBridge.getSettingInt && gamepadBridge.getSettingInt(name, 0) != 0;
}
// O jogo só lê botões de item quando Player_UpdateUpperBody roda (z_player.c:4189),
// Player_CanUpdateItems (3566), Player_UpdateItems (2625) e Player_ProcessItemButtons
// (2532) deixam; pausa e texto abertos também bloqueiam o botão.
bool ItemButtonsActive(Player* player, PlayState* play) {
    constexpr uint32_t blocking = PLAYER_STATE1_LOADING | PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE |
                                  PLAYER_STATE1_CARRYING_ACTOR;
    const bool waitingPutAway =
        player->actionFunc == Player_Action_WaitForPutAway &&
        !((player->stateFlags1 & PLAYER_STATE1_START_CHANGING_HELD_ITEM) &&
          (player->heldItemId == ITEM_LAST_USED || player->heldItemId == ITEM_NONE));
    const bool changingHeldItem = player->upperActionFunc == Player_UpperAction_ChangeHeldItem &&
                                  Player_ItemToItemAction(player->heldItemId) != player->heldItemAction;
    const bool hookshotWithoutActor = (player->heldItemAction == PLAYER_IA_HOOKSHOT ||
                                       player->heldItemAction == PLAYER_IA_LONGSHOT) &&
                                      player->heldActor == nullptr;
    return player->actor.category == ACTORCAT_PLAYER && !(player->stateFlags1 & blocking) && !waitingPutAway &&
           !changingHeldItem && !hookshotWithoutActor &&
           (SettingEnabled("gEnhancements.QuickPutaway") ||
            !(player->stateFlags1 & PLAYER_STATE1_START_CHANGING_HELD_ITEM)) &&
           (player->heldItemAction == player->itemAction || (player->stateFlags1 & PLAYER_STATE1_SHIELDING)) &&
           gSaveContext.health != 0 && play->csCtx.state == CS_STATE_IDLE && player->csAction == 0 &&
           play->shootingGalleryStatus == 0 && play->activeCamera == CAM_ID_MAIN &&
           play->transitionTrigger != TRANS_TRIGGER_START && gSaveContext.timerState != TIMER_STATE_STOP &&
           play->pauseCtx.state == 0 && play->msgCtx.msgMode == MSGMODE_NONE;
}
constexpr uint8_t AGE_REQ_NONE_VALUE = 9; // AGE_REQ_NONE, z_kaleido_scope.h:21
// Regras de func_80083108 (z_parameter.c:800-1317) que desabilitariam a lente ou a
// máscara num botão C, mais a idade de CHECK_AGE_REQ_ITEM (z_kaleido_scope.h:25).
bool ItemEnabledLikeCButton(Player* player, PlayState* play, uint8_t item) {
    const s32 hazard = Player_GetEnvironmentalHazard(play);
    if ((player->stateFlags1 & (PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_CLIMBING_LADDER)) ||
        (player->stateFlags2 & PLAYER_STATE2_CRAWLING) || play->shootingGalleryStatus > 1 ||
        play->bombchuBowlingStatus != 0 || play->sceneNum == SCENE_FISHING_POND ||
        GameInteractor_PacifistModeActive() || (hazard >= 2 && hazard < 5) ||
        (gSaveContext.eventInf[0] & 0xF) == 1) {
        return false;
    }
    if (!SettingEnabled("gCheats.TimelessEquipment") && gItemAgeReqs[item] != AGE_REQ_NONE_VALUE &&
        gItemAgeReqs[item] != gSaveContext.linkAge) {
        return false;
    }
    if (item == ITEM_LENS) {
        return play->interfaceCtx.restrictions.all == 0 || play->sceneNum == SCENE_TREASURE_BOX_SHOP;
    }
    return play->interfaceCtx.restrictions.tradeItems == 0 || SettingEnabled("gEnhancements.MMBunnyHood");
}
// Botões que mantêm a máscara sem PersistentMasks (z_player.c:2516-2530).
bool ItemOnItemButton(uint8_t item) {
    const int count = SettingEnabled("gEnhancements.DpadEquips") ? 8 : 4;
    for (int button = 1; button < count; ++button) {
        if (gSaveContext.equips.buttonItems[button] == item) return true;
    }
    return false;
}
// Lente ligada pelo atalho: Magic_Update a mantém fora dos botões (z_parameter.c).
bool lensKeptWithoutButton = false;
ShipNativeStatus SHIP_NATIVE_CALL UseItemShortcut(uint8_t item) {
    const bool lens = item == ITEM_LENS;
    if (!lens && (item < ITEM_MASK_KEATON || item > ITEM_MASK_TRUTH)) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto* player = static_cast<Player*>(CurrentPlayer());
    if (!player || !ItemButtonsActive(player, gPlayState) || !ItemEnabledLikeCButton(player, gPlayState, item))
        return SHIP_NATIVE_UNSUPPORTED;
    if (lens) {
        if (gSaveContext.inventory.items[SLOT_LENS] != ITEM_LENS) return SHIP_NATIVE_UNSUPPORTED;
        const bool wasActive = gPlayState->actorCtx.lensActive != 0;
        Player_UseItem(gPlayState, player, ITEM_LENS);
        const bool active = gPlayState->actorCtx.lensActive != 0;
        if (active == wasActive) return SHIP_NATIVE_UNSUPPORTED;
        lensKeptWithoutButton = active;
        return SHIP_NATIVE_OK;
    }
    if (player->currentMask == PLAYER_MASK_NONE &&
        (gSaveContext.inventory.items[SLOT_TRADE_CHILD] != item ||
         (!ItemOnItemButton(item) && !SettingEnabled("gEnhancements.PersistentMasks")))) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto previousMask = player->currentMask;
    Player_UseItem(gPlayState, player, item);
    return player->currentMask != previousMask ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}
// Posições capturadas em Interface_DrawItemButtons (z_parameter.c); HIDDEN usa x=-9999.
struct CapturedItemButton {
    int16_t x = 0;
    int16_t y = 0;
    int16_t size = 0;
    uint8_t alpha = 0;
    uint32_t frame = 0;
    bool valid = false;
};
std::array<CapturedItemButton, 4> itemButtons{};
ShipNativeStatus SHIP_NATIVE_CALL ItemButtonRect(uint8_t button, int16_t* x, int16_t* y, int16_t* size,
                                                 uint8_t* alpha) {
    if (button < LINKSPAN_OOT_ITEM_BUTTON_C_LEFT || button > LINKSPAN_OOT_ITEM_BUTTON_C_RIGHT || !x || !y ||
        !size || !alpha)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!OnGameThread() || !gPlayState) return SHIP_NATIVE_UNSUPPORTED;
    const auto& captured = itemButtons[button];
    // O HUD Lua desenha antes de Interface_Draw: vale a captura do frame anterior.
    if (!captured.valid || gPlayState->state.frames - captured.frame > 1 || captured.x <= -9999 ||
        captured.size <= 0)
        return SHIP_NATIVE_UNSUPPORTED;
    *x = captured.x;
    *y = captured.y;
    *size = captured.size;
    *alpha = captured.alpha;
    return SHIP_NATIVE_OK;
}
uint8_t SHIP_NATIVE_CALL HasFile(const char* path) {
    if (!OnGameThread() || !ValidText(path) || !resourceBridge.hasFile) return 0;
    try { return resourceBridge.hasFile(path); } catch (...) { return 0; }
}
ShipNativeStatus SHIP_NATIVE_CALL ReadFile(const char* path, uint8_t* output, uint32_t capacity,
                                           uint32_t* outputSize) {
    if (!OnGameThread() || !ValidText(path) || !outputSize || (!output && capacity) || !resourceBridge.readFile)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    try { return resourceBridge.readFile(path, output, capacity, outputSize); }
    catch (...) { return SHIP_NATIVE_FAILURE; }
}
ShipNativeStatus SHIP_NATIVE_CALL ListFiles(const char* mask, ShipOotResourcePathFn callback, void* user) {
    if (!OnGameThread() || !mask || std::strlen(mask) > 4096 || !callback || !resourceBridge.listFiles)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    try { return resourceBridge.listFiles(mask, callback, user); }
    catch (...) { return SHIP_NATIVE_FAILURE; }
}
ShipNativeStatus SHIP_NATIVE_CALL DirtyResources(const char* mask) {
    if (!OnGameThread() || !ValidText(mask) || !resourceBridge.dirtyResources)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    try { return resourceBridge.dirtyResources(mask); }
    catch (...) { return SHIP_NATIVE_FAILURE; }
}
ShipNativeStatus SHIP_NATIVE_CALL UnloadResource(const char* path) {
    if (!OnGameThread() || !ValidText(path) || !resourceBridge.unloadResource)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    try { return resourceBridge.unloadResource(path); }
    catch (...) { return SHIP_NATIVE_FAILURE; }
}
ShipNativeStatus SHIP_NATIVE_CALL MountArchive(const char* path, uint64_t* handle) {
    if (!OnGameThread() || !ValidText(path) || !handle || !resourceBridge.mountArchive)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    *handle = 0;
    try { return resourceBridge.mountArchive(path, handle); }
    catch (...) { return SHIP_NATIVE_FAILURE; }
}
ShipNativeStatus SHIP_NATIVE_CALL UnmountArchive(uint64_t handle) {
    if (!OnGameThread() || !handle || !resourceBridge.unmountArchive) return SHIP_NATIVE_INVALID_ARGUMENT;
    try { return resourceBridge.unmountArchive(handle); }
    catch (...) { return SHIP_NATIVE_FAILURE; }
}
ShipNativeStatus SHIP_NATIVE_CALL GetGameVersions(uint32_t* output, uint32_t capacity, uint32_t* outputCount) {
    if (!OnGameThread() || !outputCount || (!output && capacity) || !resourceBridge.getGameVersions)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    try { return resourceBridge.getGameVersions(output, capacity, outputCount); }
    catch (...) { return SHIP_NATIVE_FAILURE; }
}
ShipNativeStatus SHIP_NATIVE_CALL ReadFileLayers(const char* path, ShipOotResourceLayerFn callback, void* user) {
    if (!OnGameThread() || !ValidText(path) || !callback || !resourceBridge.readFileLayers)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    try { return resourceBridge.readFileLayers(path, callback, user); }
    catch (...) { return SHIP_NATIVE_FAILURE; }
}
const ShipOotEngineV1 engineV1{
    sizeof(ShipOotEngineV1), LINKSPAN_OOT_LAYOUT_ID,
    sizeof(PlayState), sizeof(Player), sizeof(SaveContext), Play, CurrentPlayer, Save, Spawn, Kill
};
const ShipOotMovementV1 movementV1{
    sizeof(ShipOotMovementV1), LINKSPAN_OOT_LAYOUT_ID,
    InputCurrent, InputPressed, InputReleased, StickX, StickY, RightStickX, RightStickY,
    HasGamepad, GamepadButtons, ClearGamepadButtonBindings, BindGamepadButton, ReloadGamepadMappings,
    GetSettingInt, SetSettingInt,
    Grounded, Rolling, Jump, Roll
};
const ShipOotMovementV2 movementV2{
    sizeof(ShipOotMovementV2), LINKSPAN_OOT_LAYOUT_ID,
    InputCurrent, InputPressed, InputReleased, StickX, StickY, RightStickX, RightStickY,
    HasGamepad, GamepadButtons, ClearGamepadButtonBindings, BindGamepadButton, ReloadGamepadMappings,
    GetSettingInt, SetSettingInt,
    Grounded, Rolling, Jump, Roll,
    GamepadAxis, BindGamepadAxis, UseItemShortcut, ItemButtonRect
};
const ShipOotResourcesV1 resourcesV1{
    sizeof(ShipOotResourcesV1), HasFile, ReadFile, ListFiles, DirtyResources, UnloadResource,
    MountArchive, UnmountArchive, GetGameVersions
};
const ShipOotResourcesV2 resourcesV2{
    sizeof(ShipOotResourcesV2), HasFile, ReadFile, ListFiles, DirtyResources, UnloadResource,
    MountArchive, UnmountArchive, GetGameVersions, ReadFileLayers
};
}

void SetOotNativeGamepadBridge(OotNativeGamepadBridge bridge) {
    gamepadBridge = bridge;
}

void SetOotNativeResourceBridge(OotNativeResourceBridge bridge) {
    resourceBridge = bridge;
}

ShipLua::NativeProviderPolicy CreateOotNativePolicy() {
    gameThread = std::this_thread::get_id();
    lensKeptWithoutButton = false;
    itemButtons = {};
    InitializeOotNativeRegistry(gameThread);
    ShipLua::NativeProviderPolicy policy;
    policy.enabled = true;
    policy.services.push_back({LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION,
                               sizeof(engineV1), &engineV1});
    policy.services.push_back({LINKSPAN_OOT_MOVEMENT_SERVICE, LINKSPAN_OOT_MOVEMENT_VERSION,
                               sizeof(movementV1), &movementV1});
    policy.services.push_back({LINKSPAN_OOT_MOVEMENT_SERVICE, LINKSPAN_OOT_MOVEMENT_VERSION_2,
                               sizeof(movementV2), &movementV2});
    policy.services.push_back({LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION,
                               sizeof(resourcesV1), &resourcesV1});
    policy.services.push_back({LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION_2,
                               sizeof(resourcesV2), &resourcesV2});
    const auto& registry = GetOotNativeRegistryService();
    policy.services.push_back({LINKSPAN_OOT_REGISTRY_SERVICE, LINKSPAN_OOT_REGISTRY_VERSION,
                               sizeof(registry), &registry});
    return policy;
}
}

// Chamadas pelo código C do jogo (z_parameter.c), sempre na thread do jogo.
extern "C" s32 LinkSpan_KeepLensWithoutButton(PlayState* play, s32 lensOnButton) {
    // Lente num botão ou já desligada encerra a exceção do atalho: religada por um
    // botão C, volta a seguir a regra normal do jogo.
    if (lensOnButton || !play || !play->actorCtx.lensActive) {
        ShipLuaHost::lensKeptWithoutButton = false;
        return lensOnButton;
    }
    return ShipLuaHost::lensKeptWithoutButton ? 1 : 0;
}

extern "C" void LinkSpan_CaptureItemButton(PlayState* play, s32 button, s16 x, s16 y, s16 size, u16 alpha) {
    if (!play || button < static_cast<s32>(LINKSPAN_OOT_ITEM_BUTTON_C_LEFT) ||
        button > static_cast<s32>(LINKSPAN_OOT_ITEM_BUTTON_C_RIGHT))
        return;
    ShipLuaHost::itemButtons[button] = {x, y, size, static_cast<uint8_t>(std::min<u16>(alpha, 255)),
                                        play->state.frames, true};
}
