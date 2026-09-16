#include "OotNativeSave.h"

#include <cstring>
#include <map>
#include <mutex>
#include <string_view>

namespace ShipLuaHost {
namespace {

constexpr std::string_view kHostPrefix = "linkspan.";

struct Block {
    nlohmann::json data;
    uint32_t storedVersion = 0;
    bool hasData = false;
    bool required = false;
};

struct Opened {
    std::string name;
    uint32_t version = 0;
};

struct SaveState {
    std::mutex mutex;
    std::thread::id ownerThread;
    uint64_t nextHandle = 1;
    std::map<uint64_t, Opened> handles;
    std::map<std::string, uint64_t, std::less<>> handlesByName;
    std::map<std::string, Block, std::less<>> blocks;
    // Estado de antes do begin, por namespace com transação aberta.
    std::map<std::string, Block, std::less<>> transactions;
    int32_t slot = -1;
};

SaveState& State() {
    static SaveState state;
    return state;
}

bool OnOwnerThread() {
    return State().ownerThread == std::this_thread::get_id();
}

bool ValidName(std::string_view name) {
    if (name.empty() || name.size() > LINKSPAN_OOT_SAVE_MAX_NAME || name.front() == '.' || name.back() == '.' ||
        name.find('.') == std::string_view::npos) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool IsHostName(std::string_view name) {
    return name.substr(0, kHostPrefix.size()) == kHostPrefix;
}

// Handles só mudam na thread dona; a leitura dispensa o mutex nela.
const Opened* Find(uint64_t handle) {
    auto& state = State();
    const auto found = state.handles.find(handle);
    return found == state.handles.end() ? nullptr : &found->second;
}

ShipNativeStatus SHIP_NATIVE_CALL OpenNamespace(const char* name, uint32_t version, uint64_t* handle) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_FAILURE;
    }
    if (!name || !ValidName(name) || IsHostName(name) || !version || !handle) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    try {
        const auto existing = state.handlesByName.find(name);
        if (existing != state.handlesByName.end()) {
            if (state.handles.at(existing->second).version != version) {
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
            *handle = existing->second;
            return SHIP_NATIVE_OK;
        }
        const auto id = state.nextHandle++;
        state.handles.emplace(id, Opened{name, version});
        state.handlesByName.emplace(name, id);
        *handle = id;
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL Read(uint64_t handle, char* output, uint32_t capacity, uint32_t* outputSize) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_FAILURE;
    }
    const auto* opened = Find(handle);
    if (!opened || !outputSize || (!output && capacity)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    *outputSize = 0;
    const auto block = state.blocks.find(opened->name);
    if (block == state.blocks.end() || !block->second.hasData) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    try {
        const auto text = block->second.data.dump();
        if (text.size() > LINKSPAN_OOT_SAVE_MAX_BYTES) {
            return SHIP_NATIVE_LIMIT;
        }
        *outputSize = static_cast<uint32_t>(text.size());
        if (!output) {
            return SHIP_NATIVE_OK;
        }
        if (capacity < text.size()) {
            return SHIP_NATIVE_LIMIT;
        }
        std::memcpy(output, text.data(), text.size());
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL Write(uint64_t handle, const char* json, uint32_t length) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_FAILURE;
    }
    const auto* opened = Find(handle);
    if (!opened || !json || !length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (length > LINKSPAN_OOT_SAVE_MAX_BYTES) {
        return SHIP_NATIVE_LIMIT;
    }
    try {
        auto parsed = nlohmann::json::parse(json, json + length, nullptr, false);
        if (parsed.is_discarded()) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        auto& state = State();
        std::lock_guard lock(state.mutex);
        auto& block = state.blocks[opened->name];
        block.data = std::move(parsed);
        block.storedVersion = opened->version;
        block.hasData = true;
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL Erase(uint64_t handle) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_FAILURE;
    }
    const auto* opened = Find(handle);
    if (!opened) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto block = state.blocks.find(opened->name);
    if (block != state.blocks.end()) {
        block->second.data = nullptr;
        block->second.hasData = false;
        block->second.storedVersion = 0;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL GetStoredVersion(uint64_t handle, uint32_t* version) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_FAILURE;
    }
    const auto* opened = Find(handle);
    if (!opened || !version) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto block = state.blocks.find(opened->name);
    *version = block != state.blocks.end() && block->second.hasData ? block->second.storedVersion : 0;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL SetRequired(uint64_t handle, uint8_t required) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_FAILURE;
    }
    const auto* opened = Find(handle);
    if (!opened) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    try {
        state.blocks[opened->name].required = required != 0;
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL Begin(uint64_t handle) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_FAILURE;
    }
    const auto* opened = Find(handle);
    if (!opened) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (state.transactions.find(opened->name) != state.transactions.end()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        const auto block = state.blocks.find(opened->name);
        state.transactions.emplace(opened->name, block == state.blocks.end() ? Block{} : block->second);
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus EndTransaction(uint64_t handle, bool keep) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_FAILURE;
    }
    const auto* opened = Find(handle);
    if (!opened) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto transaction = state.transactions.find(opened->name);
    if (transaction == state.transactions.end()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        if (!keep) {
            state.blocks[opened->name] = std::move(transaction->second);
        }
        state.transactions.erase(transaction);
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL Commit(uint64_t handle) {
    return EndTransaction(handle, true);
}

ShipNativeStatus SHIP_NATIVE_CALL Rollback(uint64_t handle) {
    return EndTransaction(handle, false);
}

int32_t SHIP_NATIVE_CALL GetSlot() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    return state.slot;
}

const ShipOotSaveV1 kService{sizeof(ShipOotSaveV1), OpenNamespace, Read,   Write,    Erase, GetStoredVersion,
                             SetRequired,           Begin,         Commit, Rollback, GetSlot};

} // namespace

void InitializeOotNativeSave(std::thread::id ownerThread) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.ownerThread = ownerThread;
    // Blocos pertencem ao save carregado e sobrevivem à recarga dos mods; só handles e transações recomeçam.
    state.handles.clear();
    state.handlesByName.clear();
    state.transactions.clear();
}

void ResetOotNativeSave() {
    InitializeOotNativeSave(std::this_thread::get_id());
}

const ShipOotSaveV1& GetOotNativeSaveService() {
    return kService;
}

void ClearOotSaveData() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.blocks.clear();
    state.transactions.clear();
    state.slot = -1;
}

void SetOotSaveSlot(int32_t slot) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.slot = slot;
}

