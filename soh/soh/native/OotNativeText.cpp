#include "OotNativeText.h"

namespace ShipLuaHost {
namespace {

struct TextState {
    std::thread::id ownerThread;
    OotTextBridge bridge;
};

TextState& State() {
    static TextState state;
    return state;
}

bool OnOwnerThread() {
    const auto& state = State();
    return state.ownerThread != std::thread::id{} && state.ownerThread == std::this_thread::get_id();
}

bool ValidLanguage(uint32_t language) {
    return language < LINKSPAN_OOT_TEXT_LANGUAGES;
}

ShipNativeStatus SHIP_NATIVE_CALL SetMessage(uint32_t language, uint32_t id, uint8_t boxType, uint8_t boxPos,
                                             const uint8_t* bytes, uint32_t length) {
    // O terminador acrescentado conta no limite.
    if (!OnOwnerThread() || !ValidLanguage(language) || id > LINKSPAN_OOT_TEXT_MAX_ID || boxType > 15 || boxPos > 15 ||
        (!bytes && length) || length >= LINKSPAN_OOT_TEXT_MAX_MESSAGE) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto set = State().bridge.set;
    if (!set) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto typePos = static_cast<uint8_t>((boxType << 4) | boxPos);
    return set(static_cast<int32_t>(language), static_cast<uint16_t>(id), typePos,
               reinterpret_cast<const char*>(bytes), length)
               ? SHIP_NATIVE_OK
               : SHIP_NATIVE_FAILURE;
}

ShipNativeStatus SHIP_NATIVE_CALL RemoveMessage(uint32_t language, uint32_t id) {
    if (!OnOwnerThread() || !ValidLanguage(language) || id > LINKSPAN_OOT_TEXT_MAX_ID) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto remove = State().bridge.remove;
    if (!remove) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    return remove(static_cast<int32_t>(language), static_cast<uint16_t>(id)) ? SHIP_NATIVE_OK : SHIP_NATIVE_FAILURE;
}

ShipNativeStatus SHIP_NATIVE_CALL ClearLanguage(uint32_t language) {
    if (!OnOwnerThread() || !ValidLanguage(language)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto clear = State().bridge.clear;
    if (!clear) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    return clear(static_cast<int32_t>(language)) ? SHIP_NATIVE_OK : SHIP_NATIVE_FAILURE;
}

ShipNativeStatus SHIP_NATIVE_CALL ResetLanguage(uint32_t language) {
    if (!OnOwnerThread() || !ValidLanguage(language)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto reset = State().bridge.reset;
    if (!reset) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    return reset(static_cast<int32_t>(language)) ? SHIP_NATIVE_OK : SHIP_NATIVE_FAILURE;
}

struct ListCall {
    ShipOotMessageFn callback;
    void* user;
    ShipNativeStatus status;
};

int32_t Visit(void* user, uint16_t id, uint8_t typePos, const char* bytes, uint32_t size) {
    auto& call = *static_cast<ListCall*>(user);
    call.status = call.callback(call.user, id, static_cast<uint8_t>(typePos >> 4), static_cast<uint8_t>(typePos & 0xF),
                                reinterpret_cast<const uint8_t*>(bytes), size);
    return call.status == SHIP_NATIVE_OK ? 0 : 1;
}

ShipNativeStatus SHIP_NATIVE_CALL ListMessages(uint32_t language, ShipOotMessageFn callback, void* user) {
    if (!OnOwnerThread() || !ValidLanguage(language) || !callback) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto forEach = State().bridge.forEach;
    if (!forEach) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    ListCall call{ callback, user, SHIP_NATIVE_OK };
    forEach(static_cast<int32_t>(language), Visit, &call);
    return call.status;
}

const ShipOotTextV1 textV1{ sizeof(ShipOotTextV1), SetMessage, RemoveMessage, ClearLanguage, ResetLanguage,
                            ListMessages };

} // namespace

void SetOotTextBridge(const OotTextBridge& bridge) {
    State().bridge = bridge;
}

void InitializeOotNativeText(std::thread::id ownerThread) {
    State().ownerThread = ownerThread;
}

const ShipOotTextV1& GetOotNativeTextService() {
    return textV1;
}

} // namespace ShipLuaHost
