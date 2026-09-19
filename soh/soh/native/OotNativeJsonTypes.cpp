#include "OotNativeJsonTypes.h"

#include <cstring>
#include <limits>
#include <map>
#include <string_view>

namespace ShipLuaHost {
namespace {

// Uma faixa maior viraria centenas de fábricas no loader para nada.
constexpr uint32_t kMaxVersionSpan = 64;

struct Registration {
    std::string type;
    uint32_t minVersion = 0;
    uint32_t maxVersion = 0;
    ShipOotJsonTranscodeFn transcode = nullptr;
    void* user = nullptr;
};

struct JsonTypesState {
    std::thread::id ownerThread;
    OotJsonTypesBridge bridge;
    uint64_t nextHandle = 1;
    std::map<uint64_t, Registration> registrations;
    std::map<std::string, uint64_t, std::less<>> byType;
};

JsonTypesState& State() {
    static JsonTypesState state;
    return state;
}

bool OnOwnerThread() {
    const auto& state = State();
    return state.ownerThread != std::thread::id{} && state.ownerThread == std::this_thread::get_id();
}

bool ValidType(const char* text) {
    if (!text) {
        return false;
    }
    const std::string_view type(text, strnlen(text, LINKSPAN_OOT_RESOURCES_MAX_JSON_TYPE + 1));
    if (type.empty() || type.size() > LINKSPAN_OOT_RESOURCES_MAX_JSON_TYPE || type.front() == '/' ||
        type.back() == '/') {
        return false;
    }
    for (const char c : type) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-' || c == '/';
        if (!ok) {
            return false;
        }
    }
    return true;
}

ShipNativeStatus SHIP_NATIVE_CALL AppendXml(void* writer, const char* bytes, uint32_t length) {
    if (!bytes && length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        static_cast<std::string*>(writer)->append(bytes ? bytes : "", length);
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

} // namespace

void SetOotJsonTypesBridge(const OotJsonTypesBridge& bridge) {
    State().bridge = bridge;
}

void InitializeOotNativeJsonTypes(std::thread::id ownerThread) {
    auto& state = State();
    state.registrations.clear();
    state.byType.clear();
    state.ownerThread = ownerThread;
}

ShipNativeStatus SHIP_NATIVE_CALL RegisterOotJsonType(const ShipOotJsonTypeSpecV1* spec, uint64_t* handle) {
    if (!OnOwnerThread() || !spec || spec->size < sizeof(ShipOotJsonTypeSpecV1) || !handle ||
        !ValidType(spec->type) || !spec->transcode || spec->min_version > spec->max_version ||
        spec->max_version - spec->min_version >= kMaxVersionSpan ||
        spec->max_version > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *handle = 0;
    auto& state = State();
    try {
        std::string type(spec->type);
        if (state.byType.contains(type)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        if (!state.bridge.declareType) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        if (!state.bridge.declareType(type, spec->min_version, spec->max_version)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        const uint64_t id = state.nextHandle++;
        state.registrations.emplace(id, Registration{ type, spec->min_version, spec->max_version, spec->transcode,
                                                      spec->user });
        state.byType.emplace(std::move(type), id);
        *handle = id;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL UnregisterOotJsonType(uint64_t handle) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    const auto found = state.registrations.find(handle);
    if (found == state.registrations.end()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    state.byType.erase(found->second.type);
    state.registrations.erase(found);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus TranscodeOotJson(const std::string& type, const std::string& path, uint32_t version,
                                  std::string& xml) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& state = State();
    const auto owner = state.byType.find(type);
    if (owner == state.byType.end()) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const Registration& registration = state.registrations.at(owner->second);
    if (version < registration.minVersion || version > registration.maxVersion) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    // O transcodificador pode desregistrar o próprio tipo; a cópia mantém a chamada válida.
    const auto transcode = registration.transcode;
    void* user = registration.user;
    xml.clear();
    return transcode(user, path.c_str(), version, AppendXml, &xml);
}

} // namespace ShipLuaHost
