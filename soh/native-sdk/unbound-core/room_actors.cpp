#include "room_actors.h"
#include "actor_registry.h"
#include "unbound_format.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace LinkSpanUnbound {
namespace {

using Json = nlohmann::ordered_json;

constexpr const char* ROOM_SCHEMA = "unbound/room/1";

// SPEC §3: objetos mesclam por chave, null apaga, arrays e escalares substituem e
// "$replace": true descarta o valor das camadas de baixo (a chave sai do resultado).
void MergeLayer(Json& base, const Json& patch);

// Objeto grande: mesma regra do MergeLayer com índice das chaves (lista de atores com milhares de entradas).
void MergeLayerIndexed(Json& base, const Json& patch) {
    auto& object = base.get_ref<Json::object_t&>();
    std::unordered_map<std::string, size_t> index;
    index.reserve(object.size() + patch.size());
    for (size_t i = 0; i < object.size(); ++i) {
        index.emplace(object.data()[i].first, i);
    }
    std::vector<bool> removed(object.size(), false);
    bool anyRemoved = false;
    for (const auto& [key, value] : patch.items()) {
        const auto found = index.find(key);
        const bool present = found != index.end() && !removed[found->second];
        if (value.is_null()) {
            if (present) {
                removed[found->second] = true;
                anyRemoved = true;
            }
            continue;
        }
        size_t at = present ? found->second : object.size();
        if (!present) {
            object.emplace_back(key, value.is_object() ? Json::object() : value);
            removed.push_back(false);
            index[key] = at;
            if (!value.is_object()) {
                continue;
            }
        } else if (!value.is_object()) {
            object.data()[at].second = value;
            continue;
        }
        Json& current = object.data()[at].second;
        const auto replace = value.find("$replace");
        if (!current.is_object() || (replace != value.end() && replace->is_boolean() && replace->get<bool>())) {
            current = Json::object();
        }
        MergeLayer(current, value);
        current.erase("$replace");
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

void MergeLayer(Json& base, const Json& patch) {
    if (base.is_object() && base.size() + patch.size() > 16) {
        MergeLayerIndexed(base, patch);
        return;
    }
    for (const auto& [key, value] : patch.items()) {
        if (value.is_null()) {
            base.erase(key);
            continue;
        }
        if (!value.is_object()) {
            base[key] = value;
            continue;
        }
        auto current = base.find(key);
        const auto replace = value.find("$replace");
        if (current == base.end() || !current->is_object() ||
            (replace != value.end() && replace->is_boolean() && replace->get<bool>())) {
            base[key] = Json::object();
            current = base.find(key);
        }
        MergeLayer(*current, value);
        current->erase("$replace");
    }
}

// Inteiro do §2: número (fração truncada), string decimal ou 0x inteira, booleano.
bool ReadInteger(const Json& value, int64_t& output) {
    if (value.is_boolean()) {
        output = value.get<bool>() ? 1 : 0;
        return true;
    }
    if (value.is_number_integer()) {
        output = value.is_number_unsigned() ? static_cast<int64_t>(value.get<uint64_t>()) : value.get<int64_t>();
        return true;
    }
    if (value.is_number_float()) {
        const double number = value.get<double>();
        if (!std::isfinite(number)) {
            return false;
        }
        output = static_cast<int64_t>(std::trunc(number));
        return true;
    }
    if (!value.is_string()) {
        return false;
    }
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text.find_first_of(" \t\r\n") != std::string::npos) {
        return false;
    }
    size_t start = (text[0] == '-' || text[0] == '+') ? 1 : 0;
    const bool hex = text.size() > start + 2 && text[start] == '0' && (text[start + 1] == 'x' || text[start + 1] == 'X');
    const char* begin = text.c_str();
    char* end = nullptr;
    const long long parsed = std::strtoll(begin, &end, hex ? 16 : 10);
    if (end != begin + text.size() || end == begin + start) {
        return false;
    }
    output = parsed;
    return true;
}

// Guarda na largura do campo do motor, com wrap (§2).
int16_t Wrap16(int64_t value) {
    return static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint64_t>(value)));
}

int16_t ReadField(const Json& entry, const char* key) {
    const auto found = entry.find(key);
    int64_t value = 0;
    return found != entry.end() && ReadInteger(*found, value) ? Wrap16(value) : 0;
}

