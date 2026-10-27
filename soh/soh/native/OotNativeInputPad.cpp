// RFC 0032: ponte C do hook oot.input.pad, chamada pelo PadMgr_HandleRetraceMsg (padmgr.c). Fica fora do
// OotNativeHooksGame.cpp para o teste compilar sem o jogo (soh/native-sdk/tests/InputPadTests.cpp).
#include "OotNativeHooks.h"

#include <algorithm>

#include "oot_hooks.h"
#include "libultraship/libultra/controller.h"

struct GameState;
extern "C" GameState* gGameState;

// Sem assinante não monta payload nem toca nos pads, com ou sem mods carregados.
extern "C" void LinkSpan_DispatchInputPad(OSContPad* pads, int count) {
    static uint32_t frame = 0;
    ++frame;
    const uint64_t point = ShipLuaHost::GetOotHookPoints().inputPad;
    if (!pads || count <= 0 || !ShipLuaHost::HasOotHooks(point)) {
        return;
    }
    ShipOotInputPadHookV1 payload{ sizeof(payload) };
    payload.frame = frame;
    payload.game_state = gGameState;
    payload.port_count = static_cast<uint8_t>(std::min(count, 4));
    for (int i = 0; i < payload.port_count; ++i) {
        auto& pad = payload.pads[i];
        pad.button = pads[i].button;
        pad.stick_x = pads[i].stick_x;
        pad.stick_y = pads[i].stick_y;
        pad.right_stick_x = pads[i].right_stick_x;
        pad.right_stick_y = pads[i].right_stick_y;
        pad.err_no = pads[i].err_no;
    }
    ShipLuaHost::GetOotHookRegistry()->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
    for (int i = 0; i < payload.port_count; ++i) {
        const auto& pad = payload.pads[i];
        pads[i].button = pad.button;
        pads[i].stick_x = pad.stick_x;
        pads[i].stick_y = pad.stick_y;
        pads[i].right_stick_x = pad.right_stick_x;
        pads[i].right_stick_y = pad.right_stick_y;
    }
}
