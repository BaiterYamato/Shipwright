// Instala os desvios da página de itens do kaleido (NEI-003).
//
// O inventário estendido do fork não é um menu novo: é o mesmo kaleido do jogo com três funções
// trocadas. `KaleidoScope_DrawItemSelect` é quem desenha a grade, move o cursor, troca de página
// (ExtInv_SwitchPage) e escolhe o item; as outras duas montam e concluem a animação de equipar no
// botão C. O resto do menu — mapa, equipamento, quest, as setas de página, o fundo — continua sendo
// o do host, que é o "sem substituir o menu inteiro" pedido no plano.
//
// As três versões do fork vivem na própria DLL (extracted/z_kaleido_item.c, tiradas do commit fixo
// pelo fork/sync.py) e chamam o soh.exe pelos mesmos thunks do escape hatch que o resto do fork usa.
// Por isso os desvios só entram depois que o StartFork resolveu a tabela de símbolos: antes disso as
// funções copiadas chamariam ponteiro nulo já no primeiro frame de menu.
#include "kaleido_glue.h"

#include <string>
#include <vector>

extern "C" {
// Definidas em C na mesma DLL. PlayState* chega como void*: só o endereço é usado, e a chamada
// verdadeira vem do jogo, com o mesmo ABI de ponteiro do protótipo do host.
void KaleidoScope_DrawItemSelect(void* play);
void KaleidoScope_UpdateItemEquip(void* play);
void KaleidoScope_SetupItemEquip(void* play, uint16_t item, uint16_t slot, int16_t animX, int16_t animY);
// fork/inventory_unit.c
void NeiInv_FillNeiPage(void);
void NeiInv_ClearNeiPage(void);
void NeiInv_ReadState(int32_t* pages, int32_t* currentPage, uint32_t* occupied);
}

namespace LinkSpanNei {
namespace {

struct Desvio {
    const char* name;
    void* detour;
};

const Desvio kDesvios[] = {
    { "KaleidoScope_DrawItemSelect", reinterpret_cast<void*>(&KaleidoScope_DrawItemSelect) },
    { "KaleidoScope_UpdateItemEquip", reinterpret_cast<void*>(&KaleidoScope_UpdateItemEquip) },
    { "KaleidoScope_SetupItemEquip", reinterpret_cast<void*>(&KaleidoScope_SetupItemEquip) },
};

struct KaleidoState {
    const ShipNativeRuntime* runtime = nullptr;
    std::vector<uint64_t> patches;
    std::string status = "desligado";
};

KaleidoState gKaleido;

} // namespace

void StartKaleido(const ShipNativeRuntime* runtime) {
    StopKaleido();
    gKaleido.runtime = runtime;
    if (!runtime || runtime->abi_minor < 3 || !runtime->resolve_symbol || !runtime->install_patch) {
        gKaleido.status = "desligado: host sem escape hatch";
        return;
    }
    for (const Desvio& desvio : kDesvios) {
        uintptr_t target = 0;
        if (runtime->resolve_symbol(runtime->context, desvio.name, &target) != SHIP_NATIVE_OK) {
            gKaleido.status = std::string("desligado: ") + desvio.name + " não resolvido";
            StopKaleido();
            return;
        }
        // O trampolim é exigido pelo install_patch (original NULL é INVALID_ARGUMENT), mas fica sem
        // uso de propósito: a versão do fork substitui a do host, não a embrulha. Chamar as duas
        // desenharia a grade duas vezes e leria o input em dobro.
        void* original = nullptr;
        uint64_t patch = 0;
        if (runtime->install_patch(runtime->context, target, desvio.detour, &original, &patch) != SHIP_NATIVE_OK) {
            gKaleido.status = std::string("desligado: desvio de ") + desvio.name + " recusado";
            StopKaleido();
            return;
        }
        gKaleido.patches.push_back(patch);
    }
    gKaleido.status = "ativo (" + std::to_string(gKaleido.patches.size()) + " desvios)";
}

void StopKaleido() {
    if (gKaleido.runtime && gKaleido.runtime->remove_patch) {
        // Ordem inversa da instalação, como no fork_glue: o host desfaz o trampolim mais novo primeiro.
        for (auto it = gKaleido.patches.rbegin(); it != gKaleido.patches.rend(); ++it) {
            gKaleido.runtime->remove_patch(gKaleido.runtime->context, *it);
        }
    }
    gKaleido.patches.clear();
    gKaleido.status = "desligado";
}

const std::string& KaleidoStatus() {
    return gKaleido.status;
}

ShipNativeStatus FillInventory(bool clear) {
    if (gKaleido.patches.empty()) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (clear) {
        NeiInv_ClearNeiPage();
    } else {
        NeiInv_FillNeiPage();
    }
    return SHIP_NATIVE_OK;
}

std::string InventoryStatus() {
    if (gKaleido.patches.empty()) {
        return "inventário: host";
    }
    int32_t pages = 0;
    int32_t current = 0;
    uint32_t occupied = 0;
    NeiInv_ReadState(&pages, &current, &occupied);
    return "inventário: " + std::to_string(pages) + " páginas (" + std::to_string(pages * 24) + " slots), atual=" +
           std::to_string(current) + ", ocupados fora da vanilla=" + std::to_string(occupied);
}

} // namespace LinkSpanNei
