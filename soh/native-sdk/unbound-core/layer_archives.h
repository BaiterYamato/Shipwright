#pragma once

// HOST-SLIM-001 F4 (RFC 0029): leitura por camadas pelo serviço de recursos e a política das camadas Unbound na raiz
// do VFS. Sem o estado da DLL: os efeitos passam pelo serviço e pelas saídas, para o teste rodar sem o jogo.
#include <cstdint>
#include <string>
#include <vector>

#include "json_merge.h"
#include "oot_hooks.h"
#include "oot_resources.h"

namespace LinkSpanUnbound {

constexpr uint32_t MAX_LAYERS = 64;
// unbound.json de cada camada: um por mod Unbound, então o teto de documento (64) desligava a base e a checagem
// de conflitos a partir do 64º mod.
constexpr uint32_t MAX_MANIFEST_LAYERS = 1024;
constexpr uint32_t MAX_TOTAL_INPUT = 4 * 1024 * 1024;
// Documentos de cena, colisão e texto: uma sala grande passa fácil dos 64 KiB da factory JSON.
constexpr uint32_t MAX_DOCUMENT_LAYER = 64 * 1024 * 1024;
constexpr uint32_t MAX_DOCUMENT_TOTAL = 256 * 1024 * 1024;
constexpr const char* MANIFEST_PATH = "unbound.json";

struct LayerCollector {
    std::vector<LayerDocument> layers;
    uint32_t totalBytes = 0;
    uint32_t maxLayer = SHIP_NATIVE_MAX_BYTES;
    uint32_t maxTotal = MAX_TOTAL_INPUT;
    uint32_t maxLayers = MAX_LAYERS;
};

LayerCollector DocumentCollector();
LayerCollector ManifestCollector();
// ShipOotResourceLayerFn de read_file_layers: acumula as camadas em um LayerCollector dentro dos limites dele.
ShipNativeStatus SHIP_NATIVE_CALL CollectLayer(void* user, const ShipOotResourceLayerV2* layer, const char* archivePath,
                                               const uint8_t* data);
const char* StatusName(ShipNativeStatus status);

struct LayerMountSummary {
    uint32_t mounted = 0;
    uint32_t alreadyAtRoot = 0;
    uint32_t failed = 0;

    std::string Report() const;
};

// Monta na raiz os archives do oot.mods.archives que são camada: .o2r cujo unbound.json, lido da montagem privada
// (private_prefix) e vindo do próprio archive, passa em IsLayerManifest. Pula o archive que já fornece o unbound.json
// da raiz. Os handles montados vão para mounted, na ordem do payload; as linhas para logs/linkspan-unbound.log vão
// para log. O chamador confere o buffer do hook; aqui só size e stride do payload.
ShipNativeStatus MountLayerArchives(const ShipOotResourcesV2& resources, const ShipOotModArchivesHookV1& payload,
                                    std::vector<uint64_t>& mounted, std::vector<std::string>& log,
                                    LayerMountSummary& summary);
// Desmonta na ordem inversa da montagem e esvazia mounted.
void UnmountLayerArchives(const ShipOotResourcesV2& resources, std::vector<uint64_t>& mounted);

} // namespace LinkSpanUnbound
