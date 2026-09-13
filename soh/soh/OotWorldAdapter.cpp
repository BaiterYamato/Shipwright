#include "OotWorldAdapter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "functions.h"
#include "z64.h"
#include "variables.h"
}

extern "C" PlayState* gPlayState;

namespace ShipLuaHost {
namespace {

using ShipLua::AssetReference;
using ShipLua::ErrorCode;
using ShipLua::PortableItem;
using ShipLua::PortablePlayerState;
using ShipLua::Result;
using ShipLua::WorldId;

struct NativeAsset {
    const char* logicalId;
    const char* resourcePath;
};

constexpr std::array<NativeAsset, 3> kNativeAssets = {
    NativeAsset{ "oot.player.sword.kokiri",
                 "__OTR__objects/object_link_child/gLinkChildLeftFistAndKokiriSwordNearDL" },
    NativeAsset{ "oot.player.sword.master",
                 "__OTR__objects/object_link_boy/gLinkAdultLeftHandHoldingMasterSwordNearDL" },
    NativeAsset{ "oot.player.sword.biggoron",
                 "__OTR__objects/object_gi_longsword/gGiBiggoronSwordDL" },
};

ShipLua::WorldAssetCatalog ProbeNativeAssets() {
    ShipLua::WorldAssetCatalog assets = ShipLua::CreateDefaultWorldAssetCatalog();
    Ship::Context* context = Ship::Context::GetRawInstance();
    const auto resourceManager = context != nullptr ? context->GetResourceManager() : nullptr;
    const auto archiveManager = resourceManager != nullptr ? resourceManager->GetArchiveManager() : nullptr;

    for (const NativeAsset& native : kNativeAssets) {
        bool archiveValidated = false;
        bool bundleLoaded = false;
        if (archiveManager != nullptr && archiveManager->HasFile(native.resourcePath)) {
            const auto archive = archiveManager->GetArchiveFromFile(native.resourcePath);
            archiveValidated = archive != nullptr && archive->IsLoaded() &&
                               (!archive->HasGameVersion() ||
                                archiveManager->IsGameVersionValid(archive->GetGameVersion()));
            if (archiveValidated) {
                bundleLoaded = resourceManager->LoadResource(native.resourcePath) != nullptr;
            }
        }

        const ShipLua::WorldAssetProbe probe{
            .asset = { WorldId::Oot, native.logicalId },
            .host = WorldId::Oot,
            .factoryContract = "fast.display_list.bundle",
            .contractVersion = 1,
            .archiveValidated = archiveValidated,
            .namespaceIsolated = true,
            .bundleLoaded = bundleLoaded,
        };
        const auto recorded = assets.RecordProbe(probe);
        if (!recorded.isOk()) {
            SPDLOG_WARN("ShipLua não validou o asset OoT {}: {}", native.logicalId,
                        recorded.message);
        }
    }
    return assets;
}

constexpr std::uint16_t kOwnedSwordMask = 0x0007;

PortableItem MakeItem(std::string id, std::uint32_t quantity = 1) {
    PortableItem item;
    item.id = std::move(id);
    item.origin = WorldId::Oot;
    item.quantity = quantity;
    return item;
}

void AddSlotItem(PortablePlayerState& state, std::uint8_t slot, std::uint8_t expected,
                 const char* canonicalId, bool hasAmmo) {
    if (gSaveContext.inventory.items[slot] != expected) {
        return;
    }
    const std::uint32_t quantity = hasAmmo
                                       ? static_cast<std::uint32_t>(
                                             std::max<std::int32_t>(0, gSaveContext.inventory.ammo[slot]))
                                       : 1;
    state.items.push_back(MakeItem(canonicalId, quantity));
}

void AddSword(PortablePlayerState& state, std::uint8_t equipValue, const char* canonicalId,
              const char* assetId) {
    const std::uint16_t ownedFlag = static_cast<std::uint16_t>(1U << (equipValue - 1));
    if ((gSaveContext.inventory.equipment & ownedFlag) == 0) {
        return;
    }
    PortableItem item = MakeItem(canonicalId);
    item.equipped =
        ((gSaveContext.equips.equipment & gEquipMasks[EQUIP_TYPE_SWORD]) >>
         gEquipShifts[EQUIP_TYPE_SWORD]) == equipValue;
    item.visualAsset = AssetReference{ WorldId::Oot, assetId };
    state.items.push_back(std::move(item));
}

std::optional<std::uint16_t> ResolveEntrance(const std::string& id) {
    if (id == "oot.kakariko") {
        return ENTR_KAKARIKO_VILLAGE_FRONT_GATE;
    }
    if (id == "oot.market") {
        return ENTR_MARKET_SOUTH_EXIT;
    }
    return std::nullopt;
}

void SetSword(std::uint8_t equipValue, std::uint8_t itemId, bool equipped) {
    gSaveContext.inventory.equipment |= static_cast<std::uint16_t>(1U << (equipValue - 1));
    if (equipped) {
        gSaveContext.equips.equipment =
            static_cast<std::uint16_t>((gSaveContext.equips.equipment & gEquipNegMasks[EQUIP_TYPE_SWORD]) |
                                       (equipValue << gEquipShifts[EQUIP_TYPE_SWORD]));
        gSaveContext.equips.buttonItems[0] = itemId;
    }
}

void ApplyItem(const OotWorldAdapter::PendingItem& pending) {
    const PortableItem& item = pending.source;
    const std::uint8_t quantity = static_cast<std::uint8_t>(std::min<std::uint32_t>(item.quantity, 99));
    if (pending.targetId == "shared.bow") {
        if (((gSaveContext.inventory.upgrades & gUpgradeMasks[UPG_QUIVER]) >>
             gUpgradeShifts[UPG_QUIVER]) == 0) {
            Inventory_ChangeUpgrade(UPG_QUIVER, 1);
        }
        gSaveContext.inventory.items[SLOT_BOW] = ITEM_BOW;
        gSaveContext.inventory.ammo[SLOT_BOW] = quantity;
    } else if (pending.targetId == "shared.bombs") {
        if (((gSaveContext.inventory.upgrades & gUpgradeMasks[UPG_BOMB_BAG]) >>
             gUpgradeShifts[UPG_BOMB_BAG]) == 0) {
            Inventory_ChangeUpgrade(UPG_BOMB_BAG, 1);
        }
        gSaveContext.inventory.items[SLOT_BOMB] = ITEM_BOMB;
        gSaveContext.inventory.ammo[SLOT_BOMB] = quantity;
    } else if (pending.targetId == "shared.bombchu") {
        gSaveContext.inventory.items[SLOT_BOMBCHU] = ITEM_BOMBCHU;
        gSaveContext.inventory.ammo[SLOT_BOMBCHU] = quantity;
    } else if (pending.targetId == "shared.hookshot") {
        gSaveContext.inventory.items[SLOT_HOOKSHOT] = ITEM_HOOKSHOT;
    } else if (pending.targetId == "shared.ocarina") {
        gSaveContext.inventory.items[SLOT_OCARINA] = ITEM_OCARINA_TIME;
    } else if (pending.targetId == "oot.sword.kokiri") {
        SetSword(EQUIP_VALUE_SWORD_KOKIRI, ITEM_SWORD_KOKIRI, item.equipped);
    } else if (pending.targetId == "oot.sword.master") {
        SetSword(EQUIP_VALUE_SWORD_MASTER, ITEM_SWORD_MASTER, item.equipped);
    } else if (pending.targetId == "oot.sword.biggoron") {
        SetSword(EQUIP_VALUE_SWORD_BIGGORON, ITEM_SWORD_BGS, item.equipped);
    }
}

} // namespace

OotWorldAdapter::OotWorldAdapter(ShipLua::PortableItemCatalog catalog)
    : mCatalog(std::move(catalog)), mAssets(ProbeNativeAssets()) {}

WorldId OotWorldAdapter::Id() const noexcept {
    return WorldId::Oot;
}

Result<PortablePlayerState> OotWorldAdapter::CapturePlayerState() {
    if (gSaveContext.fileNum == 0xFF) {
        return Result<PortablePlayerState>::err(ErrorCode::InvalidState,
                                                "nenhum save OoT está carregado");
    }
    PortablePlayerState state;
    state.health = static_cast<std::uint16_t>(std::max<std::int32_t>(0, gSaveContext.health));
    state.healthCapacity =
        static_cast<std::uint16_t>(std::max<std::int32_t>(0, gSaveContext.healthCapacity));
    state.rupees = static_cast<std::uint32_t>(std::max<std::int32_t>(0, gSaveContext.rupees));

    AddSlotItem(state, SLOT_BOW, ITEM_BOW, "shared.bow", true);
    AddSlotItem(state, SLOT_BOMB, ITEM_BOMB, "shared.bombs", true);
    AddSlotItem(state, SLOT_BOMBCHU, ITEM_BOMBCHU, "shared.bombchu", true);
    if (gSaveContext.inventory.items[SLOT_HOOKSHOT] == ITEM_HOOKSHOT ||
        gSaveContext.inventory.items[SLOT_HOOKSHOT] == ITEM_LONGSHOT) {
        state.items.push_back(MakeItem("shared.hookshot"));
    }
    if (gSaveContext.inventory.items[SLOT_OCARINA] == ITEM_OCARINA_FAIRY ||
        gSaveContext.inventory.items[SLOT_OCARINA] == ITEM_OCARINA_TIME) {
        state.items.push_back(MakeItem("shared.ocarina"));
    }
    AddSword(state, EQUIP_VALUE_SWORD_KOKIRI, "oot.sword.kokiri", "oot.player.sword.kokiri");
    AddSword(state, EQUIP_VALUE_SWORD_MASTER, "oot.sword.master", "oot.player.sword.master");
    AddSword(state, EQUIP_VALUE_SWORD_BIGGORON, "oot.sword.biggoron", "oot.player.sword.biggoron");

    const Result<void> valid = ShipLua::ValidatePortablePlayerState(state);
    if (!valid.isOk()) {
        return Result<PortablePlayerState>::err(valid.code, valid.message);
    }
    return Result<PortablePlayerState>::ok(std::move(state));
}

bool OotWorldAdapter::CanResolveAsset(const AssetReference& asset) const noexcept {
    return mAssets.CanResolve(asset, WorldId::Oot);
}

Result<ShipLua::WorldImportPreview> OotWorldAdapter::PrepareImport(
    const PortablePlayerState& state, const ShipLua::WorldDestination& destination) {
    AbortImport();
    if (destination.world != WorldId::Oot || !ResolveEntrance(destination.id).has_value()) {
        return Result<ShipLua::WorldImportPreview>::err(ErrorCode::Unsupported,
                                                        "destino OoT desconhecido");
    }
    const Result<void> stateValid = ShipLua::ValidatePortablePlayerState(state);
    if (!stateValid.isOk()) {
        return Result<ShipLua::WorldImportPreview>::err(stateValid.code, stateValid.message);
    }

    PendingImport pending{ state, destination, {} };
    ShipLua::WorldImportPreview preview;
    std::uint32_t equippedSwordCount = 0;
    for (const PortableItem& item : state.items) {
        const Result<void> itemValid = mCatalog.Validate(item);
        if (!itemValid.isOk()) {
            return Result<ShipLua::WorldImportPreview>::err(itemValid.code, itemValid.message);
        }
        const auto resolution = mCatalog.ResolveForWorld(item.id, WorldId::Oot);
        if (!resolution.isOk()) {
            if (resolution.code == ErrorCode::Unsupported) {
                preview.deferredItemIds.push_back(item.id);
                continue;
            }
            return Result<ShipLua::WorldImportPreview>::err(resolution.code, resolution.message);
        }
        preview.acceptedItemIds.push_back(item.id);
        if (resolution.value->translated) {
            preview.translatedItemIds.push_back(item.id);
        }
        if (item.equipped && resolution.value->targetId.rfind("oot.sword.", 0) == 0 &&
            ++equippedSwordCount > 1) {
            return Result<ShipLua::WorldImportPreview>::err(
                ErrorCode::InvalidArgument, "estado portátil possui mais de uma espada equipada");
        }
        pending.items.push_back({ item, resolution.value->targetId });
    }
    mPending = std::move(pending);
    return Result<ShipLua::WorldImportPreview>::ok(std::move(preview));
}

Result<void> OotWorldAdapter::CommitImport() {
    if (!mPending.has_value()) {
        return Result<void>::err(ErrorCode::InvalidState, "importação OoT não foi preparada");
    }
    const std::optional<std::uint16_t> entrance = ResolveEntrance(mPending->destination.id);
    if (!entrance.has_value()) {
        return Result<void>::err(ErrorCode::InvalidState, "destino OoT pendente ficou inválido");
    }

    gSaveContext.healthCapacity = static_cast<std::int16_t>(mPending->state.healthCapacity);
    gSaveContext.health = static_cast<std::int16_t>(
        std::min(mPending->state.health, mPending->state.healthCapacity));
    gSaveContext.rupees = static_cast<std::int16_t>(std::min<std::uint32_t>(mPending->state.rupees, 9999));

    for (const std::uint8_t slot : { SLOT_BOW, SLOT_BOMB, SLOT_BOMBCHU, SLOT_HOOKSHOT, SLOT_OCARINA }) {
        gSaveContext.inventory.items[slot] = ITEM_NONE;
        gSaveContext.inventory.ammo[slot] = 0;
    }
    gSaveContext.inventory.equipment &= static_cast<std::uint16_t>(~kOwnedSwordMask);
    gSaveContext.equips.equipment &= gEquipNegMasks[EQUIP_TYPE_SWORD];
    gSaveContext.equips.buttonItems[0] = ITEM_NONE;

    for (const PendingItem& item : mPending->items) {
        ApplyItem(item);
    }

    gSaveContext.entranceIndex = *entrance;
    if (gPlayState != nullptr) {
        gPlayState->nextEntranceIndex = *entrance;
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->transitionType = TRANS_TYPE_FADE_BLACK_FAST;
    }
    mPending.reset();
    return Result<void>::ok();
}

void OotWorldAdapter::AbortImport() noexcept {
    mPending.reset();
}

} // namespace ShipLuaHost
