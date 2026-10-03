#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>
#include "oot_engine.h"
#include "oot_registry.h"
#include "oot_resources.h"
#include "oot_layout_id.h"
#include "oot_hooks.h"
#include "z64.h"
#include "package_assets.h"
#include "item_icons.h"
#include "camera_continuity.h"
#include "menu_input.h"

namespace {
// SDL usa posições Xbox. No Switch Pro: B->SDL A, A->SDL B, Y->SDL X, X->SDL Y.
constexpr uint8_t SDL_BUTTON_B_NINTENDO = 0;
constexpr uint8_t SDL_BUTTON_A_NINTENDO = 1;
constexpr uint8_t SDL_BUTTON_Y_NINTENDO = 2;
constexpr uint8_t SDL_BUTTON_X_NINTENDO = 3;
// Botão "−" (SDL_CONTROLLER_BUTTON_BACK): L do N64.
constexpr uint8_t SDL_BUTTON_MINUS = 4;
// Clique do analógico direito (SDL_CONTROLLER_BUTTON_RIGHTSTICK): atalho de lente e máscara.
constexpr uint8_t SDL_BUTTON_RIGHT_STICK = 8;
// R físico (SDL_CONTROLLER_BUTTON_RIGHTSHOULDER): segurar abre o menu de itens.
constexpr uint8_t SDL_BUTTON_R = 10;
// D-pad direita (SDL_CONTROLLER_BUTTON_DPAD_RIGHT): C-Up nativo, Navi e primeira pessoa.
constexpr uint8_t SDL_BUTTON_DPAD_RIGHT = 14;
// D-pad cima, baixo e esquerda (SDL_CONTROLLER_BUTTON_DPAD_UP/DOWN/LEFT): traje, botas e ocarina.
constexpr uint8_t SDL_BUTTON_DPAD_UP = 11;
constexpr uint8_t SDL_BUTTON_DPAD_DOWN = 12;
constexpr uint8_t SDL_BUTTON_DPAD_LEFT = 13;
// ZL (SDL_CONTROLLER_AXIS_TRIGGERLEFT): o Z (mira) do mapeamento padrão e também o escudo.
constexpr uint8_t SDL_AXIS_ZL = 4;
// ZR (SDL_CONTROLLER_AXIS_TRIGGERRIGHT): usa o item do botão C equipado.
constexpr uint8_t SDL_AXIS_ZR = 5;
constexpr int16_t ZR_HELD = 8000;
constexpr uint32_t PhysicalButton(uint8_t button) { return uint32_t{ 1 } << button; }
constexpr const char* FREE_LOOK_SETTING = "gSettings.FreeLook.Enabled";
// Sem a chave na config, o SoH usa o eixo vertical invertido (z_camera.c, Camera_FreeLook).
constexpr const char* FREE_LOOK_INVERT_Y_SETTING = "gSettings.FreeLook.InvertYAxis";
// Primeira pessoa e mira (func_8084ABD8 e func_8083FD78 em z_player.c): o analógico direito move
// a visão e o esquerdo anda com o Link, porque o MoveInFirstPerson só vale junto do RightStickAim.
// Sem as chaves de inversão na config, o SoH usa o eixo vertical invertido do N64.
struct ForcedSetting {
    const char* name;
    int32_t fallback;
    int32_t value;
};
constexpr ForcedSetting FIRST_PERSON_SETTINGS[] = {
    { "gSettings.Controls.RightStickAim", 0, 1 },
    { "gSettings.MoveInFirstPerson", 0, 1 },
    { "gSettings.Controls.InvertAimingYAxis", 1, 0 },
    { "gSettings.Controls.InvertZAimingYAxis", 1, 0 },
};
constexpr const char* PERSISTENT_MASKS_SETTING = "gEnhancements.PersistentMasks";
// Settings transitórios do host: ficam só em memória e nunca vão para a config. A sonda
// devolve a soma dos recursos que o host suporta.
constexpr const char* TRANSIENT_SETTINGS_PROBE = "linkspan.transient_settings";
constexpr int32_t TRANSIENT_HIDE_ITEM_BUTTONS = 1;
constexpr int32_t TRANSIENT_SWORD_OVER_SHIELD = 2;
constexpr int32_t TRANSIENT_DPAD_HUD = 4;
// Com B pressionado, o host não entrega o R ao Player: o escudo do ZL baixa para a espada.
constexpr const char* SWORD_OVER_SHIELD_SETTING = "linkspan.input.sword_over_shield";
// D-pad do HUD nativo com as funções do mod: o host desenha o fundo e informa a posição das
// direções, na ordem cima, baixo, esquerda e direita (índices 4..7 de get_item_button_rect).
constexpr const char* DPAD_HUD_SETTING = "linkspan.hud.dpad";
constexpr uint8_t DPAD_HUD_SLOTS[] = { 4, 5, 6, 7 };
constexpr const char* HIDDEN_ITEM_BUTTON_SETTINGS[] = { nullptr, "linkspan.hud.hide_item_button.c_left",
                                                        "linkspan.hud.hide_item_button.c_down",
                                                        "linkspan.hud.hide_item_button.c_right" };
// Toque rápido no R3 alterna a lente; segurar por este tempo alterna a máscara.
constexpr long long SHORTCUT_HOLD_MILLISECONDS = 400;
// Segurar o D-pad cima/baixo por este tempo abre o menu de traje ou botas.
constexpr long long QUICK_SWAP_HOLD_MILLISECONDS = 400;
// Nos menus, o analógico direito anda uma opção ao passar de MENU_STICK_PRESS e só anda
// de novo depois de voltar para dentro de MENU_STICK_RELEASE.
constexpr int MENU_STICK_PRESS = 45;
constexpr int MENU_STICK_RELEASE = 20;
constexpr const char* TUNIC_RESULTS[] = { "tunic-blocked", "tunic-kokiri", "tunic-goron", "tunic-zora" };
constexpr const char* BOOTS_RESULTS[] = { "boots-blocked", "boots-kokiri", "boots-iron", "boots-hover" };
// Botão virtual de cada índice de SaveContext.equips.buttonItems (1..3 = C).
constexpr uint16_t ITEM_BUTTONS[] = { 0, BTN_CLEFT, BTN_CDOWN, BTN_CRIGHT };

enum class Phase { Ready, Airborne, Rolling, Running };

// Toque/segurar do D-pad para traje ou botas. Valores de equipamento 1..3 (EQUIP_VALUE_*).
struct EquipGesture {
    bool wasDown = false;
    bool quickSwap = false;
    bool waitRelease = false;
    uint8_t highlight = 0;
    uint8_t last = 0;
    int8_t stickLatch = 0;
    std::chrono::steady_clock::time_point pressedAt{};
};

// Menu do R: destaque entre C-Left, C-Down e C-Right, na ordem do HUD.
struct ItemMenu {
    bool open = false;
    uint8_t highlight = LINKSPAN_OOT_ITEM_BUTTON_C_LEFT;
    LinkSpanMenuInput navigation{};
};

struct Mod {
    const ShipOotEngineV1* engine;
    const ShipOotMovementV2* movement;
    const ShipOotResourcesV2* resources;
    const ShipOotRegistryV1* registry;
    const void* const* itemIcons = nullptr;
    CameraGeometryFn cameraGeometry = nullptr;
    CameraEpoch cameraEpoch{};
    uint64_t registrySpace = 0;
    uint64_t jumpEntry = 0;
    uint64_t sprintEntry = 0;
    uint16_t keyboardJumpMask = 0;
    Phase phase = Phase::Ready;
    bool jumpWasDown = false;
    bool observedRoll = false;
    bool faceBindingsApplied = false;
    bool freeLookApplied = false;
    int32_t previousFreeLook = 0;
    bool invertYApplied = false;
    int32_t previousInvertY = 1;
    // Quantos FIRST_PERSON_SETTINGS o configure aplicou, na ordem, e o valor anterior de cada um.
    size_t firstPersonApplied = 0;
    int32_t previousFirstPerson[std::size(FIRST_PERSON_SETTINGS)] = {};
    uint32_t cameraFollowDelayMilliseconds = 500;
    bool cameraFreeLookActive = false;
    bool cameraWaitCenter = false;
    std::chrono::steady_clock::time_point lastCameraInput = std::chrono::steady_clock::now();
    bool persistentMasksApplied = false;
    int32_t previousPersistentMasks = 0;
    bool itemHudApplied = false;
    bool swordOverShieldApplied = false;
    bool dpadHudApplied = false;
    uint64_t iconArchive = 0;
    bool shortcutWasDown = false;
    bool shortcutHoldFired = false;
    std::chrono::steady_clock::time_point shortcutPressedAt{};
    uint8_t selectedItemButton = LINKSPAN_OOT_ITEM_BUTTON_C_LEFT;
    bool selectWasDown = false;
    ItemMenu itemMenu{};
    bool ocarinaWasDown = false;
    EquipGesture tunic{};
    EquipGesture boots{};
    const ShipNativeRuntime* runtime = nullptr;
    uint64_t menuInputHook = 0;
    bool menuInputCaptured = false;
    int8_t menuLeftX = 0;
    int8_t menuRightX = 0;
};

enum class CameraChange { None, FreeLook, Automatic };

// Um passo por inclinação do analógico direito: -1 esquerda, +1 direita, 0 parado.
int ReadMenuStep(const Mod& mod, int8_t& latch) {
    const int x = mod.movement->get_right_stick_x(0);
    if (latch != 0) {
        if (x > -MENU_STICK_RELEASE && x < MENU_STICK_RELEASE) latch = 0;
        return 0;
    }
    if (x >= MENU_STICK_PRESS) {
        latch = 1;
        return 1;
    }
    if (x <= -MENU_STICK_PRESS) {
        latch = -1;
        return -1;
    }
    return 0;
}

// Parado, a câmera livre mantém o ângulo escolhido. Quando Link volta a andar com
// o analógico direito parado há cameraFollowDelayMilliseconds, o FreeLook é
// desligado e o Camera_Normal1 do jogo assume a partir do eye atual: sem salto,
// girando para trás de Link com a suavização nativa. Desligar a CVar (em vez de
// só zerar play->manualCamera) impede que drift do analógico religue o modo manual.
// Com um menu aberto ou em primeira pessoa o analógico direito tem outro uso (escolher a
// opção ou mirar): o FreeLook fica desligado e, depois, o analógico precisa voltar ao
// centro antes de mover a câmera.
void SuspendCamera(Mod& mod, PlayState* play) {
    if (mod.cameraFreeLookActive && mod.movement->set_setting_int(FREE_LOOK_SETTING, 0) == SHIP_NATIVE_OK) {
        mod.cameraFreeLookActive = false;
    }
    if (play && !mod.cameraFreeLookActive) play->manualCamera = false;
}

CameraChange UpdateCamera(Mod& mod, PlayState* play, bool stickBusy) {
    constexpr int CAMERA_DEADZONE = 12;
    constexpr int MOVE_DEADZONE = 20;
    const int x = mod.movement->get_right_stick_x(0);
    const int y = mod.movement->get_right_stick_y(0);
    const auto now = std::chrono::steady_clock::now();
    const bool stickActive = (x * x) + (y * y) >= CAMERA_DEADZONE * CAMERA_DEADZONE;
    if (mod.cameraEpoch.Observe(play)) {
        SuspendCamera(mod, play);
        mod.lastCameraInput = now;
    }
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    const auto* player = static_cast<const Player*>(mod.engine->get_player());
    if (!CameraCanResume(play, save, player)) {
        SuspendCamera(mod, play);
        return CameraChange::None;
    }
    if (stickBusy) {
        mod.cameraWaitCenter = true;
        SuspendCamera(mod, play);
        return CameraChange::None;
    }
    if (mod.cameraWaitCenter) {
        if (stickActive) return CameraChange::None;
        mod.cameraWaitCenter = false;
        mod.lastCameraInput = now;
    }
    if (stickActive) {
        mod.lastCameraInput = now;
        if (!mod.cameraFreeLookActive || !play->manualCamera) {
            if (ResumeCameraFromView(*play, *play->cameraPtrs[CAM_ID_MAIN], mod.cameraGeometry) &&
                mod.movement->set_setting_int(FREE_LOOK_SETTING, 1) == SHIP_NATIVE_OK) {
                mod.cameraFreeLookActive = true;
                return CameraChange::FreeLook;
            }
            SuspendCamera(mod, play);
        }
        return CameraChange::None;
    }
    const int moveX = mod.movement->get_stick_x(0);
    const int moveY = mod.movement->get_stick_y(0);
    const bool moving = (moveX * moveX) + (moveY * moveY) >= MOVE_DEADZONE * MOVE_DEADZONE;
    if (mod.cameraFreeLookActive && moving &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - mod.lastCameraInput).count() >=
            mod.cameraFollowDelayMilliseconds &&
        mod.movement->set_setting_int(FREE_LOOK_SETTING, 0) == SHIP_NATIVE_OK) {
        mod.cameraFreeLookActive = false;
        play->manualCamera = false;
        return CameraChange::Automatic;
    }
    return CameraChange::None;
}

