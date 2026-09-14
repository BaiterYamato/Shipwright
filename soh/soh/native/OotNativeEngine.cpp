#include "OotNativeEngine.h"
#include "OotNativeRegistry.h"
#include "oot_engine.h"
#include "oot_layout_id.h"
#include "oot_ocarina.h"
#include <algorithm>
#include <array>
#include <atomic>
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
// Tira a ocarina no mesmo frame (z_player.c:6003) e toca o som do Player (1816).
s32 Player_ActionHandler_13(Player* player, PlayState* play);
void func_808328EC(Player* player, u16 sfxId);
// Idade por equipamento do menu de pausa (z_kaleido_scope_PAL.c:971).
extern u8 gEquipAgeReqs[][4];
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
// Settings transitórios dos providers: ficam só em memória, nunca viram CVar nem vão para a
// config, e zeram quando a ponte de gamepad e settings é trocada. A sonda devolve a soma dos
// recursos deste host.
constexpr const char* TRANSIENT_SETTINGS_PROBE = "linkspan.transient_settings";
constexpr int32_t TRANSIENT_HIDE_ITEM_BUTTONS = 1;
constexpr int32_t TRANSIENT_SWORD_OVER_SHIELD = 2;
constexpr int32_t TRANSIENT_DPAD_HUD = 4;
// Botões C do HUD, índices 1..3.
constexpr const char* HIDDEN_ITEM_BUTTON_SETTINGS[] = { nullptr, "linkspan.hud.hide_item_button.c_left",
                                                        "linkspan.hud.hide_item_button.c_down",
                                                        "linkspan.hud.hide_item_button.c_right" };
