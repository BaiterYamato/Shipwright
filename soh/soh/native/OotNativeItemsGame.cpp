// Liga linkspan.oot.items/actors (OotNativeItems.cpp) ao jogo: ícones em gItemIcons, botões C,
// uso do item pelos VB do Player, entradas do ActorDB e botões gravados pelo nome no save.
#include "OotNativeItems.h"

#include <cstdarg>
#include <spdlog/spdlog.h>

#include "OotNativeSave.h"
#include "OotNativeView.h"
#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ResourceManagerHelpers.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

constexpr const char* kButtonsBlock = "linkspan.items";
constexpr uint32_t kButtonsVersion = 1;
constexpr size_t kOtrPrefixLength = 7; // "__OTR__"

void ClearEquips(ItemEquips& equips, uint8_t item, bool registeredOnly) {
    for (size_t button = 1; button < ARRAY_COUNT(equips.buttonItems); ++button) {
        const uint8_t current = equips.buttonItems[button];
        const bool drop = registeredOnly ? (ShipLuaHost::IsOotSyntheticItemId(current) && !ShipLuaHost::FindOotItem(current))
                                         : current == item;
        if (drop) {
            equips.buttonItems[button] = ITEM_NONE;
            if (button <= ARRAY_COUNT(equips.cButtonSlots)) {
                equips.cButtonSlots[button - 1] = SLOT_NONE;
            }
        }
    }
}

// Um id sem ícone não pode ficar em botão nenhum: o HUD desenharia uma textura nula.
void SetItemVisual(uint8_t item, const char* icon, uint8_t age) {
    gItemIcons[item] = const_cast<char*>(icon);
    gItemAgeReqs[item] = age;
    if (!icon) {
        ClearEquips(gSaveContext.equips, item, false);
        ClearEquips(gSaveContext.childEquips, item, false);
        ClearEquips(gSaveContext.adultEquips, item, false);
    }
}

uint8_t ItemsGetButton(uint8_t button) {
    return gSaveContext.equips.buttonItems[button];
}

void ItemsSetButton(uint8_t button, uint8_t item) {
    gSaveContext.equips.buttonItems[button] = item;
    gSaveContext.equips.cButtonSlots[button - 1] = SLOT_NONE;
    if (gPlayState && item != ITEM_NONE) {
        Interface_LoadItemIcon1(gPlayState, button);
    }
}

void SHIP_NATIVE_CALL NoActorFn(void*, void*, void*) {
}

struct CallbackScope {
    CallbackScope() {
        ShipLuaHost::EnterOotActorCallback();
    }
    ~CallbackScope() {
        ShipLuaHost::LeaveOotActorCallback();
    }
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;
};

void LinkSpanActorInit(Actor* actor, PlayState* play) {
    const auto* type = ShipLuaHost::FindOotActorType(actor->id);
    if (!type || !type->active) {
        Actor_Kill(actor);
        return;
    }
    const auto spec = type->spec;
    CallbackScope scope;
    (spec.init ? spec.init : NoActorFn)(spec.user, actor, play);
}

// Ator morto por unregister_actor_type chega aqui depois do mod ter saído.
void LinkSpanActorDestroy(Actor* actor, PlayState* play) {
    const auto* type = ShipLuaHost::FindOotActorType(actor->id);
    if (type && type->active && type->spec.destroy) {
        const auto spec = type->spec;
        CallbackScope scope;
        spec.destroy(spec.user, actor, play);
    }
}

void LinkSpanActorUpdate(Actor* actor, PlayState* play) {
    const auto* type = ShipLuaHost::FindOotActorType(actor->id);
    if (!type || !type->active) {
        Actor_Kill(actor);
        return;
    }
    const auto spec = type->spec;
    CallbackScope scope;
    (spec.update ? spec.update : NoActorFn)(spec.user, actor, play);
}

void LinkSpanActorDraw(Actor* actor, PlayState* play) {
    const auto* type = ShipLuaHost::FindOotActorType(actor->id);
    if (!type || !type->active || !type->spec.draw) {
        return;
    }
    const auto spec = type->spec;
    CallbackScope scope;
    ShipLuaHost::SetOotActorDrawActive(true);
    ShipLuaHost::EnterOotRenderScope();
    spec.draw(spec.user, actor, play);
    ShipLuaHost::LeaveOotRenderScope();
    ShipLuaHost::SetOotActorDrawActive(false);
}

int32_t AddActorType(const char* name, const ShipOotActorTypeSpecV1& spec) {
    if (!ActorDB::Instance || spec.category >= ACTORCAT_MAX || spec.instance_size < sizeof(Actor)) {
        return -1;
    }
    int id = ActorDB::Instance->RetrieveId(name);
    if (id >= 0) {
        auto& entry = ActorDB::Instance->RetrieveEntry(id).entry;
        if (entry.init != static_cast<ActorFunc>(LinkSpanActorInit)) {
            return -1; // nome de um ator que não é de mod
        }
        entry.category = spec.category;
        entry.flags = spec.flags;
        entry.objectId = spec.object_id;
        entry.instanceSize = spec.instance_size;
        return id;
    }
    ActorDBInit init;
    init.name = name;
    init.desc = "Link-Span";
    init.category = spec.category;
    init.flags = spec.flags;
    init.objectId = spec.object_id;
    init.instanceSize = spec.instance_size;
    init.init = LinkSpanActorInit;
    init.destroy = LinkSpanActorDestroy;
    init.update = LinkSpanActorUpdate;
    init.draw = LinkSpanActorDraw;
    return ActorDB::Instance->AddEntry(init).entry.id;
}