// R3: soltar antes de SHORTCUT_HOLD_MILLISECONDS alterna a lente; segurar até o
// limite alterna a máscara uma única vez e ignora a soltura. As regras do jogo
// (estado do Player, cena, idade, magia) ficam no host (movement v2).
const char* UpdateShortcut(Mod& mod, const Player& player, uint32_t physical) {
    const bool down = (physical & PhysicalButton(SDL_BUTTON_RIGHT_STICK)) != 0;
    const auto now = std::chrono::steady_clock::now();
    if (down && !mod.shortcutWasDown) {
        mod.shortcutPressedAt = now;
        mod.shortcutHoldFired = false;
    }
    const char* result = nullptr;
    const auto* play = static_cast<const PlayState*>(mod.engine->get_play_state());
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (play && save) {
        if (down && !mod.shortcutHoldFired &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - mod.shortcutPressedAt).count() >=
                SHORTCUT_HOLD_MILLISECONDS) {
            mod.shortcutHoldFired = true;
            // Máscara em uso: qualquer item de máscara a tira, como o botão C nativo.
            const uint8_t mask = player.currentMask != PLAYER_MASK_NONE
                ? static_cast<uint8_t>(ITEM_MASK_KEATON + player.currentMask - PLAYER_MASK_KEATON)
                : save->inventory.items[SLOT_TRADE_CHILD];
            result = mod.movement->player_use_item_shortcut(mask) != SHIP_NATIVE_OK ? "mask-blocked"
                     : player.currentMask != PLAYER_MASK_NONE                      ? "mask-on"
                                                                                    : "mask-off";
        } else if (!down && mod.shortcutWasDown && !mod.shortcutHoldFired) {
            result = mod.movement->player_use_item_shortcut(ITEM_LENS) != SHIP_NATIVE_OK ? "lens-blocked"
                     : play->actorCtx.lensActive                                           ? "lens-on"
                                                                                           : "lens-off";
        }
    }
    mod.shortcutWasDown = down;
    return result;
}

ShipNativeStatus Write(ShipNativeWriteFn write, void* writer, const char* text) {
    return write(writer, text, static_cast<uint32_t>(std::strlen(text)));
}

// ZR segura o botão C equipado: o jogo o trata como o C nativo, inclusive
// mirar arco e gancho enquanto o gatilho continua pressionado.
ShipNativeStatus BindItemTrigger(Mod& mod, uint8_t itemButton) {
    for (uint8_t button = LINKSPAN_OOT_ITEM_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEM_BUTTON_C_RIGHT; ++button) {
        if (mod.movement->clear_gamepad_button_bindings(0, ITEM_BUTTONS[button]) != SHIP_NATIVE_OK)
            return SHIP_NATIVE_FAILURE;
    }
    return mod.movement->bind_gamepad_axis(0, ITEM_BUTTONS[itemButton], SDL_AXIS_ZR, 1);
}

bool HostSupports(const Mod& mod, int32_t feature) {
    return (mod.movement->get_setting_int(TRANSIENT_SETTINGS_PROBE, 0) & feature) != 0;
}

// Só o botão C equipado no ZR aparece no HUD. O host oculta os outros só em memória: nada vai
// para a config, e um host sem o recurso mantém os três botões visíveis.
void ApplyItemHud(Mod& mod) {
    if (!HostSupports(mod, TRANSIENT_HIDE_ITEM_BUTTONS)) return;
    for (uint8_t button = LINKSPAN_OOT_ITEM_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEM_BUTTON_C_RIGHT; ++button) {
        mod.movement->set_setting_int(HIDDEN_ITEM_BUTTON_SETTINGS[button], button == mod.selectedItemButton ? 0 : 1);
    }
    mod.itemHudApplied = true;
}

void RestoreItemHud(Mod& mod) {
    if (!mod.itemHudApplied) return;
    for (uint8_t button = LINKSPAN_OOT_ITEM_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEM_BUTTON_C_RIGHT; ++button) {
        mod.movement->set_setting_int(HIDDEN_ITEM_BUTTON_SETTINGS[button], 0);
    }
    mod.itemHudApplied = false;
}

