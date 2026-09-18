#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <shiplua/native/NativeHooks.h>

#include "oot_hooks.h"

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
    uint64_t roomActors = 0;
};
void SetOotHookLogger(void (*logger)(const std::string& message));
// Cria um registro novo com os pontos do host declarados. Chamar na thread do jogo.
std::shared_ptr<ShipLua::NativeHookRegistry> CreateOotHookRegistry();
ShipLua::NativeHookRegistry* GetOotHookRegistry();
const OotHookPoints& GetOotHookPoints();
void ResetOotHooks();
// Dispara um ponto oot.save.* (observe) se houver hook; na thread do jogo.
void DispatchOotSaveHook(uint64_t point, int32_t slot, int32_t otherSlot);
// Dispara oot.room.actors sobre `entries` (`count` em uso de `capacity`) e devolve a
// contagem final. Payload inválido depois dos hooks (contagem acima da capacidade,
// buffer ou capacidade trocados) mantém `count` e registra no log.
uint32_t DispatchOotRoomActors(void* play, int32_t sceneId, int32_t room, int32_t layer, const char* roomPath,
                               ShipOotActorEntryV2* entries, uint32_t count, uint32_t capacity);
} // namespace ShipLuaHost
