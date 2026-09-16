#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <shiplua/native/NativeHooks.h>

namespace ShipLuaHost {
struct OotHookPoints {
    uint64_t playUpdate = 0;
    uint64_t actorUpdate = 0;
    uint64_t actorDraw = 0;
};
void SetOotHookLogger(void (*logger)(const std::string& message));
// Cria um registro novo com os pontos do host declarados. Chamar na thread do jogo.
std::shared_ptr<ShipLua::NativeHookRegistry> CreateOotHookRegistry();
ShipLua::NativeHookRegistry* GetOotHookRegistry();
const OotHookPoints& GetOotHookPoints();
void ResetOotHooks();
} // namespace ShipLuaHost
