#include "unbound_format.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

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

} // namespace

void MergeJson(Json& base, const Json& overlay) {
    if (!base.is_object() || !overlay.is_object() || IsReplace(overlay)) {
        base = overlay;
        return;
    }
    for (const auto& [key, value] : overlay.items()) {
        if (value.is_null()) {
            base.erase(key);
        } else if (value.is_object() && base.contains(key) && base[key].is_object()) {
            MergeJson(base[key], value);
        } else {
            base[key] = value;
        }
    }
}

void StripDirectives(Json& doc) {
    if (!doc.is_object()) {
        return;
    }
    doc.erase(kReplace);
    for (auto it = doc.begin(); it != doc.end();) {
        if (it->is_null()) {
            it = doc.erase(it);
        } else {
            StripDirectives(*it);
            ++it;
        }
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
        Json doc = Json::parse(layer.json, nullptr, false, true);
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

std::vector<std::string> ListKeys(const Json& list) {
    std::vector<std::string> keys;
    if (!list.is_object()) {
        return keys;
    }
    const auto order = list.find(kOrder);
    if (order != list.end() && order->is_array()) {
        for (const auto& key : *order) {
            if (!key.is_string()) {
                continue;
            }
            const std::string name = key.get<std::string>();
            if (!name.empty() && name[0] != '$' && list.contains(name) &&
                std::find(keys.begin(), keys.end(), name) == keys.end()) {
                keys.push_back(name);
            }
        }
    }
    std::vector<std::pair<bool, std::pair<long long, std::string>>> rest;
    for (const auto& [key, value] : list.items()) {
        if ((!key.empty() && key[0] == '$') || std::find(keys.begin(), keys.end(), key) != keys.end()) {
            continue;
        }
        long long number = 0;
        const bool isInteger = IsIntegerKey(key, number);
        rest.push_back({ !isInteger, { isInteger ? number : 0, key } });
    }
    std::sort(rest.begin(), rest.end());
    for (const auto& entry : rest) {
        keys.push_back(entry.second.second);
    }
    return keys;
}

std::vector<std::string> PositionalKeys(const Json& list, const std::string& what) {
    if (list.is_object() && list.contains(kOrder)) {
        throw DocumentError(what + ": $order não vale numa lista posicional");
    }
    std::vector<std::string> keys = ListKeys(list);
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] != std::to_string(i)) {
            throw DocumentError(what + ": lista posicional com buraco no índice " + std::to_string(i) +
                                " (chave '" + keys[i] + "')");
        }
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
