// Liga o código de itens do fork NEI (compilado em C na mesma DLL) ao host: resolve funções e variáveis do
// soh.exe pelo escape hatch (nei_host_imports.c), define os itens do fork no registro linkspan.nei.items e
// desvia Player_Update e Player_Draw para rodar o update e o desenho dos itens do fork, nos mesmos pontos em
// que o z_player.c do fork os chama. Sem escape hatch (outro soh.exe) o registro segue e o fork fica desligado.
#include "fork_glue.h"

#include <string>
#include <vector>

#include "fork_items.h"
#include "kaleido_glue.h"
#include "registry.h"

namespace LinkSpanNei {
namespace {

using ActorFn = void (*)(void* actor, void* play);

struct ForkState {
    const ShipNativeRuntime* runtime = nullptr;
    Registry* registry = nullptr;
    ActorFn originalUpdate = nullptr;
    ActorFn originalDraw = nullptr;
    uint64_t updatePatch = 0;
    uint64_t drawPatch = 0;
    std::vector<uint64_t> items;
    bool active = false;
    std::string status = "desligado";
};

ForkState gFork;

int Resolve(void* context, const char* name, uintptr_t* address) {
    return gFork.runtime->resolve_symbol(context, name, address) == SHIP_NATIVE_OK ? 0 : 1;
}

void PlayerUpdate(void* actor, void* play) {
    gFork.originalUpdate(actor, play);
    if (gFork.active) {
        CustomItems_Update(actor, play);
    }
}

void PlayerDraw(void* actor, void* play) {
    gFork.originalDraw(actor, play);
    if (gFork.active) {
        CustomItems_OverrideDraw(actor, play);
    }
}

ShipNativeStatus SHIP_NATIVE_CALL IgnoreUse(void*, uint64_t, uint8_t) {
    return SHIP_NATIVE_OK; // o fork lê o botão sozinho (ItemInput_Update)
}

ShipNativeStatus Patch(const char* name, ActorFn detour, ActorFn* original, uint64_t* patch) {
    uintptr_t target = 0;
    ShipNativeStatus status = gFork.runtime->resolve_symbol(gFork.runtime->context, name, &target);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    return gFork.runtime->install_patch(gFork.runtime->context, target, reinterpret_cast<void*>(detour),
                                        reinterpret_cast<void**>(original), patch);
}

ShipNativeStatus DefineItems() {
    for (uint32_t i = 0; i < gNeiForkItemCount; ++i) {
        const NeiForkItem& fork = gNeiForkItems[i];
        // Ícone provisório: os assets do fork não têm licença e entram pelo NEI-007.
        NeiItemLevelV1 level{ sizeof(NeiItemLevelV1), "textures/icon_item_static/gItemIconHookshotTex",
                              { fork.name, nullptr, nullptr }, 0 };
        const std::string message = std::string("You got the ") + fork.name + "!";
        NeiItemDefinitionV1 definition{};
        definition.size = sizeof(definition);
        definition.id = fork.id;
        definition.age = LINKSPAN_OOT_ITEM_AGE_ANY;
        definition.model_path = "objects/gameplay_keep/gHeartPieceInteriorDL"; // provisório (NEI-007)
        definition.model_layer = LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT;
        definition.model_scale = 0.025f;
        definition.get_messages[0] = message.c_str();
        definition.level_count = 1;
        definition.levels = &level;
        definition.use = IgnoreUse;
        uint64_t item = 0;
        ShipNativeStatus status = gFork.registry->Define(&definition, &item);
        if (status != SHIP_NATIVE_OK) {
            gFork.status = std::string("define ") + fork.id + " falhou";
            return status;
        }
        gFork.items.push_back(item);
        NeiItemStateV1 state{ sizeof(state) };
        status = gFork.registry->GetState(item, &state);
        if (status != SHIP_NATIVE_OK) {
            return status;
        }
        NeiFork_MapItem(state.runtime_id, fork.logicalId);
    }
    return SHIP_NATIVE_OK;
}

} // namespace

void StartFork(const ShipNativeRuntime* runtime, Registry* registry) {
    gFork = ForkState{};
    gFork.runtime = runtime;
    gFork.registry = registry;
    if (runtime->abi_minor < 3 || !runtime->resolve_symbol || !runtime->install_patch) {
        gFork.status = "desligado: host sem escape hatch";
        return;
    }
    const char* failed = nullptr;
    if (nei_host_resolve(Resolve, runtime->context, &failed) != 0) {
        // UNSUPPORTED aqui quase sempre é soh.exe fora do host_fingerprints do manifesto.
        gFork.status = std::string("desligado: símbolo ") + (failed ? failed : "?") + " não resolvido";
        return;
    }
    if (DefineItems() != SHIP_NATIVE_OK) {
        StopFork();
        return;
    }
    if (Patch("Player_Update", PlayerUpdate, &gFork.originalUpdate, &gFork.updatePatch) != SHIP_NATIVE_OK ||
        Patch("Player_Draw", PlayerDraw, &gFork.originalDraw, &gFork.drawPatch) != SHIP_NATIVE_OK) {
        StopFork();
        gFork.status = "desligado: desvio de Player_Update/Player_Draw recusado";
        return;
    }
    gFork.active = true;
    // Só agora: a página de itens do kaleido do fork chama o host pelos mesmos thunks resolvidos
    // acima, e o menu pode abrir no primeiro frame. Se os desvios forem recusados, o fork continua —
    // os itens funcionam, o inventário é que fica o do host (NEI-003).
    StartKaleido(runtime);
    gFork.status = "ativo (" + std::to_string(gFork.items.size()) + " itens) | kaleido: " + KaleidoStatus();
}

void StopFork() {
    gFork.active = false;
    StopKaleido();
    if (gFork.runtime && gFork.runtime->remove_patch) {
        if (gFork.drawPatch) {
            gFork.runtime->remove_patch(gFork.runtime->context, gFork.drawPatch);
        }
        if (gFork.updatePatch) {
            gFork.runtime->remove_patch(gFork.runtime->context, gFork.updatePatch);
        }
    }
    gFork.drawPatch = gFork.updatePatch = 0;
    if (gFork.registry) {
        for (const uint64_t item : gFork.items) {
            gFork.registry->Remove(item);
        }
    }
    gFork.items.clear();
    NeiFork_ClearItems();
}

bool ForkActive() {
    return gFork.active;
}

const std::string& ForkStatus() {
    return gFork.status;
}

ShipNativeStatus GiveForkItem(const char* id) {
    if (!gFork.active || !gFork.registry) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    uint64_t item = 0;
    ShipNativeStatus status = gFork.registry->Find(id, &item);
    if (status == SHIP_NATIVE_OK) {
        status = gFork.registry->Grant(item);
    }
    if (status == SHIP_NATIVE_OK) {
        status = gFork.registry->Equip(item, LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT);
    }
    return status;
}

} // namespace LinkSpanNei
