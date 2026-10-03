#include "scene_references.h"

#include <algorithm>
#include <map>
#include <set>

#include "actor_registry.h"
#include "unbound_format.h"

namespace LinkSpanUnbound {

bool DocumentKindFor(const std::string& type, int version, DocumentKind& kind) {
    static constexpr struct {
        const char* type;
        int version;
        DocumentKind kind;
    } kTypes[] = {
        { "unbound/scene", 1, DocumentKind::Scene },
        { "unbound/room", 1, DocumentKind::Room },
        { "unbound/collision", 3, DocumentKind::Collision },
        { "unbound/paths", 1, DocumentKind::Paths },
    };
    for (const auto& entry : kTypes) {
        if (type == entry.type && version == entry.version) {
            kind = entry.kind;
            return true;
        }
    }
    return false;
}

const char* GameVersionName(uint32_t version) {
    static constexpr struct {
        uint32_t crc;
        const char* name;
    } kVersions[] = {
        { 0xEC7011B7, "NTSC-US 1.0" },    { 0xD43DA81F, "NTSC-US 1.1" },    { 0x693BA2AE, "NTSC-US 1.2" },
        { 0xB044B569, "PAL 1.0" },        { 0xB2055FBD, "PAL 1.1" },        { 0xF7F52DB8, "NTSC-JP GC CE" },
        { 0xF611F4BA, "NTSC-JP GC" },     { 0xF3DD35BA, "NTSC-US GC" },     { 0x09465AC3, "PAL GC" },
        { 0xF43B45BA, "NTSC-JP MQ" },     { 0xF034001A, "NTSC-US MQ" },     { 0x1D4136F3, "PAL MQ" },
        { 0x871E1C92, "PAL GC debug 1" }, { 0x87121EFE, "PAL GC debug 2" }, { 0x917D18F6, "PAL GC MQ debug" },
        { 0x3D81FB3E, "iQue TW" },        { 0xB1E1E07B, "iQue CN" },
    };
    for (const auto& entry : kVersions) {
        if (entry.crc == version) {
            return entry.name;
        }
    }
    return "";
}

int RegisteredVersion(const std::string& type) {
    for (const int version : { 1, 3 }) {
        DocumentKind kind;
        if (DocumentKindFor(type, version, kind)) {
            return version;
        }
    }
    return 0;
}

bool IsRawFileReference(const std::string& field) {
    return field == "bulk.file";
}

std::string ResourceLookupPath(const std::string& field, const std::string& path) {
    constexpr char kPrefix[] = "__OTR__";
    if (IsRawFileReference(field)) {
        return path;
    }
    return path.rfind(kPrefix, 0) == 0 ? path.substr(sizeof(kPrefix) - 1) : path;
}

ReferenceReport CollectReferences(const std::vector<LayerDocument>& layers, TranscodeContext context) {
    ReferenceReport report;
    MergedDocument merged;
    const bool mergedOk = MergeLayers(layers, true, merged);
    report.notes = merged.notes;
    report.schema = merged.schema;
    DocumentKind kind = DocumentKind::Scene;
    if (!mergedOk) {
        return report;
    }
    if (!DocumentKindFor(merged.type, merged.version, kind)) {
        // Tipo do framework com versão que ele não registra: o host não acha fábrica e o documento não carrega.
        const int registered = RegisteredVersion(merged.type);
        if (registered) {
            report.typed = true;
            report.error = "$schema " + merged.schema + ": o framework registra " + merged.type + "/" +
                           std::to_string(registered) + " e o host não carrega outra versão";
        }
        return report;
    }
    report.typed = true;
    report.kind = kind;
    report.path = context.path;
    context.references.clear();
    context.notes.clear();
    try {
        switch (kind) {
            case DocumentKind::Scene:
                TranscodeScene(merged.doc, false, context);
                break;
            case DocumentKind::Room:
                TranscodeScene(merged.doc, true, context);
                break;
            case DocumentKind::Collision:
                TranscodeCollision(merged.doc, context);
                break;
            case DocumentKind::Paths:
                TranscodePaths(merged.doc, context);
                break;
        }
        report.accepted = true;
    } catch (const DocumentError& error) {
        report.error = error.what();
    } catch (const std::exception& error) {
        report.error = std::string("falha: ") + error.what();
    }
    report.notes.insert(report.notes.end(), context.notes.begin(), context.notes.end());
    if (!report.accepted) {
        return report;
    }
    // Salas e colisão aparecem uma vez por setup; o mesmo recurso em dois campos conta nos dois.
    std::set<std::pair<std::string, std::string>> seen;
    for (auto& reference : context.references) {
        if (seen.insert(reference).second) {
            report.references.push_back(std::move(reference));
        }
    }
    report.document = std::move(merged.doc);
    return report;
}

namespace {

// Os headers que o TranscodeScene emite: o 0 e os alternativos numéricos de 1 a 255 (o primeiro alias de cada
// número). Um alternativo {} existe (EndMarker) e não herda o 0; um ausente cai no 0 (ou 3 -> 2 -> 0), que já é
// conferido (z_scene_otr.cpp).
std::vector<ListItem> GraphSetups(const Json& document) {
    std::vector<ListItem> result;
    const Json& setups = Sub(document, "setups");
    if (!setups.contains("0")) {
        return result;
    }
    result.emplace_back("0", &setups.at("0"));
    std::set<int64_t> emitted;
    for (const auto& [key, value] : ListItems(setups)) {
        int64_t index = -1;
        if (key.find_first_not_of("0123456789") == std::string::npos && ParseIntString(key, index) && index > 0 &&
            index <= 255 && value->is_object() && emitted.insert(index).second) {
            result.emplace_back(key, value);
        }
    }
    return result;
}

// Os itens fora de uma lista; viram um grupo de findings (uma nota só por lista).
struct GraphNote {
    std::vector<std::string> items;

