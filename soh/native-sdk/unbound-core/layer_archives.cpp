#include "layer_archives.h"

#include <filesystem>
#include <utility>

#include "unbound_docs.h"

namespace LinkSpanUnbound {

namespace fs = std::filesystem;

namespace {

struct OwnManifest {
    fs::path archive;
    std::string json;
    bool found = false;
    bool oversized = false;
};

// Camada de outro archive com o mesmo prefixo não conta nem esbarra nos limites: só a do próprio archive é copiada.
ShipNativeStatus SHIP_NATIVE_CALL CollectOwnManifest(void* user, const ShipOotResourceLayerV2* layer,
                                                     const char* archivePath, const uint8_t* data) {
    auto* own = static_cast<OwnManifest*>(user);
    if (!own || !layer || layer->size < sizeof(ShipOotResourceLayerV2) || !archivePath || (!data && layer->data_size)) {
        return SHIP_NATIVE_LIMIT;
    }
    try {
        std::error_code error;
        if (own->found ||
            !fs::equivalent(fs::path(std::string(archivePath, layer->archive_path_length)), own->archive, error)) {
            return SHIP_NATIVE_OK;
        }
        own->found = true;
        own->oversized = layer->data_size > kMaxLayerManifestBytes;
        if (!own->oversized && data) {
            own->json.assign(reinterpret_cast<const char*>(data), layer->data_size);
        }
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

// unbound.json de um archive pela montagem privada do host (mod/<stem>/). Lê por camadas: dois .o2r de mesmo nome em
// pastas diferentes dividem o prefixo, e só a camada vinda do próprio archive serve. Falso se ele não tiver o arquivo
// ou se o arquivo passar de kMaxLayerManifestBytes.
bool ReadPrivateManifest(const ShipOotResourcesV2& resources, const std::string& prefix, const fs::path& archive,
                         std::string& bytes) {
    OwnManifest own{ archive };
    if (resources.read_file_layers((prefix + MANIFEST_PATH).c_str(), CollectOwnManifest, &own) != SHIP_NATIVE_OK ||
        !own.found || own.oversized) {
        return false;
    }
    bytes = std::move(own.json);
    return true;
}

} // namespace

LayerCollector DocumentCollector() {
    LayerCollector collector;
    collector.maxLayer = MAX_DOCUMENT_LAYER;
    collector.maxTotal = MAX_DOCUMENT_TOTAL;
    return collector;
}

LayerCollector ManifestCollector() {
    LayerCollector collector = DocumentCollector();
    collector.maxLayers = MAX_MANIFEST_LAYERS;
    return collector;
}

ShipNativeStatus SHIP_NATIVE_CALL CollectLayer(void* user, const ShipOotResourceLayerV2* layer, const char* archivePath,
                                               const uint8_t* data) {
    auto* collector = static_cast<LayerCollector*>(user);
    if (!collector || !layer || layer->size < sizeof(ShipOotResourceLayerV2) || !archivePath ||
        (!data && layer->data_size) || layer->layer_index != collector->layers.size() ||
        layer->layer_count > collector->maxLayers || layer->data_size > collector->maxLayer ||
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

std::string LayerMountSummary::Report() const {
    return "montadas=" + std::to_string(mounted) + " ja_na_raiz=" + std::to_string(alreadyAtRoot) +
           " falhas=" + std::to_string(failed);
}

ShipNativeStatus MountLayerArchives(const ShipOotResourcesV2& resources, const ShipOotModArchivesHookV1& payload,
                                    std::vector<uint64_t>& mounted, std::vector<std::string>& log,
                                    LayerMountSummary& summary) {
    if (payload.size < sizeof(ShipOotModArchivesHookV1) ||
        (payload.count && (!payload.archives || payload.archive_stride < sizeof(ShipOotModArchiveV1)))) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    try {
        std::vector<fs::path> atRoot;
        LayerCollector manifests = ManifestCollector();
        if (resources.read_file_layers(MANIFEST_PATH, CollectLayer, &manifests) == SHIP_NATIVE_OK) {
            for (const auto& layer : manifests.layers) {
                atRoot.push_back(fs::path(layer.archive));
            }
        }
        const auto alreadyAtRoot = [&atRoot](const fs::path& archive) {
            for (const auto& root : atRoot) {
                std::error_code error;
                if (fs::equivalent(root, archive, error)) {
                    return true;
                }
            }
            return false;
        };
        // Reserva antes de montar: um push_back que falhasse depois do mount deixaria a camada fora do Shutdown.
        mounted.reserve(mounted.size() + payload.count);
        summary = {};
        const auto* bytes = reinterpret_cast<const uint8_t*>(payload.archives);
        for (uint32_t i = 0; i < payload.count; ++i) {
            const auto& entry =
                *reinterpret_cast<const ShipOotModArchiveV1*>(bytes + static_cast<size_t>(i) * payload.archive_stride);
            if (entry.size < sizeof(ShipOotModArchiveV1) || !entry.path || !entry.private_prefix) {
                continue;
            }
            const fs::path archive(entry.path); // mesma codificação estreita em que o host gerou o caminho
            std::string manifest;
            if (archive.extension() != ".o2r" || !*entry.private_prefix ||
                !ReadPrivateManifest(resources, entry.private_prefix, archive, manifest) ||
                !IsLayerManifest(manifest)) {
                continue;
            }
            if (alreadyAtRoot(archive)) {
                ++summary.alreadyAtRoot;
                log.push_back(std::string("camada já na raiz: ") + entry.path);
                continue;
            }
            uint64_t handle = 0;
            const auto status = resources.mount_archive(entry.path, &handle);
            if (status == SHIP_NATIVE_OK) {
                mounted.push_back(handle);
                ++summary.mounted;
                log.push_back(std::string("camada montada na raiz: ") + entry.path);
            } else {
                ++summary.failed;
                log.push_back(std::string("camada não montada (") + StatusName(status) + "): " + entry.path);
            }
        }
        log.push_back("camadas: " + summary.Report());
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

void UnmountLayerArchives(const ShipOotResourcesV2& resources, std::vector<uint64_t>& mounted) {
    for (auto layer = mounted.rbegin(); layer != mounted.rend(); ++layer) {
        resources.unmount_archive(*layer);
    }
    mounted.clear();
}

} // namespace LinkSpanUnbound
