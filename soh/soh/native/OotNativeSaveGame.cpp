// Liga linkspan.oot.save (OotNativeSave.cpp) ao SaveManager do SoH: seção "linkspan" no arquivo,
// hooks oot.save.* e as flags das cenas de mod gravadas pelo nome da cena.
#include "OotNativeSave.h"

#include <spdlog/spdlog.h>

#include "OotNativeHooks.h"
#include "OotNativeItems.h"
#include "OotNativeScenes.h"
#include "soh/SaveManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "macros.h"
#include "variables.h"
}

namespace ShipLuaHost {
// OotNativeRandoGame.cpp
void StoreOotRandoSeed();
void RestoreOotRandoSeedFromSave();
// OotNativeAnchorGame.cpp
void RegisterOotAnchorGameHooks();
// OotNativeScenes.cpp (fora do header para não mexer no layout id)
bool OotEntranceStableName(int32_t entranceIndex, std::string& name, int32_t& layer);
bool OotEntranceGroupFromStableName(const std::string& name, int32_t& group);
} // namespace ShipLuaHost

namespace {

constexpr const char* kSectionName = "linkspan";
constexpr int kSectionVersion = 1;
constexpr const char* kSceneFlagsBlock = "linkspan.scenes";
constexpr uint32_t kSceneFlagsVersion = 1;
constexpr const char* kHorseSceneBlock = "linkspan.horse.scene";
constexpr uint32_t kHorseSceneVersion = 1;
constexpr const char* kEntrancesBlock = "linkspan.entrances";
constexpr uint32_t kEntrancesVersion = 1;

void SaveSection(SaveContext*, int, bool) {
    auto section = ShipLuaHost::ExportOotSaveSection();
    SaveManager::Instance->SaveData("namespaces", section["namespaces"]);
}

void LoadSection() {
    nlohmann::json namespaces = nlohmann::json::object();
    SaveManager::Instance->LoadData("namespaces", namespaces, nlohmann::json::object());
    nlohmann::json section = nlohmann::json::object();
    section["namespaces"] = std::move(namespaces);
    ShipLuaHost::ImportOotSaveSection(section);
}

void InitFile(bool) {
    ShipLuaHost::ClearOotSaveData();
    ShipLuaHost::ReplaceOotCustomSceneFlags({});
    ShipLuaHost::ReplaceOotHorseSceneSnapshotName({});
}

void StoreHorseScene() {
    // Ainda não resolvido (ex.: salvo antes de uma cena adulta): o id em horseData pode ser o de outro registro.
    std::string name = ShipLuaHost::PendingOotHorseSceneSnapshotName();
    if (name.empty() && !ShipLuaHost::OotSceneStableName(gSaveContext.horseData.scene, name)) {
        return;
    }
    ShipLuaHost::SetOotHostSaveBlock(kHorseSceneBlock, kHorseSceneVersion, nlohmann::json{ { "scene", name } });
}

void RestoreHorseScene() {
    // Um save sem o bloco não herda o snapshot do save carregado antes.
    ShipLuaHost::ReplaceOotHorseSceneSnapshotName({});
    nlohmann::json data;
    uint32_t version = 0;
    if (ShipLuaHost::GetOotHostSaveBlock(kHorseSceneBlock, data, version) && version == kHorseSceneVersion &&
        data.is_object() && data.contains("scene") && data["scene"].is_string()) {
        ShipLuaHost::ReplaceOotHorseSceneSnapshotName(data["scene"].get<std::string>());
    }
}

// Unbound 0.8: o número de uma cena ou entrada de mod sai na ordem do registro e depende dos mods
// montados, então um número salvo passa a apontar para a cena de outro mod quando um mod entra ou sai. O bloco
// guarda pelo nome a cena salva e as três entradas que o save lembra: a atual, o Farore's Wind e a cópia de
// reserva do port. A seção base continua com os números (save sem Link-Span, entradas vanilla), e o nome,
// quando existe, vence no load. Entrada vanilla vai como null.
nlohmann::json SavedEntranceName(int32_t entranceIndex) {
    std::string name;
    int32_t layer = 0;
    if (!ShipLuaHost::OotEntranceStableName(entranceIndex, name, layer)) {
        return nullptr;
    }
    return nlohmann::json{ { "name", name }, { "layer", layer } };
}

void StoreEntrances() {
    std::string sceneName;
    const int32_t sceneId = gSaveContext.savedSceneNum;
    const bool customScene =
        sceneId >= LINKSPAN_OOT_SCENES_FIRST_CUSTOM_ID && ShipLuaHost::OotSceneStableName(sceneId, sceneName);
    nlohmann::json data{
        { "savedScene", customScene ? nlohmann::json(sceneName) : nlohmann::json(nullptr) },
        { "entrance", SavedEntranceName(gSaveContext.entranceIndex) },
        { "fwEntrance", SavedEntranceName(gSaveContext.fw.entranceIndex) },
        { "backupFwEntrance", SavedEntranceName(gSaveContext.ship.backupFW.entranceIndex) },
    };
    bool anyName = false;
    for (const auto& [key, value] : data.items()) {
        anyName = anyName || !value.is_null();
    }
    // Tudo vanilla: sem bloco anterior não há o que gravar. Com ele, os nulls vão por cima; senão o nome de
    // uma cena de mod sobreviveria a um save feito em cena vanilla e venceria no load.
    nlohmann::json existing;
    uint32_t version = 0;
    if (!anyName && !ShipLuaHost::GetOotHostSaveBlock(kEntrancesBlock, existing, version)) {
        return;
    }
    ShipLuaHost::SetOotHostSaveBlock(kEntrancesBlock, kEntrancesVersion, std::move(data));
}

// Para onde vai um save cuja cena ou entrada de mod não existe mais: o mesmo fallback do Sram_OpenSave. A
// cena é a da entrada, para os dois fallbacks nunca discordarem.
int32_t DefaultSpawnEntrance() {
    return LINK_AGE_IN_YEARS == YEARS_CHILD ? ENTR_LINKS_HOUSE_CHILD_SPAWN : ENTR_TEMPLE_OF_TIME_WARP_PAD;
}

int32_t DefaultSpawnScene() {
    return LINK_AGE_IN_YEARS == YEARS_CHILD ? SCENE_LINKS_HOUSE : SCENE_TEMPLE_OF_TIME;
}

// -1: o save nomeia uma entrada que nenhum mod carregado registra. 0: não nomeia (vanilla ou save antigo).
// 1: resolvida em `index`.
int ResolveSavedEntrance(const nlohmann::json& data, const char* key, int32_t& index, std::string& name) {
    const auto found = data.find(key);
    if (found == data.end() || !found->is_object() || !found->contains("name") || !(*found)["name"].is_string()) {
        return 0;
    }
    name = (*found)["name"].get<std::string>();
    int32_t layer = 0;
    if (found->contains("layer") && (*found)["layer"].is_number_integer()) {
        layer = (*found)["layer"].get<int32_t>();
    }
    if (layer < 0 || layer >= LINKSPAN_OOT_SCENES_ENTRANCE_LAYERS) {
        // Qualquer outra camada cairia no grupo seguinte, que é outra entrada.
        SPDLOG_WARN("Link-Span save: a entrada '{}' tem camada {}; usando 0", name, layer);
        layer = 0;
    }
    int32_t group = 0;
    if (!ShipLuaHost::OotEntranceGroupFromStableName(name, group)) {
        return -1;
    }
    index = group + layer;
    return 1;
}

// No OnLoadFile, antes de o Sram_OpenSave escolher o spawn: com "Remember Save Location" ligado é esta
// entrada que mantém o jogador na cena de mod em que salvou.
void RestoreEntrances() {
    nlohmann::json data;
    uint32_t version = 0;
    if (!ShipLuaHost::GetOotHostSaveBlock(kEntrancesBlock, data, version) || version != kEntrancesVersion ||
        !data.is_object()) {
        return;
    }
    if (data.contains("savedScene") && data["savedScene"].is_string()) {
        const std::string name = data["savedScene"].get<std::string>();
        int32_t sceneId = 0;
        if (ShipLuaHost::OotSceneIdFromStableName(name, sceneId)) {
            gSaveContext.savedSceneNum = static_cast<s16>(sceneId);
        } else {
            SPDLOG_WARN("Link-Span save: o arquivo está na cena '{}', que nenhum mod carregado registra", name);
            gSaveContext.savedSceneNum = static_cast<s16>(DefaultSpawnScene());
        }
    }
    int32_t index = 0;
    std::string name;
    switch (ResolveSavedEntrance(data, "entrance", index, name)) {
        case 1:
            gSaveContext.entranceIndex = index;
            break;
        case -1:
            SPDLOG_WARN("Link-Span save: a entrada '{}' não está registrada; o jogo começa no spawn padrão", name);
            gSaveContext.entranceIndex = DefaultSpawnEntrance();
            break;
    }
    // Farore's Wind para uma cena que o jogador não tem mais é apagado, não apontado para outro lugar.
    const std::pair<const char*, FaroresWindData*> warps[] = { { "fwEntrance", &gSaveContext.fw },
                                                               { "backupFwEntrance", &gSaveContext.ship.backupFW } };
    for (const auto& [key, warp] : warps) {
        switch (ResolveSavedEntrance(data, key, index, name)) {
            case 1:
                warp->entranceIndex = index;
                break;
            case -1:
                SPDLOG_WARN("Link-Span save: o Farore's Wind ({}) está na entrada '{}', que não está registrada; "
                            "apagado",
                            key, name);
                warp->set = 0;
                break;
        }
    }
}

void StoreSceneFlags() {
    auto flags = ShipLuaHost::ExportOotCustomSceneFlags();
    nlohmann::json existing;
    uint32_t version = 0;
    if (flags.empty() && !ShipLuaHost::GetOotHostSaveBlock(kSceneFlagsBlock, existing, version)) {
        return;
    }
    nlohmann::json data = nlohmann::json::object();
    for (const auto& [scene, value] : flags) {
        data[scene] = { value.chest, value.swch, value.clear, value.collect, value.unk, value.rooms, value.floors };
    }
    ShipLuaHost::SetOotHostSaveBlock(kSceneFlagsBlock, kSceneFlagsVersion, std::move(data));
}

void RestoreSceneFlags() {
    nlohmann::json data;
    uint32_t version = 0;
    std::map<std::string, SavedSceneFlags, std::less<>> flags;
    if (ShipLuaHost::GetOotHostSaveBlock(kSceneFlagsBlock, data, version) && version == kSceneFlagsVersion &&
        data.is_object()) {
        for (const auto& [scene, value] : data.items()) {
            if (!value.is_array() || value.size() != 7) {
                continue;
            }
            bool valid = true;
            for (const auto& field : value) {
                valid = valid && field.is_number_unsigned();
            }
            if (!valid) {
                continue;
            }
            flags[scene] = SavedSceneFlags{ value[0].get<u32>(), value[1].get<u32>(), value[2].get<u32>(),
                                            value[3].get<u32>(), value[4].get<u32>(), value[5].get<u32>(),
                                            value[6].get<u32>() };
        }
    }
    ShipLuaHost::ReplaceOotCustomSceneFlags(std::move(flags));
}

} // namespace