ShipNativeStatus ApplyNintendoBindings(Mod& mod) {
    // A físico vira a ação contextual N64 A. Y e B alimentam N64 B: ataque e
    // cancelar/guardar continuam sendo decididos pelo estado normal do jogo.
    // D-pad direita alimenta o C-Up nativo (Navi e primeira pessoa). O ZL segue como o
    // Z (mira) do mapeamento padrão e também ergue o escudo (R do N64); com Y ou B
    // pressionado, o host baixa o escudo para a espada sair. "−" é o L do N64 e o L físico
    // fica livre. R3, R e o resto do D-pad (ocarina, traje e botas) são lidos
    // fisicamente, sem botão virtual.
    const uint16_t remappedButtons[] = { BTN_A, BTN_B, BTN_CUP, BTN_DRIGHT, BTN_R, BTN_L,
                                         BTN_DUP, BTN_DDOWN, BTN_DLEFT };
    for (const uint16_t button : remappedButtons) {
        if (mod.movement->clear_gamepad_button_bindings(0, button) != SHIP_NATIVE_OK) {
            mod.movement->reload_gamepad_mappings(0);
            return SHIP_NATIVE_FAILURE;
        }
    }
    if (mod.movement->bind_gamepad_button(0, BTN_A, SDL_BUTTON_A_NINTENDO) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_B, SDL_BUTTON_Y_NINTENDO) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_B, SDL_BUTTON_B_NINTENDO) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_CUP, SDL_BUTTON_DPAD_RIGHT) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_axis(0, BTN_R, SDL_AXIS_ZL, 1) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_L, SDL_BUTTON_MINUS) != SHIP_NATIVE_OK ||
        BindItemTrigger(mod, mod.selectedItemButton) != SHIP_NATIVE_OK) {
        mod.movement->reload_gamepad_mappings(0);
        return SHIP_NATIVE_FAILURE;
    }
    mod.faceBindingsApplied = true;
    ApplyItemHud(mod);
    if (!mod.swordOverShieldApplied && HostSupports(mod, TRANSIENT_SWORD_OVER_SHIELD) &&
        mod.movement->set_setting_int(SWORD_OVER_SHIELD_SETTING, 1) == SHIP_NATIVE_OK) {
        mod.swordOverShieldApplied = true;
    }
    if (!mod.dpadHudApplied && HostSupports(mod, TRANSIENT_DPAD_HUD) &&
        mod.movement->set_setting_int(DPAD_HUD_SETTING, 1) == SHIP_NATIVE_OK) {
        mod.dpadHudApplied = true;
    }
    return SHIP_NATIVE_OK;
}

bool HasItem(const SaveContext& save, uint8_t button) {
    return save.equips.buttonItems[button] != ITEM_NONE;
}

uint8_t FirstItemButton(const SaveContext& save) {
    for (uint8_t button = LINKSPAN_OOT_ITEM_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEM_BUTTON_C_RIGHT; ++button) {
        if (HasItem(save, button)) return button;
    }
    return 0;
}

// Próximo botão C com item na direção pedida, sem dar a volta.
uint8_t StepItemButton(uint8_t current, int direction, const SaveContext& save) {
    for (int candidate = current + direction;
         candidate >= LINKSPAN_OOT_ITEM_BUTTON_C_LEFT && candidate <= LINKSPAN_OOT_ITEM_BUTTON_C_RIGHT;
         candidate += direction) {
        if (HasItem(save, static_cast<uint8_t>(candidate))) return static_cast<uint8_t>(candidate);
    }
    return current;
}

// Fecha o seletor e descarta o analógico capturado: a próxima abertura não herda a leitura de antes de
// uma pausa, da ocarina ou da troca de cena.
void ResetItemMenu(Mod& mod) {
    mod.itemMenu = {};
    mod.menuInputCaptured = false;
    mod.menuLeftX = mod.menuRightX = 0;
}

// Segurar R abre o menu no botão C equipado; o analógico direito anda para os lados e
// soltar o R equipa o destacado no ZR. Com o ZR pressionado a troca é recusada: soltaria
// o item em uso.
const char* UpdateItemSelection(Mod& mod, uint32_t physical) {
    const bool down = (physical & PhysicalButton(SDL_BUTTON_R)) != 0;
    const auto* play = static_cast<const PlayState*>(mod.engine->get_play_state());
    if (play && play->pauseCtx.state != 0) {
        mod.selectWasDown = down;
        ResetItemMenu(mod);
        return nullptr;
    }
    const bool pressed = down && !mod.selectWasDown;
    const bool released = !down && mod.selectWasDown;
    mod.selectWasDown = down;
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (!save) {
        ResetItemMenu(mod);
        return nullptr;
    }
    if (pressed) {
        const uint8_t start =
            HasItem(*save, mod.selectedItemButton) ? mod.selectedItemButton : FirstItemButton(*save);
        if (!start) return "item-menu-empty";
        mod.itemMenu = ItemMenu{ true, start, {} };
        return "item-menu";
    }
    if (!mod.itemMenu.open) return nullptr;
    if (down) {
        const bool next = (physical & (PhysicalButton(SDL_BUTTON_DPAD_RIGHT) |
                                      PhysicalButton(SDL_BUTTON_DPAD_UP))) != 0;
        const bool previous = (physical & (PhysicalButton(SDL_BUTTON_DPAD_LEFT) |
                                          PhysicalButton(SDL_BUTTON_DPAD_DOWN))) != 0;
        const int leftX = mod.menuInputCaptured ? mod.menuLeftX : mod.movement->get_stick_x(0);
        const int rightX = mod.menuInputCaptured ? mod.menuRightX : mod.movement->get_right_stick_x(0);
        mod.menuInputCaptured = false;
        if (const int step = LinkSpanMenuStep(&mod.itemMenu.navigation, leftX, rightX,
                next == previous ? 0 : next ? 1 : -1)) {
            mod.itemMenu.highlight = StepItemButton(mod.itemMenu.highlight, step, *save);
        }
        return nullptr;
    }
    mod.itemMenu.open = false;
    mod.menuInputCaptured = false;
    const uint8_t target = mod.itemMenu.highlight;
    if (!released || target == mod.selectedItemButton) return nullptr;
    if (mod.movement->get_gamepad_axis(0, SDL_AXIS_ZR) > ZR_HELD) return "item-zr-held";
    if (BindItemTrigger(mod, target) != SHIP_NATIVE_OK) {
        mod.movement->reload_gamepad_mappings(0);
        mod.faceBindingsApplied = false;
        return "mapping-error";
    }
    mod.selectedItemButton = target;
    ApplyItemHud(mod);
    return target == LINKSPAN_OOT_ITEM_BUTTON_C_LEFT   ? "item-c-left"
           : target == LINKSPAN_OOT_ITEM_BUTTON_C_DOWN ? "item-c-down"
                                                       : "item-c-right";
}

ShipNativeStatus SHIP_NATIVE_CALL CaptureItemMenuInput(void* user, const ShipNativeHookCall* call) {
    auto& mod = *static_cast<Mod*>(user);
    auto* play = static_cast<PlayState*>(static_cast<ShipOotPlayHookV1*>(call->payload)->play_state);
    const auto* player = static_cast<const Player*>(mod.engine->get_player());
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (!play || !player || !save || play->pauseCtx.state ||
        (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) || !mod.movement->has_gamepad(0))
        return SHIP_NATIVE_OK;
    const bool rHeld = (mod.movement->get_gamepad_buttons(0) & PhysicalButton(SDL_BUTTON_R)) != 0;
    if (!mod.itemMenu.open && (!rHeld || !FirstItemButton(*save))) return SHIP_NATIVE_OK;
    Input& input = play->state.input[0];
    mod.menuLeftX = input.rel.stick_x;
    mod.menuRightX = input.rel.right_stick_x;
    mod.menuInputCaptured = true;
    // The selector owns navigation. Do not walk Link or enter Navi/first person
    // while using the left stick or D-pad to choose a hotbar slot.
    const uint16_t directions = BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT | BTN_CUP;
    input.cur.button &= ~directions;
    input.press.button &= ~directions;
    input.cur.stick_x = input.cur.stick_y = 0;
    input.rel.stick_x = input.rel.stick_y = 0;
    input.cur.right_stick_x = input.cur.right_stick_y = 0;
    input.rel.right_stick_x = input.rel.right_stick_y = 0;
    return SHIP_NATIVE_OK;
}

// D-pad esquerda tira a ocarina do inventário sem ocupar botão C.
const char* UpdateOcarina(Mod& mod, uint32_t physical) {
    const bool down = (physical & PhysicalButton(SDL_BUTTON_DPAD_LEFT)) != 0;
    const bool pressed = down && !mod.ocarinaWasDown;
    mod.ocarinaWasDown = down;
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (!pressed || !save) return nullptr;
    const uint8_t ocarina = save->inventory.items[SLOT_OCARINA];
    if (ocarina != ITEM_OCARINA_FAIRY && ocarina != ITEM_OCARINA_TIME) return "ocarina-blocked";
    return mod.movement->player_use_item_shortcut(ocarina) == SHIP_NATIVE_OK ? "ocarina" : "ocarina-blocked";
}

