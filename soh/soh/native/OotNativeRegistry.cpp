#include "OotNativeRegistry.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ShipLuaHost {
namespace {

struct Entry {
    uint64_t handle = 0;
    int32_t id = 0;
    std::string name;
    std::vector<uint8_t> payload;
};

struct Space {
    uint64_t handle = 0;
    std::string name;
    int32_t firstId = 0;
    int32_t lastId = 0;
    uint32_t stride = 1;
    std::map<int32_t, Entry> entries;
    std::map<std::string, int32_t, std::less<>> idsByName;
};

struct RegistryState {
    std::thread::id ownerThread;
    uint64_t nextHandle = 1;
    uint32_t entryCount = 0;
    std::map<uint64_t, Space> spaces;
    std::map<std::string, uint64_t, std::less<>> spacesByName;
    std::unordered_map<uint64_t, std::pair<uint64_t, int32_t>> entryLocations;
};

RegistryState& State() {
    static RegistryState state;
    return state;
}

bool OnOwnerThread() {
    const auto& state = State();
    return state.ownerThread != std::thread::id{} && std::this_thread::get_id() == state.ownerThread;
}

bool ValidName(const char* name) {
    if (!name || !*name) {
        return false;
    }
    const size_t length = std::strlen(name);
    return length <= LINKSPAN_OOT_REGISTRY_MAX_NAME;
}

uint64_t AllocateHandle(RegistryState& state) {
    if (state.nextHandle == 0 || state.nextHandle == std::numeric_limits<uint64_t>::max()) {
        return 0;
    }
    return state.nextHandle++;
}

Space* FindSpace(uint64_t handle) {
    auto& spaces = State().spaces;
    const auto found = spaces.find(handle);
    return found == spaces.end() ? nullptr : &found->second;
}

Entry* FindEntry(uint64_t handle) {
    auto& state = State();
    const auto location = state.entryLocations.find(handle);
    if (location == state.entryLocations.end()) {
        return nullptr;
    }
    auto* space = FindSpace(location->second.first);
    if (!space) {
        return nullptr;
    }
    const auto entry = space->entries.find(location->second.second);
    return entry == space->entries.end() ? nullptr : &entry->second;
}

ShipNativeStatus SHIP_NATIVE_CALL CreateSpace(const char* name, int32_t firstId, int32_t lastId, uint32_t stride,
                                              uint64_t* spaceHandle) {
    if (!OnOwnerThread() || !ValidName(name) || !spaceHandle || firstId == LINKSPAN_OOT_REGISTRY_AUTO_ID ||
        firstId > lastId || stride == 0) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *spaceHandle = 0;
    try {
        auto& state = State();
        if (state.spaces.size() >= LINKSPAN_OOT_REGISTRY_MAX_SPACES) {
            return SHIP_NATIVE_LIMIT;
        }
        if (state.spacesByName.find(name) != state.spacesByName.end()) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        const uint64_t handle = AllocateHandle(state);
        if (!handle) {
            return SHIP_NATIVE_LIMIT;
        }
        Space space;
        space.handle = handle;
        space.name = name;
        space.firstId = firstId;
        space.lastId = lastId;
        space.stride = stride;
        const auto named = state.spacesByName.emplace(name, handle);
        if (!named.second) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        try {
            const auto inserted = state.spaces.emplace(handle, std::move(space));
            if (!inserted.second) {
                state.spacesByName.erase(named.first);
                return SHIP_NATIVE_FAILURE;
            }
        } catch (...) {
            state.spacesByName.erase(named.first);
            throw;
        }
        *spaceHandle = handle;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL FindSpaceByName(const char* name, uint64_t* spaceHandle) {
    if (!OnOwnerThread() || !ValidName(name) || !spaceHandle) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *spaceHandle = 0;
    try {
        const auto found = State().spacesByName.find(name);
        if (found == State().spacesByName.end()) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        *spaceHandle = found->second;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL DestroySpace(uint64_t spaceHandle) {
    if (!OnOwnerThread() || !spaceHandle) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        auto& state = State();
        const auto found = state.spaces.find(spaceHandle);
        if (found == state.spaces.end()) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        for (const auto& [id, entry] : found->second.entries) {
            (void)id;
            state.entryLocations.erase(entry.handle);
        }
        state.entryCount -= static_cast<uint32_t>(found->second.entries.size());
        state.spacesByName.erase(found->second.name);
        state.spaces.erase(found);
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL RegisterEntry(uint64_t spaceHandle, const char* name, int32_t requestedId,
                                                const uint8_t* payload, uint32_t payloadSize, uint64_t* entryHandle,
                                                int32_t* assignedId) {
    if (!OnOwnerThread() || !spaceHandle || !ValidName(name) || (!payload && payloadSize) ||
        payloadSize > SHIP_NATIVE_MAX_BYTES || !entryHandle || !assignedId) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *entryHandle = 0;
    *assignedId = LINKSPAN_OOT_REGISTRY_AUTO_ID;
    try {
        auto& state = State();
        auto* space = FindSpace(spaceHandle);
        if (!space) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        if (state.entryCount >= LINKSPAN_OOT_REGISTRY_MAX_ENTRIES) {
            return SHIP_NATIVE_LIMIT;
        }
        if (space->idsByName.find(name) != space->idsByName.end()) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }

        int32_t id = requestedId;
        if (id == LINKSPAN_OOT_REGISTRY_AUTO_ID) {
            bool foundFree = false;
            for (int64_t candidate = space->firstId; candidate <= space->lastId;
                 candidate += static_cast<int64_t>(space->stride)) {
                id = static_cast<int32_t>(candidate);
                if (space->entries.find(id) == space->entries.end()) {
                    foundFree = true;
                    break;
                }
                if (candidate > static_cast<int64_t>(space->lastId) - space->stride) {
                    break;
                }
            }
            if (!foundFree) {
                return SHIP_NATIVE_LIMIT;
            }
        } else {
            const int64_t offset = static_cast<int64_t>(id) - space->firstId;
            if (id < space->firstId || id > space->lastId || offset % space->stride != 0 ||
                space->entries.find(id) != space->entries.end()) {
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
        }

        const uint64_t handle = AllocateHandle(state);
        if (!handle) {
            return SHIP_NATIVE_LIMIT;
        }
        Entry entry;
        entry.handle = handle;
        entry.id = id;
        entry.name = name;
        if (payloadSize) {
            entry.payload.assign(payload, payload + payloadSize);
        }
        const auto named = space->idsByName.emplace(name, id);
        if (!named.second) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        std::map<int32_t, Entry>::iterator insertedEntry;
        try {
            const auto inserted = space->entries.emplace(id, std::move(entry));
            if (!inserted.second) {
                space->idsByName.erase(named.first);
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
            insertedEntry = inserted.first;
        } catch (...) {
            space->idsByName.erase(named.first);
            throw;
        }
        try {
            const auto located = state.entryLocations.emplace(handle, std::make_pair(spaceHandle, id));
            if (!located.second) {
                space->entries.erase(insertedEntry);
                space->idsByName.erase(named.first);
                return SHIP_NATIVE_FAILURE;
            }
        } catch (...) {
            space->entries.erase(insertedEntry);
            space->idsByName.erase(named.first);
            throw;
        }
        ++state.entryCount;
        *entryHandle = handle;
        *assignedId = id;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL UnregisterEntry(uint64_t entryHandle) {
    if (!OnOwnerThread() || !entryHandle) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        auto& state = State();
        const auto location = state.entryLocations.find(entryHandle);
        if (location == state.entryLocations.end()) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        auto* space = FindSpace(location->second.first);
        if (!space) {
            return SHIP_NATIVE_FAILURE;
        }
        const auto entry = space->entries.find(location->second.second);
        if (entry == space->entries.end()) {
            return SHIP_NATIVE_FAILURE;
        }
        space->idsByName.erase(entry->second.name);
        space->entries.erase(entry);
        state.entryLocations.erase(location);
        --state.entryCount;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL FindEntryByName(uint64_t spaceHandle, const char* name, uint64_t* entryHandle,
                                                  int32_t* id) {
    if (!OnOwnerThread() || !spaceHandle || !ValidName(name) || !entryHandle || !id) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *entryHandle = 0;
    *id = LINKSPAN_OOT_REGISTRY_AUTO_ID;
    try {
        auto* space = FindSpace(spaceHandle);
        if (!space) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        const auto named = space->idsByName.find(name);
        if (named == space->idsByName.end()) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        const auto entry = space->entries.find(named->second);
        if (entry == space->entries.end()) {
            return SHIP_NATIVE_FAILURE;
        }
        *entryHandle = entry->second.handle;
        *id = entry->second.id;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL FindEntryById(uint64_t spaceHandle, int32_t id, uint64_t* entryHandle) {
    if (!OnOwnerThread() || !spaceHandle || !entryHandle || id == LINKSPAN_OOT_REGISTRY_AUTO_ID) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *entryHandle = 0;
    try {
        auto* space = FindSpace(spaceHandle);
        if (!space) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        const auto entry = space->entries.find(id);
        if (entry == space->entries.end()) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        *entryHandle = entry->second.handle;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL ReadEntry(uint64_t entryHandle, char* nameOutput, uint32_t nameCapacity,
                                            uint32_t* nameSize, uint8_t* payloadOutput, uint32_t payloadCapacity,
                                            uint32_t* payloadSize, int32_t* id) {
    if (!OnOwnerThread() || !entryHandle || !nameSize || !payloadSize || !id || (!nameOutput && nameCapacity) ||
        (!payloadOutput && payloadCapacity)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *nameSize = 0;
    *payloadSize = 0;
    *id = LINKSPAN_OOT_REGISTRY_AUTO_ID;
    try {
        const auto* entry = FindEntry(entryHandle);
        if (!entry) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        *nameSize = static_cast<uint32_t>(entry->name.size());
        *payloadSize = static_cast<uint32_t>(entry->payload.size());
        *id = entry->id;
        if (!nameOutput && nameCapacity == 0 && !payloadOutput && payloadCapacity == 0) {
            return SHIP_NATIVE_OK;
        }
        if (nameCapacity < *nameSize || payloadCapacity < *payloadSize) {
            return SHIP_NATIVE_LIMIT;
        }
        if (*nameSize) {
            std::memcpy(nameOutput, entry->name.data(), *nameSize);
        }
        if (*payloadSize) {
            std::memcpy(payloadOutput, entry->payload.data(), *payloadSize);
        }
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL ListEntries(uint64_t spaceHandle, ShipOotRegistryEntryFn callback, void* user) {
    if (!OnOwnerThread() || !spaceHandle || !callback) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        const auto* space = FindSpace(spaceHandle);
        if (!space) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        struct SnapshotEntry {
            uint64_t handle;
            int32_t id;
            std::string name;
        };
        std::vector<SnapshotEntry> snapshot;
        snapshot.reserve(space->entries.size());
        for (const auto& [id, entry] : space->entries) {
            snapshot.push_back({ entry.handle, id, entry.name });
        }
        for (const auto& entry : snapshot) {
            const ShipNativeStatus status =
                callback(user, entry.handle, entry.id, entry.name.data(), static_cast<uint32_t>(entry.name.size()));
            if (status != SHIP_NATIVE_OK) {
                return status;
            }
        }
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

const ShipOotRegistryV1 registryV1{
    sizeof(ShipOotRegistryV1), CreateSpace,     FindSpaceByName, DestroySpace, RegisterEntry,
    UnregisterEntry,           FindEntryByName, FindEntryById,   ReadEntry,    ListEntries
};

} // namespace

void InitializeOotNativeRegistry(std::thread::id ownerThread) {
    auto& state = State();
    state = RegistryState{};
    state.ownerThread = ownerThread;
}

void ResetOotNativeRegistry() {
    State() = RegistryState{};
}

const ShipOotRegistryV1& GetOotNativeRegistryService() {
    return registryV1;
}

} // namespace ShipLuaHost
