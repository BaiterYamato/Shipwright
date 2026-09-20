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
#include "variables.h"
}

namespace {

constexpr const char* kSectionName = "linkspan";
constexpr int kSectionVersion = 1;
constexpr const char* kSceneFlagsBlock = "linkspan.scenes";
constexpr uint32_t kSceneFlagsVersion = 1;
constexpr const char* kHorseSceneBlock = "linkspan.horse.scene";
constexpr uint32_t kHorseSceneVersion = 1;

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
    SaveManager::Instance->AddInitFunction(InitFile);
    SaveManager::Instance->AddLoadFunction(kSectionName, kSectionVersion, LoadSection);
    SaveManager::Instance->AddSaveFunction(kSectionName, kSectionVersion, SaveSection, true, SECTION_PARENT_NONE);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadFile>([](int32_t fileNum) {
        SetOotSaveSlot(fileNum);
        RestoreSceneFlags();
        RestoreHorseScene();
        RestoreOotItemButtonsFromSave();
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
    StoreOotItemButtons();
}

void OotAfterCopy(int32_t from, int32_t to) {
    DispatchOotSaveHook(GetOotHookPoints().saveCopied, to, from);
}

} // namespace ShipLuaHost
