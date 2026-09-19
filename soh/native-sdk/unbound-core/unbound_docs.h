#pragma once

// Documentos do formato 2 que não viram recurso do jogo: o manifesto unbound.json (SPEC.md §6) e a
// tabela de texto text/<lang>/messages.json (§5). Puro: recebe as camadas já lidas.
#include <cstdint>
#include <string>
#include <vector>

#include "json_merge.h"

namespace LinkSpanUnbound {

constexpr int kReaderFormatVersion = 2;

struct ManifestCheck {
    bool valid = false; // objeto JSON com a versão do leitor
    bool base = false;  // valid e "scenes" em features (§1.3)
    std::string note;   // motivo da recusa
};

// Manifesto de uma camada. Os manifestos não mesclam (§6).
ManifestCheck CheckManifest(const std::string& json);

struct TextMessage {
    uint32_t id = 0;
    uint8_t box = 0;
    uint8_t ypos = 0;
    std::string bytes; // sem o 0x02 final, no máximo kMaxMessageBytes
};

constexpr uint32_t kMaxMessageBytes = 8191; // o jogo acrescenta o 0x02: 8 192 no total

struct TextTable {
    // A camada de cima trouxe $replace em "messages" (ou na raiz): a tabela do jogo é esvaziada antes.
    bool replaceTable = false;
    std::vector<TextMessage> messages; // em ordem de chave (§3.5)
    std::vector<uint32_t> removed;     // ids cujo valor final é null
    std::vector<std::string> notes;
    uint32_t layersUsed = 0;
};

// Mescla as camadas de text/<lang>/messages.json da menor para a maior prioridade. false se nenhuma
// camada serviu. A tabela vanilla do jogo faz o papel da camada base (o conversor não exporta texto).
bool BuildTextTable(const std::vector<LayerDocument>& layers, TextTable& out);

// Texto JSON (UTF-8) para os bytes do jogo: U+0000–U+00FF viram um byte; o resto vira '?'.
std::string DecodeMessageText(const std::string& utf8, uint32_t& replaced);

} // namespace LinkSpanUnbound
