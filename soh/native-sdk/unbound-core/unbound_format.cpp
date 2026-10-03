#include "unbound_format.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace LinkSpanUnbound {
namespace {

constexpr const char* kReplace = "$replace";
constexpr const char* kOrder = "$order";
constexpr const char* kSchema = "$schema";

bool IsReplace(const Json& obj) {
    const auto it = obj.find(kReplace);
    return it != obj.end() && it->is_boolean() && it->get<bool>();
}

bool IsIntegerKey(const std::string& key, long long& value) {
    if (key.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    value = std::strtoll(key.c_str(), &end, 10);
    return errno == 0 && end != nullptr && *end == '\0';
}

bool IsDigitOf(char c, int base) {
    if (c >= '0' && c <= '9') {
        return true;
    }
    return base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
}

// Objetos até este tamanho procuram chave em ordem (mais barato que montar um índice).
constexpr size_t kLinearKeys = 16;

// Monta o DOM pelo SAX do nlohmann com um índice de chaves por objeto em construção.
class DomBuilder {
  public:
    Json root;

    bool null() {
        *Slot() = nullptr;
        return true;
    }
    bool boolean(bool value) {
        *Slot() = value;
        return true;
    }
    bool number_integer(Json::number_integer_t value) {
        *Slot() = value;
        return true;
    }
    bool number_unsigned(Json::number_unsigned_t value) {
        *Slot() = value;
        return true;
    }
    bool number_float(Json::number_float_t value, const Json::string_t&) {
        *Slot() = value;
        return true;
    }
    bool string(Json::string_t& value) {
        *Slot() = std::move(value);
        return true;
    }
    bool binary(Json::binary_t& value) {
        *Slot() = Json::binary(std::move(value));
        return true;
    }
    bool start_object(std::size_t) {
        Json* slot = Slot();
        *slot = Json::object();
        mFrames.push_back({ slot, {}, {} });
        return true;
    }
    bool key(Json::string_t& key) {
        mFrames.back().key = std::move(key);
        return true;
    }
    bool end_object() {
        mFrames.pop_back();
        return true;
    }
    bool start_array(std::size_t) {
        Json* slot = Slot();
        *slot = Json::array();
        mFrames.push_back({ slot, {}, {} });
        return true;
    }
    bool end_array() {
        mFrames.pop_back();
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) {
        return false;
    }

  private:
    struct Frame {
        Json* value;
        std::unordered_map<std::string, size_t> index; // vazio até o objeto passar de kLinearKeys
        std::string key;
    };
    std::vector<Frame> mFrames;

    // Onde vai o próximo valor. O pai não muda enquanto um filho é montado, então o ponteiro do filho vale.
    Json* Slot() {
        if (mFrames.empty()) {
            return &root;
        }
        Frame& top = mFrames.back();
        if (top.value->is_array()) {
            top.value->push_back(nullptr);
            return &top.value->back();
        }
        auto& object = top.value->get_ref<Json::object_t&>();
        if (top.index.empty() && object.size() < kLinearKeys) {
            for (auto& [name, value] : object) {
                if (name == top.key) {
                    return &value;
                }
            }
        } else {
            if (top.index.empty()) {
                for (size_t i = 0; i < object.size(); ++i) {
                    top.index.emplace(object.data()[i].first, i);
                }
            }
            const auto [found, inserted] = top.index.try_emplace(top.key, object.size());
            if (!inserted) {
                return &object.data()[found->second].second;
            }
        }
        object.emplace_back(std::move(top.key), nullptr);
        return &object.back().second;
    }
};

} // namespace

Json ParseJson(const std::string& text, bool ignoreComments) {
    DomBuilder builder;
    if (!Json::sax_parse(text, &builder, Json::input_format_t::json, true, ignoreComments)) {
        return Json(Json::value_t::discarded);
    }
    return std::move(builder.root);
}

void MergeJson(Json& base, const Json& overlay) {
    if (!base.is_object() || !overlay.is_object() || IsReplace(overlay)) {
        base = overlay;
        return;
    }
    if (base.size() + overlay.size() <= kLinearKeys) {
        for (const auto& [key, value] : overlay.items()) {
            if (value.is_null()) {
                base.erase(key);
            } else if (value.is_object() && base.contains(key) && base[key].is_object()) {
                MergeJson(base[key], value);
            } else {
                base[key] = value;
            }
        }
        return;
    }
    // Objeto grande (de um lado ou do outro): índice das chaves em vez de uma busca linear por chave de cima. A regra
    // é a mesma do laço acima, inclusive com chave repetida no overlay: chave removida conta como ausente, e uma
    // menção seguinte a recria no fim; chave acrescentada entra no índice. As removidas saem numa passada só.
    auto& object = base.get_ref<Json::object_t&>();
    std::unordered_map<std::string, size_t> index;
    index.reserve(object.size() + overlay.size());
    for (size_t i = 0; i < object.size(); ++i) {
        index.emplace(object.data()[i].first, i);
    }
    std::vector<bool> removed(object.size(), false);
    bool anyRemoved = false;
    for (const auto& [key, value] : overlay.items()) {
        const auto found = index.find(key);
        const bool present = found != index.end() && !removed[found->second];
        if (!present) {
            if (!value.is_null()) {
                object.emplace_back(key, value);
                removed.push_back(false);
                index[key] = object.size() - 1;
            }
        } else if (value.is_null()) {
            removed[found->second] = true;
            anyRemoved = true;
        } else if (value.is_object() && object.data()[found->second].second.is_object()) {
            MergeJson(object.data()[found->second].second, value);
        } else {
            object.data()[found->second].second = value;
        }
    }
    if (anyRemoved) {
        Json kept = Json::object();
        auto& keptObject = kept.get_ref<Json::object_t&>();
        keptObject.reserve(object.size());
        for (size_t i = 0; i < object.size(); ++i) {
            if (!removed[i]) {
                keptObject.emplace_back(object.data()[i].first, std::move(object.data()[i].second));
            }
        }
        base = std::move(kept);
    }
}

void StripDirectives(Json& doc) {
    if (!doc.is_object()) {
        return;
    }
    // Uma passada: apagar um a um no ordered_map (um vetor) custava o quadrado do número de nulls.
    auto& object = doc.get_ref<Json::object_t&>();
    const auto dropped = [](const auto& entry) { return entry.first == kReplace || entry.second.is_null(); };
    if (std::any_of(object.begin(), object.end(), dropped)) {
        Json kept = Json::object();
        auto& keptObject = kept.get_ref<Json::object_t&>();
        keptObject.reserve(object.size());
        for (auto& entry : object) {
            if (!dropped(entry)) {
                keptObject.emplace_back(entry.first, std::move(entry.second));
            }
        }
        doc = std::move(kept);
    }
    for (auto& entry : doc.get_ref<Json::object_t&>()) {
        StripDirectives(entry.second);
    }
}

bool MergeLayers(const std::vector<LayerDocument>& layers, bool strictStart, MergedDocument& out) {
    out = {};
    Json merged;
    for (const auto& layer : layers) {
        if (strictStart && (layer.json.empty() || layer.json.front() != '{')) {
            out.notes.push_back(layer.archive + ": o primeiro byte não é '{'; camada pulada");
            continue;
        }
        if (!JsonDepthWithin(layer.json)) {
            out.notes.push_back(layer.archive + ": aninhamento acima de " + std::to_string(kMaxJsonDepth) +
                                " níveis; camada pulada");
            continue;
        }
        Json doc = ParseJson(layer.json);
        if (doc.is_discarded() || !doc.is_object()) {
            out.notes.push_back(layer.archive + ": JSON inválido ou raiz que não é objeto; camada pulada");
            continue;
        }
        // Só o $schema de topo em string conta, e o da camada mais alta vence (§3.6).
        const auto schema = doc.find(kSchema);
        if (schema != doc.end() && schema->is_string()) {
            out.schema = schema->get<std::string>();
        }
        doc.erase(kSchema);
        if (out.layersUsed == 0) {
            merged = std::move(doc);
        } else {
            MergeJson(merged, doc);
        }
        ++out.layersUsed;
    }
    if (out.layersUsed == 0) {
        return false;
    }
    StripDirectives(merged);
    out.doc = std::move(merged);
    if (!out.schema.empty() && !ParseSchema(out.schema, out.type, out.version)) {
        out.type.clear();
        out.version = 0;
    }
    return true;
}

std::vector<ListItem> ListItems(const Json& list) {
    std::vector<ListItem> items;
    if (!list.is_object()) {
        return items;
    }
    const auto& object = list.get_ref<const Json::object_t&>();
    std::unordered_set<std::string> ordered;
    const auto order = list.find(kOrder);
    if (order != list.end() && order->is_array()) {
        std::unordered_map<std::string, const Json*> byKey;
        byKey.reserve(object.size());
        for (const auto& [key, value] : object) {
            byKey.emplace(key, &value);
        }
        for (const auto& key : *order) {
            if (!key.is_string()) {
                continue;
            }
            const std::string& name = key.get_ref<const std::string&>();
            const auto found = byKey.find(name);
            if (!name.empty() && name[0] != '$' && found != byKey.end() && ordered.insert(name).second) {
                items.emplace_back(name, found->second);
            }
        }
    }
    struct Rest {
        bool notInteger;
        long long number;
        const std::string* key;
        const Json* value;
    };
    std::vector<Rest> rest;
    rest.reserve(object.size());
    for (const auto& [key, value] : object) {
        if ((!key.empty() && key[0] == '$') || ordered.count(key)) {
            continue;
        }
        long long number = 0;
        const bool isInteger = IsIntegerKey(key, number);
        rest.push_back({ !isInteger, isInteger ? number : 0, &key, &value });
    }
    std::sort(rest.begin(), rest.end(), [](const Rest& a, const Rest& b) {
        if (a.notInteger != b.notInteger) {
            return a.notInteger < b.notInteger;
        }
        if (a.number != b.number) {
            return a.number < b.number;
        }
        return *a.key < *b.key;
    });
    for (const auto& entry : rest) {
        items.emplace_back(*entry.key, entry.value);
    }
    return items;
}

std::vector<std::string> ListKeys(const Json& list) {
    std::vector<std::string> keys;
    for (auto& item : ListItems(list)) {
        keys.push_back(std::move(item.first));
    }
    return keys;
}

std::vector<ListItem> PositionalItems(const Json& list, const std::string& what) {
    if (list.is_object() && list.contains(kOrder)) {
        throw DocumentError(what + ": $order não vale numa lista posicional");
    }
    std::vector<ListItem> items = ListItems(list);
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].first != std::to_string(i)) {
            throw DocumentError(what + ": lista posicional com buraco no índice " + std::to_string(i) +
                                " (chave '" + items[i].first + "')");
        }
    }
    return items;
}

