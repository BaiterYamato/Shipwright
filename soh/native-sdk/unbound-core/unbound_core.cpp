#include <algorithm>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "include/linkspan/unbound/json_factory.h"
#include "json_merge.h"
#include "oot_resources.h"
#include "oot_scenes.h"
#include "scene_registry.h"

namespace {

constexpr uint32_t MAX_LAYERS = 64;
constexpr uint32_t MAX_TOTAL_INPUT = 4 * 1024 * 1024;
constexpr uint32_t MAX_HANDLES = 1024;
constexpr const char* ACTOR_PATCH_SCHEMA = "linkspan.unbound.actor-patch/v1";
constexpr const char* SCENE_REGISTRY_PATH = "unbound/scenes.json";

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
    // Opcional: sem ele a factory JSON continua e load_scene_registry responde unsupported.
    const ShipOotScenesV1* scenes = nullptr;
    std::vector<uint64_t> sceneHandles;
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
};

ShipNativeStatus SHIP_NATIVE_CALL CollectLayer(void* user, const ShipOotResourceLayerV2* layer, const char* archivePath,
                                               const uint8_t* data) {
    auto* collector = static_cast<LayerCollector*>(user);
    if (!collector || !layer || layer->size < sizeof(ShipOotResourceLayerV2) || !archivePath ||
        (!data && layer->data_size) || layer->layer_index != collector->layers.size() ||
        layer->layer_count > MAX_LAYERS || layer->data_size > SHIP_NATIVE_MAX_BYTES ||
        collector->totalBytes > MAX_TOTAL_INPUT - layer->data_size) {
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
        LayerCollector collector;
        const auto read = state->resources->read_file_layers(SCENE_REGISTRY_PATH, CollectLayer, &collector);
        UnregisterScenes(*state);
        const std::string layers = "layers=" + std::to_string(collector.layers.size());
        if (read != SHIP_NATIVE_OK || collector.layers.empty()) {
            return WriteText(write, writer,
                             "scenes=0; entrances=0; notes=0; " + layers + "; read=" +
                                 (read == SHIP_NATIVE_OK ? "ok" : StatusName(read)));
        }
        LinkSpanUnbound::MergeResult merged;
        LinkSpanUnbound::SceneRegistryDocument document;
        std::string error;
        if (!LinkSpanUnbound::MergeSchemaFreeDocuments(collector.layers, merged, error) ||
            !LinkSpanUnbound::ParseSceneRegistry(merged.json, document, error)) {
            return WriteText(write, writer, "scenes=0; entrances=0; notes=0; " + layers + "; error=" + error);
        }
        uint32_t scenesRegistered = 0;
        uint32_t entrancesRegistered = 0;
        std::vector<std::string> notes = document.notes;
        for (const auto& scene : document.scenes) {
            ShipOotSceneDefinitionV1 definition{};
            definition.size = sizeof(definition);
            definition.name = scene.name.c_str();
            definition.display_name = scene.displayName.c_str();
            definition.scene_path = scene.path.c_str();
            definition.requested_id = scene.sceneId;
            definition.draw_config = scene.drawConfig;
            uint64_t handle = 0;
            int32_t sceneId = 0;
            const auto sceneStatus = state->scenes->register_scene(&definition, &handle, &sceneId);
            if (sceneStatus != SHIP_NATIVE_OK) {
                notes.push_back(scene.name + ": recusada pelo host (" + StatusName(sceneStatus) + ")");
                continue;
            }
            state->sceneHandles.push_back(handle);
            ++scenesRegistered;
            for (const auto& entrance : scene.entrances) {
                ShipOotEntranceDefinitionV1 entranceDefinition{};
                entranceDefinition.size = sizeof(entranceDefinition);
                entranceDefinition.key = entrance.key.c_str();
                entranceDefinition.requested_index = entrance.index;
                entranceDefinition.spawn = entrance.spawn;
                entranceDefinition.continue_bgm = entrance.continueBgm ? 1 : 0;
                entranceDefinition.show_title_card = entrance.showTitleCard ? 1 : 0;
                entranceDefinition.end_transition = entrance.endTransition;
                entranceDefinition.start_transition = entrance.startTransition;
                int32_t index = 0;
                const auto entranceStatus = state->scenes->register_entrance(handle, &entranceDefinition, &index);
                if (entranceStatus != SHIP_NATIVE_OK) {
                    notes.push_back(scene.name + "/" + entrance.key + ": recusada pelo host (" +
                                    StatusName(entranceStatus) + ")");
                } else {
                    ++entrancesRegistered;
                }
            }
        }
        std::string text = "scenes=" + std::to_string(scenesRegistered) + "; entrances=" +
                           std::to_string(entrancesRegistered) + "; notes=" + std::to_string(notes.size()) + "; " +
                           layers;
        for (const auto& note : notes) {
            text += "; " + note;
        }
        return WriteText(write, writer, std::move(text));
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
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
    state->scenes = static_cast<const ShipOotScenesV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_SCENES_SERVICE, LINKSPAN_OOT_SCENES_VERSION, sizeof(ShipOotScenesV1)));
    state->ownerThread = std::this_thread::get_id();
    if (!runtime->register_function ||
        runtime->register_function(runtime->context, "load_scene_registry", LoadSceneRegistry, state) !=
            SHIP_NATIVE_OK) {
        delete state;
        return SHIP_NATIVE_FAILURE;
    }
    try {
        state->schemas.push_back(
            { ACTOR_PATCH_SCHEMA, "actor_patch",
              LINKSPAN_UNBOUND_JSON_MERGE_OBJECTS_RECURSIVE | LINKSPAN_UNBOUND_JSON_REPLACE_ARRAYS });
    } catch (...) {
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
        UnregisterScenes(*state);
    }
    delete state;
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 1u, Init, Shutdown,
    };
    return &descriptor;
}
