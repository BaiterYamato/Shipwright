// Liga linkspan.oot.items/actors (OotNativeItems.cpp) ao jogo: ícones em gItemIcons, botões C,
// uso do item pelos VB do Player, entradas do ActorDB e botões gravados pelo nome no save.
#include "OotNativeItems.h"

#include <cstdarg>
#include <spdlog/spdlog.h>

#include "OotNativeSave.h"
#include "OotNativeView.h"
#include "soh/ActorDB.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/item-tables/ItemTableTypes.h"
#include "soh/ResourceManagerHelpers.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace ShipLuaHost {
GetItemEntry BuildOotSyntheticGetItemEntry(uint8_t item);
}

namespace {

constexpr const char* kButtonsBlock = "linkspan.items";
constexpr uint32_t kButtonsVersion = 1;
constexpr size_t kOtrPrefixLength = 7; // "__OTR__"
// modIndex dos GetItemEntry de itens sintéticos ("LS"); MOD_NONE e MOD_RANDOMIZER são 0 e 1.
constexpr uint16_t kGetItemModIndex = 0x4C53;
// Texto da caixa de get-item, montado pelo OnOpenText; fora das tabelas vanilla (até 0x70FF).
constexpr uint16_t kGetItemTextId = 0x7F00;

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

// CustomDrawFunc do GetItemEntry: a matriz já está acima do Link, com escala 0.2 e giro.
void DrawGetItemModel(PlayState* play, GetItemEntry* entry) {
    const auto* record = ShipLuaHost::FindOotItem(static_cast<uint8_t>(entry->itemId));
    if (!record || !record->hasGetItem || !ResourceMgr_FileExists(record->modelPath + kOtrPrefixLength)) {
        return;
    }
    Matrix_Scale(record->modelScale, record->modelScale, record->modelScale, MTXMODE_APPLY);
    auto* dlist = reinterpret_cast<Gfx*>(const_cast<char*>(record->modelPath));
    if (record->modelLayer == LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT) {
        Gfx_DrawDListXlu(play, dlist);
    } else {
        Gfx_DrawDListOpa(play, dlist);
    }
}

ShipNativeStatus ItemsGiveItem(uint8_t item) {
    // A cena de abertura do título também é uma PlayState com Player; lá o item nunca chegaria.
    if (!gPlayState || !GET_PLAYER(gPlayState) || gSaveContext.gameMode != GAMEMODE_NORMAL ||
        gPlayState->csCtx.state != CS_STATE_IDLE) {
        return SHIP_NATIVE_LIMIT;
    }
    return GiveItemEntryWithoutActor(gPlayState, ShipLuaHost::BuildOotSyntheticGetItemEntry(item))
               ? SHIP_NATIVE_OK
               : SHIP_NATIVE_LIMIT;
}

void BuildGetItemMessage(uint16_t*, bool* loadFromMessageTable) {
    Player* player = gPlayState ? GET_PLAYER(gPlayState) : nullptr;
    if (!player || player->getItemEntry.modIndex != kGetItemModIndex) {
        return;
    }
    const auto* record = ShipLuaHost::FindOotItem(static_cast<uint8_t>(player->getItemEntry.itemId));
    if (!record || !record->hasGetItem) {
        return;
    }
    CustomMessage message(record->message, record->message, record->message);
    message.AutoFormat();
    message.LoadIntoFont();
    *loadFromMessageTable = false;
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

// Get-item de um item sintético: give_item e as checks do randomizer (OotNativeRandoGame.cpp).
GetItemEntry BuildOotSyntheticGetItemEntry(uint8_t item) {
    GetItemEntry entry = GET_ITEM(item, OBJECT_GI_HEART, 0, kGetItemTextId, 0x80, CHEST_ANIM_LONG,
                                  ITEM_CATEGORY_MAJOR, kGetItemModIndex, GI_HEART_PIECE);
    entry.drawFunc = DrawGetItemModel;
    return entry;
}

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
    bridge.giveItem = ItemsGiveItem;
    bridge.getLanguage = [] { return static_cast<uint8_t>(gSaveContext.language); };
    SetOotItemsBridge(bridge);

    GameInteractor::Instance->RegisterGameHookForID<GameInteractor::OnOpenText>(kGetItemTextId,
                                                                                BuildGetItemMessage);
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

// Chamadas por z_player.c, na thread do jogo.
extern "C" s32 LinkSpan_IsSyntheticGetItem(u16 modIndex) {
    return modIndex == kGetItemModIndex ? 1 : 0;
}

// Interface_DrawAmmoCount: número de um item sintético com set_item_ammo.
extern "C" s32 LinkSpan_GetSyntheticItemAmmo(s32 item, s16* ammo, s32* full) {
    const auto* record = item >= 0 && item <= 0xFF ? ShipLuaHost::FindOotItem(static_cast<uint8_t>(item)) : nullptr;
    if (!record || record->ammo == LINKSPAN_OOT_ITEMS_NO_AMMO) {
        return 0;
    }
    *ammo = static_cast<s16>(record->ammo);
    *full = record->ammoFull && record->ammo >= record->ammoFull ? 1 : 0;
    return 1;
}

// Caixa de texto do get-item aberta: entrega ao mod. Depois, como o Item_Give, avisa o OnItemReceive: é por ele que
// a fila do randomizer dá a check como entregue (sem isso ela entrega de novo a cada frame livre).
extern "C" void LinkSpan_ReceiveSyntheticItem(PlayState*, u16 item) {
    const auto* record = ShipLuaHost::FindOotItem(static_cast<uint8_t>(item));
    if (record && record->receive) {
        const auto receive = record->receive;
        const auto user = record->receiveUser;
        receive(user, static_cast<uint8_t>(item));
    }
    GameInteractor_ExecuteOnItemReceiveHooks(ShipLuaHost::BuildOotSyntheticGetItemEntry(static_cast<uint8_t>(item)));
}