namespace ShipLuaHost {

void RegisterOotSaveSection() {
    static bool registered = false;
    if (registered || !SaveManager::Instance || !GameInteractor::Instance) {
        return;
    }
    registered = true;
    RegisterOotAnchorGameHooks();
    SaveManager::Instance->AddInitFunction(InitFile);
    SaveManager::Instance->AddLoadFunction(kSectionName, kSectionVersion, LoadSection);
    SaveManager::Instance->AddSaveFunction(kSectionName, kSectionVersion, SaveSection, true, SECTION_PARENT_NONE);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadFile>([](int32_t fileNum) {
        SetOotSaveSlot(fileNum);
        RestoreSceneFlags();
        RestoreHorseScene();
        RestoreEntrances();
        RestoreOotItemButtonsFromSave();
        RestoreOotRandoSeedFromSave();
        for (const auto& name : MissingRequiredOotNamespaces()) {
            SPDLOG_WARN("Link-Span save: o arquivo {} depende de '{}', que nenhum mod carregado abriu", fileNum + 1,
                        name);
        }
        DispatchOotSaveHook(GetOotHookPoints().saveLoaded, fileNum, -1);
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnDeleteFile>(
        [](int32_t fileNum) { DispatchOotSaveHook(GetOotHookPoints().saveDeleted, fileNum, -1); });
}

// Na thread do jogo, antes de SaveSection copiar o SaveContext e mandar gravar em outra thread.
void OotBeforeSave(int32_t fileNum, int32_t sectionId) {
    if (sectionId != SECTION_ID_BASE || fileNum < 0 || fileNum >= SaveManager::MaxFiles) {
        return;
    }
    SetOotSaveSlot(fileNum);
    DispatchOotSaveHook(GetOotHookPoints().saveSaving, fileNum, -1);
    StoreSceneFlags();
    StoreHorseScene();
    StoreEntrances();
    StoreOotItemButtons();
    StoreOotRandoSeed();
}

void OotAfterCopy(int32_t from, int32_t to) {
    DispatchOotSaveHook(GetOotHookPoints().saveCopied, to, from);
}

} // namespace ShipLuaHost