void KillActors(int16_t actorId, uint8_t category) {
    if (!gPlayState || category >= ACTORCAT_MAX) {
        return;
    }
    for (Actor* actor = gPlayState->actorCtx.actorLists[category].head; actor; actor = actor->next) {
        if (actor->id == actorId) {
            Actor_Kill(actor);
        }
    }
}

ShipNativeStatus ItemsDrawDisplayList(void* play, const char* path, uint8_t translucent) {
    if (!ResourceMgr_FileExists(path + kOtrPrefixLength)) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* state = static_cast<PlayState*>(play);
    auto* dlist = reinterpret_cast<Gfx*>(const_cast<char*>(path));
    if (translucent) {
        Gfx_DrawDListXlu(state, dlist);
    } else {
        Gfx_DrawDListOpa(state, dlist);
    }
    return SHIP_NATIVE_OK;
}

s32 VaItem(va_list original) {
    va_list args;
    va_copy(args, original);
    const s32 item = va_arg(args, s32);
    va_end(args);
    return item;
}

} // namespace

namespace ShipLuaHost {

void RegisterOotItemGameHooks() {
    static bool registered = false;
    if (registered || !GameInteractor::Instance) {
        return;
    }
    registered = true;
    // O binding vale antes de qualquer mod registrar itens ou atores.
    OotItemsBridge bridge;
    bridge.setItemVisual = SetItemVisual;
    bridge.getButtonItem = ItemsGetButton;
    bridge.setButtonItem = ItemsSetButton;
    bridge.addActorType = AddActorType;
    bridge.killActors = KillActors;
    bridge.drawDisplayList = ItemsDrawDisplayList;
    SetOotItemsBridge(bridge);

    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnVanillaBehavior>(
        VB_ITEM_ACTION_BE_NONE, [](GIVanillaBehavior, bool* should, va_list original) {
            if (IsOotSyntheticItemId(VaItem(original))) {
                *should = true;
            }
        });
    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnVanillaBehavior>(
        VB_CHANGE_HELD_ITEM_AND_USE_ITEM, [](GIVanillaBehavior, bool* should, va_list original) {
            const s32 item = VaItem(original);
            if (!IsOotSyntheticItemId(item)) {
                return;
            }
            *should = false;
            const auto* record = FindOotItem(static_cast<uint8_t>(item));
            if (!record || !record->use ||
                (record->age != LINKSPAN_OOT_ITEM_AGE_ANY && record->age != gSaveContext.linkAge)) {
                return;
            }
            uint8_t button = 0xFF;
            for (uint8_t b = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; b <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT; ++b) {
                if (gSaveContext.equips.buttonItems[b] == item) {
                    button = b;
                    break;
                }
            }
            const auto use = record->use;
            const auto user = record->user;
            use(user, static_cast<uint8_t>(item), button);
        });
}

// Na thread do jogo, antes da seção base ir para o arquivo.
void StoreOotItemButtons() {
    const auto buttons = ExportOotItemButtons();
    nlohmann::json existing;
    uint32_t version = 0;
    if (buttons.empty() && !GetOotHostSaveBlock(kButtonsBlock, existing, version)) {
        return;
    }
    nlohmann::json data = nlohmann::json::object();
    for (const auto& [button, name] : buttons) {
        data[std::to_string(button)] = name;
    }
    SetOotHostSaveBlock(kButtonsBlock, kButtonsVersion, nlohmann::json{ { "buttons", std::move(data) } });
}

// Depois de carregar o arquivo inteiro: nomes voltam a ids; id sintético sem registro sai.
void RestoreOotItemButtonsFromSave() {
    std::map<uint8_t, std::string> buttons;
    nlohmann::json data;
    uint32_t version = 0;
    if (GetOotHostSaveBlock(kButtonsBlock, data, version) && version == kButtonsVersion && data.is_object() &&
        data.contains("buttons") && data["buttons"].is_object()) {
        for (const auto& [key, value] : data["buttons"].items()) {
            if (!value.is_string() || key.size() != 1 || key[0] < '1' || key[0] > '3') {
                continue;
            }
            buttons[static_cast<uint8_t>(key[0] - '0')] = value.get<std::string>();
        }
    }
    RestoreOotItemButtons(buttons);
    for (const auto& [button, name] : buttons) {
        if (gSaveContext.equips.buttonItems[button] == ITEM_NONE) {
            SPDLOG_WARN("Link-Span items: o botão C {} tinha '{}', que nenhum mod carregado registrou", button, name);
        }
    }
    ClearEquips(gSaveContext.childEquips, ITEM_NONE, true);
    ClearEquips(gSaveContext.adultEquips, ITEM_NONE, true);
}

} // namespace ShipLuaHost
