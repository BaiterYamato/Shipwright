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

namespace {

constexpr uint32_t MAX_LAYERS = 64;
constexpr uint32_t MAX_TOTAL_INPUT = 4 * 1024 * 1024;
constexpr uint32_t MAX_HANDLES = 1024;
constexpr const char* ACTOR_PATCH_SCHEMA = "linkspan.unbound.actor-patch/v1";

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
    state->ownerThread = std::this_thread::get_id();
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
    delete static_cast<State*>(instance);
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 1u, Init, Shutdown,
    };
    return &descriptor;
}