std::array<uint8_t, 4> hiddenItemButtons{};
// Espada acima do escudo, aplicada por LinkSpan_FilterPlayerInput.
constexpr const char* SWORD_OVER_SHIELD_SETTING = "linkspan.input.sword_over_shield";
bool swordOverShield = false;
bool shieldDelivered = false;
bool swordPending = false;
// D-pad do HUD tomado por provider: o fundo é desenhado mesmo sem DpadEquips e os ícones dos itens
// do D-pad saem; o provider desenha os seus na posição que get_item_button_rect devolve para 4..7.
constexpr const char* DPAD_HUD_SETTING = "linkspan.hud.dpad";
bool dpadHudOwned = false;
// Fundo do D-pad tomado: o Interface_Draw só o captura, e o hook do HUD Lua do frame seguinte o desenha
// antes dos callbacks.
struct CapturedDpadBackground {
    int16_t x = 0;
    int16_t y = 0;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t alpha = 0;
    uint32_t frame = 0;
    bool valid = false;
};
CapturedDpadBackground dpadBackground{};
int HiddenItemButtonIndex(const char* name) {
    for (int button = 1; button <= 3; ++button) {
        if (std::strcmp(name, HIDDEN_ITEM_BUTTON_SETTINGS[button]) == 0) return button;
    }
    return 0;
}
int32_t SHIP_NATIVE_CALL GetSettingInt(const char* name, int32_t fallback) {
    if (!OnGameThread() || !name || !*name) return fallback;
    if (std::strcmp(name, TRANSIENT_SETTINGS_PROBE) == 0)
        return TRANSIENT_HIDE_ITEM_BUTTONS | TRANSIENT_SWORD_OVER_SHIELD | TRANSIENT_DPAD_HUD;
    if (const int button = HiddenItemButtonIndex(name)) return hiddenItemButtons[button];
    if (std::strcmp(name, SWORD_OVER_SHIELD_SETTING) == 0) return swordOverShield ? 1 : 0;
    if (std::strcmp(name, DPAD_HUD_SETTING) == 0) return dpadHudOwned ? 1 : 0;
    return gamepadBridge.getSettingInt ? gamepadBridge.getSettingInt(name, fallback) : fallback;
}
ShipNativeStatus SHIP_NATIVE_CALL SetSettingInt(const char* name, int32_t value) {
    if (!OnGameThread() || !name || !*name) return SHIP_NATIVE_UNSUPPORTED;
    if (std::strcmp(name, TRANSIENT_SETTINGS_PROBE) == 0) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (const int button = HiddenItemButtonIndex(name)) {
        hiddenItemButtons[button] = value != 0 ? 1 : 0;
        return SHIP_NATIVE_OK;
    }
    if (std::strcmp(name, SWORD_OVER_SHIELD_SETTING) == 0) {
        swordOverShield = value != 0;
        shieldDelivered = swordPending = false;
        return SHIP_NATIVE_OK;
    }
    if (std::strcmp(name, DPAD_HUD_SETTING) == 0) {
        dpadHudOwned = value != 0;
        return SHIP_NATIVE_OK;
    }
    return gamepadBridge.setSettingInt ? gamepadBridge.setSettingInt(name, value) : SHIP_NATIVE_UNSUPPORTED;
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
bool IsOcarinaItem(uint8_t item) {
    return item == ITEM_OCARINA_FAIRY || item == ITEM_OCARINA_TIME;
}
bool IsEquipmentItem(uint8_t item) {
    return item >= ITEM_TUNIC_KOKIRI && item <= ITEM_BOOTS_HOVER;
}
// Regras de func_80083108 (z_parameter.c:800-1317) que desabilitariam o item num botão
// C, mais a idade de CHECK_AGE_REQ_ITEM e CHECK_AGE_REQ_EQUIP (z_kaleido_scope.h:23-25).
bool ItemEnabledLikeCButton(Player* player, PlayState* play, uint8_t item) {
    const bool ocarina = IsOcarinaItem(item);
    const bool equipment = IsEquipmentItem(item);
    const s32 hazard = Player_GetEnvironmentalHazard(play);
    if ((player->stateFlags1 & (PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_CLIMBING_LADDER)) ||
        (player->stateFlags2 & PLAYER_STATE2_CRAWLING) || play->shootingGalleryStatus > 1 ||
        play->bombchuBowlingStatus != 0 || play->sceneNum == SCENE_FISHING_POND ||
        GameInteractor_PacifistModeActive()) {
        return false;
    }
    // Submerso só o equipamento continua (z_parameter.c:924-968); no evento de arco a
    // cavalo, só a ocarina (983-1039).
    if ((hazard >= 2 && hazard < 5 && !equipment) || ((gSaveContext.eventInf[0] & 0xF) == 1 && !ocarina)) {
        return false;
    }
    const bool timeless = SettingEnabled("gCheats.TimelessEquipment");
    if (equipment) {
        const bool tunic = item <= ITEM_TUNIC_ZORA;
        const uint8_t value = static_cast<uint8_t>(item - (tunic ? ITEM_TUNIC_KOKIRI : ITEM_BOOTS_KOKIRI) + 1);
        const uint8_t age = gEquipAgeReqs[tunic ? EQUIP_TYPE_TUNIC : EQUIP_TYPE_BOOTS][value];
        // Equipamento num botão C fica sempre habilitado (z_parameter.c:1096-1106).
        return timeless || age == AGE_REQ_NONE_VALUE || age == gSaveContext.linkAge;
    }
    if (!timeless && gItemAgeReqs[item] != AGE_REQ_NONE_VALUE && gItemAgeReqs[item] != gSaveContext.linkAge) {
        return false;
    }
    if (item == ITEM_LENS) {
        return play->interfaceCtx.restrictions.all == 0 || play->sceneNum == SCENE_TREASURE_BOX_SHOP;
    }
    if (ocarina) return play->interfaceCtx.restrictions.ocarina == 0;
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
// Traje e botas trocam "apesar do estado" como no AssignableTunicsAndBoots
// (soh/soh/Enhancements/Items/AssignableTunicsAndBoots.cpp:18-24), fora de pausa e texto.
bool EquipmentShortcutActive(Player* player, PlayState* play) {
    constexpr uint32_t blocking = PLAYER_STATE1_LOADING | PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_IN_ITEM_CS |
                                  PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_TALKING | PLAYER_STATE1_DEAD;
    return player->actor.category == ACTORCAT_PLAYER && !(player->stateFlags1 & blocking) &&
           !(player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) && player->csAction == 0 &&
           gSaveContext.health != 0 && play->csCtx.state == CS_STATE_IDLE && play->pauseCtx.state == 0 &&
           play->msgCtx.msgMode == MSGMODE_NONE;
}
ShipNativeStatus UseEquipmentShortcut(Player* player, PlayState* play, uint8_t item) {
    const bool tunic = item <= ITEM_TUNIC_ZORA;
    const s16 type = tunic ? EQUIP_TYPE_TUNIC : EQUIP_TYPE_BOOTS;
    const u16 value = static_cast<u16>(item - (tunic ? ITEM_TUNIC_KOKIRI : ITEM_BOOTS_KOKIRI) + 1);
    if (!EquipmentShortcutActive(player, play) || !ItemEnabledLikeCButton(player, play, item) ||
        !CHECK_OWNED_EQUIP(type, value - 1))
        return SHIP_NATIVE_UNSUPPORTED;
    // Usar o equipado volta ao Kokiri, como o botão do SoH.
    const u16 current = static_cast<u16>(CUR_EQUIP_VALUE(type));
    const u16 next = current == value ? (tunic ? EQUIP_VALUE_TUNIC_KOKIRI : EQUIP_VALUE_BOOTS_KOKIRI) : value;
    if (next == current) return SHIP_NATIVE_UNSUPPORTED;
    Inventory_ChangeEquipment(type, next);
    Player_SetEquipmentData(play, player);
    func_808328EC(player, !tunic && next == EQUIP_VALUE_BOOTS_IRON ? NA_SE_PL_WALK_HEAVYBOOTS : NA_SE_PL_CHANGE_ARMS);
    return SHIP_NATIVE_OK;
}
// Ocarina: Player_UseItem arma unk_6AD como o botão C e Player_ActionHandler_13 começa a
// tocar no mesmo frame, igual a func_8084B4D4 (z_player.c:12892). Chamado depois do update,
// sem isso o Player_ProcessItemButtons do frame seguinte guardaria a ocarina fora dos botões.
ShipNativeStatus UseOcarinaShortcut(Player* player, PlayState* play, uint8_t item) {
    if (gSaveContext.inventory.items[SLOT_OCARINA] != item || player->actionFunc == Player_Action_Roll ||
        player->upperActionFunc == Player_UpperAction_ChangeHeldItem ||
        (player->stateFlags1 & PLAYER_STATE1_START_CHANGING_HELD_ITEM))
        return SHIP_NATIVE_UNSUPPORTED;
    const s8 expected = Player_ItemToItemAction(item);
    Player_UseItem(play, player, item);
    if (player->unk_6AD != 4 || player->itemAction != expected) return SHIP_NATIVE_UNSUPPORTED;
    Player_ActionHandler_13(player, play);
    if (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) return SHIP_NATIVE_OK;
    // Fora do chão o handler recusa: desfaz o pedido para não sobrar no próximo frame.
    player->itemAction = player->heldItemAction;
    player->unk_6AD = 0;
    return SHIP_NATIVE_UNSUPPORTED;
}
ShipNativeStatus SHIP_NATIVE_CALL UseItemShortcut(uint8_t item) {
    const bool lens = item == ITEM_LENS;
    const bool mask = item >= ITEM_MASK_KEATON && item <= ITEM_MASK_TRUTH;
    if (!lens && !mask && !IsOcarinaItem(item) && !IsEquipmentItem(item)) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto* player = static_cast<Player*>(CurrentPlayer());
    if (!player) return SHIP_NATIVE_UNSUPPORTED;
    if (IsEquipmentItem(item)) return UseEquipmentShortcut(player, gPlayState, item);
    if (!ItemButtonsActive(player, gPlayState) || !ItemEnabledLikeCButton(player, gPlayState, item))
        return SHIP_NATIVE_UNSUPPORTED;
    if (IsOcarinaItem(item)) return UseOcarinaShortcut(player, gPlayState, item);
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
// Índices de SaveContext.equips.buttonItems no SoH: 1..3 são os botões C e 4..7 o D-pad (cima,
// baixo, esquerda e direita), capturado quando o HUD desenha o D-pad.
constexpr uint8_t LAST_CAPTURED_BUTTON = 7;
std::array<CapturedItemButton, LAST_CAPTURED_BUTTON + 1> itemButtons{};
ShipNativeStatus SHIP_NATIVE_CALL ItemButtonRect(uint8_t button, int16_t* x, int16_t* y, int16_t* size,
                                                 uint8_t* alpha) {
    if (button < LINKSPAN_OOT_ITEM_BUTTON_C_LEFT || button > LAST_CAPTURED_BUTTON || !x || !y || !size || !alpha)
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
// linkspan.oot.ocarina v1 (OOT-MIC-001). func_800EE6F4 (code_800EC960.c) publica o
// estado a cada update de input da ocarina, dentro de func_800F3054 (graph.c:393),
// na thread do jogo e depois do game.frame; a música pendente é consumida ali.
std::atomic<uint8_t> ocarinaActive{0};
std::atomic<uint16_t> ocarinaSongFlags{0};
std::atomic<int32_t> pendingOcarinaSong{-1};
bool OcarinaPatternValid(uint8_t song) {
    if (song > OCARINA_SONG_SCARECROW_SPAWN) return false;
    const auto& pattern = gOcarinaSongButtons[song];
    if (pattern.numButtons < 2 || pattern.numButtons > LINKSPAN_OOT_OCARINA_MAX_NOTES) return false;
    return song != OCARINA_SONG_SCARECROW_SPAWN ||
           sOcarinaSongNotes[OCARINA_SONG_SCARECROW_SPAWN][1].volume != 0xFF;
}
uint8_t SHIP_NATIVE_CALL OcarinaActive() {
    return OnGameThread() ? ocarinaActive.load() : 0;
}
uint16_t SHIP_NATIVE_CALL OcarinaSongFlags() {
    return OnGameThread() ? ocarinaSongFlags.load() : 0;
}
// Doze músicas fixas; a do Espantalho (12) só existe depois de gravada (code_800EC960.c:1420).
uint8_t SHIP_NATIVE_CALL OcarinaSongCount() {
    if (!OnGameThread()) return 0;
    return OcarinaPatternValid(OCARINA_SONG_SCARECROW_SPAWN) ? OCARINA_SONG_SCARECROW_SPAWN + 1
                                                            : OCARINA_SONG_SCARECROW_SPAWN;
}
ShipNativeStatus SHIP_NATIVE_CALL OcarinaSongPattern(uint8_t song, uint8_t* notes, uint32_t capacity,
                                                     uint32_t* outputCount) {
    if (!outputCount || (!notes && capacity) || song > OCARINA_SONG_SCARECROW_SPAWN)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    *outputCount = 0;
    if (!OnGameThread() || !OcarinaPatternValid(song)) return SHIP_NATIVE_UNSUPPORTED;
    const auto& info = gOcarinaSongButtons[song];
    *outputCount = info.numButtons;
    if (!notes) return SHIP_NATIVE_OK;
    if (capacity < info.numButtons) return SHIP_NATIVE_LIMIT;
    std::memcpy(notes, info.buttonsIndex, info.numButtons);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL OcarinaSubmitSong(uint8_t song) {
    if (song > OCARINA_SONG_SCARECROW_SPAWN) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!OnGameThread() || !ocarinaActive.load() || !(ocarinaSongFlags.load() & (1u << song)))
        return SHIP_NATIVE_UNSUPPORTED;
    pendingOcarinaSong.store(song);
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
const ShipOotOcarinaV1 ocarinaV1{
    sizeof(ShipOotOcarinaV1), OcarinaActive, OcarinaSongFlags, OcarinaSongCount, OcarinaSongPattern,
    OcarinaSubmitSong
};
}

void SetOotNativeGamepadBridge(OotNativeGamepadBridge bridge) {
    gamepadBridge = bridge;
    // Os settings transitórios pertencem aos mods que usavam a ponte anterior.
    hiddenItemButtons = {};
    swordOverShield = shieldDelivered = swordPending = false;
    dpadHudOwned = false;
    dpadBackground = {};
}

void SetOotNativeResourceBridge(OotNativeResourceBridge bridge) {
    resourceBridge = bridge;
}

ShipLua::NativeProviderPolicy CreateOotNativePolicy() {
    gameThread = std::this_thread::get_id();
    lensKeptWithoutButton = false;
    itemButtons = {};
    dpadBackground = {};
    ocarinaActive.store(0);
    ocarinaSongFlags.store(0);
    pendingOcarinaSong.store(-1);
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
    policy.services.push_back({LINKSPAN_OOT_OCARINA_SERVICE, LINKSPAN_OOT_OCARINA_VERSION,
                               sizeof(ocarinaV1), &ocarinaV1});
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
        button > static_cast<s32>(ShipLuaHost::LAST_CAPTURED_BUTTON))
        return;
    ShipLuaHost::itemButtons[button] = {x, y, size, static_cast<uint8_t>(std::min<u16>(alpha, 255)),
                                        play->state.frames, true};
}

// Chamadas por func_800EE6F4 (code_800EC960.c) a cada update de input da ocarina.
extern "C" void OotNative_PublishOcarinaState(u8 active, u16 availableSongFlags) {
    ShipLuaHost::ocarinaActive.store(active ? 1 : 0);
    ShipLuaHost::ocarinaSongFlags.store(active ? availableSongFlags : 0);
    // Música entregue para uma ocarina que já fechou não vale para a próxima.
    if (!active) ShipLuaHost::pendingOcarinaSong.store(-1);
}

extern "C" s32 OotNative_TakePendingOcarinaSong(void) {
    return ShipLuaHost::pendingOcarinaSong.exchange(-1);
}

// Chamada por Interface_DrawItemButtons e Interface_Draw (z_parameter.c) para cada botão C.
extern "C" s32 LinkSpan_ItemButtonHidden(s32 button) {
    return button >= 1 && button <= 3 && ShipLuaHost::hiddenItemButtons[button] ? 1 : 0;
}

// Chamada por Interface_Draw (z_parameter.c) antes de desenhar o D-pad do HUD.
extern "C" s32 LinkSpan_DpadHudOwned(void) {
    return ShipLuaHost::dpadHudOwned ? 1 : 0;
}

// Chamada por Interface_Draw (z_parameter.c) no lugar do desenho do fundo do D-pad tomado.
extern "C" void LinkSpan_CaptureDpadBackground(PlayState* play, s16 x, s16 y, u8 r, u8 g, u8 b, u16 alpha) {
    if (!play) return;
    ShipLuaHost::dpadBackground = { x, y, r, g, b, static_cast<uint8_t>(std::min<u16>(alpha, 255)),
                                    play->state.frames, true };
}

// Para o hook do HUD Lua (ShipLuaBootstrap.cpp), que roda antes do Interface_Draw: vale a captura do frame
// anterior, e só enquanto um provider mantém o D-pad tomado.
extern "C" s32 LinkSpan_OwnedDpadBackground(PlayState* play, s16* x, s16* y, u8* r, u8* g, u8* b, u8* alpha) {
    const auto& captured = ShipLuaHost::dpadBackground;
    if (!play || !x || !y || !r || !g || !b || !alpha || !ShipLuaHost::dpadHudOwned || !captured.valid ||
        play->state.frames - captured.frame > 1)
        return 0;
    *x = captured.x;
    *y = captured.y;
    *r = captured.r;
    *g = captured.g;
    *b = captured.b;
    *alpha = captured.alpha;
    return 1;
}

// Chamada por Player_Update (z_player.c) com a cópia do input do Player. O OoT não ataca com o
// escudo erguido (func_8083BB20 exige !PLAYER_STATE1_SHIELDING); com o escudo no gatilho da mira,
// B pressionado tira o R do input. Se B foi apertado com o escudo erguido, o aperto chega no frame
// seguinte, depois de o escudo baixar.
extern "C" void LinkSpan_FilterPlayerInput(Input* input) {
    if (!input || !ShipLuaHost::swordOverShield) {
        ShipLuaHost::shieldDelivered = ShipLuaHost::swordPending = false;
        return;
    }
    const bool swordHeld = CHECK_BTN_ALL(input->cur.button, BTN_B);
    if (swordHeld || ShipLuaHost::swordPending) {
        input->cur.button &= ~BTN_R;
        input->press.button &= ~BTN_R;
    }
    if (ShipLuaHost::swordPending) {
        input->press.button |= BTN_B;
        ShipLuaHost::swordPending = false;
    } else if (swordHeld && CHECK_BTN_ALL(input->press.button, BTN_B) && ShipLuaHost::shieldDelivered) {
        input->press.button &= ~BTN_B;
        ShipLuaHost::swordPending = true;
    }
    ShipLuaHost::shieldDelivered = CHECK_BTN_ALL(input->cur.button, BTN_R);
}
