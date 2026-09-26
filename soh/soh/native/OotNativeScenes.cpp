#include "OotNativeScenes.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ShipLuaHost {
bool InOotJsonTranscode(); // OotNativeJsonTypes.cpp
namespace {

constexpr int32_t ENTRANCE_LAYERS = LINKSPAN_OOT_SCENES_ENTRANCE_LAYERS;
constexpr size_t MAX_ENTRANCE_NAME = LINKSPAN_OOT_SCENES_MAX_NAME * 2 + 1;
constexpr uint8_t MAX_END_TRANSITION = ENTRANCE_INFO_END_TRANS_TYPE_MASK >> ENTRANCE_INFO_END_TRANS_TYPE_SHIFT;
constexpr uint8_t MAX_START_TRANSITION = ENTRANCE_INFO_START_TRANS_TYPE_MASK >> ENTRANCE_INFO_START_TRANS_TYPE_SHIFT;
// Unbound 0.8: o último grupo termina antes das entradas de retorno dinâmico (0x7FF9..0x7FFF), que o jogo resolve
// sem ler a tabela; um grupo em 0x7FF8 ou 0x7FFC ficaria inalcançável.
constexpr int32_t LAST_ENTRANCE_GROUP =
    (ENTR_RETURN_YOUSEI_IZUMI_YOKO - ENTRANCE_LAYERS) / ENTRANCE_LAYERS * ENTRANCE_LAYERS;
static_assert(LAST_ENTRANCE_GROUP == 0x7FF4 && LAST_ENTRANCE_GROUP == LINKSPAN_OOT_SCENES_MAX_ENTRANCE_INDEX);

struct SceneRecord {
    int32_t id = 0;
    std::string name;
    std::string displayName;
    std::string path;
    uint8_t drawConfig = 0;
    std::string titleCard;
    bool horse = false;
    bool horseHasSpawn = false;
    Vec3f horseSpawn{};
    int16_t horseAngle = 0;
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
    // override_scene: (id vanilla, variante MQ) -> recurso.
    std::map<std::pair<int32_t, bool>, std::string> overrides;
    std::set<int32_t> vanillaHorseScenes;
    uint32_t customHorseSceneCount = 0;
    std::string horseSceneSnapshotName;
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

// Um transcode JSON resolve exits por nome numa thread do pool, com a thread do jogo esperando o recurso.
bool OnLookupThread() {
    return OnOwnerThread() || InOotJsonTranscode();
}

// Os mapas de nome (scenes, handlesByName, entrances) são lidos por FindScene/FindEntrance também na thread do
// transcode, e escritos só na thread do jogo. Espera do jogo pelo recurso não é garantia de exclusão (carga
// assíncrona, prefetch), então a escrita pega o lock exclusivo, só em volta da mudança nos mapas e sem chamar
// nada de fora com ele, e a busca pega o compartilhado.
std::shared_mutex& NamesMutex() {
    static std::shared_mutex mutex;
    return mutex;
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

ShipNativeStatus RegisterSceneRecord(const ShipOotSceneDefinitionV1* definition, const char* titleCard,
                                     const ShipOotSceneDefinitionV3* horseDefinition, uint64_t* sceneHandle,
                                     int32_t* sceneId) {
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
        if (horseDefinition) {
            if (horseDefinition->horse_has_spawn && !horseDefinition->horse_enabled) {
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
            if (!std::isfinite(horseDefinition->horse_x) || !std::isfinite(horseDefinition->horse_y) ||
                !std::isfinite(horseDefinition->horse_z)) {
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
            record.horse = horseDefinition->horse_enabled != 0;
            record.horseHasSpawn = horseDefinition->horse_has_spawn != 0;
            record.horseSpawn = { horseDefinition->horse_x, horseDefinition->horse_y, horseDefinition->horse_z };
            record.horseAngle = horseDefinition->horse_angle;
        }
        if (titleCard && *titleCard) {
            const size_t titleLength = BoundedLength(titleCard, LINKSPAN_OOT_SCENES_MAX_PATH);
            if (!titleLength) {
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
            record.titleCard.assign(titleCard, titleLength);
        }
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
        const std::unique_lock lock(NamesMutex());
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
        if (state.scenes.at(handle).horse) {
            ++state.customHorseSceneCount;
        }
        state.nextSceneId = std::max(state.nextSceneId, id + 1);
        *sceneHandle = handle;
        *sceneId = id;
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL RegisterScene(const ShipOotSceneDefinitionV1* definition, uint64_t* sceneHandle,
                                                int32_t* sceneId) {
    if (!OnOwnerThread() || !definition || definition->size < sizeof(ShipOotSceneDefinitionV1) || !sceneHandle ||
        !sceneId) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    return RegisterSceneRecord(definition, nullptr, nullptr, sceneHandle, sceneId);
}

ShipNativeStatus SHIP_NATIVE_CALL RegisterSceneV2(const ShipOotSceneDefinitionV2* definition, uint64_t* sceneHandle,
                                                  int32_t* sceneId) {
    if (!OnOwnerThread() || !definition || definition->size < sizeof(ShipOotSceneDefinitionV2) || !sceneHandle ||
        !sceneId) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    // Os campos V1 são o prefixo da V2.
    const ShipOotSceneDefinitionV1 v1{ sizeof(ShipOotSceneDefinitionV1), definition->name, definition->display_name,
                                       definition->scene_path, definition->requested_id, definition->draw_config };
    return RegisterSceneRecord(&v1, definition->title_card_texture, nullptr, sceneHandle, sceneId);
}

ShipNativeStatus SHIP_NATIVE_CALL RegisterSceneV3(const ShipOotSceneDefinitionV3* definition, uint64_t* sceneHandle,
                                                  int32_t* sceneId) {
    if (!OnOwnerThread() || !definition || definition->size < sizeof(ShipOotSceneDefinitionV3) || !sceneHandle ||
        !sceneId) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const ShipOotSceneDefinitionV1 v1{ sizeof(ShipOotSceneDefinitionV1), definition->name, definition->display_name,
                                       definition->scene_path, definition->requested_id, definition->draw_config };
    return RegisterSceneRecord(&v1, definition->title_card_texture, definition, sceneHandle, sceneId);
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
            if (index > LAST_ENTRANCE_GROUP) {
                return SHIP_NATIVE_LIMIT;
            }
        } else if (index < first || index > LAST_ENTRANCE_GROUP || index % ENTRANCE_LAYERS != 0) {
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
        {
            const std::unique_lock lock(NamesMutex());
            scene->second.entrances.push_back(name);
            try {
                state.entrances.emplace(name, index);
                state.groups.insert(index);
            } catch (...) {
                state.entrances.erase(name);
                scene->second.entrances.pop_back();
                throw;
            }
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
    if (scene->second.horse) {
        --state.customHorseSceneCount;
    }
    {
        const std::unique_lock lock(NamesMutex());
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
    }
    // A tabela não encolhe: índices já lidos pelo jogo continuam dentro dela.
    PublishTable(state);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL FindScene(const char* name, int32_t* sceneId) {
    if (!OnLookupThread() || !sceneId) {
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
        const std::shared_lock lock(NamesMutex());
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
    if (!OnLookupThread() || !entranceIndex) {
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
        const std::shared_lock lock(NamesMutex());
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

int32_t SHIP_NATIVE_CALL GetSceneCount() {
    return std::max(Vanilla().data.sceneCount, 0);
}

bool IsVanillaScene(int32_t sceneId) {
    return sceneId >= 0 && sceneId < Vanilla().data.sceneCount;
}

bool HasMasterQuest(int32_t sceneId) {
    const auto& data = Vanilla().data;
    return IsVanillaScene(sceneId) && data.sceneHasMasterQuest && data.sceneHasMasterQuest(sceneId);
}

void CopyText(char* output, size_t capacity, const std::string& text) {
    const size_t length = std::min(text.size(), capacity - 1);
    std::memcpy(output, text.data(), length);
    output[length] = '\0';
}

ShipNativeStatus SHIP_NATIVE_CALL GetSceneInfo(int32_t sceneId, uint8_t masterQuest, ShipOotSceneInfoV1* info) {
    if (!OnOwnerThread() || !info || info->size < sizeof(ShipOotSceneInfoV1)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const uint32_t size = info->size;
    std::memset(info, 0, sizeof(ShipOotSceneInfoV1));
    info->size = size;
    info->scene_id = sceneId;
    try {
        const auto& data = Vanilla().data;
        if (IsVanillaScene(sceneId)) {
            const bool mq = HasMasterQuest(sceneId);
            const char* file = data.sceneFileName ? data.sceneFileName(sceneId) : nullptr;
            const std::string fileName = file ? file : "";
            std::string path;
            info->overridden = OotSceneOverridePath(sceneId, mq && masterQuest, path) ? 1 : 0;
            if (!info->overridden && !fileName.empty()) {
                const char* folder = mq ? (masterQuest ? "mq" : "nonmq") : "shared";
                path = std::string("scenes/") + folder + "/" + fileName + "/" + fileName;
            }
            info->draw_config = data.sceneDrawConfig ? data.sceneDrawConfig(sceneId) : 0;
            info->has_master_quest = mq ? 1 : 0;
            const char* name = data.sceneNames ? data.sceneNames[sceneId] : nullptr;
            CopyText(info->name, sizeof(info->name), name ? name : "");
            CopyText(info->file_name, sizeof(info->file_name), fileName);
            CopyText(info->scene_path, sizeof(info->scene_path), path);
            return SHIP_NATIVE_OK;
        }
        const SceneRecord* record = FindRecordById(sceneId);
        if (!record) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        info->is_custom = 1;
        info->draw_config = record->drawConfig;
        CopyText(info->name, sizeof(info->name), record->name);
        CopyText(info->scene_path, sizeof(info->scene_path), record->path);
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL OverrideScene(int32_t sceneId, uint8_t masterQuest, const char* scenePath) {
    if (!OnOwnerThread() || !IsVanillaScene(sceneId) || masterQuest > 1 || (masterQuest && !HasMasterQuest(sceneId))) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const bool mq = masterQuest != 0;
    auto& overrides = State().overrides;
    if (!scenePath || !*scenePath) {
        overrides.erase({ sceneId, mq });
        return SHIP_NATIVE_OK;
    }
    const size_t length = BoundedLength(scenePath, LINKSPAN_OOT_SCENES_MAX_PATH);
    if (!length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        overrides[{ sceneId, mq }].assign(scenePath, length);
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

const ShipOotScenesV1 scenesService{
    sizeof(ShipOotScenesV1), RegisterScene, RegisterEntrance, UnregisterScene, FindScene, FindEntrance,
    TravelToEntrance,
};

const ShipOotScenesV2 scenesServiceV2{
    sizeof(ShipOotScenesV2), RegisterScene, RegisterEntrance, UnregisterScene, FindScene, FindEntrance,
    TravelToEntrance, RegisterSceneV2, GetSceneCount, GetSceneInfo, OverrideScene,
};

const ShipOotScenesV3 scenesServiceV3{
    sizeof(ShipOotScenesV3), RegisterScene, RegisterEntrance, UnregisterScene, FindScene, FindEntrance,
    TravelToEntrance, RegisterSceneV2, GetSceneCount, GetSceneInfo, OverrideScene, RegisterSceneV3,
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
    {
        const std::unique_lock lock(NamesMutex());
        state = ScenesState{};
    }
    state.ownerThread = ownerThread;
    const auto& vanilla = Vanilla().data;
    for (int32_t i = 0; vanilla.horseScenes && i < std::max(vanilla.horseSceneCount, 0); ++i) {
        state.vanillaHorseScenes.insert(vanilla.horseScenes[i]);
    }
    state.publishedCount = VanillaEntranceCount();
}

void ResetOotNativeScenes() {
    InitializeOotNativeScenes(std::thread::id{});
}

const ShipOotScenesV1& GetOotNativeScenesService() {
    return scenesService;
}

const ShipOotScenesV2& GetOotNativeScenesServiceV2() {
    return scenesServiceV2;
}

const ShipOotScenesV3& GetOotNativeScenesServiceV3() {
    return scenesServiceV3;
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

const std::string* OotCustomSceneTitleCard(int32_t sceneId) {
    const SceneRecord* record = FindRecordById(sceneId);
    return record && !record->titleCard.empty() ? &record->titleCard : nullptr;
}

bool OotSceneOverridePath(int32_t sceneId, bool masterQuest, std::string& path) {
    const auto& overrides = State().overrides;
    const auto found = overrides.find({ sceneId, masterQuest });
    if (found == overrides.end()) {
        return false;
    }
    try {
        path = found->second;
        return true;
    } catch (...) {
        return false;
    }
}

bool OotSceneStableName(int32_t sceneId, std::string& name) {
    const auto& data = Vanilla().data;
    if (sceneId >= 0 && sceneId < data.sceneCount && data.sceneNames && data.sceneNames[sceneId]) {
        name = data.sceneNames[sceneId];
        return true;
    }
    if (const SceneRecord* record = FindRecordById(sceneId)) {
        name = record->name;
        return true;
    }
    return false;
}

bool OotSceneIdFromStableName(const std::string& name, int32_t& sceneId) {
    const auto& vanilla = Vanilla().sceneIds;
    if (const auto found = vanilla.find(name); found != vanilla.end()) {
        sceneId = found->second;
        return true;
    }
    const auto& state = State();
    if (const auto found = state.handlesByName.find(name); found != state.handlesByName.end()) {
        sceneId = state.scenes.at(found->second).id;
        return true;
    }
    return false;
}

// Entradas de mod no save (Unbound 0.8). O número de cada uma sai na ordem do registro e muda com os mods
// montados; o save guarda o nome do grupo e a camada dentro dele. Declaradas em quem usa, fora do header, para
// não mexer no layout id. Na thread do jogo, a mesma que escreve o registro.
bool OotEntranceStableName(int32_t entranceIndex, std::string& name, int32_t& layer) {
    if (entranceIndex < FirstCustomEntranceIndex()) {
        return false;
    }
    const int32_t group = entranceIndex - entranceIndex % ENTRANCE_LAYERS;
    const auto& state = State();
    if (!state.groups.contains(group)) {
        return false;
    }
    for (const auto& [entranceName, index] : state.entrances) {
        if (index == group) {
            try {
                name = entranceName;
            } catch (...) {
                return false;
            }
            layer = entranceIndex - group;
            return true;
        }
    }
    return false;
}

bool OotEntranceGroupFromStableName(const std::string& name, int32_t& group) {
    const auto& entrances = State().entrances;
    const auto found = entrances.find(name);
    if (found == entrances.end()) {
        return false;
    }
    group = found->second;
    return true;
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

std::map<std::string, SavedSceneFlags, std::less<>> ExportOotCustomSceneFlags() {
    return State().flags;
}

void ReplaceOotCustomSceneFlags(std::map<std::string, SavedSceneFlags, std::less<>> flags) {
    State().flags = std::move(flags);
}

bool OotSceneHorseAllowed(int32_t sceneId) {
    const auto& state = State();
    if (state.vanillaHorseScenes.contains(sceneId)) {
        return true;
    }
    // A semeadura normal ocorre no init do host; este fallback mantém as cinco cenas vanilla idênticas durante
    // qualquer chamada de inicialização anterior a ele.
    const auto& vanilla = Vanilla().data;
    for (int32_t i = 0; vanilla.horseScenes && i < std::max(vanilla.horseSceneCount, 0); ++i) {
        if (vanilla.horseScenes[i] == sceneId) {
            return true;
        }
    }
    const SceneRecord* record = FindRecordById(sceneId);
    return record && record->horse;
}

bool OotHasRegisteredHorseScenes() {
    return State().customHorseSceneCount != 0;
}

bool OotSceneHorseSpawn(int32_t sceneId, Vec3f& pos, int16_t& angle) {
    const SceneRecord* record = FindRecordById(sceneId);
    if (!record || !record->horse || !record->horseHasSpawn) {
        return false;
    }
    pos = record->horseSpawn;
    angle = record->horseAngle;
    return true;
}

bool OotSceneUsesGeneratedHorseCall(int32_t sceneId) {
    const SceneRecord* record = FindRecordById(sceneId);
    return record && record->horse;
}

void ReplaceOotHorseSceneSnapshotName(std::string name) {
    State().horseSceneSnapshotName = std::move(name);
}

const std::string& PendingOotHorseSceneSnapshotName() {
    return State().horseSceneSnapshotName;
}

bool ResolveOotHorseSceneSnapshotName(int32_t& sceneId) {
    auto& name = State().horseSceneSnapshotName;
    if (name.empty() || !OotSceneIdFromStableName(name, sceneId)) {
        return false;
    }
    // Consumido: dali em diante a Epona muda de cena pelo jogo, e reaplicar o nome do save a levaria de volta.
    name.clear();
    return true;
}

BetterSceneSelectEntry* OotBuildBetterWarpScenes(BetterSceneSelectEntry* vanilla, int32_t vanillaCount,
                                                  void (*loadFunc)(SelectContext*, int32_t), int32_t& count) {
    static std::vector<BetterSceneSelectEntry> entries;
    static std::deque<std::string> text;
    entries.clear();
    text.clear();
    if (!vanilla || vanillaCount <= 0) {
        count = 0;
        return nullptr;
    }
    entries.assign(vanilla, vanilla + vanillaCount);
    const auto string = [](std::string value) -> char* {
        text.push_back(std::move(value));
        return text.back().data();
    };
    const auto& state = State();
    for (const auto& [_, scene] : state.scenes) {
        if (scene.entrances.empty()) {
            continue;
        }
        BetterSceneSelectEntry entry{};
        char* const name = string(std::to_string(entries.size() + 1) + ": " + scene.displayName);
        entry.japaneseName = name;
        entry.englishName = name;
        entry.germanName = name;
        entry.frenchName = name;
        entry.loadFunc = loadFunc;
        for (const auto& fullName : scene.entrances) {
            if (entry.entranceCount >= 18) {
                break;
            }
            const auto found = state.entrances.find(fullName);
            if (found == state.entrances.end()) {
                continue;
            }
            BetterSceneSelectEntrancePair& pair = entry.entrancePairs[entry.entranceCount++];
            char* const key = string(fullName.substr(scene.name.size() + 1));
            pair.japaneseName = key;
            pair.englishName = key;
            pair.germanName = key;
            pair.frenchName = key;
            pair.entranceIndex = found->second;
            pair.canBeMQ = false;
        }
        if (entry.entranceCount) {
            entries.push_back(entry);
        }
    }
    count = static_cast<int32_t>(entries.size());
    return entries.data();
}

} // namespace ShipLuaHost
