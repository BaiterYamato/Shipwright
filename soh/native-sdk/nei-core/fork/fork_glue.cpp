// Liga o código de itens do fork NEI (compilado em C na mesma DLL) ao host: resolve funções e variáveis do
// soh.exe pelo escape hatch (nei_host_imports.c), define os itens do fork no registro linkspan.nei.items e
// desvia Player_Update e Player_Draw para rodar o update e o desenho dos itens do fork, nos mesmos pontos em
// que o z_player.c do fork os chama. Sem escape hatch (outro soh.exe) o registro segue e o fork fica desligado.
#include "fork_glue.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "fork_items.h"
#include "fork_models.h"
#include "kaleido_glue.h"
#include "overlay_glue.h"
#include "registry.h"

// actor_guard.c (NEI-006).
extern "C" void NeiActor_Frame(void);
extern "C" void NeiActor_Unload(int dryRun);

namespace LinkSpanNei {
namespace {

using ActorFn = void (*)(void* actor, void* play);

struct ForkItem {
    uint64_t handle = 0;
    uint8_t logical = 0;
    std::string id;
};

struct ForkState {
    const ShipNativeRuntime* runtime = nullptr;
    Registry* registry = nullptr;
    ActorFn originalUpdate = nullptr;
    ActorFn originalDraw = nullptr;
    uint64_t updatePatch = 0;
    uint64_t drawPatch = 0;
    std::vector<ForkItem> items;
    uint32_t withoutAssets = 0; // itens do fork deixados de fora por falta do componente de assets
    bool active = false;
    std::string status = "desligado";
};

ForkState gFork;

int Resolve(void* context, const char* name, uintptr_t* address) {
    return gFork.runtime->resolve_symbol(context, name, address) == SHIP_NATIVE_OK ? 0 : 1;
}

// A posse do registro (linkspan.nei.items, save "nei.items") é quem manda; a página do NEI (gNeiSave, save
// "nei.state") é a vitrine que o kaleido do fork desenha. Item recebido por qualquer caminho — get-item, grant de
// outro mod, save carregado — aparece na célula dele no frame seguinte.
void SyncInventory() {
    for (const ForkItem& item : gFork.items) {
        NeiItemStateV1 state{ sizeof(state) };
        if (gFork.registry->GetState(item.handle, &state) == SHIP_NATIVE_OK && state.owned) {
            NeiInv_PlaceItem(item.logical);
        }
    }
}

void PlayerUpdate(void* actor, void* play) {
    gFork.originalUpdate(actor, play);
    if (gFork.active) {
        NeiActor_Frame();
        SyncInventory();
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

ShipNativeStatus SHIP_NATIVE_CALL Received(void* user, uint64_t) {
    NeiInv_ReceiveItem(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(user)));
    return SHIP_NATIVE_OK;
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

// Glifos de botão da fonte do jogo que o fork põe no texto; o registro só aceita ASCII.
const char* Glyph(unsigned char c) {
    switch (c) {
        case 0x9F:
            return "A";
        case 0xA0:
            return "B";
        case 0xA1:
        case 0xA5:
        case 0xA6:
        case 0xA7:
        case 0xA8:
            return "C";
        case 0xA2:
            return "L";
        case 0xA3:
            return "R";
        case 0xA4:
            return "Z";
        case 0xAA:
            return "Stick";
        case 0xAB:
            return "D-Pad";
        default:
            return "";
    }
}

// Texto do fork para ASCII: glifo de botão vira a letra, letra acentuada em UTF-8 some.
std::string Ascii(const char* text) {
    std::string out;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); p && *p; ++p) {
        if (*p >= 0xC0) {
            while ((p[1] & 0xC0) == 0x80) {
                ++p;
            }
        } else if (*p >= 0x80) {
            out += Glyph(*p);
        } else if (*p >= 0x20) {
            out += static_cast<char>(*p);
        }
    }
    return out;
}

// "You got the %gDeku Leaf%w!..." -> "Deku Leaf": o primeiro trecho colorido do texto do get-item do fork.
std::string NameFromMessage(const std::string& text) {
    for (size_t at = text.find('%'); at != std::string::npos && at + 2 < text.size(); at = text.find('%', at + 1)) {
        if (text[at + 1] == 'w') {
            continue;
        }
        const size_t end = text.find("%w", at + 2);
        if (end != std::string::npos && end > at + 2) {
            return text.substr(at + 2, end - at - 2);
        }
    }
    return {};
}

std::string Slug(const std::string& name) {
    std::string slug;
    for (const char c : name) {
        if (c == '\'') {
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(c))) {
            slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!slug.empty() && slug.back() != '_') {
            slug += '_';
        }
    }
    while (!slug.empty() && slug.back() == '_') {
        slug.pop_back();
    }
    return slug;
}

// Primeira caixa do texto do fork (até o primeiro ^), no limite do linkspan.oot.items.
std::string GetItemMessage(const std::string& full, const std::string& name) {
    std::string text = full.substr(0, full.find('^'));
    if (text.size() > LINKSPAN_OOT_ITEMS_MAX_MESSAGE) {
        text.resize(LINKSPAN_OOT_ITEMS_MAX_MESSAGE);
        const size_t cut = text.rfind('&');
        text.resize(cut != std::string::npos ? cut : LINKSPAN_OOT_ITEMS_MAX_MESSAGE - 2);
    }
    if (!text.empty() && text.back() == '%') {
        text.pop_back();
    }
    return text.empty() ? "You got the " + name + "!" : text;
}

ShipNativeStatus DefineItems() {
    uint8_t logical[64];
    const uint32_t count = NeiFork_ListItems(logical, sizeof(logical));
    const bool core = NeiAssets_CoreMounted() != 0;
    for (uint32_t i = 0; i < count; ++i) {
        NeiForkItemInfo info{};
        if (!NeiFork_DescribeItem(logical[i], &info)) {
            continue;
        }
        const bool assets = core && NeiAssets_ComponentMounted(info.component);
        // Sem os assets, só os itens que não desenham modelo do fork (Feather e Cape) rodam sem pedir ao resource
        // manager um caminho que não existe; os outros ficam fora do registro nesta sessão.
        if (!assets && NeiFork_NeedsAssets(logical[i])) {
            ++gFork.withoutAssets;
            continue;
        }
        const std::string full = Ascii(info.message);
        std::string name = NameFromMessage(full);
        if (name.empty()) {
            const char* fallback = NeiFork_FallbackName(logical[i]);
            name = fallback ? fallback : "NEI Item " + std::to_string(logical[i]);
        }
        const std::string id = "skijer.nei." + Slug(name);
        const std::string message = GetItemMessage(full, name);
        const char* icon = assets && info.icon ? info.icon : "textures/icon_item_static/gItemIconHookshotTex";
        NeiItemLevelV1 level{ sizeof(NeiItemLevelV1), icon, { name.c_str(), nullptr, nullptr }, 0 };
        NeiItemDefinitionV1 definition{};
        definition.size = sizeof(definition);
        definition.id = id.c_str();
        definition.age = info.age;
        if (assets && info.modelPath) {
            definition.model_path = info.modelPath;
            definition.model_layer = info.modelLayer == NEI_MODEL_LAYER_TRANSLUCENT
                                         ? LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT
                                         : LINKSPAN_OOT_ITEMS_LAYER_OPAQUE;
            definition.model_scale = info.modelScale;
        } else {
            definition.model_path = "objects/gameplay_keep/gHeartPieceInteriorDL"; // provisório sem assets
            definition.model_layer = LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT;
            definition.model_scale = 0.025f;
        }
        definition.get_messages[0] = message.c_str();
        definition.level_count = 1;
        definition.levels = &level;
        definition.use = IgnoreUse;
        definition.received = Received;
        definition.user = reinterpret_cast<void*>(static_cast<uintptr_t>(logical[i]));
        uint64_t item = 0;
        ShipNativeStatus status = gFork.registry->Define(&definition, &item);
        if (status != SHIP_NATIVE_OK) {
            gFork.status = "define " + id + " falhou";
            return status;
        }
        gFork.items.push_back({ item, logical[i], id });
        NeiItemStateV1 state{ sizeof(state) };
        status = gFork.registry->GetState(item, &state);
        if (status != SHIP_NATIVE_OK) {
            return status;
        }
        NeiFork_MapItem(state.runtime_id, logical[i]);
    }
    return SHIP_NATIVE_OK;
}

// "skijer.nei.deku_leaf", "deku_leaf" ou "Deku Leaf" -> item do fork.
const ForkItem* FindItem(const std::string& query) {
    const std::string slug = Slug(query.rfind("skijer.nei.", 0) == 0 ? query.substr(11) : query);
    for (const ForkItem& item : gFork.items) {
        if (item.id == "skijer.nei." + slug) {
            return &item;
        }
    }
    return nullptr;
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
    // As funções do host que o fork mudou (Player_ActionToMeleeWeapon, Player_UseItem...) passam a entrar na
    // versão do fork. Desvio recusado deixa aquela função com a versão do host; o status conta.
    StartOverlays(runtime);
    gFork.active = true;
    // Só agora: a página de itens do kaleido do fork chama o host pelos mesmos thunks resolvidos
    // acima, e o menu pode abrir no primeiro frame. Se os desvios forem recusados, o fork continua —
    // os itens funcionam, o inventário é que fica o do host (NEI-003).
    StartKaleido(runtime);
    gFork.status = "ativo (" + std::to_string(gFork.items.size()) + " itens" +
                   (gFork.withoutAssets ? ", " + std::to_string(gFork.withoutAssets) + " sem assets" : "") +
                   ") | kaleido: " + KaleidoStatus() + " | host: " + OverlayStatus();
}

void StopFork() {
    gFork.active = false;
    // Antes de tirar os desvios: nenhum ator do jogo pode continuar com update/draw/destroy dentro da DLL.
    if (gFork.runtime) {
        NeiActor_Unload(0);
    }
    StopKaleido();
    StopOverlays();
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
        for (const ForkItem& item : gFork.items) {
            gFork.registry->Remove(item.handle);
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

ShipNativeStatus ReceiveForkItem(const std::string& query, std::string& result) {
    if (!gFork.active || !gFork.registry) {
        result = "fork desligado";
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const ForkItem* item = FindItem(query);
    if (!item) {
        result = "item desconhecido: " + query;
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const ShipNativeStatus status = gFork.registry->Give(item->handle);
    result = item->id + (status == SHIP_NATIVE_OK ? ": get-item" : ": recusado");
    return status;
}

std::string ListForkItems() {
    std::string text;
    for (const ForkItem& item : gFork.items) {
        NeiItemStateV1 state{ sizeof(state) };
        gFork.registry->GetState(item.handle, &state);
        char line[160];
        std::snprintf(line, sizeof(line), "%s%s(0x%02X->0x%02X%s%s)", text.empty() ? "" : " ",
                      item.id.c_str() + 11, item.logical, state.runtime_id, state.owned ? ",posse" : "",
                      NeiInv_HasItem(item.logical) ? ",pagina" : "");
        text += line;
    }
    return text;
}

} // namespace LinkSpanNei
