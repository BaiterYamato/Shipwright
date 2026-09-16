#include "OotNativeScenes.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ShipLuaHost {
namespace {

constexpr int32_t ENTRANCE_LAYERS = LINKSPAN_OOT_SCENES_ENTRANCE_LAYERS;
constexpr size_t MAX_ENTRANCE_NAME = LINKSPAN_OOT_SCENES_MAX_NAME * 2 + 1;
constexpr uint8_t MAX_END_TRANSITION = ENTRANCE_INFO_END_TRANS_TYPE_MASK >> ENTRANCE_INFO_END_TRANS_TYPE_SHIFT;
constexpr uint8_t MAX_START_TRANSITION = ENTRANCE_INFO_START_TRANS_TYPE_MASK >> ENTRANCE_INFO_START_TRANS_TYPE_SHIFT;

struct SceneRecord {
    int32_t id = 0;
    std::string name;
    std::string displayName;
    std::string path;
    uint8_t drawConfig = 0;
    std::vector<std::string> entrances;
};

// Estado dos mods: zera no init e no shutdown do host.
struct ScenesState {
    std::thread::id ownerThread;
    uint64_t nextHandle = 1;
    int32_t nextSceneId = LINKSPAN_OOT_SCENES_FIRST_CUSTOM_ID;
    int32_t nextEntranceIndex = 0;
    // Vazia enquanto só há entradas vanilla; depois, cópia vanilla seguida dos grupos dos mods.
    std::vector<EntranceInfo> table;
    int32_t publishedCount = 0;
    std::map<uint64_t, SceneRecord> scenes;
    std::map<int32_t, uint64_t> handlesById;
    std::map<std::string, uint64_t, std::less<>> handlesByName;
    std::map<std::string, int32_t, std::less<>> entrances;
    std::set<int32_t> groups;
    // Por nome da cena: a flag sobrevive a um novo registro da mesma cena na sessão.
    std::map<std::string, SavedSceneFlags, std::less<>> flags;
    SavedSceneFlags scratch{};
};

// Dados do jogo: entregues uma vez pelo binding, sobrevivem ao reset dos mods.
struct VanillaState {
    OotVanillaScenes data;
    std::unordered_map<std::string, int32_t> sceneIds;
    std::unordered_map<std::string, int32_t> entranceIndices;
    OotEntranceTableListener listener = nullptr;
    OotSceneTravelFn travel = nullptr;
};

ScenesState& State() {
    static ScenesState state;
    return state;
}

VanillaState& Vanilla() {
    static VanillaState vanilla;
    return vanilla;
}

bool OnOwnerThread() {
    const auto& state = State();
    return state.ownerThread != std::thread::id{} && std::this_thread::get_id() == state.ownerThread;
}

// Tamanho da string, ou 0 quando ela é nula, vazia ou maior que o limite.
size_t BoundedLength(const char* text, size_t limit) {
    if (!text) {
        return 0;
    }
    const size_t length = strnlen(text, limit + 1);
    return length > limit ? 0 : length;
}

int32_t VanillaEntranceCount() {
    return std::max(Vanilla().data.entranceCount, 0);
}

int32_t FirstCustomEntranceIndex() {
    return (VanillaEntranceCount() + ENTRANCE_LAYERS - 1) / ENTRANCE_LAYERS * ENTRANCE_LAYERS;
}

// Mesma marca das posições sem uso da tabela vanilla (SCENE_UNUSED_6E).
EntranceInfo UnusedEntrance() {
    EntranceInfo unused{};
    unused.scene = static_cast<decltype(unused.scene)>(Vanilla().data.sceneCount);
    return unused;
}

void PublishTable(ScenesState& state) {
    state.publishedCount = state.table.empty() ? VanillaEntranceCount() : static_cast<int32_t>(state.table.size());
    if (const auto listener = Vanilla().listener) {
        listener(state.table.empty() ? nullptr : state.table.data(), state.publishedCount);
    }
}

const SceneRecord* FindRecordById(int32_t sceneId) {
    const auto& state = State();
    const auto handle = state.handlesById.find(sceneId);
    if (handle == state.handlesById.end()) {
        return nullptr;
    }
    const auto scene = state.scenes.find(handle->second);
    return scene == state.scenes.end() ? nullptr : &scene->second;
}

ShipNativeStatus SHIP_NATIVE_CALL RegisterScene(const ShipOotSceneDefinitionV1* definition, uint64_t* sceneHandle,
                                                int32_t* sceneId) {
    if (!OnOwnerThread() || !definition || definition->size < sizeof(ShipOotSceneDefinitionV1) || !sceneHandle ||
        !sceneId) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *sceneHandle = 0;
    *sceneId = 0;
    const size_t nameLength = BoundedLength(definition->name, LINKSPAN_OOT_SCENES_MAX_NAME);
    const size_t pathLength = BoundedLength(definition->scene_path, LINKSPAN_OOT_SCENES_MAX_PATH);
    const bool hasDisplayName = definition->display_name && *definition->display_name;
    const size_t displayLength = BoundedLength(definition->display_name, LINKSPAN_OOT_SCENES_MAX_NAME);
    if (!nameLength || !pathLength || (hasDisplayName && !displayLength) ||
        definition->draw_config >= Vanilla().data.drawConfigCount) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    try {
        SceneRecord record;
        record.name.assign(definition->name, nameLength);
        record.displayName = hasDisplayName ? std::string(definition->display_name, displayLength) : record.name;
        record.path.assign(definition->scene_path, pathLength);
        record.drawConfig = definition->draw_config;
        if (Vanilla().sceneIds.contains(record.name) || state.handlesByName.contains(record.name)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        int32_t id = definition->requested_id;
        if (id == LINKSPAN_OOT_SCENES_AUTO) {
            id = state.nextSceneId;
            if (id > LINKSPAN_OOT_SCENES_MAX_ID) {
                return SHIP_NATIVE_LIMIT;
            }
        } else if (id < LINKSPAN_OOT_SCENES_FIRST_CUSTOM_ID || id > LINKSPAN_OOT_SCENES_MAX_ID) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        if (state.handlesById.contains(id)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        if (state.nextHandle == std::numeric_limits<uint64_t>::max()) {
            return SHIP_NATIVE_LIMIT;
        }
        const uint64_t handle = state.nextHandle;
        const std::string name = record.name;
        record.id = id;
        state.scenes.emplace(handle, std::move(record));
        try {
            state.handlesById.emplace(id, handle);
            state.handlesByName.emplace(name, handle);
        } catch (...) {
            state.handlesById.erase(id);
            state.scenes.erase(handle);
            throw;
        }
        ++state.nextHandle;
        state.nextSceneId = std::max(state.nextSceneId, id + 1);
        *sceneHandle = handle;
        *sceneId = id;
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL RegisterEntrance(uint64_t sceneHandle, const ShipOotEntranceDefinitionV1* definition,
                                                   int32_t* entranceIndex) {
    if (!OnOwnerThread() || !definition || definition->size < sizeof(ShipOotEntranceDefinitionV1) || !entranceIndex) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *entranceIndex = 0;
    auto& state = State();
    const auto scene = state.scenes.find(sceneHandle);
    const size_t keyLength = BoundedLength(definition->key, LINKSPAN_OOT_SCENES_MAX_NAME);
    if (scene == state.scenes.end() || !keyLength || definition->spawn > LINKSPAN_OOT_SCENES_MAX_SPAWN ||
        definition->end_transition > MAX_END_TRANSITION || definition->start_transition > MAX_START_TRANSITION) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& vanilla = Vanilla().data;
    if (vanilla.entranceCount > 0 && !vanilla.entrances) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    ShipNativeStatus status = SHIP_NATIVE_OK;
    bool tableChanged = false;
    try {
        const std::string name = scene->second.name + "/" + std::string(definition->key, keyLength);
        const int32_t first = FirstCustomEntranceIndex();
        int32_t index = definition->requested_index;
        if (Vanilla().entranceIndices.contains(name) || state.entrances.contains(name)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        if (index == LINKSPAN_OOT_SCENES_AUTO) {
            index = std::max(state.nextEntranceIndex, first);
            if (index > LINKSPAN_OOT_SCENES_MAX_ENTRANCE_INDEX) {
                return SHIP_NATIVE_LIMIT;
            }
        } else if (index < first || index > LINKSPAN_OOT_SCENES_MAX_ENTRANCE_INDEX || index % ENTRANCE_LAYERS != 0) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        if (state.groups.contains(index)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }

        EntranceInfo info{};
        info.scene = static_cast<decltype(info.scene)>(scene->second.id);
        info.spawn = static_cast<decltype(info.spawn)>(definition->spawn);
        info.field = static_cast<decltype(info.field)>(
            (definition->continue_bgm ? ENTRANCE_INFO_CONTINUE_BGM_FLAG : 0) |
            (definition->show_title_card ? ENTRANCE_INFO_DISPLAY_TITLE_CARD_FLAG : 0) |
            ((definition->end_transition << ENTRANCE_INFO_END_TRANS_TYPE_SHIFT) & ENTRANCE_INFO_END_TRANS_TYPE_MASK) |
            ((definition->start_transition << ENTRANCE_INFO_START_TRANS_TYPE_SHIFT) &
             ENTRANCE_INFO_START_TRANS_TYPE_MASK));

        if (state.table.empty()) {
            state.table.assign(vanilla.entrances, vanilla.entrances + VanillaEntranceCount());
            tableChanged = true;
        }
        const size_t required = static_cast<size_t>(index) + ENTRANCE_LAYERS;
        if (state.table.size() < required) {
            state.table.resize(required, UnusedEntrance());
            tableChanged = true;
        }
        scene->second.entrances.push_back(name);
        try {
            state.entrances.emplace(name, index);
            state.groups.insert(index);
        } catch (...) {
            state.entrances.erase(name);
            scene->second.entrances.pop_back();
            throw;
        }
        std::fill_n(state.table.begin() + index, ENTRANCE_LAYERS, info);
        tableChanged = true;
        state.nextEntranceIndex = std::max(state.nextEntranceIndex, index + ENTRANCE_LAYERS);
        *entranceIndex = index;
    } catch (...) {
        status = SHIP_NATIVE_FAILURE;
    }
    if (tableChanged) {
        PublishTable(state);
    }
    return status;
}

ShipNativeStatus SHIP_NATIVE_CALL UnregisterScene(uint64_t sceneHandle) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    const auto scene = state.scenes.find(sceneHandle);
    if (scene == state.scenes.end()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const EntranceInfo unused = UnusedEntrance();
    for (const auto& name : scene->second.entrances) {
        const auto entrance = state.entrances.find(name);
        if (entrance == state.entrances.end()) {
            continue;
        }
        std::fill_n(state.table.begin() + entrance->second, ENTRANCE_LAYERS, unused);
        state.groups.erase(entrance->second);
        state.entrances.erase(entrance);
    }
    state.handlesById.erase(scene->second.id);
    state.handlesByName.erase(scene->second.name);
    state.scenes.erase(scene);
    // A tabela não encolhe: índices já lidos pelo jogo continuam dentro dela.
    PublishTable(state);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL FindScene(const char* name, int32_t* sceneId) {
    if (!OnOwnerThread() || !sceneId) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *sceneId = 0;
    const size_t length = BoundedLength(name, LINKSPAN_OOT_SCENES_MAX_NAME);
    if (!length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        const std::string key(name, length);
        const auto& vanilla = Vanilla().sceneIds;
        if (const auto found = vanilla.find(key); found != vanilla.end()) {
            *sceneId = found->second;
            return SHIP_NATIVE_OK;
        }
        const auto& state = State();
        if (const auto found = state.handlesByName.find(key); found != state.handlesByName.end()) {
            *sceneId = state.scenes.at(found->second).id;
            return SHIP_NATIVE_OK;
        }
        return SHIP_NATIVE_UNSUPPORTED;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL FindEntrance(const char* name, int32_t* entranceIndex) {
    if (!OnOwnerThread() || !entranceIndex) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *entranceIndex = 0;
    const size_t length = BoundedLength(name, MAX_ENTRANCE_NAME);
    if (!length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        const std::string key(name, length);
        const auto& vanilla = Vanilla().entranceIndices;
        if (const auto found = vanilla.find(key); found != vanilla.end()) {
            *entranceIndex = found->second;
            return SHIP_NATIVE_OK;
        }
        const auto& state = State();
        if (const auto found = state.entrances.find(key); found != state.entrances.end()) {
            *entranceIndex = found->second;
            return SHIP_NATIVE_OK;
        }
        return SHIP_NATIVE_UNSUPPORTED;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL TravelToEntrance(int32_t entranceIndex) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const bool vanilla = entranceIndex >= 0 && entranceIndex < VanillaEntranceCount();
    if (!vanilla && !State().groups.contains(entranceIndex)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto travel = Vanilla().travel;
    return travel ? travel(entranceIndex) : SHIP_NATIVE_UNSUPPORTED;
}

const ShipOotScenesV1 scenesService{
    sizeof(ShipOotScenesV1), RegisterScene, RegisterEntrance, UnregisterScene, FindScene, FindEntrance,
    TravelToEntrance,
};

} // namespace

void SetOotVanillaScenes(const OotVanillaScenes& data) {
    auto& vanilla = Vanilla();
    vanilla.data = data;
    vanilla.sceneIds.clear();
    vanilla.entranceIndices.clear();
    for (int32_t id = 0; data.sceneNames && id < data.sceneCount; ++id) {
        if (data.sceneNames[id]) {
            vanilla.sceneIds.emplace(data.sceneNames[id], id);
        }
    }
    for (int32_t index = 0; data.entranceNames && index < data.entranceCount; ++index) {
        if (data.entranceNames[index]) {
            vanilla.entranceIndices.emplace(data.entranceNames[index], index);
        }
    }
    State().publishedCount = VanillaEntranceCount();
}

void SetOotEntranceTableListener(OotEntranceTableListener listener) {
    Vanilla().listener = listener;
}

void SetOotSceneTravel(OotSceneTravelFn travel) {
    Vanilla().travel = travel;
}

void InitializeOotNativeScenes(std::thread::id ownerThread) {
    // O jogo volta à tabela vanilla antes de a combinada deixar de existir.
    if (const auto listener = Vanilla().listener) {
        listener(nullptr, VanillaEntranceCount());
    }
    auto& state = State();
    state = ScenesState{};
    state.ownerThread = ownerThread;
    state.publishedCount = VanillaEntranceCount();
}

void ResetOotNativeScenes() {
    InitializeOotNativeScenes(std::thread::id{});
}

const ShipOotScenesV1& GetOotNativeScenesService() {
    return scenesService;
}

int32_t OotEntranceCount() {
    return State().publishedCount;
}

bool FindOotCustomScene(int32_t sceneId, OotCustomScene& scene) {
    const SceneRecord* record = FindRecordById(sceneId);
    if (!record) {
        return false;
    }
    try {
        scene.path = record->path;
        scene.drawConfig = record->drawConfig;
        return true;
    } catch (...) {
        return false;
    }
}

const std::string* OotCustomSceneDisplayName(int32_t sceneId) {
    const SceneRecord* record = FindRecordById(sceneId);
    return record ? &record->displayName : nullptr;
}

SavedSceneFlags* OotCustomSceneFlags(int32_t sceneId) {
    auto& state = State();
    if (const SceneRecord* record = FindRecordById(sceneId)) {
        try {
            return &state.flags[record->name];
        } catch (...) {
        }
    }
    state.scratch = SavedSceneFlags{};
    return &state.scratch;
}

} // namespace ShipLuaHost
