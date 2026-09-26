// Coremod linkspan.nei (NEI-002): publica linkspan.nei.items v1 sobre linkspan.oot.items v3 e
// linkspan.oot.save. O estado dos itens vai no bloco de save "nei.items"; os hooks de save trazem e
// gravam o bloco. Tudo roda na thread do jogo; chamada de outra thread = INVALID_ARGUMENT.
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <thread>

#include "assets.h"
#include "fork/fork_glue.h"
#include "fork/kaleido_glue.h"

// fork/pipeline_probe.c: estado do pipeline de Player do fork, lido no frame corrente (NEI-005).
extern "C" uint32_t NeiPipeline_Describe(char* out, uint32_t capacity);
extern "C" int NeiActor_Stats(char* out, int capacity);
extern "C" int NeiActor_TestSomaria(int spawn);
extern "C" void NeiActor_Unload(int dryRun);
extern "C" void NeiTest_GrantMagic(void);
extern "C" int NeiTest_SpawnEnemy(int kind);
extern "C" int NeiTest_EquipExt(int which);
extern "C" void WeaponUpgrade_SetRazor(unsigned char on);
extern "C" void WeaponUpgrade_SetGilded(unsigned char on);
extern "C" int NeiActor_UnloadStats(char* out, int capacity);
#include "fork_save.h"
#include "include/linkspan/nei/nei_items.h"
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
    // Arquivo cujo estado está na memória. Um arquivo novo não passa por oot.save.loaded: o jogo chama
    // Save_InitFile e grava, então sem isto o primeiro save do arquivo novo levaria os itens do anterior.
    int32_t loadedSlot = -1;
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
        // Estado do pipeline de Player (NEI-005). Só faz sentido com o fork ligado: sem escape hatch
        // as funções do fork não rodam e o Player não é o desta DLL.
        if (LinkSpanNei::ForkActive()) {
            char pipeline[192];
            const uint32_t size = NeiPipeline_Describe(pipeline, sizeof(pipeline));
            if (size) {
                text += " | " + std::string(pipeline, size);
            }
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

// Prova do NEI-003 em jogo: "inv_fill" enche a página do NEI com os itens que o fork declara para
// ela e "inv_fill clear" a esvazia. É instrumento de teste, não aquisição: a aquisição de verdade
// é o get-item do fork, que entra no NEI-008.
ShipNativeStatus SHIP_NATIVE_CALL InvFill(void*, const char* args, uint32_t length, ShipNativeWriteFn write,
                                          void* writer) {
    if (!write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const bool clear = args && length == 5 && std::strncmp(args, "clear", 5) == 0;
    try {
        const ShipNativeStatus status = LinkSpanNei::FillInventory(clear);
        const std::string text = status == SHIP_NATIVE_OK
                                     ? (clear ? "página do NEI esvaziada | " : "página do NEI preenchida | ") +
                                           LinkSpanNei::InventoryStatus()
                                     : std::string("recusado: o kaleido do NEI não está ativo");
        return write(writer, text.data(), static_cast<uint32_t>(text.size()));
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

// Prova do NEI-006 em jogo: "actors spawn" cria uma de cada invocação da Cane of Somaria na frente do Link e
// "actors kill" mata todas, pelas funções do fork; o stats mostra criados, vivos e liberados.
ShipNativeStatus SHIP_NATIVE_CALL Actors(void*, const char* args, uint32_t length, ShipNativeWriteFn write,
                                         void* writer) {
    if (!write || !LinkSpanNei::ForkActive()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const bool spawn = args && length == 5 && std::strncmp(args, "spawn", 5) == 0;
    const bool kill = args && length == 4 && std::strncmp(args, "kill", 4) == 0;
    const bool probe = args && length == 5 && std::strncmp(args, "probe", 5) == 0;
    if (!spawn && !kill && !probe) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    char text[256];
    int size = 0;
    if (probe) {
        // Ensaio do descarregamento: quantos atores apontam para a DLL agora e o que aconteceria com eles.
        NeiActor_Unload(1);
        char unload[96];
        const int count = NeiActor_UnloadStats(unload, sizeof(unload));
        size = std::snprintf(text, sizeof(text), "descarregamento (ensaio): %.*s", count > 0 ? count : 0, unload);
    } else {
        const int result = NeiActor_TestSomaria(spawn ? 1 : 0);
        char stats[128];
        const int count = NeiActor_Stats(stats, sizeof(stats));
        size = std::snprintf(text, sizeof(text), "%s -> %d | %.*s", spawn ? "spawn" : "kill", result,
                             count > 0 ? count : 0, stats);
    }
    return write(writer, text, static_cast<uint32_t>(size));
}

// Prova das ondas de itens (NEI-008..011): "equip shovel" dá a posse do item do fork e o põe no C esquerdo, sem
// get-item, para o teste usar o item logo em seguida.
ShipNativeStatus SHIP_NATIVE_CALL EquipTest(void*, const char* args, uint32_t length, ShipNativeWriteFn write,
                                            void* writer) {
    if (!write || !args || length == 0 || length > 64) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    std::string id(args, length);
    if (id.rfind("skijer.nei.", 0) != 0) {
        id = "skijer.nei." + id;
    }
    const ShipNativeStatus status = LinkSpanNei::GiveForkItem(id.c_str());
    const std::string text = id + (status == SHIP_NATIVE_OK ? ": no C esquerdo" : ": recusado");
    write(writer, text.data(), static_cast<uint32_t>(text.size()));
    return status;
}

// Prova das ondas de itens: medidor de magia cheio, só em memória (o save de teste é de antes da Grande Fada).
ShipNativeStatus SHIP_NATIVE_CALL MagicTest(void*, const char*, uint32_t, ShipNativeWriteFn write, void* writer) {
    if (!write || !LinkSpanNei::ForkActive()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    NeiTest_GrantMagic();
    static const char text[] = "magia e vida cheias (so em memoria)";
    return write(writer, text, sizeof(text) - 1);
}

// Prova da onda B (NEI-009): um inimigo à frente do Link, para dano e collider ("enemy", "enemy tektite",
// "enemy dodojr", "enemy wolfos", "enemy armos"). O stats mostra a vida dele.
ShipNativeStatus SHIP_NATIVE_CALL EnemyTest(void*, const char* args, uint32_t length, ShipNativeWriteFn write,
                                            void* writer) {
    if (!write || !LinkSpanNei::ForkActive()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    static const char* const kNames[] = { "deku baba", "tektite", "dodojr", "wolfos", "armos" };
    const std::string name = args ? std::string(args, length) : std::string();
    const int kind = name == "tektite" ? 1 : name == "dodojr" ? 2 : name == "wolfos" ? 3 : name == "armos" ? 4 : 0;
    const int result = NeiTest_SpawnEnemy(kind);
    char text[64];
    const int size = std::snprintf(text, sizeof(text), "inimigo: %s %s (%d)", kNames[kind],
                                   result == 0 ? "criado" : "falhou", result);
    return write(writer, text, static_cast<uint32_t>(size));
}

// Prova da onda D (NEI-011): os itens de id u16 do fork no C esquerdo ("eqx slate", "eqx hourglass", "eqx crystal",
// "eqx seasons"), fora do registro de ids u8.
ShipNativeStatus SHIP_NATIVE_CALL EquipExtTest(void*, const char* args, uint32_t length, ShipNativeWriteFn write,
                                               void* writer) {
    if (!write || !LinkSpanNei::ForkActive()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    static const char* const kNames[] = { "slate", "hourglass", "crystal", "seasons" };
    const std::string name = args ? std::string(args, length) : std::string();
    int which = -1;
    for (int i = 0; i < 4; ++i) {
        if (name == kNames[i]) {
            which = i;
        }
    }
    if (which < 0) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const int result = NeiTest_EquipExt(which);
    char text[64];
    const int size = std::snprintf(text, sizeof(text), "eqx %s: %s (%d)", kNames[which],
                                   result == 0 ? "no C esquerdo" : "falhou", result);
    return write(writer, text, static_cast<uint32_t>(size));
}

// Prova da onda B (NEI-009): nível da Kokiri Sword pelos upgrades do fork ("upgrade razor", "upgrade gilded";
// sem argumento volta à Kokiri). Só em memória até o jogo salvar.
ShipNativeStatus SHIP_NATIVE_CALL UpgradeTest(void*, const char* args, uint32_t length, ShipNativeWriteFn write,
                                              void* writer) {
    if (!write || !LinkSpanNei::ForkActive()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const std::string level = args ? std::string(args, length) : std::string();
    WeaponUpgrade_SetRazor(level == "razor" || level == "gilded");
    WeaponUpgrade_SetGilded(level == "gilded");
    const std::string text = "kokiri sword: " + (level.empty() ? std::string("kokiri") : level);
    return write(writer, text.data(), static_cast<uint32_t>(text.size()));
}

// Aquisição de um item do fork pelo get-item do registro (NEI-008): "give deku_leaf". Sem argumento, lista os
// itens do fork no registro com o id runtime, a posse e se já estão na página do NEI.
ShipNativeStatus SHIP_NATIVE_CALL Give(void*, const char* args, uint32_t length, ShipNativeWriteFn write,
                                       void* writer) {
    if (!Owned() || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        std::string text;
        ShipNativeStatus status = SHIP_NATIVE_OK;
        if (!args || length == 0) {
            text = LinkSpanNei::ListForkItems();
        } else {
            status = LinkSpanNei::ReceiveForkItem(std::string(args, length), text);
        }
        write(writer, text.data(), static_cast<uint32_t>(text.size()));
        return status;
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
    // Opcional: sem ele os textos próprios do fork (Time Gate, Lantern, Pictobox) ficam sem caixa.
    LinkSpanNei::BindText(static_cast<const ShipOotTextV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_TEXT_SERVICE, LINKSPAN_OOT_TEXT_VERSION, sizeof(ShipOotTextV1))));
    gCore->registry.Attach(items, save, saveHandle);
    gCore->forkSave.Attach(save, forkSaveHandle);
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
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "stats", Stats, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "inv_fill", InvFill, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "give", Give, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "actors", Actors, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "equip", EquipTest, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "magic", MagicTest, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "enemy", EnemyTest, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "upgrade", UpgradeTest, nullptr);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "eqx", EquipExtTest, nullptr);
    }
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
