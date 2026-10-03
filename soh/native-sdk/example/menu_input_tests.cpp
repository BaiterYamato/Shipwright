#include "provider.cpp"
#include <memory>
std::string ProviderAssetsDirectory() { return {}; }
namespace {
PlayState* play;
SaveContext* save;
Player* player;
uint32_t physical;
int8_t leftX, rightX;
void* SHIP_NATIVE_CALL Play() { return play; }
void* SHIP_NATIVE_CALL Save() { return save; }
void* SHIP_NATIVE_CALL PlayerPointer() { return player; }
uint8_t SHIP_NATIVE_CALL HasPad(uint8_t) { return 1; }
uint32_t SHIP_NATIVE_CALL Buttons(uint8_t) { return physical; }
int8_t SHIP_NATIVE_CALL Left(uint8_t) { return leftX; }
int8_t SHIP_NATIVE_CALL Right(uint8_t) { return rightX; }
int16_t SHIP_NATIVE_CALL Axis(uint8_t, uint8_t) { return 0; }
ShipNativeStatus SHIP_NATIVE_CALL Clear(uint8_t, uint16_t) { return SHIP_NATIVE_OK; }
ShipNativeStatus SHIP_NATIVE_CALL Bind(uint8_t, uint16_t, uint8_t, int8_t) { return SHIP_NATIVE_OK; }
int32_t SHIP_NATIVE_CALL Setting(const char*, int32_t fallback) { return fallback; }
void Check(bool condition, const char* label) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
}
int main() {
    LinkSpanMenuInput nav{};
    Check(LinkSpanMenuStep(&nav, 60, 0, 0) == 1, "left stick selects");
    Check(LinkSpanMenuStep(&nav, 60, 0, 0) == 0, "held left stick does not repeat");
    Check(LinkSpanMenuStep(&nav, 60, -60, 0) == -1, "right stick independent of held left");
    Check(LinkSpanMenuStep(&nav, 60, -60, 1) == 1, "D-pad still works with sticks held");
    Check(LinkSpanMenuStep(&nav, 60, -60, 1) == 0, "D-pad hold does not repeat");
    LinkSpanMenuStep(&nav, 0, 0, 0);
    Check(LinkSpanMenuStep(&nav, -60, 60, 0) == 1, "simultaneous sticks produce one deterministic step");
    const auto playOwner = std::make_unique<PlayState>();
    const auto saveOwner = std::make_unique<SaveContext>();
    const auto playerOwner = std::make_unique<Player>();
    play = playOwner.get(); save = saveOwner.get();
    player = playerOwner.get();
    save->equips.buttonItems[1] = ITEM_STICK;
    save->equips.buttonItems[2] = ITEM_NUT;
    save->equips.buttonItems[3] = ITEM_BOMB;
    ShipOotEngineV1 engine{}; engine.get_play_state = Play; engine.get_save_context = Save;
    engine.get_player = PlayerPointer;
    ShipOotMovementV2 movement{};
    movement.get_stick_x = Left; movement.get_right_stick_x = Right;
    movement.get_gamepad_axis = Axis; movement.clear_gamepad_button_bindings = Clear;
    movement.bind_gamepad_axis = Bind;
    movement.get_setting_int = Setting;
    movement.has_gamepad = HasPad; movement.get_gamepad_buttons = Buttons;
    Mod mod{ &engine, &movement, nullptr, nullptr };
    const auto r = PhysicalButton(SDL_BUTTON_R);
    Check(std::string(UpdateItemSelection(mod, r)) == "item-menu", "R opens real provider menu");
    leftX = 60; UpdateItemSelection(mod, r);
    Check(mod.itemMenu.highlight == 2, "left stick moves real hotbar highlight");
    UpdateItemSelection(mod, r);
    Check(mod.itemMenu.highlight == 2, "held left does not skip slot");
    leftX = 0; UpdateItemSelection(mod, r);
    UpdateItemSelection(mod, r | PhysicalButton(SDL_BUTTON_DPAD_RIGHT));
    Check(mod.itemMenu.highlight == 3, "D-pad moves real hotbar highlight");
    Check(std::string(UpdateItemSelection(mod, 0)) == "item-c-right" && mod.selectedItemButton == 3,
          "release R binds selected hotbar slot");
    mod.itemMenu.open = true; mod.selectWasDown = true;
    mod.itemMenu.highlight = 1; mod.itemMenu.navigation = {};
    play->state.input[0].cur.button = play->state.input[0].press.button = BTN_CUP | BTN_DUP;
    play->state.input[0].cur.stick_x = play->state.input[0].rel.stick_x = 60;
    ShipOotPlayHookV1 payload{sizeof(payload), play};
    ShipNativeHookCall call{}; call.payload = &payload;
    physical = r;
    CaptureItemMenuInput(&mod, &call);
    Check(!play->state.input[0].cur.button && !play->state.input[0].press.button &&
          !play->state.input[0].rel.stick_x && mod.menuLeftX == 60,
          "selector captures left stick and suppresses movement/Navi input");
    leftX = rightX = 0;
    UpdateItemSelection(mod, r);
    Check(mod.itemMenu.highlight == 2, "captured stick still navigates after gameplay input is suppressed");
    mod.tunic.waitRelease = true;
    Check(!UpdateEquipGesture(mod, mod.tunic, PhysicalButton(SDL_BUTTON_DPAD_UP), SDL_BUTTON_DPAD_UP, EQUIP_TYPE_TUNIC) &&
          !mod.tunic.quickSwap, "D-pad held after selector cannot open clothes menu");
    Check(!UpdateEquipGesture(mod, mod.tunic, 0, SDL_BUTTON_DPAD_UP, EQUIP_TYPE_TUNIC) &&
          !mod.tunic.waitRelease, "releasing selector navigation cannot change clothes");
    std::puts("Hotbar: both sticks, D-pad, debounce and real slot binding passed.");
}
