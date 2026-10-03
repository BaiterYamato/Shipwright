// Liga linkspan.oot.randomizer (OotNativeRando.cpp) ao randomizer do SoH. Cada posição RG_LINKSPAN_ITEM_k recebe o
// nome e o get-item sintético do item de mod que a seed pôs nela; o spoiler guarda o nome do registro e o arquivo
// guarda a lista no bloco "linkspan.randomizer".
#include "OotNativeRando.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <spdlog/spdlog.h>

#include "OotNativeItems.h"
#include "OotNativeSave.h"
#include "soh/Enhancements/randomizer/item.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/randomizer_check_objects.h"

#include "z64.h"

extern "C" {
#include "variables.h"
}

namespace ShipLuaHost {
GetItemEntry BuildOotSyntheticGetItemEntry(uint8_t item);
}

namespace {

constexpr const char* kSeedBlock = "linkspan.randomizer";
constexpr uint32_t kSeedVersion = 1;
constexpr int kSlots = RG_LINKSPAN_ITEM_63 - RG_LINKSPAN_ITEM_0 + 1;
static_assert(kSlots == LINKSPAN_OOT_RANDOMIZER_MAX_ITEMS);

// Nome gravado em itemNameToEnum por posição, para tirar na próxima seed.
std::array<std::string, kSlots> gSlotNames;

// Mesmas regras de nome do linkspan.oot.items (id namespaced, sem espaço): nenhum item vanilla do randomizer casa.
bool IsModItemName(const std::string& name) {
    if (name.empty() || name.size() > LINKSPAN_OOT_ITEMS_MAX_NAME || name.front() == '.' || name.back() == '.' ||
        name.find('.') == std::string::npos) {
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

bool FindItemByName(const std::string& name, uint8_t& id) {
    for (uint32_t item = 0; item <= 0xFF; ++item) {
        const auto* record = ShipLuaHost::FindOotItem(static_cast<uint8_t>(item));
        if (record && record->name == name && record->hasGetItem) {
            id = static_cast<uint8_t>(item);
            return true;
        }
    }
    return false;
}

// Sem nome, ou com o mod ausente, a posição é uma rupia azul.
void ApplySlot(int slot, const std::string* name) {
    const auto rg = static_cast<RandomizerGet>(RG_LINKSPAN_ITEM_0 + slot);
    auto& names = Rando::StaticData::itemNameToEnum;
    const auto previous = names.find(gSlotNames[slot]);
    if (previous != names.end() && previous->second == rg) {
        names.erase(previous);
    }
    const std::string label = name ? *name : "Link-Span Item " + std::to_string(slot);
    uint8_t id = 0;
    const bool found = name && FindItemByName(*name, id);
    Rando::Item& item = Rando::StaticData::RetrieveItem(rg);
    if (found) {
        item = Rando::Item(rg, Text{ label, label, label }, ITEMTYPE_ITEM, GI_RUPEE_BLUE, false, LOGIC_NONE, RHT_NONE,
                           id, OBJECT_GI_HEART, 0, 0, 0x80, CHEST_ANIM_LONG, ITEM_CATEGORY_MAJOR, MOD_RANDOMIZER);
        *item.GetGIEntry() = ShipLuaHost::BuildOotSyntheticGetItemEntry(id);
    } else {
        item = Rando::Item(rg, Text{ label, label, label }, ITEMTYPE_ITEM, GI_RUPEE_BLUE, false, LOGIC_NONE, RHT_NONE,
                           ITEM_RUPEE_BLUE, OBJECT_GI_RUPY, GID_RUPEE_BLUE, 0xCC, 0x01, CHEST_ANIM_SHORT,
                           ITEM_CATEGORY_JUNK, MOD_NONE);
        if (name) {
            SPDLOG_WARN("Link-Span randomizer: a seed tem '{}', que nenhum mod carregado oferece; a check dá uma "
                        "rupia azul",
                        *name);
        }
    }
    gSlotNames[slot] = name ? *name : std::string{};
    if (name) {
        names[*name] = rg;
    }
}

void ApplySeed(const std::vector<std::string>& seedNames) {
    ShipLuaHost::SetOotRandoSeedItems(seedNames);
    const auto applied = ShipLuaHost::GetOotRandoSeedItems();
    for (int slot = 0; slot < kSlots; ++slot) {
        const bool named = slot < static_cast<int>(applied.size()) && !applied[slot].empty();
        ApplySlot(slot, named ? &applied[slot] : nullptr);
    }
}

} // namespace

// Chamada por GenerateItemPool (item_pool.cpp), na thread da geração: a seed nova leva as ofertas atuais.
std::vector<std::pair<RandomizerGet, int>> LinkSpan_PrepareRandoModItems(bool enabled) {
    std::vector<std::string> seedNames;
    std::vector<std::pair<RandomizerGet, int>> pool;
    if (enabled) {
        for (const auto& offer : ShipLuaHost::GetOotRandoOffers()) {
            if (seedNames.size() >= static_cast<size_t>(kSlots)) {
                break;
            }
            pool.emplace_back(static_cast<RandomizerGet>(RG_LINKSPAN_ITEM_0 + seedNames.size()), offer.copies);
            seedNames.push_back(offer.name);
        }
    }
    ApplySeed(seedNames);
    return pool;
}

// Chamada por ParseItemLocationsJson (SeedContext.cpp) antes de resolver os nomes: os itens de mod do spoiler
// ganham posição na faixa.
void LinkSpan_ParseRandoSpoilerItems(const nlohmann::json& locations) {
    std::vector<std::string> seedNames;
    const auto add = [&](const nlohmann::json& value) {
        if (!value.is_string()) {
            return;
        }
        const auto name = value.get<std::string>();
        if (IsModItemName(name) && std::find(seedNames.begin(), seedNames.end(), name) == seedNames.end()) {
            seedNames.push_back(name);
        }
    };
    for (const auto& [location, value] : locations.items()) {
        if (value.is_object()) {
            if (value.contains("item")) {
                add(value["item"]);
            }
        } else {
            add(value);
        }
    }
    if (seedNames.size() > static_cast<size_t>(kSlots)) {
        SPDLOG_WARN("Link-Span randomizer: o spoiler tem {} itens de mod; só os {} primeiros cabem", seedNames.size(),
                    kSlots);
    }
    ApplySeed(seedNames);
}

namespace ShipLuaHost {

namespace {
const char* HintDescription(RandomizerCheckType type) {
    switch (type) {
        case RCTYPE_SKULL_TOKEN: return "guarded by a golden creature";
        case RCTYPE_COW: return "offered by a bovine friend";
        case RCTYPE_SHOP: case RCTYPE_MERCHANT: case RCTYPE_SCRUB: return "available for trade";
        case RCTYPE_BOSS_HEART_OR_OTHER_REWARD: return "held by a powerful foe";
        case RCTYPE_DUNGEON_REWARD: return "deep within this place";
        case RCTYPE_FREESTANDING: return "lying in plain sight";
        case RCTYPE_POT: return "inside a vessel";
        case RCTYPE_CRATE: case RCTYPE_NLCRATE: case RCTYPE_SMALL_CRATE: return "inside a container";
        case RCTYPE_CHEST_GAME: return "behind a game of chance";
        case RCTYPE_SONG_LOCATION: return "waiting to be learned";
        case RCTYPE_BEEHIVE: return "guarded by buzzing insects";
        case RCTYPE_GRASS: case RCTYPE_BUSH: return "hidden in the brush";
        case RCTYPE_TREE: case RCTYPE_NLTREE: return "above in the branches";
        default: return "somewhere in this area";
    }
}
template <size_t N> bool CopyHintText(char (&out)[N], const std::string& text) {
    if (text.size() >= N) return false;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return true;
}
uint32_t RandoItemCount() { return RG_MAX; }
ShipNativeStatus RandoItemInfo(uint32_t item, ShipOotRandomizerItemInfoV2* info) {
    if (item <= RG_NONE || item >= RG_MAX) return SHIP_NATIVE_INVALID_ARGUMENT;
    const auto name = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(item)).GetName().GetEnglish();
    if (name.empty()) return SHIP_NATIVE_FAILURE;
    ShipOotRandomizerItemInfoV2 result{sizeof(result), item};
    if (!CopyHintText(result.name, name)) return SHIP_NATIVE_LIMIT;
    *info = result;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus RandoFindUncollected(uint32_t item, ShipOotRandomizerHintV2* hint) {
    if (item <= RG_NONE || item >= RG_MAX) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (gSaveContext.ship.quest.id != QUEST_RANDOMIZER) return SHIP_NATIVE_UNSUPPORTED;
    auto ctx = Rando::Context::GetInstance();
    if (!ctx) return SHIP_NATIVE_UNSUPPORTED;
    auto& locations = Rando::StaticData::GetLocationTable();
    for (size_t i = 0; i < RC_MAX; ++i) {
        const auto check = static_cast<RandomizerCheck>(i);
        if (locations[check].GetRandomizerCheck() == RC_UNKNOWN_CHECK) continue;
        const auto loc = ctx->GetItemLocation(check);
        if (!loc || loc->GetPlacedRandomizerGet() != item || loc->GetCheckStatus() == RCSHOW_COLLECTED ||
            loc->GetCheckStatus() == RCSHOW_SAVED) continue;
        ShipOotRandomizerHintV2 result{sizeof(result), item, static_cast<uint32_t>(check)};
        if (!CopyHintText(result.item_name, loc->GetPlacedItemName().GetEnglish()) ||
            !CopyHintText(result.area_name, RandomizerCheckObjects::GetRCAreaName(locations[check].GetArea())) ||
            !CopyHintText(result.description, HintDescription(locations[check].GetRCType()))) return SHIP_NATIVE_LIMIT;
        *hint = result;
        return SHIP_NATIVE_OK;
    }
    return SHIP_NATIVE_FAILURE;
}
const bool kQueryBound = [] {
    BindOotRandoQueryBackend({RandoItemCount, RandoItemInfo, RandoFindUncollected});
    return true;
}();
} // namespace

// OotBeforeSave (OotNativeSaveGame.cpp), na thread do jogo.
void StoreOotRandoSeed() {
    const auto names = gSaveContext.ship.quest.id == QUEST_RANDOMIZER ? GetOotRandoSeedItems()
                                                                       : std::vector<std::string>{};
    nlohmann::json existing;
    uint32_t version = 0;
    if (names.empty() && !GetOotHostSaveBlock(kSeedBlock, existing, version)) {
        return;
    }
    SetOotHostSaveBlock(kSeedBlock, kSeedVersion, nlohmann::json{ { "items", names } });
}

// OnLoadFile, depois do LoadRandomizer: as posições salvas voltam a apontar para os itens pelo nome.
void RestoreOotRandoSeedFromSave() {
    std::vector<std::string> names;
    nlohmann::json data;
    uint32_t version = 0;
    if (gSaveContext.ship.quest.id == QUEST_RANDOMIZER && GetOotHostSaveBlock(kSeedBlock, data, version) &&
        version == kSeedVersion && data.is_object() && data.contains("items") && data["items"].is_array()) {
        for (const auto& value : data["items"]) {
            names.push_back(value.is_string() ? value.get<std::string>() : std::string{});
        }
    }
    ApplySeed(names);
}

} // namespace ShipLuaHost
