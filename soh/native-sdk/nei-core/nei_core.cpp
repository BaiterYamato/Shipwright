// Coremod linkspan.nei (NEI-002): publica linkspan.nei.items v1 sobre linkspan.oot.items v3 e
// linkspan.oot.save. O estado dos itens vai no bloco de save "nei.items"; os hooks de save trazem e
// gravam o bloco. Tudo roda na thread do jogo; chamada de outra thread = INVALID_ARGUMENT.
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <charconv>
#include "fork/backend_glue.h"
#include "fork/backend_unit.h"
#include <thread>

#include "assets.h"
#include "fork/fork_glue.h"
#include "fork/kaleido_glue.h"
#include "fork/progression.h"

// fork/pipeline_probe.c: estado do pipeline de Player do fork, lido no frame corrente (NEI-005).
extern "C" uint32_t NeiPipeline_Describe(char* out, uint32_t capacity);
extern "C" int NeiActor_Stats(char* out, int capacity);
#include "fork_save.h"
#include "include/linkspan/nei/nei_items.h"
#include "oot_anchor.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_layout_id.h"
#include "registry.h"
#include "text.h"

namespace {

struct Core {
    LinkSpanNei::Registry registry;
    LinkSpanNei::ForkSave forkSave;
    std::thread::id owner;
    const ShipOotEngineV1* engine = nullptr;
    const ShipOotSaveV1* save = nullptr;
    // Arquivo cujo estado está na memória. Um arquivo novo não passa por oot.save.loaded: o jogo chama
    // Save_InitFile e grava, então sem isto o primeiro save do arquivo novo levaria os itens do anterior.
    int32_t loadedSlot = -1;
    // Anchor (NEI-014): opcional. Com ele os blocos "nei.items" e "nei.state" entram no estado do time.
    const ShipOotAnchorV1* anchor = nullptr;
    bool anchorShared = false;
    uint32_t teamLoads = 0;
};

// A tabela do serviço não leva contexto: uma instância do coremod por processo.
Core* gCore = nullptr;

LinkSpanNei::Registry* Owned() {
    return gCore && gCore->owner == std::this_thread::get_id() ? &gCore->registry : nullptr;
}

// Outros mods chamam a tabela direto, sem o Invoke do host no meio: nenhuma exceção atravessa a fronteira C.
#define NEI_FORWARD(call)                                        \
    auto* registry = Owned();                                    \
    if (!registry) {                                             \
        return SHIP_NATIVE_INVALID_ARGUMENT;                     \
    }                                                            \
    try {                                                        \
        return registry->call;                                   \
    } catch (...) {                                              \
        return SHIP_NATIVE_FAILURE;                              \
    }

ShipNativeStatus SHIP_NATIVE_CALL DefineItem(const NeiItemDefinitionV1* definition, uint64_t* item) {
    NEI_FORWARD(Define(definition, item));
}
ShipNativeStatus SHIP_NATIVE_CALL RemoveItem(uint64_t item) {
    NEI_FORWARD(Remove(item));
}
ShipNativeStatus SHIP_NATIVE_CALL FindItem(const char* id, uint64_t* item) {
    NEI_FORWARD(Find(id, item));
}
ShipNativeStatus SHIP_NATIVE_CALL ListItems(NeiItemVisitFn visit, void* user) {
    NEI_FORWARD(List(visit, user));
}
ShipNativeStatus SHIP_NATIVE_CALL GetState(uint64_t item, NeiItemStateV1* state) {
    NEI_FORWARD(GetState(item, state));
}
ShipNativeStatus SHIP_NATIVE_CALL GiveItem(uint64_t item) {
    NEI_FORWARD(Give(item));
}
ShipNativeStatus SHIP_NATIVE_CALL GrantItem(uint64_t item) {
    NEI_FORWARD(Grant(item));
}
ShipNativeStatus SHIP_NATIVE_CALL RevokeItem(uint64_t item) {
    NEI_FORWARD(Revoke(item));
}
ShipNativeStatus SHIP_NATIVE_CALL SetCount(uint64_t item, uint16_t count) {
    NEI_FORWARD(SetCount(item, count));
}
ShipNativeStatus SHIP_NATIVE_CALL AddCount(uint64_t item, int32_t delta, uint16_t* result) {
    NEI_FORWARD(AddCount(item, delta, result));
}
ShipNativeStatus SHIP_NATIVE_CALL SetLevel(uint64_t item, uint8_t level) {
    NEI_FORWARD(SetLevel(item, level));
}
ShipNativeStatus SHIP_NATIVE_CALL Equip(uint64_t item, uint8_t button) {
    NEI_FORWARD(Equip(item, button));
}
ShipNativeStatus SHIP_NATIVE_CALL Unequip(uint64_t item) {
    NEI_FORWARD(Unequip(item));
}
ShipNativeStatus SHIP_NATIVE_CALL GetName(uint64_t item, uint8_t language, char* output, uint32_t capacity,
                                          uint32_t* outputSize) {
    NEI_FORWARD(GetName(item, language, output, capacity, outputSize));
}

#undef NEI_FORWARD

const NeiItemsV1 kService{ sizeof(NeiItemsV1), DefineItem, RemoveItem, FindItem,  ListItems,
                           GetState,           GiveItem,   GrantItem,  RevokeItem, SetCount,
                           AddCount,           SetLevel,   Equip,      Unequip,   GetName };

// O arquivo do payload, ou -1 quando o host não mandou um (ponto antigo ou payload curto).
int32_t HookSlot(const ShipNativeHookCall* call) {
    if (!call || !call->payload || call->payload_size < sizeof(ShipOotSaveHookV1)) {
        return -1;
    }
    const auto* save = static_cast<const ShipOotSaveHookV1*>(call->payload);
    return save->size >= sizeof(ShipOotSaveHookV1) ? save->slot : -1;
}

ShipNativeStatus SHIP_NATIVE_CALL OnLoaded(void*, const ShipNativeHookCall* call) {
    if (gCore && gCore->owner == std::this_thread::get_id()) {
        try {
            gCore->forkSave.OnSaveLoaded();
            gCore->registry.OnSaveLoaded();
            gCore->loadedSlot = HookSlot(call);
        } catch (...) {
            return SHIP_NATIVE_FAILURE;
        }
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnSaving(void*, const ShipNativeHookCall* call) {
    if (gCore && gCore->owner == std::this_thread::get_id()) {
        const int32_t slot = HookSlot(call);
        // Arquivo que não foi carregado nesta sessão (novo, apagado ou sobrescrito por cópia) começa do zero.
        if (slot != gCore->loadedSlot) {
            try {
                gCore->forkSave.ResetForNewSlot();
                gCore->registry.ResetForNewSlot();
            } catch (...) {
                return SHIP_NATIVE_FAILURE;
            }
            gCore->loadedSlot = slot;
        }
        gCore->registry.Flush();
        // Sem escape hatch o fork não roda: não regravar seu bloco preserva o save já existente.
        if (LinkSpanNei::ForkActive()) {
            gCore->forkSave.OnSaving();
        }
    }
    return SHIP_NATIVE_OK;
}

// O estado do time do Anchor substituiu os blocos: relê como num load, sem mexer no vínculo do arquivo.
ShipNativeStatus SHIP_NATIVE_CALL OnAnchorState(void*, const ShipNativeHookCall*) {
    if (gCore && gCore->owner == std::this_thread::get_id()) {
        try {
            gCore->forkSave.OnSaveLoaded();
            gCore->registry.OnSaveLoaded();
            ++gCore->teamLoads;
        } catch (...) {
            return SHIP_NATIVE_FAILURE;
        }
    }
    return SHIP_NATIVE_OK;
}

// Apagar ou sobrescrever por cópia o arquivo carregado desliga o vínculo: o próximo save dele recomeça.
ShipNativeStatus SHIP_NATIVE_CALL OnSlotReplaced(void*, const ShipNativeHookCall* call) {
    if (gCore && gCore->owner == std::this_thread::get_id() && HookSlot(call) == gCore->loadedSlot) {
        gCore->loadedSlot = -1;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Stats(void*, const char*, uint32_t length, ShipNativeWriteFn write, void* writer) {
    auto* registry = Owned();
    if (!registry || length || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        std::string text = registry->Stats() + " | fork: " + LinkSpanNei::ForkStatus() + " | " +
                           LinkSpanNei::InventoryStatus() + " | arquivo=" + std::to_string(gCore->loadedSlot) +
                           " | assets: " + LinkSpanNei::AssetsStatus();
        if (gCore->anchorShared) {
            text += " | anchor: " + std::string(gCore->anchor->connected() ? "conectado" : "desconectado") +
                    " estados=" + std::to_string(gCore->teamLoads);
        }
        // Estado do pipeline de Player (NEI-005). Só faz sentido com o fork ligado: sem escape hatch
        // as funções do fork não rodam e o Player não é o desta DLL.
        if (LinkSpanNei::ForkActive()) {
            char pipeline[192];
            const uint32_t size = NeiPipeline_Describe(pipeline, sizeof(pipeline));
            if (size) {
                text += " | " + std::string(pipeline, size);
            }
            text += " | " + LinkSpanNei::ForkLightStatus();
            char equipment[128];
            const uint32_t equipmentSize = NeiEquipment_Describe(equipment, sizeof(equipment));
            if (equipmentSize) text += " | " + std::string(equipment, equipmentSize);
            char actors[128];
            const int count = NeiActor_Stats(actors, sizeof(actors));
            if (count > 0) {
                text += " | atores: " + std::string(actors, static_cast<size_t>(count));
            }
        }
        // Bloco de versão futura: o arquivo é lido com os sentinelas e não é regravado; fica visível no stats.
        const uint32_t stored = gCore->forkSave.StoredVersion();
        if (stored > LinkSpanNei::kForkSaveVersion) {
            text += " | nei.state v" + std::to_string(stored) + " preservado (nao suportado)";
        }
        return write(writer, text.data(), static_cast<uint32_t>(text.size()));
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

// Lista para a aba Mods: id, nome, caminho do ícone e posse separados por TAB.
// O registro inclui itens definidos por add-ons; a interface não depende de IDs runtime.
ShipNativeStatus SHIP_NATIVE_CALL Catalog(void*, const char*, uint32_t length, ShipNativeWriteFn write, void* writer) {
    auto* registry = Owned();
    if (!registry || length || !write) return SHIP_NATIVE_INVALID_ARGUMENT;
    try {
        struct CatalogContext { LinkSpanNei::Registry* registry; std::string lines; } context{registry, {}};
        const auto visit = [](void* user, uint64_t item, const char* id) -> ShipNativeStatus {
            auto& catalog = *static_cast<CatalogContext*>(user);
            char name[LINKSPAN_NEI_MAX_NAME + 1]{};
            uint32_t size = 0;
            const auto status = catalog.registry->GetName(item, LINKSPAN_OOT_LANGUAGE_ENGLISH,
                                                          name, LINKSPAN_NEI_MAX_NAME, &size);
            if (status != SHIP_NATIVE_OK) return status;
            NeiItemStateV1 state{sizeof(state)};
            if (catalog.registry->GetState(item, &state) != SHIP_NATIVE_OK) return SHIP_NATIVE_FAILURE;
            catalog.lines += id;
            catalog.lines += '\t';
            catalog.lines.append(name, size);
            catalog.lines += '\t';
            catalog.lines += catalog.registry->IconPath(item);
            catalog.lines += '\t';
            catalog.lines += state.owned ? '1' : '0';
            catalog.lines += '\n';
            return SHIP_NATIVE_OK;
        };
        const auto status = registry->List(visit, &context);
        return status == SHIP_NATIVE_OK
            ? write(writer, context.lines.data(), static_cast<uint32_t>(context.lines.size())) : status;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL PowerCatalog(void*, const char*, uint32_t length,
                                             ShipNativeWriteFn write, void* writer) {
    if (!Owned() || length || !write) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!LinkSpanNei::ForkActive()) return write(writer, "", 0);
    char catalog[8192];
    return write(writer, catalog, NeiProgress_Catalog(catalog, sizeof(catalog)));
}

bool GameplayReady() {
    return LinkSpanNei::ForkActive() && gCore && gCore->engine && gCore->engine->get_player() &&
           gCore->save && gCore->save->get_slot() >= 0;
}
ShipNativeStatus SHIP_NATIVE_CALL EquipmentCatalog(void*, const char*, uint32_t length,
                                                  ShipNativeWriteFn write, void* writer) {
    if (!Owned() || length || !write) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!LinkSpanNei::ForkActive()) return write(writer,"",0);
    char value[8192]; return write(writer,value,NeiEquipment_Catalog(value,sizeof(value)));
}
ShipNativeStatus SHIP_NATIVE_CALL EquipmentToggle(void*, const char* request, uint32_t length,
                                                 ShipNativeWriteFn write, void* writer) {
    if (!Owned() || !request || !write || length!=3 || request[1]!='.' ||
        request[0]<'0' || request[0]>'3' || request[2]<'1' || request[2]>'3') return SHIP_NATIVE_INVALID_ARGUMENT;
    const char* message="Open a save file before changing equipment.";
    if (GameplayReady()) message=NeiEquipment_Toggle(request[0]-'0',request[2]-'0') ?
        "Equipment updated. Use L on the inventory Equipment page to select owned equipment." : "Equipment unavailable.";
    return write(writer,message,(uint32_t)std::strlen(message));
}
ShipNativeStatus SHIP_NATIVE_CALL BottlesStatus(void*, const char*, uint32_t length,
                                               ShipNativeWriteFn write, void* writer) {
    if (!Owned() || length || !write) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!LinkSpanNei::ForkActive()) return write(writer,"",0);
    char value[192]; return write(writer,value,NeiBottles_Status(value,sizeof(value)));
}
ShipNativeStatus SHIP_NATIVE_CALL BottlesAction(void*, const char* request, uint32_t length,
                                               ShipNativeWriteFn write, void* writer) {
    if (!Owned() || !request || !write) return SHIP_NATIVE_INVALID_ARGUMENT;
    const bool add=length==3 && !std::memcmp(request,"add",3);
    const bool toggle=length==10 && !std::memcmp(request,"bottomless",10);
    if (!add && !toggle) return SHIP_NATIVE_INVALID_ARGUMENT;
    const char* message="Open a save file before changing bottles.";
    if (GameplayReady()) {
        const bool ok=add?NeiBottles_Add()!=0:NeiBottles_ToggleBottomless()!=0;
        message=ok ? (add?"Bottle added to the inventory wheel.":"Bottomless Bottle updated in bottle slot 4.") :
            "No free bottle slot. Existing contents were preserved.";
    }
    return write(writer,message,(uint32_t)std::strlen(message));
}

ShipNativeStatus SHIP_NATIVE_CALL SensorCatalog(void*, const char*, uint32_t length,
                                               ShipNativeWriteFn write, void* writer) {
    if (!Owned() || length || !write) return SHIP_NATIVE_INVALID_ARGUMENT;
    const auto value = LinkSpanNei::ForkActive() ? LinkSpanNei::SensorCatalog() : std::string{};
    return write(writer, value.data(), (uint32_t)value.size());
}
ShipNativeStatus SHIP_NATIVE_CALL SensorWishes(void*, const char*, uint32_t length,
                                              ShipNativeWriteFn write, void* writer) {
    if (!Owned() || length || !write) return SHIP_NATIVE_INVALID_ARGUMENT;
    const auto value = LinkSpanNei::ForkActive() ? LinkSpanNei::SensorWishes() : std::string{};
    return write(writer, value.data(), (uint32_t)value.size());
}
ShipNativeStatus SHIP_NATIVE_CALL SensorSelect(void*, const char* request, uint32_t length,
                                              ShipNativeWriteFn write, void* writer) {
    if (!Owned() || !request || !write || length < 3 || length > 12 || request[0] < '0' ||
        request[0] > '4' || request[1] != ':' || !LinkSpanNei::ForkActive()) return SHIP_NATIVE_INVALID_ARGUMENT;
    unsigned item = 0;
    const auto parsed = std::from_chars(request + 2, request + length, item);
    if (parsed.ec != std::errc{} || parsed.ptr != request + length ||
        !LinkSpanNei::SetSensorWish(request[0] - '0', item)) return SHIP_NATIVE_INVALID_ARGUMENT;
    constexpr const char* value = "Sensor wish list updated.";
    return write(writer, value, (uint32_t)std::strlen(value));
}

ShipNativeStatus SHIP_NATIVE_CALL PowerToggle(void*, const char* request, uint32_t length,
                                            ShipNativeWriteFn write, void* writer) {
    auto* registry = Owned();
    if (!registry || !request || !write || length != 3 || request[1] != '.' ||
        request[0] < '0' || request[0] > '3' || request[2] < '0' || request[2] > '5')
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!LinkSpanNei::ForkActive() || !gCore->engine || !gCore->engine->get_player() ||
        !gCore->save || gCore->save->get_slot() < 0) {
        constexpr const char* message = "Open a save file before changing abilities.";
        return write(writer, message, (uint32_t)std::strlen(message));
    }
    try {
        const unsigned family = request[0] - '0', index = request[2] - '0';
        const int mask = NeiProgress_NextMask(family, index, NeiProgress_GetMask(family));
        if (mask < 0) return SHIP_NATIVE_INVALID_ARGUMENT;
        uint64_t item = 0;
        if (registry->Find(NeiProgress_ItemId(family), &item) != SHIP_NATIVE_OK) return SHIP_NATIVE_UNSUPPORTED;
        const auto status = mask ? registry->Grant(item) : registry->Revoke(item);
        if (status != SHIP_NATIVE_OK) return status;
        NeiProgress_SetMask(family, (uint8_t)mask);
        LinkSpanNei::RefreshForkInventory();
        constexpr const char* message = "Ability updated. Equip the item from the inventory.";
        return write(writer, message, (uint32_t)std::strlen(message));
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL TestToggle(void*, const char* request, uint32_t length,
                                            ShipNativeWriteFn write, void* writer) {
    auto* registry = Owned();
    if (!registry || !request || !write || !length || length > LINKSPAN_NEI_MAX_ID ||
        std::memchr(request, 0, length)) return SHIP_NATIVE_INVALID_ARGUMENT;
    try {
        if (!gCore->engine || !gCore->engine->get_player() || !gCore->save || gCore->save->get_slot() < 0) {
            constexpr const char* message = "Open a save file before changing items.";
            return write(writer, message, static_cast<uint32_t>(std::strlen(message)));
        }
        const std::string id(request, length);
        uint64_t item = 0;
        if (registry->Find(id.c_str(), &item) != SHIP_NATIVE_OK) {
            constexpr const char* message = "Item unavailable in this session.";
            return write(writer, message, static_cast<uint32_t>(std::strlen(message)));
        }
        NeiItemStateV1 state{sizeof(state)};
        if (registry->GetState(item, &state) != SHIP_NATIVE_OK) return SHIP_NATIVE_FAILURE;
        const auto status = state.owned ? registry->Revoke(item) : registry->Grant(item);
        if (status != SHIP_NATIVE_OK) return status;
        LinkSpanNei::RefreshForkInventory();
        const char* message = state.owned ? "Item disabled and removed from the inventory."
                                          : "Item enabled in the inventory.";
        return write(writer, message, static_cast<uint32_t>(std::strlen(message)));
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

// Concessão instantânea: o menu pode estar aberto no pause. Só modifica um arquivo em gameplay.
ShipNativeStatus SHIP_NATIVE_CALL TestGive(void*, const char* request, uint32_t length,
                                         ShipNativeWriteFn write, void* writer) {
    auto* registry = Owned();
    if (!registry || !request || !write || !length || length > LINKSPAN_NEI_MAX_ID ||
        std::memchr(request, 0, length)) return SHIP_NATIVE_INVALID_ARGUMENT;
    try {
        if (!gCore->engine || !gCore->engine->get_player() || !gCore->save || gCore->save->get_slot() < 0) {
            constexpr const char* message = "Open a save file before granting items.";
            return write(writer, message, static_cast<uint32_t>(std::strlen(message)));
        }
        const std::string id(request, length);
        uint64_t item = 0;
        if (registry->Find(id.c_str(), &item) != SHIP_NATIVE_OK) {
            constexpr const char* message = "Item unavailable in this session.";
            return write(writer, message, static_cast<uint32_t>(std::strlen(message)));
        }
        const auto status = registry->Grant(item);
        if (status != SHIP_NATIVE_OK) return status;
        constexpr const char* message = "Item granted. Close the menu to refresh the inventory.";
        return write(writer, message, static_cast<uint32_t>(std::strlen(message)));
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus Observe(const ShipNativeRuntime* runtime, const char* point, ShipNativeHookFn callback) {
    const ShipNativeHookSpec spec{ sizeof(ShipNativeHookSpec), point, LINKSPAN_OOT_HOOKS_VERSION,
                                   sizeof(ShipOotSaveHookV1), SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_BEFORE,
                                   0, callback, nullptr };
    uint64_t handle = 0;
    return runtime->register_hook(runtime->context, &spec, &handle);
}

void Release() {
    if (gCore) {
        LinkSpanNei::StopFork();
        LinkSpanNei::UnmountAssets();
        gCore->registry.Detach();
        gCore->forkSave.Detach();
        LinkSpanNei::BindText(nullptr);
        delete gCore;
        gCore = nullptr;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || !instance || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_minor < 2 ||
        !runtime->get_service || !runtime->register_service || !runtime->register_function ||
        !runtime->register_hook || gCore) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    const auto* items = static_cast<const ShipOotItemsV3*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ITEMS_SERVICE, LINKSPAN_OOT_ITEMS_VERSION_3, sizeof(ShipOotItemsV3)));
    const auto* save = static_cast<const ShipOotSaveV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_SAVE_SERVICE, LINKSPAN_OOT_SAVE_VERSION, sizeof(ShipOotSaveV1)));
    if (!engine || !engine->layout_id || std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) || !items || !save) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    uint64_t saveHandle = 0;
    ShipNativeStatus status = save->open_namespace(LinkSpanNei::kSaveNamespace, LinkSpanNei::kSaveVersion,
                                                   &saveHandle);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    uint64_t forkSaveHandle = 0;
    status = save->open_namespace(LinkSpanNei::kForkSaveNamespace, LinkSpanNei::kForkSaveVersion, &forkSaveHandle);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    gCore = new (std::nothrow) Core;
    if (!gCore) {
        return SHIP_NATIVE_FAILURE;
    }
    gCore->owner = std::this_thread::get_id();
    gCore->engine = engine;
    gCore->save = save;
    // Opcional: sem ele os textos próprios do fork (Time Gate, Lantern, Pictobox) ficam sem caixa.
    LinkSpanNei::BindText(static_cast<const ShipOotTextV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_TEXT_SERVICE, LINKSPAN_OOT_TEXT_VERSION, sizeof(ShipOotTextV1))));
    gCore->registry.Attach(items, save, saveHandle);
    gCore->forkSave.Attach(save, forkSaveHandle);
    // Host sem Anchor (ou mais antigo): os itens seguem só locais. Os dois blocos vão juntos, como o gSaveContext
    // inteiro que o Anchor sincroniza: o do fork também guarda posse (ownedItems, upgrades).
    gCore->anchor = static_cast<const ShipOotAnchorV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ANCHOR_SERVICE, LINKSPAN_OOT_ANCHOR_VERSION, sizeof(ShipOotAnchorV1)));
    gCore->anchorShared = gCore->anchor && gCore->anchor->share_namespace(saveHandle) == SHIP_NATIVE_OK &&
                          gCore->anchor->share_namespace(forkSaveHandle) == SHIP_NATIVE_OK;
    status = Observe(runtime, LINKSPAN_OOT_HOOK_SAVE_LOADED, OnLoaded);
    if (status == SHIP_NATIVE_OK) {
        status = Observe(runtime, LINKSPAN_OOT_HOOK_SAVE_SAVING, OnSaving);
    }
    if (status == SHIP_NATIVE_OK) {
        status = Observe(runtime, LINKSPAN_OOT_HOOK_SAVE_DELETED, OnSlotReplaced);
    }
    if (status == SHIP_NATIVE_OK) {
        status = Observe(runtime, LINKSPAN_OOT_HOOK_SAVE_COPIED, OnSlotReplaced);
    }
    if (status == SHIP_NATIVE_OK && gCore->anchorShared) {
        status = Observe(runtime, LINKSPAN_OOT_HOOK_ANCHOR_STATE, OnAnchorState);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "stats", Stats, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "catalog", Catalog, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "test_give", TestGive, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "test_toggle", TestToggle, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "power_catalog", PowerCatalog, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "power_toggle", PowerToggle, nullptr);
    }
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "sensor_catalog", SensorCatalog, nullptr);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "sensor_wishes", SensorWishes, nullptr);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "sensor_select", SensorSelect, nullptr);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "equipment_catalog", EquipmentCatalog, nullptr);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "equipment_toggle", EquipmentToggle, nullptr);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "bottles_status", BottlesStatus, nullptr);
    if (status == SHIP_NATIVE_OK) status = runtime->register_function(runtime->context, "bottles_action", BottlesAction, nullptr);
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_service(runtime->context, LINKSPAN_NEI_ITEMS_SERVICE, LINKSPAN_NEI_ITEMS_VERSION,
                                           sizeof(NeiItemsV1), &kService);
    }
    if (status != SHIP_NATIVE_OK) {
        Release();
        return status;
    }
    // Antes do fork: os itens que desenham modelo do fork só são definidos com o núcleo dos assets montado.
    LinkSpanNei::MountAssets(runtime);
    // O código de itens do fork NEI é opcional: sem escape hatch para este soh.exe o registro segue sem ele.
    LinkSpanNei::StartFork(runtime, &gCore->registry);
    *instance = gCore;
    return SHIP_NATIVE_OK;
}

// Os mods de conteúdo saem antes do coremod e removem seus itens; o que sobrar sai aqui.
void SHIP_NATIVE_CALL Shutdown(void*) {
    Release();
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{ sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 3u, Init,
                                                  Shutdown };
    return &descriptor;
}
