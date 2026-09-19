#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace LinkSpanUnbound {

struct LayerDocument {
    std::string archive;
    std::string json;
    uint64_t contentHash = 0;
};

struct MergeResult {
    std::string json;
    uint64_t hash = 0;
    uint32_t layerCount = 0;
};

// Aninhamento máximo (objetos e arrays) de um documento de mod. Merge e transcode são recursivos: um JSON
// com milhares de níveis estouraria a pilha da thread de recursos, e estouro de pilha não é exceção.
constexpr int kMaxJsonDepth = 64;
// Varre o texto sem montar nada: true se nenhum aninhamento passa de `maxDepth`. Colchetes dentro de
// strings não contam; os de comentários contam, o que só recusa comentário com dezenas de '{'.
inline bool JsonDepthWithin(std::string_view text, int maxDepth = kMaxJsonDepth) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > maxDepth) {
                return false;
            }
        } else if ((c == '}' || c == ']') && depth > 0) {
            --depth;
        }
    }
    return true;
}

bool MergeDocuments(const std::string& schema, const std::vector<LayerDocument>& layers, MergeResult& output,
                    std::string& error);

// Documento sem $schema obrigatório, como unbound/scenes.json: objetos mesclam por chave, a camada
// de cima vence, null remove a chave e comentários são aceitos.
bool MergeSchemaFreeDocuments(const std::vector<LayerDocument>& layers, MergeResult& output, std::string& error);

} // namespace LinkSpanUnbound
