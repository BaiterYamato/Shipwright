// linkspan.oot.anchor: namespaces de save que os mods põem no estado do time do Anchor. Aqui fica só o estado; a
// ligação com o Anchor do SoH está em OotNativeAnchorGame.cpp.
#include "OotNativeAnchor.h"

#include <map>
#include <mutex>

#include "OotNativeSave.h"

namespace ShipLuaHost {
namespace {

constexpr uint32_t kTeamStateVersion = 1;

struct Shared {
    std::string name;
    uint32_t version = 0;
};

struct AnchorState {
    // O export roda também na thread de save (OnSaveFile do Anchor).
    std::mutex mutex;
    std::thread::id ownerThread;
    OotAnchorBridge bridge;
    std::map<uint64_t, Shared> shared;
};

AnchorState& State() {
    static AnchorState state;
    return state;
}

bool OnOwnerThread() {
    const auto& state = State();
    return state.ownerThread != std::thread::id{} && state.ownerThread == std::this_thread::get_id();
}

uint32_t SHIP_NATIVE_CALL Connected() {
    const auto& bridge = State().bridge;
    return OnOwnerThread() && bridge.connected && bridge.connected() ? 1 : 0;
}

ShipNativeStatus SHIP_NATIVE_CALL ShareNamespace(uint64_t handle) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    Shared entry;
    if (!GetOotSaveNamespace(handle, entry.name, entry.version)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    std::lock_guard lock(State().mutex);
    auto& shared = State().shared;
    if (shared.contains(handle)) {
        return SHIP_NATIVE_OK;
    }
    if (shared.size() >= LINKSPAN_OOT_ANCHOR_MAX_SHARED) {
        return SHIP_NATIVE_LIMIT;
    }
    shared.emplace(handle, std::move(entry));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL UnshareNamespace(uint64_t handle) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    std::lock_guard lock(State().mutex);
    return State().shared.erase(handle) ? SHIP_NATIVE_OK : SHIP_NATIVE_FAILURE;
}

const ShipOotAnchorV1 anchorV1{ sizeof(ShipOotAnchorV1), Connected, ShareNamespace, UnshareNamespace };

} // namespace

void SetOotAnchorBridge(const OotAnchorBridge& bridge) {
    State().bridge = bridge;
}

void InitializeOotNativeAnchor(std::thread::id ownerThread) {
    State().ownerThread = ownerThread;
}

void ResetOotNativeAnchor() {
    std::lock_guard lock(State().mutex);
    State().shared.clear();
}

bool OnOotAnchorOwnerThread() {
    return OnOwnerThread();
}

const ShipOotAnchorV1& GetOotNativeAnchorService() {
    return anchorV1;
}

nlohmann::json ExportOotAnchorTeamState() {
    nlohmann::json namespaces = nlohmann::json::object();
    std::lock_guard lock(State().mutex);
    for (const auto& [handle, entry] : State().shared) {
        nlohmann::json data;
        uint32_t version = 0;
        if (ExportOotSaveNamespace(entry.name, data, version)) {
            namespaces[entry.name] = nlohmann::json{ { "version", version }, { "data", std::move(data) } };
        }
    }
    if (namespaces.empty()) {
        return nullptr;
    }
    return nlohmann::json{ { "version", kTeamStateVersion }, { "namespaces", std::move(namespaces) } };
}

OotAnchorImport ImportOotAnchorTeamState(const nlohmann::json& state) {
    OotAnchorImport result;
    if (!state.is_object() || state.value("version", 0u) != kTeamStateVersion || !state.contains("namespaces") ||
        !state["namespaces"].is_object()) {
        return result;
    }
    const auto& namespaces = state["namespaces"];
    std::lock_guard lock(State().mutex);
    for (const auto& [handle, entry] : State().shared) {
        if (!namespaces.contains(entry.name)) {
            continue;
        }
        const auto& incoming = namespaces[entry.name];
        if (!incoming.is_object() || !incoming.contains("data") || incoming.value("version", 0u) != entry.version) {
            result.refused.push_back(entry.name);
            continue;
        }
        if (ReplaceOotSaveNamespace(entry.name, entry.version, incoming["data"])) {
            result.replaced.push_back(entry.name);
        }
    }
    return result;
}

} // namespace ShipLuaHost