uint8_t CurrentEquip(const SaveContext& save, uint8_t type) {
    return static_cast<uint8_t>((save.equips.equipment >> (4 * type)) & 0xF);
}

// Obtido (inventory.equipment) e permitido pela idade: Goron, Zora, ferro e flutuantes são
// de adulto (gEquipAgeReqs), salvo TimelessEquipment. O host confere de novo ao equipar.
bool EquipAvailable(const Mod& mod, const SaveContext& save, uint8_t type, uint8_t value) {
    if (value < 1 || value > 3 || !(save.inventory.equipment & (1u << (4 * type + value - 1)))) return false;
    return value == 1 || save.linkAge == LINK_AGE_ADULT ||
           mod.movement->get_setting_int("gCheats.TimelessEquipment", 0) != 0;
}

uint8_t NextEquip(const Mod& mod, const SaveContext& save, uint8_t type, uint8_t from) {
    for (uint8_t step = 1; step <= 3; ++step) {
        const uint8_t candidate = static_cast<uint8_t>((from + 2 + step) % 3 + 1);
        if (EquipAvailable(mod, save, type, candidate)) return candidate;
    }
    return from;
}

// Próximo equipamento disponível na direção pedida, na ordem dos ícones (1..3), sem dar a volta.
uint8_t StepEquip(const Mod& mod, const SaveContext& save, uint8_t type, uint8_t from, int direction) {
    for (int candidate = from + direction; candidate >= 1 && candidate <= 3; candidate += direction) {
        if (EquipAvailable(mod, save, type, static_cast<uint8_t>(candidate))) return static_cast<uint8_t>(candidate);
    }
    return from;
}

// Toque: o traje volta ao último usado e as botas alternam com as Kokiri. Sem histórico,
// vai do Kokiri para o primeiro especial disponível.
uint8_t TapTarget(const Mod& mod, const SaveContext& save, uint8_t type, const EquipGesture& gesture) {
    const uint8_t current = CurrentEquip(save, type);
    if (type == EQUIP_TYPE_BOOTS && current != EQUIP_VALUE_BOOTS_KOKIRI) return EQUIP_VALUE_BOOTS_KOKIRI;
    if (gesture.last != current && EquipAvailable(mod, save, type, gesture.last)) return gesture.last;
    return current != 1 ? 1 : NextEquip(mod, save, type, 1);
}

// D-pad cima (traje) e baixo (botas): toque troca; segurar abre o menu no equipado, o
// analógico direito escolhe para os lados e soltar veste o destacado.
const char* UpdateEquipGesture(Mod& mod, EquipGesture& gesture, uint32_t physical, uint8_t button, uint8_t type) {
    const bool down = (physical & PhysicalButton(button)) != 0;
    if (gesture.waitRelease) {
        gesture.wasDown = down;
        if (!down) gesture.waitRelease = false;
        return nullptr;
    }
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (!save) {
        gesture.wasDown = false;
        gesture.quickSwap = false;
        return nullptr;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool tunic = type == EQUIP_TYPE_TUNIC;
    const uint8_t current = CurrentEquip(*save, type);
    const char* result = nullptr;
    if (down && !gesture.wasDown) {
        gesture.pressedAt = now;
        gesture.quickSwap = false;
    }
    if (down && !gesture.quickSwap &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - gesture.pressedAt).count() >=
            QUICK_SWAP_HOLD_MILLISECONDS) {
        // O menu só abre com outra opção disponível além da atual.
        if (NextEquip(mod, *save, type, current) != current) {
            gesture.quickSwap = true;
            gesture.highlight = current;
            gesture.stickLatch = 0;
            result = tunic ? "tunic-quick-swap" : "boots-quick-swap";
        }
    } else if (down && gesture.quickSwap) {
        if (const int step = ReadMenuStep(mod, gesture.stickLatch)) {
            gesture.highlight = StepEquip(mod, *save, type, gesture.highlight, step);
        }
    }
    if (!down && gesture.wasDown) {
        const uint8_t target = gesture.quickSwap ? gesture.highlight : TapTarget(mod, *save, type, gesture);
        gesture.quickSwap = false;
        if (target != current) {
            const auto* results = tunic ? TUNIC_RESULTS : BOOTS_RESULTS;
            const uint8_t item = static_cast<uint8_t>((tunic ? ITEM_TUNIC_KOKIRI : ITEM_BOOTS_KOKIRI) + target - 1);
            if (mod.movement->player_use_item_shortcut(item) == SHIP_NATIVE_OK) {
                gesture.last = current;
                result = results[target];
            } else {
                result = results[0];
            }
        }
    }
    gesture.wasDown = down;
    return result;
}

ShipNativeStatus SHIP_NATIVE_CALL Status(void* user, const char*, uint32_t length,
                                        ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    char result[768];
    const int size = std::snprintf(result, sizeof(result),
        "perfil=nintendo; X=pulo %.2f; A=contexto/rolar/sprint; Y=espada; B=cancelar; "
        "ZR=item do C equipado; R (segurar)=menu de itens com o analógico direito; "
        "ZL=mirar + escudo (Y ataca mesmo defendendo); "
        "-=L do N64; R3=lente (toque)/máscara (segurar); D-pad direita=C-Up; D-pad esquerda=ocarina; "
        "D-pad cima=traje (toque: último; segurar: menu); D-pad baixo=botas (toque: Kokiri; segurar: menu); "
        "HUD=só o C equipado e o D-pad com traje, botas, ocarina e Navi; "
        "câmera=stick direito com vertical normal, volta a seguir ao andar após %.2fs; "
        "primeira pessoa e mira=stick direito olha (vertical normal), esquerdo anda; "
        "gamepad=%s",
        double(JUMP_VELOCITY), double(mod.cameraFollowDelayMilliseconds) / 1000.0,
        mod.movement->has_gamepad(0) ? "conectado" : "ausente");
    return size > 0 && size < int(sizeof(result)) ? write(writer, result, uint32_t(size)) : SHIP_NATIVE_FAILURE;
}

