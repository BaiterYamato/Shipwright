#include "unbound_docs.h"

#include <map>

#include "unbound_format.h"

namespace LinkSpanUnbound {
namespace {

constexpr uint32_t kMaxMessageId = 65534;

bool ReadVersion(const Json& obj, const char* key, int fallback, int& out) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        out = fallback;
        return true;
    }
    if (!it->is_number_integer()) {
        return false;
    }
    out = it->get<int>();
    return true;
}

bool IsReplace(const Json& obj) {
    const auto it = obj.find("$replace");
    return it != obj.end() && it->is_boolean() && it->get<bool>();
}

bool ParseMessageId(const std::string& key, uint32_t& id) {
    int64_t value = 0;
    if (!ParseIntString(key, value) || value < 0 || value > kMaxMessageId) {
        return false;
    }
    id = static_cast<uint32_t>(value);
    return true;
}

} // namespace

ManifestCheck CheckManifest(const std::string& json) {
    ManifestCheck check;
    if (!JsonDepthWithin(json)) {
        check.note = "unbound.json com aninhamento acima de " + std::to_string(kMaxJsonDepth) + " níveis";
        return check;
    }
    const Json doc = ParseJson(json);
    if (doc.is_discarded() || !doc.is_object()) {
        check.note = "unbound.json não é um objeto JSON";
        return check;
    }
    int formatVersion = 0;
    if (!ReadVersion(doc, "formatVersion", kReaderFormatVersion, formatVersion) ||
        formatVersion != kReaderFormatVersion) {
        check.note = "formatVersion diferente de 2";
        return check;
    }
    int required = formatVersion;
    const auto requirement = doc.find("requires");
    if (requirement != doc.end() && requirement->is_object() &&
        (!ReadVersion(*requirement, "formatVersion", formatVersion, required) || required != kReaderFormatVersion)) {
        check.note = "requires.formatVersion diferente de 2";
        return check;
    }
    check.valid = true;
    const auto features = doc.find("features");
    if (features != doc.end() && features->is_array()) {
        for (const auto& feature : *features) {
            if (feature.is_string() && feature.get<std::string>() == "scenes") {
                check.base = true;
            }
        }
    }
    return check;
}

std::string DecodeMessageText(const std::string& utf8, uint32_t& replaced) {
    std::string bytes;
    bytes.reserve(utf8.size());
    replaced = 0;
    for (size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<uint8_t>(utf8[i]);
        uint32_t codePoint = 0;
        size_t length = 1;
        if (lead < 0x80) {
            codePoint = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            codePoint = lead & 0x1F;
            length = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            codePoint = lead & 0x0F;
            length = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            codePoint = lead & 0x07;
            length = 4;
        } else {
            codePoint = 0xFFFD;
        }
        if (i + length > utf8.size()) {
            codePoint = 0xFFFD;
            length = utf8.size() - i;
        } else {
            for (size_t j = 1; j < length; ++j) {
                codePoint = (codePoint << 6) | (static_cast<uint8_t>(utf8[i + j]) & 0x3F);
            }
        }
        if (codePoint <= 0xFF) {
            bytes.push_back(static_cast<char>(codePoint));
        } else {
            bytes.push_back('?');
            ++replaced;
        }
        i += length;
    }
    return bytes;
}

bool BuildTextTable(const std::vector<LayerDocument>& layers, TextTable& out) {
    out = {};
    Json merged;
    // Chave de "messages" -> o valor mais alto que a menciona é null.
    std::map<std::string, bool> nulled;
    for (const auto& layer : layers) {
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
        doc.erase("$schema");
        const auto messages = doc.find("messages");
        if (IsReplace(doc) || (messages != doc.end() && messages->is_object() && IsReplace(*messages))) {
            out.replaceTable = true;
            nulled.clear();
        }
        if (messages != doc.end() && messages->is_object()) {
            for (const auto& [key, value] : messages->items()) {
                if (!key.empty() && key[0] != '$') {
                    nulled[key] = value.is_null();
                }
            }
        }
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
    const auto messages = merged.find("messages");
    if (messages == merged.end()) {
        return true;
    }
    if (!messages->is_object()) {
        out.notes.push_back("messages não é um objeto; nenhuma mensagem aplicada");
        return true;
    }
    std::map<uint32_t, size_t> byId;
    for (const auto& [key, item] : ListItems(*messages)) {
        const Json& value = *item;
        uint32_t id = 0;
        if (!ParseMessageId(key, id)) {
            out.notes.push_back("mensagem '" + key + "' ignorada: id fora de 0-65534");
            continue;
        }
        if (!value.is_object()) {
            out.notes.push_back("mensagem " + key + " ignorada: não é objeto");
            continue;
        }
        const auto text = value.find("text");
        if (text == value.end() || !text->is_string()) {
            out.notes.push_back("mensagem " + key + " ignorada: sem \"text\"");
            continue;
        }
        TextMessage message;
        message.id = id;
        message.box = static_cast<uint8_t>(Field(value, "box", 0) & 0xF);
        message.ypos = static_cast<uint8_t>(Field(value, "ypos", 0) & 0xF);
        uint32_t replaced = 0;
        message.bytes = DecodeMessageText(text->get<std::string>(), replaced);
        if (replaced) {
            out.notes.push_back("mensagem " + key + ": " + std::to_string(replaced) +
                                " caracteres acima de U+00FF viraram '?'");
        }
        if (!message.bytes.empty() && message.bytes.back() == '\x02') {
            message.bytes.pop_back();
        }
        if (message.bytes.size() > kMaxMessageBytes) {
            out.notes.push_back("mensagem " + key + ": " + std::to_string(message.bytes.size() + 1) +
                                " bytes, truncada em 8192");
            message.bytes.resize(kMaxMessageBytes);
        }
        const auto found = byId.find(id);
        if (found != byId.end()) {
            out.notes.push_back("mensagem " + key + " repete um id já definido por outra chave; a última vale");
            out.messages[found->second] = std::move(message);
        } else {
            byId.emplace(id, out.messages.size());
            out.messages.push_back(std::move(message));
        }
    }
    for (const auto& [key, isNull] : nulled) {
        uint32_t id = 0;
        if (isNull && !messages->contains(key) && ParseMessageId(key, id) && !byId.contains(id)) {
            out.removed.push_back(id);
        }
    }
    return true;
}

} // namespace LinkSpanUnbound
