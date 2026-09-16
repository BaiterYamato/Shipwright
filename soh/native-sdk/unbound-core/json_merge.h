#pragma once

#include <cstdint>
#include <string>
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

bool MergeDocuments(const std::string& schema, const std::vector<LayerDocument>& layers, MergeResult& output,
                    std::string& error);

// Documento sem $schema obrigatório, como unbound/scenes.json: objetos mesclam por chave, a camada
// de cima vence, null remove a chave e comentários são aceitos.
bool MergeSchemaFreeDocuments(const std::vector<LayerDocument>& layers, MergeResult& output, std::string& error);

} // namespace LinkSpanUnbound
