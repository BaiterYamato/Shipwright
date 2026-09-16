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
    uint64_t saveLoaded = 0;
    uint64_t saveSaving = 0;
    uint64_t saveDeleted = 0;
    uint64_t saveCopied = 0;
    uint64_t playerLimbDraw = 0;
};
void SetOotHookLogger(void (*logger)(const std::string& message));
// Cria um registro novo com os pontos do host declarados. Chamar na thread do jogo.
std::shared_ptr<ShipLua::NativeHookRegistry> CreateOotHookRegistry();
ShipLua::NativeHookRegistry* GetOotHookRegistry();
const OotHookPoints& GetOotHookPoints();
void ResetOotHooks();
// Dispara um ponto oot.save.* (observe) se houver hook; na thread do jogo.
void DispatchOotSaveHook(uint64_t point, int32_t slot, int32_t otherSlot);
} // namespace ShipLuaHost
