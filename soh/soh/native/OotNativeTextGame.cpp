// Liga linkspan.oot.text (OotNativeText.cpp) às tabelas de mensagens do jogo (z_message_OTR.cpp).
#include "OotNativeText.h"

#include "soh/z_message_OTR.h"

namespace {

[[maybe_unused]] const bool kTextBound = [] {
    ShipLuaHost::OotTextBridge bridge;
    bridge.set = [](int32_t language, uint16_t id, uint8_t typePos, const char* bytes, uint32_t size) -> int32_t {
        return OTRMessage_Set(language, id, typePos, bytes, size);
    };
    bridge.remove = [](int32_t language, uint16_t id) -> int32_t { return OTRMessage_Remove(language, id); };
    bridge.clear = [](int32_t language) -> int32_t { return OTRMessage_Clear(language); };
    bridge.reset = [](int32_t language) -> int32_t { return OTRMessage_Reset(language); };
    bridge.forEach = [](int32_t language,
                        int32_t (*visitor)(void*, uint16_t, uint8_t, const char*, uint32_t), void* user) -> int32_t {
        return OTRMessage_ForEach(language, reinterpret_cast<OTRMessageVisitor>(visitor), user);
    };
    ShipLuaHost::SetOotTextBridge(bridge);
    return true;
}();

} // namespace