std::vector<std::string> PositionalKeys(const Json& list, const std::string& what) {
    std::vector<std::string> keys;
    for (auto& item : PositionalItems(list, what)) {
        keys.push_back(std::move(item.first));
    }
    return keys;
}

bool ParseIntString(const std::string& text, int64_t& out) {
    size_t i = 0;
    bool negative = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }
    int base = 10;
    if (text.compare(i, 2, "0x") == 0 || text.compare(i, 2, "0X") == 0) {
        base = 16;
        i += 2;
    }
    if (i >= text.size()) {
        return false;
    }
    for (size_t j = i; j < text.size(); ++j) {
        if (!IsDigitOf(text[j], base)) {
            return false;
        }
    }
    errno = 0;
    const unsigned long long magnitude = std::strtoull(text.c_str() + i, nullptr, base);
    if (errno == ERANGE || magnitude > static_cast<unsigned long long>(INT64_MAX)) {
        return false;
    }
    out = negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
    return true;
}

bool ParseNumberString(const std::string& text, double& out) {
    int64_t integer = 0;
    if (ParseIntString(text, integer)) {
        out = static_cast<double>(integer);
        return true;
    }
    // stod aceita espaço à esquerda, hex float, inf e nan; o formato não aceita nenhum deles.
    if (text.empty() || text.find_first_of(" \t\r\n\f\vxXnNiI") != std::string::npos) {
        return false;
    }
    try {
        size_t consumed = 0;
        out = std::stod(text, &consumed);
        return consumed == text.size();
    } catch (...) { return false; }
}

