// Instala os desvios das funções do host que o fork NEI modificou (NEI-HOST-001).
//
// O overlay.py copia para a DLL a versão do fork (mesclada com a do host quando o upstream também mudou a função)
// de cada função que o fork alterou nos fontes de soh/hookable_sources.txt. Esses fontes são compilados sem
// inline e fora do LTCG, então cada função tem um endereço só no soh.exe e toda chamada passa por ele: desviar o
// endereço basta para o jogo inteiro usar a versão do fork, como no z_player.c do fork compilado no lugar.
#include "overlay_glue.h"

#include <vector>

extern "C" {
#include "overlay_table.h"
const NeiOverlayEntry* NeiOverlay_Entry(unsigned index); // nei_overlays.c (sync.py)
}

namespace LinkSpanNei {
namespace {

struct OverlayState {
    const ShipNativeRuntime* runtime = nullptr;
    std::vector<uint64_t> patches;
    std::string status = "desligado";
};

OverlayState gOverlay;

} // namespace

void StartOverlays(const ShipNativeRuntime* runtime) {
    StopOverlays();
    gOverlay.runtime = runtime;
    if (!runtime || runtime->abi_minor < 3 || !runtime->resolve_symbol || !runtime->install_patch) {
        gOverlay.status = "desligado: host sem escape hatch";
        return;
    }
    unsigned total = 0;
    std::vector<std::string> failed;
    for (const NeiOverlayEntry* entry = NeiOverlay_Entry(0); entry; entry = NeiOverlay_Entry(++total)) {
        uintptr_t target = 0;
        void* original = nullptr; // exigido pelo install_patch; a cópia do fork substitui a do host, não a embrulha
        uint64_t patch = 0;
        if (runtime->resolve_symbol(runtime->context, entry->lookup, &target) != SHIP_NATIVE_OK ||
            runtime->install_patch(runtime->context, target, entry->detour, &original, &patch) != SHIP_NATIVE_OK) {
            failed.emplace_back(entry->lookup);
            continue;
        }
        gOverlay.patches.push_back(patch);
    }
    gOverlay.status = std::to_string(gOverlay.patches.size()) + "/" + std::to_string(total) + " desvios";
    if (!failed.empty()) {
        gOverlay.status += ", recusados:";
        for (size_t i = 0; i < failed.size() && i < 8; ++i) {
            gOverlay.status += " " + failed[i];
        }
        if (failed.size() > 8) {
            gOverlay.status += " (+" + std::to_string(failed.size() - 8) + ")";
        }
    }
}

void StopOverlays() {
    if (gOverlay.runtime && gOverlay.runtime->remove_patch) {
        // Ordem inversa da instalação: o host desfaz o trampolim mais novo primeiro.
        for (auto it = gOverlay.patches.rbegin(); it != gOverlay.patches.rend(); ++it) {
            gOverlay.runtime->remove_patch(gOverlay.runtime->context, *it);
        }
    }
    gOverlay.patches.clear();
    gOverlay.status = "desligado";
}

const std::string& OverlayStatus() {
    return gOverlay.status;
}

} // namespace LinkSpanNei