    void Add(const std::string& field, int64_t value) {
        items.push_back(field + "=" + std::to_string(value));
    }

    void Write(SceneGraph& graph, const std::string& where, const std::string& what) {
        if (!items.empty()) {
            graph.findings.push_back({ where, what, std::move(items) });
        }
    }
};

// Uma nota por grupo: quantos e o primeiro.
std::vector<std::string> RenderGraphNotes(const std::vector<GraphFindings>& findings) {
    std::vector<std::string> notes;
    for (const auto& group : findings) {
        if (!group.items.empty()) {
            notes.push_back(group.where + ": " + std::to_string(group.items.size()) + " " + group.what + " (ex.: " +
                            group.items.front() + ")");
        }
    }
    return notes;
}

// Room de um lado de porta (s16): ao montar a cena, z_room.c (func_80096FE8) lê roomList[room] para dimensionar o
// buffer de salas, sem conferir o tamanho, antes de qualquer porta existir. Room negativa não lê.
void GraphDoorRooms(const ReferenceReport& scene, size_t rooms, SceneGraph& graph) {
    for (const auto& [setupKey, setup] : GraphSetups(scene.document)) {
        GraphNote outside;
        size_t slot = 0;
        for (const auto& [key, actor] : PositionalItems(Sub(*setup, "transitionActors"), "transitionActors")) {
            if (slot++ >= 65535) {
                break; // transiActorCtx.numActors é u16
            }
            for (const char* side : { "front", "back" }) {
                const int32_t room = static_cast<int16_t>(Field(Sub(*actor, side), "room"));
                if (room >= 0 && static_cast<size_t>(room) >= rooms) {
                    outside.Add(key + "." + side + ".room", room);
                }
            }
        }
        outside.Write(graph, scene.path + " setups." + setupKey + ".transitionActors",
                      "lado(s) de porta com room além das " + std::to_string(rooms) +
                          " salas da cena; ao montar a cena, o jogo lê fora da lista de salas");
    }
}

// Câmera de um lado de porta (`effects`, s8): Camera_ChangeDoorCam (z_camera.c) passa o índice ao
// Camera_GetBgCamSetting, que lê a tabela da colisão da cena sem conferir. -1 (CAM_SET_DOORC) e -99 não leem.
void GraphDoorCameras(const ReferenceReport& doc, size_t cameras, SceneGraph& graph) {
    for (const auto& [setupKey, setup] : GraphSetups(doc.document)) {
        GraphNote outside;
        size_t slot = 0;
        for (const auto& [key, actor] : PositionalItems(Sub(*setup, "transitionActors"), "transitionActors")) {
            if (slot++ >= 65535) {
                break;
            }
            // Como no TranscodeScene: id por nome ou fora da faixa vira -1 no XML e não cria porta.
            int64_t parsed = 0;
            const auto id = actor->find("id");
            const bool named = id != actor->end() && id->is_string() && !ParseIntString(id->get<std::string>(), parsed);
            const int64_t numericId = Field(*actor, "id");
            if (named || numericId < 0 || numericId >= LINKSPAN_OOT_ACTOR_MODELS_ID_BASE) {
                continue;
            }
            for (const char* side : { "front", "back" }) {
                const int32_t camera = static_cast<int8_t>(Field(Sub(*actor, side), "effects"));
                if (camera != -1 && camera != -99 && (camera < 0 || static_cast<size_t>(camera) >= cameras)) {
                    outside.Add(key + "." + side + ".effects", camera);
                }
            }
        }
        outside.Write(graph, doc.path + " setups." + setupKey + ".transitionActors",
                      "lado(s) de porta com câmera fora das " + std::to_string(cameras) +
                          " da colisão da cena; se a porta trocar a câmera, o jogo lê fora da lista");
    }
}

// Cada item de uma lista (objeto com chaves ou array) passa por `keep`; chaves de controle ($...) ficam como estão.
// As chaves já são únicas: emplace_back direto, sem a busca linear do ordered_json (32 770 câmeras viravam 1 s).
template <typename Keep> Json MapItems(const Json& list, Keep keep) {
    if (list.is_object()) {
        Json out = Json::object();
        auto& items = out.get_ref<Json::object_t&>();
        const auto& source = list.get_ref<const Json::object_t&>();
        items.reserve(source.size());
        for (const auto& [key, value] : source) {
            items.emplace_back(key, key.rfind('$', 0) == 0 ? value : keep(value));
        }
        return out;
    }
    if (list.is_array()) {
        Json out = Json::array();
        for (const auto& item : list) {
            out.push_back(keep(item));
        }
        return out;
    }
    return list;
}

Json KeepFields(const Json& item, std::initializer_list<const char*> fields) {
    if (!item.is_object()) {
        return item;
    }
    Json out = Json::object();
    auto& kept = out.get_ref<Json::object_t&>();
    for (const char* field : fields) {
        const auto found = item.find(field);
        if (found != item.end()) {
            kept.emplace_back(field, *found);
        }
    }
    return out;
}

} // namespace

Json CompactForGraph(const Json& document, DocumentKind kind) {
    if (kind == DocumentKind::Scene) {
        return document;
    }
    Json out = Json::object();
    if (kind == DocumentKind::Paths || !document.is_object()) {
        return out;
    }
    if (kind == DocumentKind::Room) {
        const auto setups = document.find("setups");
        if (setups != document.end()) {
            out["setups"] = MapItems(*setups, [](const Json& setup) {
                if (!setup.is_object()) {
                    return setup;
                }
                Json kept = Json::object();
                const auto doors = setup.find("transitionActors");
                if (doors != setup.end()) {
                    kept["transitionActors"] =
                        MapItems(*doors, [](const Json& door) { return KeepFields(door, { "id", "front", "back" }); });
                }
                if (setup.contains("exits")) {
                    kept["exits"] = Json::object(); // só a presença conta
                }
                return kept;
            });
        }
        return out;
    }
    const auto keep = [&](const char* name, std::initializer_list<const char*> fields) {
        const auto list = document.find(name);
        if (list != document.end()) {
            out[name] = MapItems(*list, [fields](const Json& item) { return KeepFields(item, fields); });
        }
    };
    keep("cameras", {});
    keep("waterBoxes", { "room" });
    keep("surfaceTypes", { "exit" });
    return out;
}

namespace {

void CollectGraphFindings(const ReferenceReport& scene, const GraphLookup& lookup, SceneGraph& graph) {
    if (!scene.accepted || scene.kind != DocumentKind::Scene) {
        return;
    }
    const auto rooms = PositionalItems(Sub(scene.document, "rooms"), "rooms");
    // numRooms é u16 e Room.num é s16: um slot acima de 32 767 não é uma sala alcançável.
    const size_t roomCount = std::min<size_t>(rooms.size(), 32768);
    GraphDoorRooms(scene, roomCount, graph);

    const std::string collisionPath = ResourceLookupPath("collision", PathField(scene.document, "collision"));
    const ReferenceReport* collision = collisionPath.empty() || !lookup ? nullptr : lookup(collisionPath);
    if (!collision || !collision->accepted || collision->kind != DocumentKind::Collision) {
        graph.gaps.push_back(scene.path + ": colisão " + (collisionPath.empty() ? "ausente" : collisionPath) +
                             " sem documento Unbound aceito; câmeras, água e exits não conferidos");
        return;
    }
    const size_t cameraCount = PositionalItems(Sub(collision->document, "cameras"), "cameras").size();

    // Room da água (s32): o jogo compara com a sala atual ou -1 (todas). Fora das salas, a água nunca liga.
    GraphNote water;
    for (const auto& [key, value] : PositionalItems(Sub(collision->document, "waterBoxes"), "waterBoxes")) {
        const int32_t room = static_cast<int32_t>(Field(*value, "room", -1));
        if (room != -1 && (room < 0 || static_cast<size_t>(room) >= roomCount)) {
            water.Add(key + ".room", room);
        }
    }
    water.Write(graph, collision->path + " waterBoxes (cena " + scene.path + ")",
                "water box(es) com room fora das " + std::to_string(roomCount) +
                    " salas da cena; a água não liga em sala nenhuma");
    GraphDoorCameras(scene, cameraCount, graph);

    // Uma sala pode trazer exits (mesmo vazio) e trocar a lista da cena até outra sala trocar de novo: a lista que o
    // Player usa depende do percurso. Aí os exits não são conferidos.
    bool exitsKnown = true;
    std::set<std::string> seenRooms;
    size_t slot = 0;
    for (const auto& [key, value] : rooms) {
        if (slot++ >= roomCount) {
            break;
        }
        const std::string path = value->is_string() ? ResourceLookupPath("rooms", value->get<std::string>()) : "";
        if (!seenRooms.insert(path).second) {
            continue;
        }
        const ReferenceReport* room = path.empty() || !lookup ? nullptr : lookup(path);
        if (!room || !room->accepted || room->kind != DocumentKind::Room) {
            exitsKnown = false;
            graph.gaps.push_back(scene.path + ": sala " + (path.empty() ? "rooms." + key : path) +
                                 " sem documento Unbound aceito; portas da sala e exits não conferidos");
            continue;
        }
        GraphDoorCameras(*room, cameraCount, graph);
        for (const auto& [setupKey, setup] : GraphSetups(room->document)) {
            if (setup->contains("exits")) {
                exitsKnown = false;
                graph.gaps.push_back(scene.path + ": " + room->path + " setups." + setupKey +
                                     " troca os exits; a lista depende do percurso, exits não conferidos");
                break;
            }
        }
    }
    if (!exitsKnown) {
        return;
    }
    // Exit da superfície (s32): z_player.c lê setupExitList[exit - 1] sem conferir. 0 é sem saída.
    const auto surfaces = PositionalItems(Sub(collision->document, "surfaceTypes"), "surfaceTypes");
    for (const auto& [setupKey, setup] : GraphSetups(scene.document)) {
        const size_t exits = PositionalItems(Sub(*setup, "exits"), "exits").size();
        GraphNote outside;
        for (const auto& [key, value] : surfaces) {
            const int32_t exit = static_cast<int32_t>(Field(*value, "exit"));
            if (exit != 0 && (exit < 0 || static_cast<size_t>(exit) > exits)) {
                outside.Add(key + ".exit", exit);
            }
        }
        outside.Write(graph, collision->path + " surfaceTypes (cena " + scene.path + " setups." + setupKey + ")",
                      "superfície(s) com exit fora de 1.." + std::to_string(exits) +
                          " (saídas da cena); lá o jogo lê fora da lista");
    }
}

} // namespace

SceneGraph CollectSceneGraph(const ReferenceReport& scene, const GraphLookup& lookup) {
    SceneGraph graph;
    CollectGraphFindings(scene, lookup, graph);
    graph.notes = RenderGraphNotes(graph.findings);
    return graph;
}

size_t DropInheritedFindings(SceneGraph& graph, const SceneGraph& base) {
    std::map<std::pair<std::string, std::string>, std::set<std::string>> known;
    for (const auto& group : base.findings) {
        known[{ group.where, group.what }].insert(group.items.begin(), group.items.end());
    }
    size_t dropped = 0;
    for (auto& group : graph.findings) {
        const auto found = known.find({ group.where, group.what });
        if (found == known.end()) {
            continue;
        }
        const size_t before = group.items.size();
        group.items.erase(std::remove_if(group.items.begin(), group.items.end(),
                                         [&](const std::string& item) { return found->second.count(item) != 0; }),
                          group.items.end());
        dropped += before - group.items.size();
    }
    graph.findings.erase(std::remove_if(graph.findings.begin(), graph.findings.end(),
                                        [](const GraphFindings& group) { return group.items.empty(); }),
                         graph.findings.end());
    graph.notes = RenderGraphNotes(graph.findings);
    return dropped;
}

} // namespace LinkSpanUnbound