ShipNativeStatus SHIP_NATIVE_CALL Configure(void* user, const char* payload, uint32_t length,
                                           ShipNativeWriteFn write, void* writer) {
    if (!payload || !length || length >= 16) return SHIP_NATIVE_INVALID_ARGUMENT;
    char value[16]{};
    std::memcpy(value, payload, length);
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    if (end == value || parsed > 0xFFFFu) return SHIP_NATIVE_INVALID_ARGUMENT;
    // Segundo valor: atraso em ms (0..60000) sem analógico direito antes de a
    // câmera voltar a seguir Link quando ele anda.
    unsigned long followDelay = 500;
    if (*end == ',') {
        char* delayEnd = nullptr;
        followDelay = std::strtoul(end + 1, &delayEnd, 10);
        if (delayEnd == end + 1 || *delayEnd != '\0' || followDelay > 60000)
            return SHIP_NATIVE_INVALID_ARGUMENT;
    } else if (*end != '\0') {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& mod = *static_cast<Mod*>(user);
    mod.keyboardJumpMask = static_cast<uint16_t>(parsed);
    mod.cameraFollowDelayMilliseconds = static_cast<uint32_t>(followDelay);
    if (!mod.freeLookApplied) {
        mod.previousFreeLook = mod.movement->get_setting_int(FREE_LOOK_SETTING, 0);
        // Enable only when the stick takes control of a valid gameplay view.
        if (mod.movement->set_setting_int(FREE_LOOK_SETTING, 0) != SHIP_NATIVE_OK) {
            return SHIP_NATIVE_FAILURE;
        }
        mod.freeLookApplied = true;
        mod.cameraFreeLookActive = false;
        mod.lastCameraInput = std::chrono::steady_clock::now();
    }
    // Analógico direito para cima olha para cima: o padrão do SoH sem a chave é invertido.
    if (!mod.invertYApplied) {
        mod.previousInvertY = mod.movement->get_setting_int(FREE_LOOK_INVERT_Y_SETTING, 1);
        if (mod.movement->set_setting_int(FREE_LOOK_INVERT_Y_SETTING, 0) != SHIP_NATIVE_OK) {
            return SHIP_NATIVE_FAILURE;
        }
        mod.invertYApplied = true;
    }
    // Primeira pessoa e mira: visão pelo analógico direito, com o vertical normal, e o esquerdo andando.
    for (; mod.firstPersonApplied < std::size(FIRST_PERSON_SETTINGS); ++mod.firstPersonApplied) {
        const ForcedSetting& setting = FIRST_PERSON_SETTINGS[mod.firstPersonApplied];
        const int32_t previous = mod.movement->get_setting_int(setting.name, setting.fallback);
        if (mod.movement->set_setting_int(setting.name, setting.value) != SHIP_NATIVE_OK) {
            return SHIP_NATIVE_FAILURE;
        }
        mod.previousFirstPerson[mod.firstPersonApplied] = previous;
    }
    // A máscara colocada pelo R3 fica fora dos botões C; sem PersistentMasks o
    // jogo a tiraria (Player_ProcessItemButtons) e o host recusa o uso.
    if (!mod.persistentMasksApplied) {
        mod.previousPersistentMasks = mod.movement->get_setting_int(PERSISTENT_MASKS_SETTING, 0);
        if (mod.movement->set_setting_int(PERSISTENT_MASKS_SETTING, 1) != SHIP_NATIVE_OK) {
            return SHIP_NATIVE_FAILURE;
        }
        mod.persistentMasksApplied = true;
    }
    // Ícones do mod (a Navi do D-pad): pasta assets/ do pacote, montada uma vez.
    if (!mod.iconArchive) {
        const std::string assets = ProviderAssetsDirectory();
        if (!assets.empty()) mod.resources->mount_archive(assets.c_str(), &mod.iconArchive);
    }
    return Write(write, writer, "configured");
}

ShipNativeStatus SHIP_NATIVE_CALL Jump(void* user, const char*, uint32_t length,
                                      ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    const ShipNativeStatus result = mod.movement->player_jump(float(JUMP_VELOCITY));
    return result == SHIP_NATIVE_OK ? Write(write, writer, "jump") : result;
}

ShipNativeStatus SHIP_NATIVE_CALL ResourceProbe(void* user, const char*, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    uint32_t size = 0;
    if (mod.resources->read_file("test/core.json", nullptr, 0, &size) != SHIP_NATIVE_OK ||
        !size || size > SHIP_NATIVE_MAX_BYTES) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* bytes = new (std::nothrow) uint8_t[size];
    if (!bytes) return SHIP_NATIVE_FAILURE;
    const auto read = mod.resources->read_file("test/core.json", bytes, size, &size);
    const auto written = read == SHIP_NATIVE_OK
        ? write(writer, reinterpret_cast<const char*>(bytes), size)
        : read;
    delete[] bytes;
    return written;
}

struct LayerProbeState {
    char archives[320]{};
    uint32_t used = 0;
    uint32_t count = 0;
    uint64_t mergedHash = 14695981039346656037ULL;
};

ShipNativeStatus SHIP_NATIVE_CALL CollectLayer(void* user, const ShipOotResourceLayerV2* layer,
                                               const char* archivePath, const uint8_t* data) {
    auto& state = *static_cast<LayerProbeState*>(user);
    if (!layer || layer->size < sizeof(ShipOotResourceLayerV2) || !archivePath ||
        (!data && layer->data_size) || layer->layer_index != state.count) {
        return SHIP_NATIVE_FAILURE;
    }
    const int written = std::snprintf(state.archives + state.used, sizeof(state.archives) - state.used,
                                      "%s%.*s", state.count ? ">" : "",
                                      static_cast<int>(layer->archive_path_length), archivePath);
    if (written < 0 || static_cast<uint32_t>(written) >= sizeof(state.archives) - state.used) {
        return SHIP_NATIVE_LIMIT;
    }
    state.used += static_cast<uint32_t>(written);
    for (uint32_t i = 0; i < layer->data_size; ++i) {
        state.mergedHash ^= data[i];
        state.mergedHash *= 1099511628211ULL;
    }
    ++state.count;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL LayerProbe(void* user, const char* payload, uint32_t length,
                                             ShipNativeWriteFn write, void* writer) {
    if ((!payload && length) || length > 4096) return SHIP_NATIVE_INVALID_ARGUMENT;
    char path[4097]{};
    if (length) std::memcpy(path, payload, length);
    else std::memcpy(path, "test/layers.json", sizeof("test/layers.json"));
    LayerProbeState state;
    const auto& mod = *static_cast<Mod*>(user);
    const auto status = mod.resources->read_file_layers(path, CollectLayer, &state);
    if (status != SHIP_NATIVE_OK) return status;
    char report[448];
    const int size = std::snprintf(report, sizeof(report), "layers=%u; order=%s; merged=%016llx",
                                   state.count, state.archives,
                                   static_cast<unsigned long long>(state.mergedHash));
    return size > 0 && size < int(sizeof(report)) ? write(writer, report, uint32_t(size)) : SHIP_NATIVE_FAILURE;
}

ShipNativeStatus SHIP_NATIVE_CALL LayerRuntimeProbe(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                                    void* writer) {
    if (length)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    uint64_t base = 0;
    uint64_t overrideLayer = 0;
    if (mod.resources->mount_archive("mods/linkspan-layer-base.zip", &base) != SHIP_NATIVE_OK || !base) {
        return Write(write, writer, "fail@mount-base");
    }
    const auto mountedOverride = mod.resources->mount_archive("mods/linkspan-layer-override.zip", &overrideLayer);
    if (mountedOverride != SHIP_NATIVE_OK || !overrideLayer) {
        mod.resources->unmount_archive(base);
        return Write(write, writer, "fail@mount-override");
    }

    LayerProbeState state;
    const auto read = mod.resources->read_file_layers("unbound/layer-probe.json", CollectLayer, &state);
    const auto unmountOverride = mod.resources->unmount_archive(overrideLayer);
    const auto unmountBase = mod.resources->unmount_archive(base);
    if (read != SHIP_NATIVE_OK || unmountOverride != SHIP_NATIVE_OK || unmountBase != SHIP_NATIVE_OK) {
        return Write(write, writer, "fail@read-or-cleanup");
    }
    char report[448];
    const int size = std::snprintf(report, sizeof(report), "layers=%u; order=%s; merged=%016llx; cleanup=ok",
                                   state.count, state.archives, static_cast<unsigned long long>(state.mergedHash));
    return size > 0 && size < int(sizeof(report)) ? write(writer, report, uint32_t(size)) : SHIP_NATIVE_FAILURE;
}

// Decodifica o arquivo "version" do archive: byte 0 = endianness
// (0 = little, 1 = big), seguido de uint32 com a versão do jogo.
uint32_t ParseVersionFile(const uint8_t* bytes, uint32_t size) {
    if (!bytes || size != 5) return 0;
    if (bytes[0] == 1) {
        return (uint32_t(bytes[1]) << 24) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 8) |
               uint32_t(bytes[4]);
    }
    return uint32_t(bytes[1]) | (uint32_t(bytes[2]) << 8) | (uint32_t(bytes[3]) << 16) |
           (uint32_t(bytes[4]) << 24);
}

struct ListCounter {
    uint32_t count = 0;
    char first[96]{};
};

ShipNativeStatus SHIP_NATIVE_CALL CountPath(void* user, const char* path, uint32_t pathLength) {
    auto& counter = *static_cast<ListCounter*>(user);
    if (!counter.count && path && pathLength < sizeof(counter.first)) {
        std::memcpy(counter.first, path, pathLength);
        counter.first[pathLength] = '\0';
    }
    ++counter.count;
    return SHIP_NATIVE_OK;
}

struct RegistryCounter {
    uint32_t count = 0;
    int32_t firstId = LINKSPAN_OOT_REGISTRY_AUTO_ID;
};

ShipNativeStatus SHIP_NATIVE_CALL CountRegistryEntry(void* user, uint64_t, int32_t id,
                                                     const char*, uint32_t) {
    auto& counter = *static_cast<RegistryCounter*>(user);
    if (!counter.count) counter.firstId = id;
    ++counter.count;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL RegistryProbe(void* user, const char*, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    uint64_t space = 0;
    if (mod.registry->find_space("example/dynamic_movement/actions", &space) != SHIP_NATIVE_OK ||
        space != mod.registrySpace) {
        return Write(write, writer, "fail@space");
    }
    uint64_t entry = 0;
    int32_t id = 0;
    if (mod.registry->find_entry_by_name(space, "example/dynamic_movement/jump", &entry, &id) !=
            SHIP_NATIVE_OK ||
        entry != mod.jumpEntry) {
        return Write(write, writer, "fail@jump");
    }
    char name[64]{};
    uint8_t payload[64]{};
    uint32_t nameSize = 0;
    uint32_t payloadSize = 0;
    int32_t readId = 0;
    if (mod.registry->read_entry(entry, name, sizeof(name), &nameSize, payload, sizeof(payload),
                                 &payloadSize, &readId) != SHIP_NATIVE_OK) {
        return Write(write, writer, "fail@read");
    }
    RegistryCounter counter;
    if (mod.registry->list_entries(space, CountRegistryEntry, &counter) != SHIP_NATIVE_OK) {
        return Write(write, writer, "fail@list");
    }
    char report[256];
    const int size = std::snprintf(
        report, sizeof(report), "space=ok; entries=%u; first=%d; jump=%.*s:%.*s; id=%d",
        counter.count, counter.firstId, static_cast<int>(nameSize), name,
        static_cast<int>(payloadSize), reinterpret_cast<const char*>(payload), readId);
    return size > 0 && size < int(sizeof(report)) ? write(writer, report, uint32_t(size)) : SHIP_NATIVE_FAILURE;
}

// Prova de runtime do OOT-CORE-001: exercita o serviço linkspan.oot.resources
// v1 contra o VFS real do jogo (versions, has_file, read_file, list_files e
// montagem/desmontagem de um archive auxiliar com marcador conhecido).
ShipNativeStatus SHIP_NATIVE_CALL ResourceRuntimeProbe(void* user, const char*, uint32_t length,
                                                       ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    const char* step = "versions";
    char report[768];
    int used = 0;

    uint32_t versionCount = 0;
    if (mod.resources->get_game_versions(nullptr, 0, &versionCount) != SHIP_NATIVE_OK || !versionCount ||
        versionCount > 8) {
        return Write(write, writer, "fail@versions-count");
    }
    uint32_t versions[8]{};
    if (mod.resources->get_game_versions(versions, 8, &versionCount) != SHIP_NATIVE_OK) {
        return Write(write, writer, "fail@versions-read");
    }
    used = std::snprintf(report, sizeof(report), "versions=%u", versionCount);
    for (uint32_t i = 0; i < versionCount && used > 0 && used < int(sizeof(report)) - 24; ++i) {
        used += std::snprintf(report + used, sizeof(report) - used, "%s0x%08X", i ? "," : ":", versions[i]);
    }

    step = "has_file";
    const uint8_t hasReal = mod.resources->has_file("objects/gameplay_keep/gArrow1Anim");
    const uint8_t hasMissing = mod.resources->has_file("linkspan/definitely-missing.bin");
    if (hasReal != 1 || hasMissing != 0) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@has_file real=%u missing=%u", hasReal,
                      hasMissing);
        return Write(write, writer, report);
    }
    used += std::snprintf(report + used, sizeof(report) - used, "; has=1/0");

    step = "read_file";
    uint32_t versionSize = 0;
    if (mod.resources->read_file("version", nullptr, 0, &versionSize) != SHIP_NATIVE_OK || versionSize != 5) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@version-size %u", versionSize);
        return Write(write, writer, report);
    }
    uint8_t versionBytes[5]{};
    if (mod.resources->read_file("version", versionBytes, sizeof(versionBytes), &versionSize) != SHIP_NATIVE_OK) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@version-read");
        return Write(write, writer, report);
    }
    const uint32_t fileVersion = ParseVersionFile(versionBytes, versionSize);
    bool versionListed = false;
    for (uint32_t i = 0; i < versionCount; ++i) versionListed = versionListed || versions[i] == fileVersion;
    used += std::snprintf(report + used, sizeof(report) - used, "; version_file=0x%08X listed=%u", fileVersion,
                          versionListed ? 1u : 0u);

    step = "list_files";
    ListCounter counter;
    if (mod.resources->list_files("objects/gameplay_keep/gArrow*", CountPath, &counter) != SHIP_NATIVE_OK ||
        !counter.count) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@list_files");
        return Write(write, writer, report);
    }
    used += std::snprintf(report + used, sizeof(report) - used, "; arrows=%u first=%s", counter.count,
                          counter.first);

    step = "mount_archive";
    uint64_t handle = 0;
    if (mod.resources->mount_archive("mods/linkspan-resource-probe.zip", &handle) != SHIP_NATIVE_OK || !handle) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@mount");
        return Write(write, writer, report);
    }
    const char* markerPath = "test/linkspan-core-001-marker.txt";
    const char* markerExpected = "linkspan-oot-core-001-marker";
    char marker[64]{};
    uint32_t markerSize = 0;
    const bool mountedVisible = mod.resources->has_file(markerPath) == 1;
    const bool markerOk = mountedVisible &&
        mod.resources->read_file(markerPath, reinterpret_cast<uint8_t*>(marker), sizeof(marker) - 1,
                                 &markerSize) == SHIP_NATIVE_OK &&
        markerSize == std::strlen(markerExpected) && !std::memcmp(marker, markerExpected, markerSize);
    const auto unmount = mod.resources->unmount_archive(handle);
    const bool gone = mod.resources->has_file(markerPath) == 0;
    used += std::snprintf(report + used, sizeof(report) - used,
                          "; mount=ok handle=%llu marker=%s unmount=%s gone=%u",
                          static_cast<unsigned long long>(handle), markerOk ? "match" : "fail",
                          unmount == SHIP_NATIVE_OK ? "ok" : "fail", gone ? 1u : 0u);
    if (!markerOk || unmount != SHIP_NATIVE_OK || !gone) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@%s", step);
    }
    return Write(write, writer, report);
}