// Número do §2: JSON numérico (fração mantida), string decimal/exponencial ou 0x inteira. Booleano não é número.
bool ReadNumber(const Json& value, double& output) {
    if (value.is_number()) {
        output = value.get<double>();
        return std::isfinite(output);
    }
    if (!value.is_string()) {
        return false;
    }
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text.find_first_of(" \t\r\n") != std::string::npos) {
        return false;
    }
    int64_t integer = 0;
    const size_t start = (text[0] == '-' || text[0] == '+') ? 1 : 0;
    if (text.size() > start + 2 && text[start] == '0' && (text[start + 1] == 'x' || text[start + 1] == 'X')) {
        if (!ReadInteger(value, integer)) {
            return false;
        }
        output = static_cast<double>(integer);
        return true;
    }
    char* end = nullptr;
    output = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(output);
}

// Posição do §2 (números, fração permitida): 3 elementos; menos que 3 vira [0,0,0].
void ReadPosition(const Json& entry, const char* key, float (&output)[3]) {
    output[0] = output[1] = output[2] = 0.0f;
    const auto found = entry.find(key);
    if (found == entry.end() || !found->is_array() || found->size() < 3) {
        return;
    }
    float values[3]{};
    for (size_t i = 0; i < 3; ++i) {
        double value = 0.0;
        if (!ReadNumber((*found)[i], value)) {
            return;
        }
        values[i] = static_cast<float>(value);
    }
    std::copy(values, values + 3, output);
}

// Vetor do §2: 3 elementos (os extras são ignorados); menos que 3 vira [0,0,0].
void ReadVector(const Json& entry, const char* key, int16_t (&output)[3]) {
    output[0] = output[1] = output[2] = 0;
    const auto found = entry.find(key);
    if (found == entry.end() || !found->is_array() || found->size() < 3) {
        return;
    }
    int16_t values[3]{};
    for (size_t i = 0; i < 3; ++i) {
        int64_t value = 0;
        if (!ReadInteger((*found)[i], value)) {
            return;
        }
        values[i] = Wrap16(value);
    }
    std::copy(values, values + 3, output);
}

// Ordem de chave do §3.5: inteiros decimais (com sinal opcional) em ordem numérica
// primeiro, depois o resto em ordem de bytes.
bool IntegerKey(const std::string& key, long long& value) {
    if (key.empty()) {
        return false;
    }
    const size_t start = (key[0] == '-' || key[0] == '+') ? 1 : 0;
    if (start == key.size() || key.find_first_not_of("0123456789", start) != std::string::npos) {
        return false;
    }
    value = std::strtoll(key.c_str(), nullptr, 10);
    return true;
}

bool KeyLess(const std::string& a, const std::string& b) {
    long long left = 0;
    long long right = 0;
    const bool leftInteger = IntegerKey(a, left);
    const bool rightInteger = IntegerKey(b, right);
    if (leftInteger != rightInteger) {
        return leftInteger;
    }
    if (leftInteger && left != right) {
        return left < right;
    }
    return a < b;
}

// Chave e valor na ordem do motor; o valor vem do próprio objeto, sem procurar a chave de novo.
std::vector<std::pair<std::string, const Json*>> EngineOrder(const Json& list) {
    std::vector<std::pair<std::string, const Json*>> order;
    std::unordered_map<std::string, const Json*> byKey;
    byKey.reserve(list.size());
    for (const auto& [key, value] : list.get_ref<const Json::object_t&>()) {
        byKey.emplace(key, &value);
    }
    std::set<std::string> seen;
    const auto explicitOrder = list.find("$order");
    if (explicitOrder != list.end() && explicitOrder->is_array()) {
        for (const auto& key : *explicitOrder) {
            if (!key.is_string()) {
                continue;
            }
            const std::string& name = key.get_ref<const std::string&>();
            const auto found = byKey.find(name);
            if (found != byKey.end() && name[0] != '$' && seen.insert(name).second) {
                order.emplace_back(name, found->second);
            }
        }
    }
    std::vector<std::pair<std::string, const Json*>> rest;
    for (const auto& [key, value] : list.get_ref<const Json::object_t&>()) {
        if (!key.empty() && key[0] != '$' && !seen.count(key)) {
            rest.emplace_back(key, &value);
        }
    }
    std::sort(rest.begin(), rest.end(), [](const auto& a, const auto& b) { return KeyLess(a.first, b.first); });
    order.insert(order.end(), rest.begin(), rest.end());
    return order;
}

} // namespace

