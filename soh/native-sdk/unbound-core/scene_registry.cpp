#include "scene_registry.h"

#include <cstdint>
#include <limits>
#include <map>

#include "unbound_format.h"

namespace LinkSpanUnbound {
namespace {

constexpr int64_t MAX_SPAWN = 127;
constexpr int64_t MAX_TRANSITION = 127;
constexpr int64_t MAX_DRAW_CONFIG = 255;

// Chave ausente devolve o padrão; presente precisa ser inteira e ficar no intervalo.
bool ReadInteger(const Json& object, const char* key, int64_t fallback, int64_t minimum, int64_t maximum,
                 int64_t& output) {
    const auto found = object.find(key);
    if (found == object.end()) {
        output = fallback;
        return true;
    }
    if (!found->is_number_integer()) {
        return false;
    }
    if (found->is_number_unsigned() &&
        found->get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return false;
    }
    const int64_t value = found->get<int64_t>();
    if (value < minimum || value > maximum) {
        return false;
    }
    output = value;
    return true;
}

// Booleano ou 0/1, como no SPEC.
bool ReadFlag(const Json& object, const char* key, bool& output) {
    const auto found = object.find(key);
    if (found == object.end()) {
        output = false;
        return true;
    }
    if (found->is_boolean()) {
        output = found->get<bool>();
        return true;
    }
    if (found->is_number_integer() && (found->get<int64_t>() == 0 || found->get<int64_t>() == 1)) {
        output = found->get<int64_t>() == 1;
        return true;
    }
    return false;
}

// Unbound 0.8 (SPEC §7): número fixado por um mod deixou de ser lido. Dois mods que fixavam o mesmo colidiam e o
// segundo sumia; o jogo numera cena e entrada na ordem do registro e tudo as endereça pelo nome. Mods antigos
// ainda trazem as chaves, então a nota diz que elas não fazem nada.
void NoteNumberIgnored(const Json& definition, const char* key, const std::string& owner,
                       std::vector<std::string>& notes) {
    if (definition.contains(key)) {
        notes.push_back(owner + ": " + key + " é obsoleto e foi ignorado; o jogo numera e tudo usa o nome (SPEC §7)");
    }
}

// Ordem de registro (§3.5): $order, depois chaves inteiras em ordem numérica, depois o resto.
std::vector<std::pair<std::string, const Json*>> OrderedEntries(const Json& object) {
    return ListItems(object);
}

bool ReadEntrance(const std::string& scene, const std::string& key, const Json& definition, RegistryEntrance& entrance,
                  std::vector<std::string>& notes) {
    const std::string label = scene + "/" + key;
    entrance.key = key;
    NoteNumberIgnored(definition, "index", label, notes);
    int64_t number = 0;
    if (!ReadInteger(definition, "spawn", 0, 0, MAX_SPAWN, number)) {
        notes.push_back(label + ": spawn fora de 0-127");
        return false;
    }
    entrance.spawn = static_cast<uint8_t>(number);
    if (!ReadInteger(definition, "endTransition", 2, 0, MAX_TRANSITION, number)) {
        notes.push_back(label + ": endTransition fora de 0-127");
        return false;
    }
    entrance.endTransition = static_cast<uint8_t>(number);
    if (!ReadInteger(definition, "startTransition", 2, 0, MAX_TRANSITION, number)) {
        notes.push_back(label + ": startTransition fora de 0-127");
        return false;
    }
    entrance.startTransition = static_cast<uint8_t>(number);
    if (!ReadFlag(definition, "showTitleCard", entrance.showTitleCard) ||
        !ReadFlag(definition, "continueBgm", entrance.continueBgm)) {
        notes.push_back(label + ": showTitleCard e continueBgm aceitam booleano ou 0/1");
        return false;
    }
    if (definition.contains("layers")) {
        notes.push_back(label + ": layers é reservado e foi ignorado; as quatro camadas ficam iguais");
    }
    return true;
}

bool ReadHorse(const std::string& name, const Json& definition, RegistryScene& scene, std::vector<std::string>& notes) {
    const auto horse = definition.find("horse");
    if (horse == definition.end()) {
        return true;
    }
    if (horse->is_boolean()) {
        scene.horse = horse->get<bool>();
        return true;
    }
    const auto pos = horse->is_object() ? horse->find("pos") : horse->end();
    const auto angle = horse->is_object() ? horse->find("angle") : horse->end();
    if (!horse->is_object() || pos == horse->end() || !pos->is_array() || pos->size() != 3 ||
        angle == horse->end() || !angle->is_number_integer() || !(*pos)[0].is_number() ||
        !(*pos)[1].is_number() || !(*pos)[2].is_number() ||
        angle->get<int64_t>() < std::numeric_limits<int16_t>::min() ||
        angle->get<int64_t>() > std::numeric_limits<int16_t>::max()) {
        notes.push_back(name + ": horse recusado; use true ou {pos:[x,y,z],angle:s16}");
        return false;
    }
    scene.horse = true;
    scene.horseHasSpawn = true;
    scene.horseX = (*pos)[0].get<float>();
    scene.horseY = (*pos)[1].get<float>();
    scene.horseZ = (*pos)[2].get<float>();
    scene.horseAngle = static_cast<int16_t>(angle->get<int64_t>());
    return true;
}

} // namespace

bool ParseSceneRegistry(const std::string& json, SceneRegistryDocument& output, std::string& error) {
    output = {};
    error.clear();
    try {
        if (!JsonDepthWithin(json)) {
            error = "unbound/scenes.json com aninhamento acima de " + std::to_string(kMaxJsonDepth) + " níveis";
            return false;
        }
        const Json root = Json::parse(json, nullptr, true, true);
        if (!root.is_object()) {
            error = "unbound/scenes.json precisa ser um objeto";
            return false;
        }
        for (const auto& [key, value] : OrderedEntries(root)) {
            if (!value->is_object()) {
                output.notes.push_back(key + ": ignorada, não é objeto");
                continue;
            }
            RegistryScene scene;
            scene.name = key;
            const auto name = value->find("name");
            scene.displayName = name != value->end() && name->is_string() ? name->get<std::string>() : key;
            const auto path = value->find("scene");
            if (path == value->end() || !path->is_string() || path->get_ref<const std::string&>().empty()) {
                output.notes.push_back(key + ": recusada, sem \"scene\"");
                continue;
            }
            scene.path = path->get<std::string>();
            NoteNumberIgnored(*value, "sceneId", key, output.notes);
            int64_t number = 0;
            if (!ReadInteger(*value, "drawConfig", 0, 0, MAX_DRAW_CONFIG, number)) {
                output.notes.push_back(key + ": recusada, drawConfig inválido");
                continue;
            }
            scene.drawConfig = static_cast<uint8_t>(number);
            ReadHorse(key, *value, scene, output.notes);
            const auto titleCard = value->find("titleCardTexture");
            if (titleCard != value->end() && titleCard->is_string()) {
                scene.titleCard = titleCard->get<std::string>();
            } else if (titleCard != value->end() && !titleCard->is_null()) {
                output.notes.push_back(key + ": titleCardTexture ignorado, não é texto");
            }
            const auto entrances = value->find("entrances");
            if (entrances != value->end() && !entrances->is_object()) {
                output.notes.push_back(key + ": entrances ignorado, não é objeto");
            } else if (entrances != value->end()) {
                for (const auto& [entranceKey, entranceValue] : OrderedEntries(*entrances)) {
                    if (!entranceValue->is_object()) {
                        output.notes.push_back(key + "/" + entranceKey + ": ignorada, não é objeto");
                        continue;
                    }
                    RegistryEntrance entrance;
                    if (ReadEntrance(key, entranceKey, *entranceValue, entrance, output.notes)) {
                        scene.entrances.push_back(std::move(entrance));
                    }
                }
            }
            output.scenes.push_back(std::move(scene));
        }
        return true;
    } catch (const nlohmann::json::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace LinkSpanUnbound