// Posição do botão C equipado para o anel do HUD Lua: "x,y,lado,alpha" ou "none".
ShipNativeStatus SHIP_NATIVE_CALL HudSelection(void* user, const char*, uint32_t length,
                                              ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    int16_t x = 0;
    int16_t y = 0;
    int16_t side = 0;
    uint8_t alpha = 0;
    if (!mod.faceBindingsApplied || !mod.movement->has_gamepad(0) ||
        mod.movement->get_item_button_rect(mod.selectedItemButton, &x, &y, &side, &alpha) != SHIP_NATIVE_OK ||
        !alpha) {
        return Write(write, writer, "none");
    }
    char result[48];
    const int size = std::snprintf(result, sizeof(result), "%d,%d,%d,%u", x, y, side, unsigned(alpha));
    return size > 0 && size < int(sizeof(result)) ? write(writer, result, uint32_t(size)) : SHIP_NATIVE_FAILURE;
}

// Menu de traje/botas aberto para o HUD Lua: "tunic;<destaque>;<opções>", "boots;..." ou "none".
ShipNativeStatus SHIP_NATIVE_CALL HudQuickSwap(void* user, const char*, uint32_t length,
                                              ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    const bool tunic = mod.tunic.quickSwap;
    if (!save || (!tunic && !mod.boots.quickSwap)) return Write(write, writer, "none");
    const uint8_t type = tunic ? EQUIP_TYPE_TUNIC : EQUIP_TYPE_BOOTS;
    const EquipGesture& gesture = tunic ? mod.tunic : mod.boots;
    char result[32];
    int size = std::snprintf(result, sizeof(result), "%s;%u;", tunic ? "tunic" : "boots", unsigned(gesture.highlight));
    bool first = true;
    for (uint8_t value = 1; value <= 3 && size > 0 && size < int(sizeof(result)) - 3; ++value) {
        if (!EquipAvailable(mod, *save, type, value)) continue;
        size += std::snprintf(result + size, sizeof(result) - size, first ? "%u" : ",%u", unsigned(value));
        first = false;
    }
    return size > 0 && size < int(sizeof(result)) ? write(writer, result, uint32_t(size)) : SHIP_NATIVE_FAILURE;
}

// D-pad do HUD para o Lua: "x,y,lado,alpha;" de cima, baixo, esquerda e direita, seguido de
// "traje,botas,ocarina" (valores de equipamento 1..3 e o item do slot da ocarina), ou "none".
ShipNativeStatus SHIP_NATIVE_CALL HudDpad(void* user, const char*, uint32_t length,
                                         ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (!mod.dpadHudApplied || !save || !mod.movement->has_gamepad(0)) return Write(write, writer, "none");
    char result[128];
    int size = 0;
    for (const uint8_t slot : DPAD_HUD_SLOTS) {
        int16_t x = 0;
        int16_t y = 0;
        int16_t side = 0;
        uint8_t alpha = 0;
        if (mod.movement->get_item_button_rect(slot, &x, &y, &side, &alpha) != SHIP_NATIVE_OK) {
            return Write(write, writer, "none");
        }
        size += std::snprintf(result + size, sizeof(result) - size, "%d,%d,%d,%u;", x, y, side, unsigned(alpha));
    }
    size += std::snprintf(result + size, sizeof(result) - size, "%u,%u,%u",
                          unsigned(CurrentEquip(*save, EQUIP_TYPE_TUNIC)),
                          unsigned(CurrentEquip(*save, EQUIP_TYPE_BOOTS)),
                          unsigned(save->inventory.items[SLOT_OCARINA]));
    return size > 0 && size < int(sizeof(result)) ? write(writer, result, uint32_t(size)) : SHIP_NATIVE_FAILURE;
}

