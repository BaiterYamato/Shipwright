#pragma once

#include <cstdint>
#include <thread>

#include "oot_text.h"

namespace ShipLuaHost {

// Tabelas de mensagens do jogo (z_message_OTR.cpp). O binding fica em OotNativeTextGame.cpp;
// os testes usam um falso. Nulo = UNSUPPORTED. Retornos seguem OTRMessage_*: 1 = feito.
struct OotTextBridge {
    int32_t (*set)(int32_t language, uint16_t id, uint8_t typePos, const char* bytes, uint32_t size) = nullptr;
    int32_t (*remove)(int32_t language, uint16_t id) = nullptr;
    int32_t (*clear)(int32_t language) = nullptr;
    int32_t (*reset)(int32_t language) = nullptr;
    // Visita as mensagens em ordem de id; um retorno diferente de 0 do visitante para e é devolvido.
    int32_t (*forEach)(int32_t language,
                       int32_t (*visitor)(void* user, uint16_t id, uint8_t typePos, const char* bytes, uint32_t size),
                       void* user) = nullptr;
};

void SetOotTextBridge(const OotTextBridge& bridge);
void InitializeOotNativeText(std::thread::id ownerThread = std::this_thread::get_id());
const ShipOotTextV1& GetOotNativeTextService();

} // namespace ShipLuaHost