int64_t ToInt(const Json& value, int64_t fallback) {
    if (value.is_number_integer()) {
        return value.is_number_unsigned() ? static_cast<int64_t>(value.get<uint64_t>()) : value.get<int64_t>();
    }
    if (value.is_number_float()) {
        const double number = value.get<double>();
        if (!std::isfinite(number)) {
            return fallback;
        }
        return static_cast<int64_t>(number); // truncado em direção a zero (§2)
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? 1 : 0;
    }
    if (value.is_string()) {
        int64_t parsed = 0;
        return ParseIntString(value.get<std::string>(), parsed) ? parsed : fallback;
    }
    return fallback;
}

double ToNumber(const Json& value, double fallback) {
    if (value.is_number()) {
        return value.get<double>();
    }
    if (value.is_string()) {
        double parsed = 0.0;
        return ParseNumberString(value.get<std::string>(), parsed) ? parsed : fallback;
    }
    return fallback;
}

bool HasNumber(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it == obj.end()) {
        return false;
    }
    return !std::isnan(ToNumber(*it, std::numeric_limits<double>::quiet_NaN()));
}

int64_t Field(const Json& obj, const char* key, int64_t fallback) {
    if (!obj.is_object()) {
        return fallback;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? fallback : ToInt(*it, fallback);
}

double NumberField(const Json& obj, const char* key, double fallback) {
    if (!obj.is_object()) {
        return fallback;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? fallback : ToNumber(*it, fallback);
}

std::string PathField(const Json& obj, const char* key) {
    if (!obj.is_object()) {
        return "";
    }
    const auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : "";
}

const Json& Sub(const Json& obj, const char* key) {
    static const Json empty = Json::object();
    if (!obj.is_object()) {
        return empty;
    }
    const auto it = obj.find(key);
    return it != obj.end() && it->is_object() ? *it : empty;
}

const Json& SubArray(const Json& obj, const char* key) {
    static const Json empty = Json::array();
    if (!obj.is_object()) {
        return empty;
    }
    const auto it = obj.find(key);
    return it != obj.end() && it->is_array() ? *it : empty;
}

Vec3 ReadVec3(const Json& value) {
    Vec3 out;
    if (value.is_array() && value.size() >= 3) {
        out.x = ToNumber(value[0]);
        out.y = ToNumber(value[1]);
        out.z = ToNumber(value[2]);
    }
    return out;
}

bool ParseSchema(const std::string& schema, std::string& type, int& version) {
    const size_t slash = schema.rfind('/');
    if (slash == std::string::npos || slash == 0) {
        return false;
    }
    const std::string suffix = schema.substr(slash + 1);
    if (suffix.empty() || suffix.size() > 9 || suffix.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    type = schema.substr(0, slash);
    version = std::stoi(suffix);
    return true;
}

} // namespace LinkSpanUnbound