// Menu do R: "<highlight>;<button>:<item>:<icon path>\t...", or "none".
ShipNativeStatus SHIP_NATIVE_CALL HudItemMenu(void* user, const char*, uint32_t length,
                                             ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (!save || !mod.itemMenu.open) return Write(write, writer, "none");
    const std::string result = ItemSelectorIcons(mod.itemMenu.highlight, save->equips.buttonItems, mod.itemIcons);
    return write(writer, result.data(), static_cast<uint32_t>(result.size()));
}

// Read-only diagnostic, including when the menu is closed.
ShipNativeStatus SHIP_NATIVE_CALL HudItemIcons(void* user, const char*, uint32_t length,
                                              ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (!save) return Write(write, writer, "none");
    const std::string result = ItemSelectorIcons(mod.itemMenu.highlight, save->equips.buttonItems, mod.itemIcons);
    return write(writer, result.data(), static_cast<uint32_t>(result.size()));
}

ShipNativeStatus SHIP_NATIVE_CALL Update(void* user, const char*, uint32_t length,
                                        ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    auto* player = static_cast<Player*>(mod.engine->get_player());
    auto* play = static_cast<PlayState*>(mod.engine->get_play_state());
    if (!player) {
        UpdateCamera(mod, play, false);
        mod.phase = Phase::Ready;
        mod.jumpWasDown = false;
        mod.observedRoll = false;
        mod.shortcutWasDown = false;
        mod.shortcutHoldFired = false;
        mod.selectWasDown = false;
        ResetItemMenu(mod);
        mod.ocarinaWasDown = false;
        mod.tunic.wasDown = mod.tunic.quickSwap = false;
        mod.boots.wasDown = mod.boots.quickSwap = false;
        return Write(write, writer, "unavailable");
    }

    const bool hasGamepad = mod.movement->has_gamepad(0) != 0;
    if (hasGamepad && !mod.faceBindingsApplied && ApplyNintendoBindings(mod) != SHIP_NATIVE_OK) {
        return Write(write, writer, "mapping-error");
    }
    const uint32_t physical = hasGamepad ? mod.movement->get_gamepad_buttons(0) : 0;
    if (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) {
        // Ocarina owns L/R/Y/X/A. In particular, R must not open the item selector
        // and X must not queue a jump after the instrument closes.
        ResetItemMenu(mod);
        mod.selectWasDown = (physical & PhysicalButton(SDL_BUTTON_R)) != 0;
        mod.jumpWasDown = (physical & PhysicalButton(SDL_BUTTON_X_NINTENDO)) != 0;
        mod.shortcutWasDown = (physical & PhysicalButton(SDL_BUTTON_RIGHT_STICK)) != 0;
        mod.ocarinaWasDown = (physical & PhysicalButton(SDL_BUTTON_DPAD_LEFT)) != 0;
        mod.tunic.wasDown = (physical & PhysicalButton(SDL_BUTTON_DPAD_UP)) != 0;
        mod.boots.wasDown = (physical & PhysicalButton(SDL_BUTTON_DPAD_DOWN)) != 0;
        mod.tunic.waitRelease = mod.tunic.wasDown;
        mod.boots.waitRelease = mod.boots.wasDown;
        mod.tunic.quickSwap = mod.boots.quickSwap = false;
        mod.phase = Phase::Ready;
        mod.observedRoll = false;
        mod.cameraWaitCenter = true;
        UpdateCamera(mod, play, true);
        return Write(write, writer, "ocarina");
    }
    if (play && play->pauseCtx.state != 0) {
        mod.cameraWaitCenter = true;
        UpdateCamera(mod, play, true);
        ResetItemMenu(mod);
        mod.selectWasDown = (physical & PhysicalButton(SDL_BUTTON_R)) != 0;
        mod.shortcutWasDown = (physical & PhysicalButton(SDL_BUTTON_RIGHT_STICK)) != 0;
        mod.ocarinaWasDown = (physical & PhysicalButton(SDL_BUTTON_DPAD_LEFT)) != 0;
        mod.tunic.wasDown = (physical & PhysicalButton(SDL_BUTTON_DPAD_UP)) != 0;
        mod.boots.wasDown = (physical & PhysicalButton(SDL_BUTTON_DPAD_DOWN)) != 0;
        return Write(write, writer, "paused");
    }
    // Todos os gestos avançam a cada frame; o primeiro evento do frame vai para o log.
    const bool itemMenuWasOpen = mod.itemMenu.open;
    const char* selection = UpdateItemSelection(mod, physical);
    const bool selecting = itemMenuWasOpen || mod.itemMenu.open;
    if (selecting) {
        // D-pad navigation belongs to this menu. Latch the other shortcuts so
        // releasing R with D-pad held cannot change clothes or draw the ocarina.
        mod.ocarinaWasDown = (physical & PhysicalButton(SDL_BUTTON_DPAD_LEFT)) != 0;
        mod.tunic.wasDown = (physical & PhysicalButton(SDL_BUTTON_DPAD_UP)) != 0;
        mod.boots.wasDown = (physical & PhysicalButton(SDL_BUTTON_DPAD_DOWN)) != 0;
        mod.tunic.waitRelease = mod.tunic.wasDown;
        mod.boots.waitRelease = mod.boots.wasDown;
        mod.tunic.quickSwap = mod.boots.quickSwap = false;
    }
    const char* const events[] = {
        selection,
        selecting ? nullptr : UpdateShortcut(mod, *player, physical),
        selecting ? nullptr : UpdateOcarina(mod, physical),
        selecting ? nullptr : UpdateEquipGesture(mod, mod.tunic, physical, SDL_BUTTON_DPAD_UP, EQUIP_TYPE_TUNIC),
        selecting ? nullptr : UpdateEquipGesture(mod, mod.boots, physical, SDL_BUTTON_DPAD_DOWN, EQUIP_TYPE_BOOTS),
    };
    const bool menuOpen = mod.itemMenu.open || mod.tunic.quickSwap || mod.boots.quickSwap;
    // Em primeira pessoa ou com a mira pronta, o analógico direito mira (FIRST_PERSON_SETTINGS).
    const bool aiming = (player->stateFlags1 & (PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_READY_TO_FIRE)) != 0;
    const CameraChange cameraChange = UpdateCamera(mod, play, menuOpen || aiming);
    for (const char* event : events) {
        if (event) return Write(write, writer, event);
    }
    const uint16_t current = mod.movement->get_input_current(0);
    const bool jumpDown = hasGamepad
        ? (physical & PhysicalButton(SDL_BUTTON_X_NINTENDO)) != 0
        : mod.keyboardJumpMask && (current & mod.keyboardJumpMask) != 0;
    const bool actionDown = hasGamepad
        ? (physical & PhysicalButton(SDL_BUTTON_A_NINTENDO)) != 0
        : (current & BTN_A) != 0;
    const bool justJumpPressed = jumpDown && !mod.jumpWasDown;
    mod.jumpWasDown = jumpDown;

    const bool grounded = mod.movement->is_player_grounded() != 0;
    const bool rolling = mod.movement->is_player_rolling() != 0;
    if (justJumpPressed && grounded && mod.movement->player_jump(float(JUMP_VELOCITY)) == SHIP_NATIVE_OK) {
        mod.phase = Phase::Airborne;
        mod.observedRoll = false;
        return Write(write, writer, "jump");
    }

    if (mod.phase == Phase::Airborne) {
        if (!grounded) return Write(write, writer, "airborne");
        mod.phase = Phase::Ready;
        return Write(write, writer, "landed");
    }

    if (rolling) {
        const bool firstRollFrame = mod.phase != Phase::Rolling;
        mod.phase = Phase::Rolling;
        mod.observedRoll = true;
        return Write(write, writer, firstRollFrame ? "roll" : "rolling");
    }

    if (mod.phase == Phase::Rolling && mod.observedRoll) {
        mod.observedRoll = false;
        if (actionDown) {
            mod.phase = Phase::Running;
            return Write(write, writer, "run");
        }
        mod.phase = Phase::Ready;
        return Write(write, writer, "idle");
    }

    if (mod.phase == Phase::Running) {
        if (!actionDown) {
            mod.phase = Phase::Ready;
            return Write(write, writer, "idle");
        }
        const int x = mod.movement->get_stick_x(0);
        const int y = mod.movement->get_stick_y(0);
        if ((x * x) + (y * y) >= 20 * 20 && player->linearVelocity < 8.0f) {
            player->linearVelocity = 8.0f;
        }
        return Write(write, writer, "running");
    }

    if (cameraChange == CameraChange::Automatic) return Write(write, writer, "camera-auto");
    if (cameraChange == CameraChange::FreeLook) return Write(write, writer, "camera-free");
    return Write(write, writer, actionDown ? "context" : "idle");
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    const auto* movement = static_cast<const ShipOotMovementV2*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_MOVEMENT_SERVICE, LINKSPAN_OOT_MOVEMENT_VERSION_2,
        sizeof(ShipOotMovementV2)));
    const auto* resources = static_cast<const ShipOotResourcesV2*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION_2,
        sizeof(ShipOotResourcesV2)));
    const auto* registry = static_cast<const ShipOotRegistryV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_REGISTRY_SERVICE, LINKSPAN_OOT_REGISTRY_VERSION,
        sizeof(ShipOotRegistryV1)));
    if (!engine || engine->size < sizeof(ShipOotEngineV1) || !engine->layout_id ||
        std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) ||
        engine->player_size != sizeof(Player) || engine->play_state_size != sizeof(PlayState) ||
        engine->save_context_size != sizeof(SaveContext) || !engine->get_player || !movement ||
        movement->size < sizeof(ShipOotMovementV2) || !movement->layout_id ||
        std::strcmp(movement->layout_id, LINKSPAN_OOT_LAYOUT_ID) ||
        !movement->get_input_current || !movement->get_stick_x || !movement->get_stick_y ||
        !movement->get_right_stick_x || !movement->get_right_stick_y ||
        !movement->has_gamepad || !movement->get_gamepad_buttons ||
        !movement->clear_gamepad_button_bindings || !movement->bind_gamepad_button ||
        !movement->reload_gamepad_mappings || !movement->get_setting_int || !movement->set_setting_int ||
        !movement->is_player_grounded ||
        !movement->is_player_rolling || !movement->player_jump || !movement->player_roll ||
        !movement->get_gamepad_axis || !movement->bind_gamepad_axis || !movement->player_use_item_shortcut ||
        !movement->get_item_button_rect)
        return SHIP_NATIVE_UNSUPPORTED;
    if (!resources || resources->size < sizeof(ShipOotResourcesV2) || !resources->has_file ||
        !resources->read_file || !resources->list_files || !resources->dirty_resources ||
        !resources->unload_resource || !resources->mount_archive || !resources->unmount_archive ||
        !resources->get_game_versions || !resources->read_file_layers) return SHIP_NATIVE_UNSUPPORTED;
    if (!registry || registry->size < sizeof(ShipOotRegistryV1) || !registry->create_space ||
        !registry->find_space || !registry->destroy_space || !registry->register_entry ||
        !registry->unregister_entry || !registry->find_entry_by_name || !registry->find_entry_by_id ||
        !registry->read_entry || !registry->list_entries) return SHIP_NATIVE_UNSUPPORTED;
    auto* mod = new (std::nothrow) Mod{engine, movement, resources, registry};
    if (!mod) return SHIP_NATIVE_FAILURE;
    // Host texture table and camera math, protected by the exact executable
    // fingerprint. No dependency on NEI IDs, its registry, or load order.
    uintptr_t icons = 0;
    uintptr_t cameraGeometry = 0;
    if (runtime->abi_minor < 3 || !runtime->resolve_symbol ||
        runtime->resolve_symbol(runtime->context, "gItemIcons", &icons) != SHIP_NATIVE_OK || !icons ||
        runtime->resolve_symbol(runtime->context, "OLib_Vec3fDiffToVecSphGeo", &cameraGeometry) != SHIP_NATIVE_OK ||
        !cameraGeometry) {
        delete mod;
        return SHIP_NATIVE_UNSUPPORTED;
    }
    mod->itemIcons = reinterpret_cast<const void* const*>(icons);
    mod->cameraGeometry = reinterpret_cast<CameraGeometryFn>(cameraGeometry);
    *instance = mod;
    if (registry->create_space("example/dynamic_movement/actions", 0x80, 0xFF, 1,
                               &mod->registrySpace) != SHIP_NATIVE_OK) {
        delete mod;
        *instance = nullptr;
        return SHIP_NATIVE_FAILURE;
    }
    int32_t assignedId = 0;
    static const char jumpPayload[] = "action=jump";
    static const char sprintPayload[] = "action=sprint";
    if (registry->register_entry(mod->registrySpace, "example/dynamic_movement/jump",
                                 LINKSPAN_OOT_REGISTRY_AUTO_ID,
                                 reinterpret_cast<const uint8_t*>(jumpPayload), sizeof(jumpPayload) - 1,
                                 &mod->jumpEntry, &assignedId) != SHIP_NATIVE_OK ||
        registry->register_entry(mod->registrySpace, "example/dynamic_movement/sprint",
                                 LINKSPAN_OOT_REGISTRY_AUTO_ID,
                                 reinterpret_cast<const uint8_t*>(sprintPayload), sizeof(sprintPayload) - 1,
                                 &mod->sprintEntry, &assignedId) != SHIP_NATIVE_OK) {
        registry->destroy_space(mod->registrySpace);
        delete mod;
        *instance = nullptr;
        return SHIP_NATIVE_FAILURE;
    }
    if (runtime->register_function(runtime->context, "status", Status, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "configure", Configure, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "jump", Jump, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "resource_probe", ResourceProbe, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "layer_probe", LayerProbe, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "layer_runtime_probe", LayerRuntimeProbe, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "resource_runtime_probe", ResourceRuntimeProbe, mod) !=
            SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "registry_probe", RegistryProbe, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "hud_selection", HudSelection, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "hud_quick_swap", HudQuickSwap, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "hud_item_menu", HudItemMenu, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "hud_item_icons", HudItemIcons, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "hud_dpad", HudDpad, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "update", Update, mod) != SHIP_NATIVE_OK) {
        registry->destroy_space(mod->registrySpace);
        delete mod;
        *instance = nullptr;
        return SHIP_NATIVE_FAILURE;
    }
    mod->runtime = runtime;
    ShipNativeHookSpec menuHook{ sizeof(menuHook), LINKSPAN_OOT_HOOK_PLAY_UPDATE, 1,
        sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_BEFORE, 0,
        CaptureItemMenuInput, mod };
    if (!runtime->register_hook || runtime->register_hook(runtime->context, &menuHook, &mod->menuInputHook) != SHIP_NATIVE_OK) {
        registry->destroy_space(mod->registrySpace);
        delete mod;
        *instance = nullptr;
        return SHIP_NATIVE_UNSUPPORTED;
    }
    return SHIP_NATIVE_OK;
}

