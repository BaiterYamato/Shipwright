#include "scene_conflicts.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "unbound_format.h"

namespace LinkSpanUnbound {
namespace {

namespace fs = std::filesystem;

constexpr uint32_t kEndOfCentralDirectory = 0x06054b50;
constexpr uint32_t kZip64End = 0x06064b50;
constexpr uint32_t kZip64Locator = 0x07064b50;
constexpr uint32_t kCentralEntry = 0x02014b50;
constexpr size_t kEndRecordSize = 22;
constexpr size_t kZip64LocatorSize = 20;
constexpr size_t kZip64EndSize = 56;
constexpr size_t kCentralEntrySize = 46;
constexpr uint64_t kMaxCentralDirectory = 64 * 1024 * 1024;

using ReadAt = std::function<bool(uint64_t offset, size_t size, std::string& out)>;

uint16_t U16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

uint32_t U32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

uint64_t U64(const unsigned char* p) {
    return static_cast<uint64_t>(U32(p)) | static_cast<uint64_t>(U32(p + 4)) << 32;
}

const unsigned char* Bytes(const std::string& text, size_t offset = 0) {
    return reinterpret_cast<const unsigned char*>(text.data()) + offset;
}

struct Directory {
    uint64_t entries = 0;
    uint64_t size = 0;
    uint64_t offset = 0;
    uint64_t endOffset = 0;  // onde o diretório central deveria terminar (registro final ou registro ZIP64)
};

// Posição do diretório central pelo registro final em `endOffset`. Campos saturados (0xFFFF/0xFFFFFFFF) são
// ZIP64: o localizador fica 20 bytes antes do registro e aponta o registro final ZIP64.
bool LocateDirectory(const ReadAt& read, uint64_t endOffset, const unsigned char* record, Directory& out,
                     std::string& error) {
    out = { U16(record + 10), U32(record + 12), U32(record + 16), endOffset };
    if (out.entries != 0xFFFF && out.size != 0xFFFFFFFF && out.offset != 0xFFFFFFFF) {
        return true;
    }
    std::string locator;
    if (endOffset < kZip64LocatorSize || !read(endOffset - kZip64LocatorSize, kZip64LocatorSize, locator) ||
        U32(Bytes(locator)) != kZip64Locator) {
        error = "ZIP64 sem localizador";
        return false;
    }
    const uint64_t end64 = U64(Bytes(locator, 8));
    const uint64_t locatorOffset = endOffset - kZip64LocatorSize;
    std::string record64;
    if (end64 > locatorOffset || locatorOffset - end64 < kZip64EndSize || !read(end64, kZip64EndSize, record64) ||
        U32(Bytes(record64)) != kZip64End) {
        error = "registro final ZIP64 inválido";
        return false;
    }
    out = { U64(Bytes(record64, 32)), U64(Bytes(record64, 40)), U64(Bytes(record64, 48)), end64 };
    return true;
}

bool ListNames(uint64_t size, const ReadAt& read, std::vector<std::string>& names, std::string& error) {
    names.clear();
    if (size < kEndRecordSize) {
        error = "pequeno demais para ser ZIP";
        return false;
    }
    // O registro final tem 22 bytes mais até 64 KiB de comentário.
    const size_t tailSize = static_cast<size_t>(std::min<uint64_t>(size, kEndRecordSize + 0xFFFF));
    std::string tail;
    if (!read(size - tailSize, tailSize, tail)) {
        error = "falha ao ler o fim do arquivo";
        return false;
    }
    // Um comentário pode conter a assinatura do registro final. Vale o candidato mais ao fim cujo diretório
    // termina exatamente onde o registro começa (sem prefixo nem lixo entre eles); sem nenhum assim, o mais ao fim
    // que ao menos cabe antes do registro.
    Directory chosen;
    bool consistent = false;
    bool loose = false;
    std::string lastError = "fim do diretório central não encontrado";
    for (size_t pos = tailSize - kEndRecordSize + 1; pos-- > 0 && !consistent;) {
        if (U32(Bytes(tail, pos)) != kEndOfCentralDirectory ||
            pos + kEndRecordSize + U16(Bytes(tail, pos + 20)) > tailSize) {
            continue;
        }
        Directory candidate;
        if (!LocateDirectory(read, size - tailSize + pos, Bytes(tail, pos), candidate, lastError)) {
            continue;
        }
        if (candidate.offset > candidate.endOffset || candidate.size > candidate.endOffset - candidate.offset) {
            lastError = "diretório central fora do arquivo";
            continue;
        }
        if (candidate.offset + candidate.size == candidate.endOffset) {
            chosen = candidate;
            consistent = true;
        } else if (!loose) {
            chosen = candidate;
            loose = true;
        }
    }
    if (!consistent && !loose) {
        error = lastError;
        return false;
    }
    if (chosen.size > kMaxCentralDirectory || chosen.entries > chosen.size / kCentralEntrySize + 1) {
        error = "diretório central grande demais ou com contagem impossível";
        return false;
    }
    std::string directory;
    if (!read(chosen.offset, static_cast<size_t>(chosen.size), directory)) {
        error = "falha ao ler o diretório central";
        return false;
    }
    size_t pos = 0;
    for (uint64_t index = 0; index < chosen.entries; ++index) {
        if (pos + kCentralEntrySize > directory.size() || U32(Bytes(directory, pos)) != kCentralEntry) {
            error = "entrada " + std::to_string(index) + " do diretório central truncada";
            return false;
        }
        const unsigned char* entry = Bytes(directory, pos);
        const size_t nameLength = U16(entry + 28);
        const size_t next = pos + kCentralEntrySize + nameLength + U16(entry + 30) + U16(entry + 32);
        if (next > directory.size()) {
            error = "entrada " + std::to_string(index) + " do diretório central truncada";
            return false;
        }
        // Nome literal: o archive do host procura o nome como está gravado, sem trocar '\' por '/'.
        names.emplace_back(directory, pos + kCentralEntrySize, nameLength);
        pos = next;
    }
    return true;
}

// Igualdade de valor JSON sem depender da ordem das chaves de objeto (ordered_json compara em ordem).
bool SameValue(const Json& a, const Json& b) {
    if (a.is_object() && b.is_object()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (const auto& [key, value] : a.items()) {
            const auto other = b.find(key);
            if (other == b.end() || !SameValue(value, *other)) {
                return false;
            }
        }
        return true;
    }
    if (a.is_array() && b.is_array()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (!SameValue(a[i], b[i])) {
                return false;
            }
        }
        return true;
    }
    return a == b;  // números inteiros e decimais comparam pelo valor
}

bool IsReplace(const Json& node) {
    const auto it = node.find("$replace");
    return it != node.end() && it->is_boolean() && it->get<bool>();
}

// Índice das chaves de um objeto mesclado: ordered_json procura em ordem, então um objeto grande ganha um mapa
// (sem ele, um objeto de 50 000 chaves custaria ~10^9 comparações por par de camadas).
class ChildIndex {
public:
    explicit ChildIndex(const Json* node) : node_(node && node->is_object() ? node : nullptr) {
        if (node_ && node_->size() > 16) {
            map_.reserve(node_->size());
            for (const auto& [key, value] : node_->items()) {
                map_.emplace(key, &value);
            }
        }
    }

