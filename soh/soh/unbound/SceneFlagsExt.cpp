#include "SceneFlagsExt.h"

#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "soh/SaveManager.h"
#include "soh/native/OotNativeScenes.h"

namespace {

// Bit n of the bitset is room n; bits below 32 are never used here (they live in the u32 masks).
using ExtBitset = std::vector<uint32_t>;
std::unordered_map<int32_t, ExtBitset> sExtClearFlags;     // persisted
std::unordered_map<int32_t, ExtBitset> sExtLiveClearFlags; // current play state
std::unordered_map<int32_t, ExtBitset> sExtTempClearFlags; // live-only

std::unordered_map<int32_t, ExtBitset>& ExtFlagMap(SceneFlagsExtKind kind) {
    return kind == SCENE_FLAGS_EXT_TEMP_CLEAR ? sExtTempClearFlags : sExtLiveClearFlags;
}

bool ExtBitTest(const ExtBitset& bits, int32_t bit) {
    size_t word = (size_t)bit / 32;
    return bit >= 0 && word < bits.size() && (bits[word] & (1u << (bit % 32)));
}

void ExtBitWrite(ExtBitset& bits, int32_t bit, bool value) {
    if (bit < 0) {
        return;
    }
    size_t word = (size_t)bit / 32;
    if (word >= bits.size()) {
        if (!value) {
            return;
        }
        bits.resize(word + 1, 0);
    }
    if (value) {
        bits[word] |= (1u << (bit % 32));
    } else {
        bits[word] &= ~(1u << (bit % 32));
    }
}

// Rooms >= 32: one word array per scene, keyed by the stable scene name so it survives id reassignment.
void SaveSection(SaveContext*, int, bool) {
    nlohmann::json scenes = nlohmann::json::object();
    for (const auto& [id, bits] : sExtClearFlags) {
        std::string name;
        if (!bits.empty() && ShipLuaHost::OotSceneStableName(id, name)) {
            scenes[name] = bits;
        }
    }
    SaveManager::Instance->SaveData("roomClearExt", scenes);
}

void LoadSection() {
    sExtClearFlags.clear();
    nlohmann::json scenes;
    SaveManager::Instance->LoadData("roomClearExt", scenes);
    if (!scenes.is_object()) {
        return;
    }
    for (const auto& [name, words] : scenes.items()) {
        int32_t id = 0;
        if (!words.is_array() || !ShipLuaHost::OotSceneIdFromStableName(name, id)) {
            continue;
        }
        ExtBitset bits;
        for (const auto& word : words) {
            bits.push_back(word.is_number_unsigned() ? word.get<uint32_t>() : 0);
        }
        if (!bits.empty()) {
            sExtClearFlags[id] = std::move(bits);
        }
    }
}

void InitSection(bool) {
    sExtClearFlags.clear();
    sExtLiveClearFlags.clear();
    sExtTempClearFlags.clear();
}

} // namespace

void SceneFlagsExt_RegisterSaveFunctions(SaveManager& saveManager) {
    // Called from SaveManager's constructor, so SaveManager::Instance is not set yet; use the reference.
    saveManager.AddLoadFunction("unbound", 1, LoadSection);
    saveManager.AddSaveFunction("unbound", 1, SaveSection, true, SECTION_PARENT_NONE);
    saveManager.AddInitFunction(InitSection);
}

extern "C" int32_t SceneFlagsExt_Get(int32_t sceneNum, SceneFlagsExtKind kind, int32_t bit) {
    auto& map = ExtFlagMap(kind);
    auto it = map.find(sceneNum);
    return it != map.end() && ExtBitTest(it->second, bit);
}

extern "C" void SceneFlagsExt_Set(int32_t sceneNum, SceneFlagsExtKind kind, int32_t bit) {
    ExtBitWrite(ExtFlagMap(kind)[sceneNum], bit, true);
}

extern "C" void SceneFlagsExt_Unset(int32_t sceneNum, SceneFlagsExtKind kind, int32_t bit) {
    ExtBitWrite(ExtFlagMap(kind)[sceneNum], bit, false);
}

extern "C" void SceneFlagsExt_LoadClear(int32_t sceneNum) {
    sExtLiveClearFlags.clear();
    sExtTempClearFlags.clear();
    auto it = sExtClearFlags.find(sceneNum);
    if (it != sExtClearFlags.end()) {
        sExtLiveClearFlags[sceneNum] = it->second;
    }
}

extern "C" void SceneFlagsExt_SaveClear(int32_t sceneNum) {
    auto it = sExtLiveClearFlags.find(sceneNum);
    if (it != sExtLiveClearFlags.end()) {
        sExtClearFlags[sceneNum] = it->second;
    } else {
        sExtClearFlags.erase(sceneNum);
    }
}