bool LensOnItemButton(const Mod& mod, const SaveContext& save) {
    const int count = mod.movement->get_setting_int("gEnhancements.DpadEquips", 0) != 0 ? 8 : 4;
    for (int button = 1; button < count; ++button) {
        if (save.equips.buttonItems[button] == ITEM_LENS) return true;
    }
    return false;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* mod = static_cast<Mod*>(instance);
    if (mod && mod->menuInputHook && mod->runtime && mod->runtime->unregister_hook)
        mod->runtime->unregister_hook(mod->runtime->context, mod->menuInputHook);
    if (mod && mod->faceBindingsApplied) mod->movement->reload_gamepad_mappings(0);
    if (mod && mod->freeLookApplied) mod->movement->set_setting_int(FREE_LOOK_SETTING, mod->previousFreeLook);
    if (mod && mod->invertYApplied)
        mod->movement->set_setting_int(FREE_LOOK_INVERT_Y_SETTING, mod->previousInvertY);
    for (size_t index = mod ? mod->firstPersonApplied : 0; index > 0; --index)
        mod->movement->set_setting_int(FIRST_PERSON_SETTINGS[index - 1].name, mod->previousFirstPerson[index - 1]);
    if (mod) RestoreItemHud(*mod);
    if (mod && mod->swordOverShieldApplied) mod->movement->set_setting_int(SWORD_OVER_SHIELD_SETTING, 0);
    if (mod && mod->dpadHudApplied) mod->movement->set_setting_int(DPAD_HUD_SETTING, 0);
    if (mod && mod->iconArchive) mod->resources->unmount_archive(mod->iconArchive);
    if (mod && mod->persistentMasksApplied)
        mod->movement->set_setting_int(PERSISTENT_MASKS_SETTING, mod->previousPersistentMasks);
    // Lente fora dos botões só seguia ligada pelo atalho: desligá-la deixa o
    // Magic_Update encerrar o consumo com o som nativo.
    if (mod) {
        auto* play = static_cast<PlayState*>(mod->engine->get_play_state());
        const auto* save = static_cast<const SaveContext*>(mod->engine->get_save_context());
        if (play && save && play->actorCtx.lensActive && !LensOnItemButton(*mod, *save))
            play->actorCtx.lensActive = false;
    }
    if (mod && mod->registrySpace) mod->registry->destroy_space(mod->registrySpace);
    delete mod;
}
}

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, SHIP_NATIVE_ABI_MINOR, Init, Shutdown
    };
    return &descriptor;
}