void ImportOotSaveSection(const nlohmann::json& section) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.blocks.clear();
    state.transactions.clear();
    if (!section.is_object() || !section.contains("namespaces") || !section["namespaces"].is_object()) {
        return;
    }
    for (const auto& [name, entry] : section["namespaces"].items()) {
        if (!ValidName(name) || !entry.is_object()) {
            continue;
        }
        Block block;
        if (entry.contains("version") && entry["version"].is_number_unsigned()) {
            block.storedVersion = entry["version"].get<uint32_t>();
        }
        if (entry.contains("required") && entry["required"].is_boolean()) {
            block.required = entry["required"].get<bool>();
        }
        if (entry.contains("data")) {
            block.data = entry["data"];
            block.hasData = true;
        } else {
            block.storedVersion = 0;
        }
        state.blocks.emplace(name, std::move(block));
    }
}

nlohmann::json ExportOotSaveSection() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    nlohmann::json namespaces = nlohmann::json::object();
    for (const auto& [name, current] : state.blocks) {
        const auto transaction = state.transactions.find(name);
        const auto& block = transaction == state.transactions.end() ? current : transaction->second;
        if (!block.hasData && !block.required) {
            continue;
        }
        nlohmann::json entry = nlohmann::json::object();
        entry["version"] = block.storedVersion;
        entry["required"] = block.required;
        if (block.hasData) {
            entry["data"] = block.data;
        }
        namespaces[name] = std::move(entry);
    }
    nlohmann::json section = nlohmann::json::object();
    section["namespaces"] = std::move(namespaces);
    return section;
}

std::vector<std::string> MissingRequiredOotNamespaces() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    std::vector<std::string> missing;
    for (const auto& [name, block] : state.blocks) {
        if (block.required && !IsHostName(name) && state.handlesByName.find(name) == state.handlesByName.end()) {
            missing.push_back(name);
        }
    }
    return missing;
}

void SetOotHostSaveBlock(const std::string& name, uint32_t version, nlohmann::json data) {
    if (!IsHostName(name) || !ValidName(name)) {
        return;
    }
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& block = state.blocks[name];
    block.data = std::move(data);
    block.storedVersion = version;
    block.hasData = true;
}

bool GetOotHostSaveBlock(const std::string& name, nlohmann::json& data, uint32_t& version) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto block = state.blocks.find(name);
    if (!IsHostName(name) || block == state.blocks.end() || !block->second.hasData) {
        return false;
    }
    data = block->second.data;
    version = block->second.storedVersion;
    return true;
}

} // namespace ShipLuaHost
