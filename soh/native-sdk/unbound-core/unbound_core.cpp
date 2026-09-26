#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "converter.h"
#include "include/linkspan/unbound/json_factory.h"
#include "json_merge.h"
#include "oot_hooks.h"
#include "oot_resources.h"
#include "oot_scenes.h"
#include "oot_text.h"
#include "room_actors.h"
#include "scene_registry.h"
#include "transcode.h"
#include "unbound_docs.h"
#include "unbound_format.h"

namespace {

namespace fs = std::filesystem;

constexpr uint32_t MAX_LAYERS = 64;
constexpr uint32_t MAX_TOTAL_INPUT = 4 * 1024 * 1024;
// Documentos de cena, colisão e texto: uma sala grande passa fácil dos 64 KiB da factory JSON.
constexpr uint32_t MAX_DOCUMENT_LAYER = 64 * 1024 * 1024;
constexpr uint32_t MAX_DOCUMENT_TOTAL = 256 * 1024 * 1024;
constexpr uint32_t MAX_HANDLES = 1024;
constexpr size_t MAX_NOTES = 12;
constexpr const char* ACTOR_PATCH_SCHEMA = "linkspan.unbound.actor-patch/v1";
constexpr const char* SCENE_REGISTRY_PATH = "unbound/scenes.json";
constexpr const char* MANIFEST_PATH = "unbound.json";
constexpr const char* BASE_ARCHIVE = "oot-unbound.o2r";
constexpr const char* BASE_STAMP = "oot-unbound.o2r.source.json";
constexpr const char* TEXT_LANGUAGES[LINKSPAN_OOT_TEXT_LANGUAGES] = { "eng", "ger", "fra", "jpn", "staff" };

enum class DocKind { Scene, Room, Collision, Paths };

struct State;

// user do transcodificador de um tipo JSON registrado no host.
struct TypeBinding {
    State* state = nullptr;
    const char* type = "";
    uint32_t version = 0;
    DocKind kind = DocKind::Scene;
};

struct Schema {
    std::string name;
    std::string type;
    uint32_t flags;
};

struct Result {
    std::string schema;
    std::string path;
    std::string json;
    uint64_t hash = 0;
    uint32_t layerCount = 0;
};

struct State {
    LinkSpanUnboundJsonFactoryV1 service{};
    const ShipOotResourcesV2* resources = nullptr;
    // v3: tipos JSON (unbound/scene...). Sem ele o framework fica só com a factory e o registro.
    const ShipOotResourcesV3* resourcesV3 = nullptr;
    // Opcional: sem ele a factory JSON continua e load_scene_registry responde unsupported.
    const ShipOotScenesV1* scenes = nullptr;
    // v2: título das cenas, lista de cenas vanilla e override (a base convertida precisa dele).
    const ShipOotScenesV2* scenesV2 = nullptr;
    const ShipOotScenesV3* scenesV3 = nullptr;
    const ShipOotTextV1* text = nullptr;
    std::vector<uint64_t> sceneHandles;
    TypeBinding bindings[4];
    std::vector<uint64_t> jsonTypes;
    // Base convertida (oot-unbound.o2r), montada no Init abaixo dos mods.
    uint64_t baseArchive = 0;
    std::string baseReport = "-";
    bool baseActive = false;
    std::vector<std::pair<int32_t, uint8_t>> overrides;
    bool textTouched[LINKSPAN_OOT_TEXT_LANGUAGES] = {};
    // O host chama o transcodificador numa thread do pool de recursos, um por vez; notas e contadores
    // também são lidos pela thread do jogo (unbound_report).
    std::mutex noteMutex;
    uint32_t transcoded[4] = {};
    uint32_t transcodeFailures = 0;
    std::deque<std::string> notes;
    // logs/linkspan-unbound.log: documentos recusados ficam registrados mesmo se o jogo cair em seguida.
    fs::path logPath;
    // oot.room.actors (host com o ponto): salas vanilla ou de mod alteradas por
    // scenes/<cena>/rooms/<n>.json. Relatórios para o Lua registrar.
    bool roomHook = false;
    uint32_t roomsSeen = 0;
    uint32_t roomsPatched = 0;
    std::string lastRoom;
    std::string lastPatch;
    std::thread::id ownerThread;
    std::vector<Schema> schemas;
    std::map<uint64_t, Result> results;
    uint64_t nextHandle = 1;
};

bool IsOwner(const State* state) {
    return state && state->ownerThread == std::this_thread::get_id();
}

const Schema* FindSchema(const State& state, const char* name) {
    if (!name || !*name || std::strlen(name) > LINKSPAN_UNBOUND_JSON_MAX_SCHEMA) {
        return nullptr;
    }
    const auto found = std::find_if(state.schemas.begin(), state.schemas.end(),
                                    [name](const Schema& schema) { return schema.name == name; });
    return found == state.schemas.end() ? nullptr : &*found;
}

uint8_t SHIP_NATIVE_CALL HasSchema(void* context, const char* schema) {
    const auto* state = static_cast<const State*>(context);
    return IsOwner(state) && FindSchema(*state, schema) ? 1 : 0;
}

ShipNativeStatus SHIP_NATIVE_CALL ListSchemas(void* context, LinkSpanUnboundJsonSchemaFn callback, void* user) {
    const auto* state = static_cast<const State*>(context);
    if (!IsOwner(state) || !callback) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    for (const auto& schema : state->schemas) {
        const auto status = callback(user, schema.name.data(), static_cast<uint32_t>(schema.name.size()),
                                     schema.type.data(), static_cast<uint32_t>(schema.type.size()), schema.flags);
        if (status != SHIP_NATIVE_OK) {
            return status;
        }
    }
    return SHIP_NATIVE_OK;
}

struct LayerCollector {
    std::vector<LinkSpanUnbound::LayerDocument> layers;
    uint32_t totalBytes = 0;
    uint32_t maxLayer = SHIP_NATIVE_MAX_BYTES;
    uint32_t maxTotal = MAX_TOTAL_INPUT;
};

LayerCollector DocumentCollector() {
    LayerCollector collector;
    collector.maxLayer = MAX_DOCUMENT_LAYER;
    collector.maxTotal = MAX_DOCUMENT_TOTAL;
    return collector;
}

ShipNativeStatus SHIP_NATIVE_CALL CollectLayer(void* user, const ShipOotResourceLayerV2* layer, const char* archivePath,
                                               const uint8_t* data) {
    auto* collector = static_cast<LayerCollector*>(user);
    if (!collector || !layer || layer->size < sizeof(ShipOotResourceLayerV2) || !archivePath ||
        (!data && layer->data_size) || layer->layer_index != collector->layers.size() ||
        layer->layer_count > MAX_LAYERS || layer->data_size > collector->maxLayer ||
        collector->totalBytes > collector->maxTotal - layer->data_size) {
        return SHIP_NATIVE_LIMIT;
    }
    try {
        collector->layers.push_back({
            std::string(archivePath, layer->archive_path_length),
            data ? std::string(reinterpret_cast<const char*>(data), layer->data_size) : std::string{},
            layer->content_hash,
        });
        collector->totalBytes += layer->data_size;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL LoadMerged(void* context, const char* schemaName, const char* path,
                                             uint64_t* handle) {
    auto* state = static_cast<State*>(context);
    if (!IsOwner(state) || !path || !*path || !handle || std::strlen(path) > 4096) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto* schema = FindSchema(*state, schemaName);
    if (!schema) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (state->results.size() >= MAX_HANDLES) {
        return SHIP_NATIVE_LIMIT;
    }
    LayerCollector collector;
    const auto read = state->resources->read_file_layers(path, CollectLayer, &collector);
    if (read != SHIP_NATIVE_OK) {
        return read;
    }
    LinkSpanUnbound::MergeResult merged;
    std::string error;
    if (!LinkSpanUnbound::MergeDocuments(schema->name, collector.layers, merged, error) ||
        merged.json.size() > SHIP_NATIVE_MAX_BYTES) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        const uint64_t assigned = state->nextHandle++;
        state->results.emplace(assigned,
                               Result{ schema->name, path, std::move(merged.json), merged.hash, merged.layerCount });
        *handle = assigned;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL ReadJson(void* context, uint64_t handle, char* output, uint32_t capacity,
                                           uint32_t* outputSize) {
    const auto* state = static_cast<const State*>(context);
    if (!IsOwner(state) || !handle || !outputSize) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto found = state->results.find(handle);
    if (found == state->results.end()) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    *outputSize = static_cast<uint32_t>(found->second.json.size());
    if (!output && capacity == 0) {
        return SHIP_NATIVE_OK;
    }
    if (!output) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (capacity < *outputSize) {
        return SHIP_NATIVE_LIMIT;
    }
    std::memcpy(output, found->second.json.data(), *outputSize);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL GetMetadata(void* context, uint64_t handle, LinkSpanUnboundJsonMetadataV1* metadata,
                                              char* schemaOutput, uint32_t schemaCapacity, char* pathOutput,
                                              uint32_t pathCapacity) {
    const auto* state = static_cast<const State*>(context);
    if (!IsOwner(state) || !handle || !metadata || metadata->size < sizeof(*metadata)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto found = state->results.find(handle);
    if (found == state->results.end()) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto& result = found->second;
    metadata->layer_count = result.layerCount;
    metadata->merged_hash = result.hash;
    metadata->schema_length = static_cast<uint32_t>(result.schema.size());
    metadata->path_length = static_cast<uint32_t>(result.path.size());
    if ((!schemaOutput && schemaCapacity) || (!pathOutput && pathCapacity)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if ((schemaOutput && schemaCapacity < metadata->schema_length) ||
        (pathOutput && pathCapacity < metadata->path_length)) {
        return SHIP_NATIVE_LIMIT;
    }
    if (schemaOutput) {
        std::memcpy(schemaOutput, result.schema.data(), metadata->schema_length);
    }
    if (pathOutput) {
        std::memcpy(pathOutput, result.path.data(), metadata->path_length);
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Release(void* context, uint64_t handle) {
    auto* state = static_cast<State*>(context);
    if (!IsOwner(state) || !handle) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    return state->results.erase(handle) == 1 ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}

void UnregisterScenes(State& state) {
    if (state.scenes) {
        for (const uint64_t handle : state.sceneHandles) {
            state.scenes->unregister_scene(handle);
        }
    }
    state.sceneHandles.clear();
}

const char* StatusName(ShipNativeStatus status) {
    switch (status) {
        case SHIP_NATIVE_INVALID_ARGUMENT:
            return "invalid";
        case SHIP_NATIVE_UNSUPPORTED:
            return "unsupported";
        case SHIP_NATIVE_LIMIT:
            return "limit";
        default:
            return "failure";
    }
}

ShipNativeStatus WriteText(ShipNativeWriteFn write, void* writer, std::string text) {
    if (text.size() > SHIP_NATIVE_MAX_BYTES) {
        text.resize(SHIP_NATIVE_MAX_BYTES);
    }
    return write(writer, text.data(), static_cast<uint32_t>(text.size()));
}

// Lê unbound/scenes.json em todas as camadas montadas e registra cenas e entradas no host.
// Chamar de novo troca o registro anterior pelo das camadas atuais.
std::string ApplySceneRegistry(State& state) {
    LayerCollector collector = DocumentCollector();
    const auto read = state.resources->read_file_layers(SCENE_REGISTRY_PATH, CollectLayer, &collector);
    UnregisterScenes(state);
    const std::string layers = "layers=" + std::to_string(collector.layers.size());
    if (read != SHIP_NATIVE_OK || collector.layers.empty()) {
        return "scenes=0; entrances=0; notes=0; " + layers + "; read=" +
               (read == SHIP_NATIVE_OK ? "ok" : StatusName(read));
    }
    LinkSpanUnbound::MergeResult merged;
    LinkSpanUnbound::SceneRegistryDocument document;
    std::string error;
    if (!LinkSpanUnbound::MergeSchemaFreeDocuments(collector.layers, merged, error) ||
        !LinkSpanUnbound::ParseSceneRegistry(merged.json, document, error)) {
        return "scenes=0; entrances=0; notes=0; " + layers + "; error=" + error;
    }
    uint32_t scenesRegistered = 0;
    uint32_t entrancesRegistered = 0;
    std::vector<std::string> notes = document.notes;
    for (const auto& scene : document.scenes) {
        uint64_t handle = 0;
        int32_t sceneId = 0;
        ShipNativeStatus sceneStatus = SHIP_NATIVE_UNSUPPORTED;
        if (state.scenesV3) {
            ShipOotSceneDefinitionV3 definition{};
            definition.size = sizeof(definition);
            definition.name = scene.name.c_str();
            definition.display_name = scene.displayName.c_str();
            definition.scene_path = scene.path.c_str();
            definition.requested_id = LINKSPAN_OOT_SCENES_AUTO;
            definition.draw_config = scene.drawConfig;
            definition.title_card_texture = scene.titleCard.c_str();
            definition.horse_enabled = scene.horse ? 1 : 0;
            definition.horse_has_spawn = scene.horseHasSpawn ? 1 : 0;
            definition.horse_x = scene.horseX;
            definition.horse_y = scene.horseY;
            definition.horse_z = scene.horseZ;
            definition.horse_angle = scene.horseAngle;
            sceneStatus = state.scenesV3->register_scene_v3(&definition, &handle, &sceneId);
        } else if (state.scenesV2) {
            ShipOotSceneDefinitionV2 definition{};
            definition.size = sizeof(definition);
            definition.name = scene.name.c_str();
            definition.display_name = scene.displayName.c_str();
            definition.scene_path = scene.path.c_str();
            definition.requested_id = LINKSPAN_OOT_SCENES_AUTO;
            definition.draw_config = scene.drawConfig;
            definition.title_card_texture = scene.titleCard.c_str();
            sceneStatus = state.scenesV2->register_scene_v2(&definition, &handle, &sceneId);
        } else {
            ShipOotSceneDefinitionV1 definition{};
            definition.size = sizeof(definition);
            definition.name = scene.name.c_str();
            definition.display_name = scene.displayName.c_str();
            definition.scene_path = scene.path.c_str();
            definition.requested_id = LINKSPAN_OOT_SCENES_AUTO;
            definition.draw_config = scene.drawConfig;
            sceneStatus = state.scenes->register_scene(&definition, &handle, &sceneId);
            if (sceneStatus == SHIP_NATIVE_OK && !scene.titleCard.empty()) {
                notes.push_back(scene.name + ": titleCardTexture exige linkspan.oot.scenes v2");
            }
            if (sceneStatus == SHIP_NATIVE_OK && scene.horse) {
                notes.push_back(scene.name + ": horse exige linkspan.oot.scenes v3");
            }
        }
        if (sceneStatus != SHIP_NATIVE_OK) {
            notes.push_back(scene.name + ": recusada pelo host (" + StatusName(sceneStatus) + ")");
            continue;
        }
        state.sceneHandles.push_back(handle);
        ++scenesRegistered;
        for (const auto& entrance : scene.entrances) {
            ShipOotEntranceDefinitionV1 entranceDefinition{};
            entranceDefinition.size = sizeof(entranceDefinition);
            entranceDefinition.key = entrance.key.c_str();
            entranceDefinition.requested_index = LINKSPAN_OOT_SCENES_AUTO;
            entranceDefinition.spawn = entrance.spawn;
            entranceDefinition.continue_bgm = entrance.continueBgm ? 1 : 0;
            entranceDefinition.show_title_card = entrance.showTitleCard ? 1 : 0;
            entranceDefinition.end_transition = entrance.endTransition;
            entranceDefinition.start_transition = entrance.startTransition;
            int32_t index = 0;
            const auto entranceStatus = state.scenes->register_entrance(handle, &entranceDefinition, &index);
            if (entranceStatus != SHIP_NATIVE_OK) {
                notes.push_back(scene.name + "/" + entrance.key + ": recusada pelo host (" +
                                StatusName(entranceStatus) + ")");
            } else {
                ++entrancesRegistered;
            }
        }
    }
    std::string text = "scenes=" + std::to_string(scenesRegistered) + "; entrances=" +
                       std::to_string(entrancesRegistered) + "; notes=" + std::to_string(notes.size()) + "; " + layers;
    for (const auto& note : notes) {
        text += "; " + note;
    }
    return text;
}

ShipNativeStatus SHIP_NATIVE_CALL LoadSceneRegistry(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                                    void* writer) {
    auto* state = static_cast<State*>(user);
    if (!IsOwner(state) || length || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!state->scenes) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    try {
        return WriteText(write, writer, ApplySceneRegistry(*state));
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

std::string DescribeActors(const std::vector<LinkSpanUnbound::RoomActor>& actors, size_t limit) {
    std::string text;
    for (size_t i = 0; i < actors.size() && i < limit; ++i) {
        const auto& actor = actors[i];
        char entry[96];
        std::snprintf(entry, sizeof(entry), "%s%zu:0x%04X@%.0f,%.0f,%.0f/0x%04X", text.empty() ? "" : " ", i,
                      static_cast<unsigned>(static_cast<uint16_t>(actor.id)), actor.pos[0], actor.pos[1],
                      actor.pos[2], static_cast<unsigned>(static_cast<uint16_t>(actor.params)));
        text += entry;
    }
    if (actors.size() > limit) {
        text += " ...";
    }
    return text;
}

// TRANSFORM de oot.room.actors: aplica scenes/<cena>/rooms/<n>.json (unbound/room/1) de todas as
// camadas sobre a lista vanilla. Sem documento, UNSUPPORTED deixa a sala como está.
ShipNativeStatus SHIP_NATIVE_CALL OnRoomActors(void* user, const ShipNativeHookCall* call) {
    auto* state = static_cast<State*>(user);
    if (!IsOwner(state) || !call || call->payload_size < sizeof(ShipOotRoomActorsHookV2)) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* payload = static_cast<ShipOotRoomActorsHookV2*>(call->payload);
    if (!payload->entries || payload->count > payload->capacity) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    try {
        std::vector<LinkSpanUnbound::RoomActor> vanilla(payload->count);
        for (uint32_t i = 0; i < payload->count; ++i) {
            const auto& entry = payload->entries[i];
            auto& actor = vanilla[i];
            actor.id = entry.id;
            std::copy(entry.pos, entry.pos + 3, actor.pos);
            std::copy(entry.rot, entry.rot + 3, actor.rot);
            actor.params = entry.params;
        }
        const std::string roomPath = payload->room_path ? payload->room_path : "";
        const std::string document = LinkSpanUnbound::RoomDocumentPath(roomPath, payload->room);
        ++state->roomsSeen;
        state->lastRoom = "scene=" + std::to_string(payload->scene_id) + " room=" + std::to_string(payload->room) +
                          " layer=" + std::to_string(payload->layer) + " doc=" +
                          (document.empty() ? std::string("-") : document) + " atores=" +
                          std::to_string(vanilla.size()) + " | " + DescribeActors(vanilla, 64);
        // Com a base convertida a sala já é um documento: as camadas dos mods mesclam nele direto.
        if (document.empty() || state->baseActive) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        LayerCollector collector = DocumentCollector();
        if (state->resources->read_file_layers(document.c_str(), CollectLayer, &collector) != SHIP_NATIVE_OK ||
            collector.layers.empty()) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        LinkSpanUnbound::RoomActorsResult result;
        std::string error;
        std::string notes;
        const bool applied = LinkSpanUnbound::ApplyRoomActorLayers(vanilla, payload->layer, collector.layers, result,
                                                                    error);
        for (const auto& note : result.notes) {
            notes += "; " + note;
        }
        if (!applied || !result.layersUsed) {
            state->lastPatch = document + ": recusado (" + (applied ? std::string("nenhuma camada válida") : error) +
                               ")" + notes;
            return SHIP_NATIVE_UNSUPPORTED;
        }
        if (result.actors.size() > payload->capacity) {
            state->lastPatch = document + ": " + std::to_string(result.actors.size()) + " atores excedem a capacidade " +
                               std::to_string(payload->capacity) + notes;
            return SHIP_NATIVE_UNSUPPORTED;
        }
        for (size_t i = 0; i < result.actors.size(); ++i) {
            const auto& actor = result.actors[i];
            auto& entry = payload->entries[i];
            entry.id = actor.id;
            std::copy(actor.pos, actor.pos + 3, entry.pos);
            std::copy(actor.rot, actor.rot + 3, entry.rot);
            entry.params = actor.params;
        }
        payload->count = static_cast<uint32_t>(result.actors.size());
        ++state->roomsPatched;
        state->lastPatch = document + ": camadas=" + std::to_string(result.layersUsed) + " atores " +
                           std::to_string(vanilla.size()) + "->" + std::to_string(result.actors.size()) + notes +
                           " | " + DescribeActors(result.actors, 64);
        return SHIP_NATIVE_OK;
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

// Relatório das salas vistas pelo hook, para o main.lua registrar quando muda.
ShipNativeStatus SHIP_NATIVE_CALL RoomReport(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                             void* writer) {
    auto* state = static_cast<State*>(user);
    if (!IsOwner(state) || length || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        return WriteText(write, writer,
                         "hook=" + std::string(state->roomHook ? "on" : "off") +
                             " salas=" + std::to_string(state->roomsSeen) +
                             " alteradas=" + std::to_string(state->roomsPatched) + "\nultima: " + state->lastRoom +
                             "\npatch: " + (state->lastPatch.empty() ? std::string("-") : state->lastPatch));
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

void AppendLog(const State& state, const std::string& line) {
    if (state.logPath.empty()) {
        return;
    }
    std::ofstream log(state.logPath, std::ios::binary | std::ios::app);
    log << line << '\n';
}

void CountTranscode(State& state, int kind) {
    std::lock_guard lock(state.noteMutex);
    if (kind < 0) {
        ++state.transcodeFailures;
    } else {
        ++state.transcoded[kind];
    }
}

void AddNote(State& state, std::string note) {
    std::lock_guard lock(state.noteMutex);
    AppendLog(state, note);
    if (note.size() > 300) {
        note.resize(300);
        note += "...";
    }
    state.notes.push_back(std::move(note));
    while (state.notes.size() > MAX_NOTES) {
        state.notes.pop_front();
    }
}

int32_t ResolveEntrance(const State& state, const std::string& name) {
    int32_t index = -1;
    if (!state.scenes || state.scenes->find_entrance(name.c_str(), &index) != SHIP_NATIVE_OK) {
        return -1;
    }
    return index;
}

// ShipOotJsonTranscodeFn dos tipos unbound/*: mescla as camadas do caminho pedido (SPEC.md §3) e escreve o
// XML que a fábrica do host lê. Documento recusado = carregamento falha, com o motivo nas notas.
ShipNativeStatus SHIP_NATIVE_CALL TranscodeJson(void* user, const char* path, uint32_t, ShipNativeWriteFn write,
                                                void* writer) {
    // Roda numa thread do pool da libultraship, com a thread do jogo esperando o recurso: só VFS, notas e
    // find_entrance aqui dentro.
    auto* binding = static_cast<TypeBinding*>(user);
    State* state = binding ? binding->state : nullptr;
    if (!state || !path || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        LayerCollector collector = DocumentCollector();
        const auto read = state->resources->read_file_layers(path, CollectLayer, &collector);
        if (read != SHIP_NATIVE_OK) {
            CountTranscode(*state, -1);
            AddNote(*state, std::string(path) + ": leitura das camadas falhou (" + StatusName(read) + ")");
            return read;
        }
        LinkSpanUnbound::MergedDocument merged;
        const bool mergedOk = LinkSpanUnbound::MergeLayers(collector.layers, true, merged);
        for (const auto& note : merged.notes) {
            AddNote(*state, std::string(path) + ": " + note);
        }
        if (!mergedOk || merged.type != binding->type) {
            CountTranscode(*state, -1);
            AddNote(*state, std::string(path) + ": recusado, nenhuma camada " + binding->type + " válida");
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        LinkSpanUnbound::TranscodeContext context;
        context.path = path;
        context.resolveEntrance = [state](const std::string& name) { return ResolveEntrance(*state, name); };
        std::string xml;
        switch (binding->kind) {
            case DocKind::Scene:
                xml = LinkSpanUnbound::TranscodeScene(merged.doc, false, context);
                break;
            case DocKind::Room:
                xml = LinkSpanUnbound::TranscodeScene(merged.doc, true, context);
                break;
            case DocKind::Collision:
                xml = LinkSpanUnbound::TranscodeCollision(merged.doc, context);
                break;
            case DocKind::Paths:
                xml = LinkSpanUnbound::TranscodePaths(merged.doc, context);
                break;
        }
        for (const auto& note : context.notes) {
            AddNote(*state, note);
        }
        constexpr size_t chunk = 1024 * 1024;
        for (size_t offset = 0; offset < xml.size(); offset += chunk) {
            const size_t size = std::min(chunk, xml.size() - offset);
            const auto status = write(writer, xml.data() + offset, static_cast<uint32_t>(size));
            if (status != SHIP_NATIVE_OK) {
                return status;
            }
        }
        CountTranscode(*state, static_cast<int>(binding->kind));
        return SHIP_NATIVE_OK;
    } catch (const LinkSpanUnbound::DocumentError& error) {
        CountTranscode(*state, -1);
        AddNote(*state, std::string(path) + ": recusado: " + error.what());
        return SHIP_NATIVE_INVALID_ARGUMENT;
    } catch (const std::exception& error) {
        CountTranscode(*state, -1);
        AddNote(*state, std::string(path) + ": falha: " + error.what());
        return SHIP_NATIVE_FAILURE;
    } catch (...) {
        CountTranscode(*state, -1);
        return SHIP_NATIVE_FAILURE;
    }
}

// Pasta do jogo: a do executável, onde o SoH gera o oot.o2r (plano §10.4).
fs::path GameDirectory() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            break;
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
    return fs::current_path();
}

LinkSpanUnbound::Json FileStamp(const fs::path& path) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error) {
        return nullptr;
    }
    const auto time = fs::last_write_time(path, error);
    return { { "size", size }, { "mtime", error ? 0 : static_cast<int64_t>(time.time_since_epoch().count()) } };
}

// Proveniência da base (plano UNBOUND-006): muda quando a ROM exportada, a build do jogo ou o conversor
// mudam, e então a base é gerada de novo.
std::string BaseProvenance(const ShipNativeRuntime* runtime, const State& state, const fs::path& gameDir) {
    LinkSpanUnbound::Json source = LinkSpanUnbound::Json::object();
    source["converter"] = LinkSpanUnbound::kConverterVersion;
    source["formatVersion"] = LinkSpanUnbound::kReaderFormatVersion;
    std::vector<uint32_t> versions(16);
    uint32_t count = 0;
    if (state.resources->get_game_versions(versions.data(), static_cast<uint32_t>(versions.size()), &count) ==
        SHIP_NATIVE_OK) {
        versions.resize(std::min<size_t>(count, versions.size()));
        source["gameVersions"] = versions;
    }
    LinkSpanUnbound::Json archives = LinkSpanUnbound::Json::object();
    for (const char* name : { "oot.o2r", "oot-mq.o2r" }) {
        archives[name] = FileStamp(gameDir / name);
    }
    source["archives"] = archives;
#ifdef _WIN32
    std::wstring exe(MAX_PATH * 4, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    exe.resize(length);
    source["game"] = FileStamp(fs::path(exe));
#endif
    if (runtime->abi_minor >= 3 && runtime->get_host_fingerprint) {
        char fingerprint[65] = {};
        uint32_t size = 0;
        if (runtime->get_host_fingerprint(runtime->context, fingerprint, 64, &size) == SHIP_NATIVE_OK && size <= 64) {
            source["host"] = std::string(fingerprint, size);
        }
    }
    return source.dump();
}

bool ReadWholeFile(const fs::path& path, std::string& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    bytes = text.str();
    return true;
}

// Grava ao lado e troca pelo nome final: um arquivo pela metade nunca substitui o anterior.
bool WriteAtomically(const fs::path& path, const std::string& bytes, std::string& error) {
    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            error = "falha ao gravar " + temp.string();
            return false;
        }
    }
    std::error_code code;
    fs::rename(temp, path, code);
    if (code) {
        error = "falha ao renomear " + temp.string() + ": " + code.message();
        fs::remove(temp, code);
        return false;
    }
    return true;
}

// Cenas vanilla presentes no VFS agora (só os archives do jogo estão montados no Init).
std::vector<LinkSpanUnbound::SceneSource> VanillaScenes(const State& state) {
    std::vector<LinkSpanUnbound::SceneSource> scenes;
    const int32_t count = state.scenesV2->get_scene_count();
    for (int32_t id = 0; id < count; ++id) {
        for (uint8_t mq = 0; mq < 2; ++mq) {
            ShipOotSceneInfoV1 info{};
            info.size = sizeof(info);
            if (state.scenesV2->get_scene_info(id, mq, &info) != SHIP_NATIVE_OK || !info.file_name[0] ||
                (mq && !info.has_master_quest)) {
                break;
            }
            if (!info.overridden && state.resources->has_file(info.scene_path)) {
                scenes.push_back({ info.scene_path, LinkSpanUnbound::SceneDirName(info.file_name, mq != 0) });
            }
        }
    }
    return scenes;
}

// UNBOUND-006: gera (ou reaproveita) oot-unbound.o2r ao lado do oot.o2r e monta. No Init só os archives do
// jogo estão montados, então a base lê o vanilla e fica abaixo dos mods que o SoH monta depois.
void PrepareBase(const ShipNativeRuntime* runtime, State& state) {
    const auto started = std::chrono::steady_clock::now();
    const fs::path gameDir = GameDirectory();
    const fs::path archive = gameDir / BASE_ARCHIVE;
    const fs::path stamp = gameDir / BASE_STAMP;
    const std::string provenance = BaseProvenance(runtime, state, gameDir);
    std::string previous;
    std::error_code exists;
    std::string summary;
    if (fs::is_regular_file(archive, exists) && ReadWholeFile(stamp, previous) && previous == provenance) {
        summary = "cache";
    } else {
        const LinkSpanUnbound::ReadResourceFn read = [&state](const std::string& path, std::string& bytes) {
            uint32_t size = 0;
            if (state.resources->read_file(path.c_str(), nullptr, 0, &size) != SHIP_NATIVE_OK) {
                return false;
            }
            bytes.resize(size);
            if (state.resources->read_file(path.c_str(), reinterpret_cast<uint8_t*>(bytes.data()), size, &size) !=
                SHIP_NATIVE_OK) {
                return false;
            }
            bytes.resize(size);
            return true;
        };
        const auto scenes = VanillaScenes(state);
        LinkSpanUnbound::ConvertReport report;
        const auto files = LinkSpanUnbound::BuildBase(read, scenes, provenance, report);
        std::string error;
        if (report.scenes == 0) {
            state.baseReport = "nenhuma cena vanilla convertida (" + std::to_string(scenes.size()) +
                               " encontradas); base desligada";
            return;
        }
        if (!WriteAtomically(archive, LinkSpanUnbound::BuildStoredZip(files), error) ||
            !WriteAtomically(stamp, provenance, error)) {
            state.baseReport = "base não gravada: " + error;
            return;
        }
        summary = "convertida cenas=" + std::to_string(report.scenes) + " salas=" + std::to_string(report.rooms) +
                  " colisoes=" + std::to_string(report.collisions) + " paths=" + std::to_string(report.paths) +
                  " falhas=" + std::to_string(report.failures) + " arquivos=" + std::to_string(files.size());
        for (size_t i = 0; i < report.errors.size() && i < 4; ++i) {
            summary += "; " + report.errors[i];
        }
    }
    const auto status = state.resources->mount_archive(archive.string().c_str(), &state.baseArchive);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    state.baseReport = summary + " ms=" + std::to_string(elapsed) + " " + archive.string() +
                       (status == SHIP_NATIVE_OK ? "" : std::string(" montagem falhou (") + StatusName(status) + ")");
    if (status != SHIP_NATIVE_OK) {
        state.baseArchive = 0;
    }
}

void ClearOverrides(State& state) {
    if (state.scenesV2) {
        for (const auto& [sceneId, mq] : state.overrides) {
            state.scenesV2->override_scene(sceneId, mq, nullptr);
        }
    }
    state.overrides.clear();
}

// §1.3/§1.6: com uma camada base (unbound.json com "scenes"), toda cena vanilla que tem scene.json no VFS
// passa a carregar dele.
std::string ActivateBase(State& state) {
    ClearOverrides(state);
    state.baseActive = false;
    LayerCollector collector = DocumentCollector();
    const auto read = state.resources->read_file_layers(MANIFEST_PATH, CollectLayer, &collector);
    uint32_t bases = 0;
    std::string notes;
    if (read == SHIP_NATIVE_OK) {
        for (const auto& layer : collector.layers) {
            const auto check = LinkSpanUnbound::CheckManifest(layer.json);
            if (check.base) {
                ++bases;
            } else if (!check.valid) {
                notes += "; " + layer.archive + ": " + check.note;
            }
        }
    }
    if (!bases) {
        return "base: inativa (manifestos=" + std::to_string(collector.layers.size()) + ")" + notes;
    }
    if (!state.scenesV2) {
        return "base: sem linkspan.oot.scenes v2, cenas vanilla seguem binárias" + notes;
    }
    state.baseActive = true;
    uint32_t routed = 0;
    const int32_t count = state.scenesV2->get_scene_count();
    for (int32_t id = 0; id < count; ++id) {
        for (uint8_t mq = 0; mq < 2; ++mq) {
            ShipOotSceneInfoV1 info{};
            info.size = sizeof(info);
            if (state.scenesV2->get_scene_info(id, mq, &info) != SHIP_NATIVE_OK || !info.file_name[0] ||
                (mq && !info.has_master_quest)) {
                break;
            }
            const std::string document = LinkSpanUnbound::SceneDirName(info.file_name, mq != 0) + "/scene.json";
            if (state.resources->has_file(document.c_str()) &&
                state.scenesV2->override_scene(id, mq, document.c_str()) == SHIP_NATIVE_OK) {
                state.overrides.emplace_back(id, mq);
                ++routed;
            }
        }
    }
    return "base: ativa (camadas=" + std::to_string(bases) + ") cenas=" + std::to_string(routed) + notes;
}

void ResetText(State& state) {
    for (uint32_t language = 0; language < LINKSPAN_OOT_TEXT_LANGUAGES; ++language) {
        if (state.textTouched[language] && state.text) {
            state.text->reset_language(language);
        }
        state.textTouched[language] = false;
    }
}

// §5: text/<lang>/messages.json mescla sobre a tabela do jogo (a base não exporta texto).
std::string ApplyText(State& state) {
    if (!state.text) {
        return "texto: sem linkspan.oot.text";
    }
    ResetText(state);
    std::string report;
    for (uint32_t language = 0; language < LINKSPAN_OOT_TEXT_LANGUAGES; ++language) {
        const std::string path = std::string("text/") + TEXT_LANGUAGES[language] + "/messages.json";
        LayerCollector collector = DocumentCollector();
        if (state.resources->read_file_layers(path.c_str(), CollectLayer, &collector) != SHIP_NATIVE_OK ||
            collector.layers.empty()) {
            continue;
        }
        LinkSpanUnbound::TextTable table;
        const bool built = LinkSpanUnbound::BuildTextTable(collector.layers, table);
        std::string line = std::string("texto ") + TEXT_LANGUAGES[language] + ": camadas=" +
                           std::to_string(table.layersUsed);
        if (built) {
            state.textTouched[language] = true;
            if (table.replaceTable) {
                state.text->clear_language(language);
                line += " tabela esvaziada";
            }
            uint32_t removed = 0;
            for (const uint32_t id : table.removed) {
                removed += state.text->remove_message(language, id) == SHIP_NATIVE_OK ? 1 : 0;
            }
            uint32_t defined = 0;
            uint32_t failed = 0;
            for (const auto& message : table.messages) {
                const auto status = state.text->set_message(
                    language, message.id, message.box, message.ypos,
                    reinterpret_cast<const uint8_t*>(message.bytes.data()), static_cast<uint32_t>(message.bytes.size()));
                (status == SHIP_NATIVE_OK ? defined : failed) += 1;
            }
            line += " definidas=" + std::to_string(defined) + " removidas=" + std::to_string(removed) +
                    " falhas=" + std::to_string(failed);
        }
        for (const auto& note : table.notes) {
            line += "; " + note;
        }
        report += (report.empty() ? "" : "\n") + line;
    }
    return report.empty() ? "texto: nenhum text/<lang>/messages.json" : report;
}

// game.ready: os archives dos mods já estão montados. Liga a base e registra as cenas. Chamar de novo refaz
// tudo com as camadas atuais.
ShipNativeStatus SHIP_NATIVE_CALL Ready(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                        void* writer) {
    auto* state = static_cast<State*>(user);
    if (!IsOwner(state) || length || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        std::string text = ActivateBase(*state);
        text += "\nregistro: " + (state->scenes ? ApplySceneRegistry(*state) : std::string("sem linkspan.oot.scenes"));
        return WriteText(write, writer, std::move(text));
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

// Primeiro frame: o SoH carrega as tabelas de mensagens depois do game.ready (OTRMessage_Init vem depois do
// ShipLua em InitOTR), então o texto entra aqui. Chamar de novo reaplica sobre a tabela restaurada.
ShipNativeStatus SHIP_NATIVE_CALL ApplyTextFunction(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                                    void* writer) {
    auto* state = static_cast<State*>(user);
    if (!IsOwner(state) || length || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        return WriteText(write, writer, ApplyText(*state));
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

// Estado da base e dos documentos transcodificados, para o main.lua registrar quando muda.
ShipNativeStatus SHIP_NATIVE_CALL UnboundReport(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                                void* writer) {
    auto* state = static_cast<State*>(user);
    if (!IsOwner(state) || length || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        std::lock_guard lock(state->noteMutex);
        std::string text = "base: " + state->baseReport + "\njson: tipos=" + std::to_string(state->jsonTypes.size()) +
                           " scene=" + std::to_string(state->transcoded[0]) +
                           " room=" + std::to_string(state->transcoded[1]) +
                           " collision=" + std::to_string(state->transcoded[2]) +
                           " paths=" + std::to_string(state->transcoded[3]) +
                           " recusados=" + std::to_string(state->transcodeFailures);
        for (const auto& note : state->notes) {
            text += "\nnota: " + note;
        }
        return WriteText(write, writer, std::move(text));
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
}

bool RegisterJsonTypes(State& state) {
    static constexpr struct {
        const char* type;
        uint32_t version;
        DocKind kind;
    } kTypes[] = {
        { "unbound/scene", 1, DocKind::Scene },
        { "unbound/room", 1, DocKind::Room },
        { "unbound/collision", 3, DocKind::Collision },
        { "unbound/paths", 1, DocKind::Paths },
    };
    for (size_t i = 0; i < std::size(kTypes); ++i) {
        auto& binding = state.bindings[i];
        binding = { &state, kTypes[i].type, kTypes[i].version, kTypes[i].kind };
        ShipOotJsonTypeSpecV1 spec{ sizeof(ShipOotJsonTypeSpecV1), binding.type, binding.version, binding.version,
                                    TranscodeJson, &binding };
        uint64_t handle = 0;
        if (state.resourcesV3->register_json_type(&spec, &handle) != SHIP_NATIVE_OK) {
            return false;
        }
        state.jsonTypes.push_back(handle);
    }
    return true;
}

void UnregisterJsonTypes(State& state) {
    if (state.resourcesV3) {
        for (const uint64_t handle : state.jsonTypes) {
            state.resourcesV3->unregister_json_type(handle);
        }
    }
    state.jsonTypes.clear();
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_minor < 1 || !runtime->get_service ||
        !runtime->register_service || !instance) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto* resources = static_cast<const ShipOotResourcesV2*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION_2,
                             sizeof(ShipOotResourcesV2)));
    if (!resources || !resources->read_file_layers) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* state = new (std::nothrow) State;
    if (!state) {
        return SHIP_NATIVE_FAILURE;
    }
    state->resources = resources;
    state->resourcesV3 = static_cast<const ShipOotResourcesV3*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION_3,
                             sizeof(ShipOotResourcesV3)));
    state->scenesV3 = static_cast<const ShipOotScenesV3*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_SCENES_SERVICE, LINKSPAN_OOT_SCENES_VERSION_3, sizeof(ShipOotScenesV3)));
    state->scenesV2 = state->scenesV3 ? reinterpret_cast<const ShipOotScenesV2*>(state->scenesV3)
                                      : static_cast<const ShipOotScenesV2*>(runtime->get_service(
                                            runtime->context, LINKSPAN_OOT_SCENES_SERVICE,
                                            LINKSPAN_OOT_SCENES_VERSION_2, sizeof(ShipOotScenesV2)));
    // ShipOotScenesV2 começa com a tabela V1.
    state->scenes = state->scenesV2 ? reinterpret_cast<const ShipOotScenesV1*>(state->scenesV2)
                                    : static_cast<const ShipOotScenesV1*>(runtime->get_service(
                                          runtime->context, LINKSPAN_OOT_SCENES_SERVICE, LINKSPAN_OOT_SCENES_VERSION,
                                          sizeof(ShipOotScenesV1)));
    state->text = static_cast<const ShipOotTextV1*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_TEXT_SERVICE, LINKSPAN_OOT_TEXT_VERSION,
                             sizeof(ShipOotTextV1)));
    state->ownerThread = std::this_thread::get_id();
    try {
        std::error_code error;
        const fs::path logs = GameDirectory() / "logs";
        fs::create_directories(logs, error);
        state->logPath = logs / "linkspan-unbound.log";
        std::ofstream(state->logPath, std::ios::binary | std::ios::trunc);
    } catch (...) {
        state->logPath.clear();
    }
    if (!runtime->register_function ||
        runtime->register_function(runtime->context, "load_scene_registry", LoadSceneRegistry, state) !=
            SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "room_report", RoomReport, state) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "ready", Ready, state) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "apply_text", ApplyTextFunction, state) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "unbound_report", UnboundReport, state) != SHIP_NATIVE_OK) {
        delete state;
        return SHIP_NATIVE_FAILURE;
    }
    // Host sem oot.room.actors (ou ABI < 1.2) segue sem alterar salas.
    if (runtime->abi_minor >= 2 && runtime->register_hook) {
        const ShipNativeHookSpec spec{ sizeof(ShipNativeHookSpec), LINKSPAN_OOT_HOOK_ROOM_ACTORS,
                                       LINKSPAN_OOT_HOOK_ROOM_ACTORS_VERSION, sizeof(ShipOotRoomActorsHookV2),
                                       SHIP_NATIVE_HOOK_TRANSFORM, 0, 0, OnRoomActors, state };
        uint64_t handle = 0;
        state->roomHook = runtime->register_hook(runtime->context, &spec, &handle) == SHIP_NATIVE_OK;
    }
    try {
        state->schemas.push_back(
            { ACTOR_PATCH_SCHEMA, "actor_patch",
              LINKSPAN_UNBOUND_JSON_MERGE_OBJECTS_RECURSIVE | LINKSPAN_UNBOUND_JSON_REPLACE_ARRAYS });
        // Formato 2: documentos de cena viram recursos do jogo. Sem v3 (host antigo) o framework fica só
        // com a factory, o registro e o patch de atores.
        if (state->resourcesV3 && !RegisterJsonTypes(*state)) {
            UnregisterJsonTypes(*state);
            state->baseReport = "tipos JSON recusados pelo host";
        } else if (!state->resourcesV3) {
            state->baseReport = "host sem linkspan.oot.resources v3";
        } else if (!state->scenesV2) {
            state->baseReport = "host sem linkspan.oot.scenes v2";
        } else {
            PrepareBase(runtime, *state);
        }
        AppendLog(*state, "base: " + state->baseReport);
    } catch (const std::exception& error) {
        state->baseReport = std::string("falha na base: ") + error.what();
    } catch (...) {
        UnregisterJsonTypes(*state);
        delete state;
        return SHIP_NATIVE_FAILURE;
    }
    state->service = {
        sizeof(LinkSpanUnboundJsonFactoryV1), state, HasSchema, ListSchemas, LoadMerged, ReadJson, GetMetadata, Release
    };
    *instance = state;
    return runtime->register_service(runtime->context, LINKSPAN_UNBOUND_JSON_FACTORY_SERVICE,
                                     LINKSPAN_UNBOUND_JSON_FACTORY_VERSION, sizeof(LinkSpanUnboundJsonFactoryV1),
                                     &state->service);
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* state = static_cast<State*>(instance);
    if (state && IsOwner(state)) {
        ResetText(*state);
        ClearOverrides(*state);
        UnregisterScenes(*state);
        UnregisterJsonTypes(*state);
        if (state->baseArchive) {
            state->resources->unmount_archive(state->baseArchive);
        }
    }
    delete state;
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init, Shutdown,
    };
    return &descriptor;
}
