// RFC 0032: ponte do hook oot.input.pad (soh/soh/native/OotNativeInputPad.cpp) contra um registro real, sem o jogo.
#include "OotNativeHooks.h"

#include <cstring>
#include <iostream>
#include <memory>

#include "libultraship/libultra/controller.h"

extern "C" void LinkSpan_DispatchInputPad(OSContPad* pads, int count);

struct GameState {
    int unused;
};
static GameState gFakeState{};
extern "C" GameState* gGameState = &gFakeState;

namespace ShipLuaHost {
namespace {
std::shared_ptr<ShipLua::NativeHookRegistry> gRegistry;
OotHookPoints gPoints;
} // namespace
ShipLua::NativeHookRegistry* GetOotHookRegistry() {
    return gRegistry.get();
}
const OotHookPoints& GetOotHookPoints() {
    return gPoints;
}
bool HasOotHooks(uint64_t point) {
    return gRegistry && point && gRegistry->HasHooks(point);
}
} // namespace ShipLuaHost

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Seen {
    uint32_t frame = 0;
    void* gameState = nullptr;
    uint8_t ports = 0;
};

ShipNativeStatus SHIP_NATIVE_CALL PressA(void* user, const ShipNativeHookCall* call) {
    auto* payload = static_cast<ShipOotInputPadHookV1*>(call->payload);
    auto* seen = static_cast<Seen*>(user);
    seen->frame = payload->frame;
    seen->gameState = payload->game_state;
    seen->ports = payload->port_count;
    payload->pads[0].button |= 0x8000; // BTN_A
    payload->pads[0].stick_x = 64;
    payload->pads[0].err_no = 7; // só leitura: o host não copia de volta
    return SHIP_NATIVE_OK;
}

} // namespace

int main() {
    using namespace ShipLuaHost;
    gRegistry =
        std::make_shared<ShipLua::NativeHookRegistry>([](const std::string& message) { std::cerr << message << '\n'; });
    const ShipNativeHookPointSpec point{ sizeof(point), LINKSPAN_OOT_HOOK_INPUT_PAD,
                                         LINKSPAN_OOT_HOOK_INPUT_PAD_VERSION, sizeof(ShipOotInputPadHookV1),
                                         SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_TRANSFORM };
    Check(gRegistry->DeclarePoint("", point, &gPoints.inputPad) == SHIP_NATIVE_OK, "declara oot.input.pad");

    OSContPad pads[4] = {};
    pads[1].button = 0x0010;
    pads[1].stick_y = -20;
    OSContPad before[4];
    std::memcpy(before, pads, sizeof(pads));
    LinkSpan_DispatchInputPad(pads, 4);
    Check(std::memcmp(before, pads, sizeof(pads)) == 0, "sem assinante os pads saem idênticos");

    Seen seen;
    const ShipNativeHookSpec hook{ sizeof(hook),
                                   LINKSPAN_OOT_HOOK_INPUT_PAD,
                                   LINKSPAN_OOT_HOOK_INPUT_PAD_VERSION,
                                   sizeof(ShipOotInputPadHookV1),
                                   SHIP_NATIVE_HOOK_TRANSFORM,
                                   0,
                                   0,
                                   PressA,
                                   &seen };
    uint64_t handle = 0;
    Check(gRegistry->Register("test.input", hook, &handle) == SHIP_NATIVE_OK, "registra o TRANSFORM");
    LinkSpan_DispatchInputPad(pads, 4);
    Check((pads[0].button & 0x8000) != 0, "o botão do hook chega ao pad");
    Check(pads[0].stick_x == 64, "o stick do hook chega ao pad");
    Check(pads[0].err_no == 0, "err_no não volta para o pad");
    Check(pads[1].button == 0x0010 && pads[1].stick_y == -20, "porta sem mudança fica igual");
    Check(seen.frame == 2, "frame cresce a cada leitura, com ou sem assinante");
    Check(seen.gameState == &gFakeState, "payload leva o GameState atual");
    Check(seen.ports == 4, "port_count limitado às portas lidas");

    LinkSpan_DispatchInputPad(pads, 2);
    Check(seen.frame == 3 && seen.ports == 2, "port_count segue o count do host");
    LinkSpan_DispatchInputPad(nullptr, 4);
    Check(seen.frame == 3, "pads nulos não despacham");

    Check(gRegistry->Unregister("test.input", handle) == SHIP_NATIVE_OK, "remove o hook");
    if (failures == 0)
        std::cout << "oot_native_input_pad_tests: ok\n";
    return failures == 0 ? 0 : 1;
}