    const Json* Find(const std::string& key) const {
        if (!node_) {
            return nullptr;
        }
        if (!map_.empty()) {
            const auto it = map_.find(key);
            return it == map_.end() ? nullptr : it->second;
        }
        const auto it = node_->find(key);
        return it == node_->end() ? nullptr : &*it;
    }

private:
    const Json* node_;
    std::unordered_map<std::string_view, const Json*> map_;
};

struct Loss {
    size_t count = 0;
    std::vector<std::string> keys;

    void Add(const std::vector<std::string>& path) {
        ++count;
        if (keys.size() < kConflictKeyExamples) {
            std::string text;
            for (const auto& key : path) {
                text += (text.empty() ? "" : ".") + key;
            }
            keys.push_back(text.empty() ? std::string("(raiz)") : text);
        }
    }
};

// Percorre a camada de baixo junto com o resultado do merge. Cada folha que ela traz precisa sobreviver: valor
// e array com o mesmo valor, null com a chave ainda ausente, objeto vazio ou com $replace ainda objeto. Em cena
// e texto, $schema de topo e a chave $replace não são dados; no registro, $schema não é dado em nível nenhum e
// $replace é uma chave como outra (MergeSchemaFreeDocuments).
void CompareNode(const Json& lower, const Json* merged, bool directives, std::vector<std::string>& path, Loss& loss) {
    if (!path.empty() && (lower.empty() || (directives && IsReplace(lower))) && !(merged && merged->is_object())) {
        loss.Add(path);
    }
    const ChildIndex index(merged);
    for (const auto& [key, value] : lower.items()) {
        if ((directives && key == "$replace") || (key == "$schema" && (!directives || path.empty()))) {
            continue;
        }
        const Json* other = index.Find(key);
        path.push_back(key);
        if (value.is_null()) {
            if (other && !other->is_null()) {
                loss.Add(path);
            }
        } else if (value.is_object()) {
            CompareNode(value, other, directives, path, loss);
        } else if (!other || !SameValue(*other, value)) {
            loss.Add(path);
        }
        path.pop_back();
    }
}

// MergeValueRemovingNulls do json_merge.cpp (merge do unbound/scenes.json).
void MergeRegistry(Json& base, const Json& patch) {
    for (const auto& [key, value] : patch.items()) {
        if (key == "$schema") {
            continue;
        }
        if (value.is_null()) {
            base.erase(key);
            continue;
        }
        if (!value.is_object()) {
            base[key] = value;
            continue;
        }
        auto current = base.find(key);
        if (current == base.end() || !current->is_object()) {
            base[key] = Json::object();
            current = base.find(key);
        }
        MergeRegistry(*current, value);
    }
}

bool EndsWith(const std::string& text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

bool ListZipNames(const std::string& bytes, std::vector<std::string>& names, std::string& error) {
    return ListNames(
        bytes.size(),
        [&bytes](uint64_t offset, size_t size, std::string& out) {
            if (offset > bytes.size() || size > bytes.size() - offset) {
                return false;
            }
            out.assign(bytes, static_cast<size_t>(offset), size);
            return true;
        },
        names, error);
}

bool ListArchiveNames(const std::string& path, std::vector<std::string>& names, std::string& error) {
    names.clear();
    std::error_code code;
    const fs::path root(path);
    if (fs::is_directory(root, code)) {
        for (fs::recursive_directory_iterator it(root, code), end; !code && it != end; it.increment(code)) {
            if (it->is_regular_file(code)) {
                names.push_back(fs::relative(it->path(), root, code).generic_string());
            }
        }
        if (code) {
            error = "pasta ilegível: " + code.message();
            return false;
        }
        return true;
    }
    const auto size = fs::file_size(root, code);
    if (code) {
        error = "arquivo ilegível: " + code.message();
        return false;
    }
    std::ifstream file(root, std::ios::binary);
    if (!file) {
        error = "arquivo não abriu";
        return false;
    }
    return ListNames(
        size,
        [&file](uint64_t offset, size_t length, std::string& out) {
            out.resize(length);
            file.clear();
            file.seekg(static_cast<std::streamoff>(offset));
            return static_cast<bool>(file.read(out.data(), static_cast<std::streamsize>(length)));
        },
        names, error);
}

bool IsComparableEntry(const std::string& name) {
    return !name.empty() && name.back() != '/' && name != "unbound.json";
}

std::string ArchiveLabel(const std::string& archive) {
    std::string path = archive;
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    size_t start = std::string::npos;
    if (const auto found = lower.rfind("/mods/"); found != std::string::npos) {
        start = found + 6;
    } else if (lower.compare(0, 5, "mods/") == 0) {
        start = 5;
    }
    if (start == std::string::npos || start >= path.size()) {
        const auto slash = path.rfind('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }
    std::string label = path.substr(start);
    if (label.compare(0, 15, ".shiplua-cache/") == 0) {
        label.erase(0, 15);
    }
    return label;
}

MergeRule RuleFor(const std::string& path, const std::vector<LayerDocument>& layers) {
    if (path == "unbound/scenes.json") {
        return MergeRule::Registry;
    }
    if (path.compare(0, 5, "text/") == 0 && EndsWith(path, "/messages.json")) {
        return MergeRule::Text;
    }
    // Documento tipado: o carregador do host reconhece JSON pelo '{' no primeiro byte e transcodifica pelo
    // $schema da camada mais alta que o traz (camada parcial pode omiti-lo quando uma de baixo o traz), sem olhar
    // prefixo nem extensão. Sem $schema "unbound/..." em camada nenhuma, o VFS entrega o arquivo de cima inteiro.
    for (const auto& layer : layers) {
        if (layer.json.empty() || layer.json.front() != '{' || !JsonDepthWithin(layer.json)) {
            continue;
        }
        const Json doc = ParseJson(layer.json);
        if (!doc.is_object()) {
            continue;
        }
        const auto schema = doc.find("$schema");
        if (schema != doc.end() && schema->is_string() && schema->get<std::string>().compare(0, 8, "unbound/") == 0) {
            return MergeRule::Scene;
        }
    }
    return MergeRule::WholeFile;
}

DocumentAnalysis AnalyzeDocument(const std::vector<LayerDocument>& layers, MergeRule rule) {
    DocumentAnalysis analysis;
    if (rule == MergeRule::WholeFile) {
        analysis.compared = layers.size();
        analysis.identical = true;
        for (size_t low = 0; low + 1 < layers.size(); ++low) {
            if (layers[low].json == layers.back().json) {
                continue;
            }
            analysis.identical = false;
            analysis.conflicts.push_back({ layers[low].archive, layers.back().archive, 1, {}, true });
        }
        return analysis;
    }
    // Mesmo parse do jogo: comentários aceitos; em cena, camada sem '{' no primeiro byte é pulada (MergeLayers
    // estrito). Camada que o jogo não usa fica de fora da comparação, com uma nota.
    const bool scene = rule == MergeRule::Scene;
    const bool directives = rule != MergeRule::Registry;
    std::vector<std::pair<const LayerDocument*, Json>> docs;
    for (const auto& layer : layers) {
        const std::string label = ArchiveLabel(layer.archive);
        if (scene && (layer.json.empty() || layer.json.front() != '{')) {
            analysis.notes.push_back(label + ": o primeiro byte não é '{'; o jogo pula esta camada");
            continue;
        }
        if (!JsonDepthWithin(layer.json)) {
            analysis.notes.push_back(label + ": aninhamento demais; o jogo pula esta camada");
            continue;
        }
        Json doc = ParseJson(layer.json);
        if (doc.is_discarded() || !doc.is_object()) {
            analysis.notes.push_back(label + (directives ? ": JSON inválido; o jogo pula esta camada"
                                                          : ": JSON inválido; o jogo recusa o registro inteiro"));
            continue;
        }
        if (directives) {
            doc.erase("$schema");
        }
        docs.emplace_back(&layer, std::move(doc));
    }
    // Pares crescem com o quadrado das camadas: acima do teto, só as camadas mais altas (as que decidem o
    // resultado) entram, com nota.
    if (docs.size() > kMaxComparedLayers) {
        analysis.notes.push_back(std::to_string(docs.size()) + " camadas de mod; só as " +
                                 std::to_string(kMaxComparedLayers) + " mais altas comparadas");
        docs.erase(docs.begin(), docs.end() - static_cast<std::ptrdiff_t>(kMaxComparedLayers));
    }
    analysis.compared = docs.size();
    for (size_t low = 0; low < docs.size(); ++low) {
        for (size_t high = low + 1; high < docs.size(); ++high) {
            Json merged = directives ? docs[low].second : Json::object();
            if (directives) {
                MergeJson(merged, docs[high].second);
            } else {
                MergeRegistry(merged, docs[low].second);
                MergeRegistry(merged, docs[high].second);
            }
            Loss loss;
            std::vector<std::string> path;
            CompareNode(docs[low].second, &merged, directives, path, loss);
            if (loss.count) {
                analysis.conflicts.push_back(
                    { docs[low].first->archive, docs[high].first->archive, loss.count, std::move(loss.keys), false });
            }
        }
    }
    return analysis;
}

} // namespace LinkSpanUnbound