std::string RoomDocumentPath(const std::string& roomPath, int32_t room) {
    if (room < 0) {
        return {};
    }
    std::string scene;
    bool masterQuest = false;
    size_t start = 0;
    while (start <= roomPath.size()) {
        const size_t end = std::min(roomPath.find('/', start), roomPath.size());
        const std::string part = roomPath.substr(start, end - start);
        if (part == "mq") {
            masterQuest = true;
        }
        static const std::string suffix = "_scene";
        if (part.size() > suffix.size() && part.compare(part.size() - suffix.size(), suffix.size(), suffix) == 0) {
            scene = part.substr(0, part.size() - suffix.size());
        }
        start = end + 1;
    }
    if (scene.empty()) {
        return {};
    }
    return "scenes/" + scene + (masterQuest ? "_mq" : "") + "/rooms/" + std::to_string(room) + ".json";
}

bool ApplyRoomActorLayers(const std::vector<RoomActor>& vanilla, int32_t setup,
                          const std::vector<LayerDocument>& layers, RoomActorsResult& output, std::string& error,
                          const std::function<int32_t(const std::string&)>& resolveActor) {
    output = {};
    error.clear();
    const std::string setupKey = std::to_string(setup);
    try {
        Json actors = Json::object();
        auto& actorObject = actors.get_ref<Json::object_t&>();
        actorObject.reserve(vanilla.size());
        for (size_t i = 0; i < vanilla.size(); ++i) {
            const auto& actor = vanilla[i];
            actorObject.emplace_back(std::to_string(i),
                                     Json{ { "id", actor.id },
                                           { "pos", { actor.pos[0], actor.pos[1], actor.pos[2] } },
                                           { "rot", { actor.rot[0], actor.rot[1], actor.rot[2] } },
                                           { "params", actor.params } });
        }
        Json merged = Json{ { "$schema", ROOM_SCHEMA }, { "setups", { { setupKey, { { "actors", actors } } } } } };
        for (const auto& layer : layers) {
            Json parsed;
            try {
                // §2: comentários são aceitos, e o primeiro byte precisa ser '{'.
                if (layer.json.empty() || layer.json[0] != '{') {
                    throw std::runtime_error("o primeiro byte precisa ser '{'");
                }
                if (!JsonDepthWithin(layer.json)) {
                    throw std::runtime_error("aninhamento acima de " + std::to_string(kMaxJsonDepth) + " níveis");
                }
                parsed = ParseJson(layer.json);
                if (parsed.is_discarded()) {
                    throw std::runtime_error("JSON inválido");
                }
            } catch (const std::exception& exception) {
                output.notes.push_back(layer.archive + ": camada pulada (" + exception.what() + ")");
                continue;
            }
            MergeLayer(merged, parsed);
            ++output.layersUsed;
        }
        const auto schema = merged.find("$schema");
        if (schema == merged.end() || !schema->is_string() || schema->get<std::string>() != ROOM_SCHEMA) {
            error = "$schema ausente ou diferente de unbound/room/1";
            return false;
        }
        const auto setups = merged.find("setups");
        if (setups == merged.end() || !setups->is_object() || !setups->contains(setupKey) ||
            !(*setups)[setupKey].is_object()) {
            return true;
        }
        const auto& setupObject = (*setups)[setupKey];
        const auto list = setupObject.find("actors");
        if (list == setupObject.end() || !list->is_object()) {
            return true;
        }
        for (const auto& [key, item] : EngineOrder(*list)) {
            const auto& entry = *item;
            if (!entry.is_object()) {
                output.notes.push_back("actors." + key + ": entrada ignorada (não é objeto)");
                continue;
            }
            RoomActor actor;
            if (!ResolveRoomActorId(entry, resolveActor, actor.id, output.notes, "actors." + key)) continue;
            ReadPosition(entry, "pos", actor.pos);
            ReadVector(entry, "rot", actor.rot);
            actor.params = ReadField(entry, "params");
            output.actors.push_back(actor);
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace LinkSpanUnbound
