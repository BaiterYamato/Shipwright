#pragma once
#include <shiplua/native/ship_native_abi.h>

// Serviço versionado por nome, versão e tamanho; fica fora dos globs do layout id
// (ExportSdk.cmake) para não invalidar os mods já compilados. Os elementos são structs z64, então
// o consumidor confere layout_id e stride antes de indexar. V1 é congelada: mudança pede V2.
#define LINKSPAN_OOT_SCENE_QUERY_SERVICE "linkspan.oot.scene_query"
#define LINKSPAN_OOT_SCENE_QUERY_VERSION 1u
#define SHIP_OOT_SCENE_SPAWNS 0u
#define SHIP_OOT_SCENE_ENTRANCES 1u
#define SHIP_OOT_SCENE_EXITS 2u
typedef struct ShipOotSceneListV1 {
    uint32_t size;
    uint32_t count;
    uint32_t stride;
    uint32_t reserved;
    const void* data;
} ShipOotSceneListV1;
typedef struct ShipOotSceneQueryV1 {
    uint32_t size;
    const char* layout_id;
    // Só na thread do jogo; dados emprestados até o fim do callback atual.
    // Spawns: ActorEntry; entradas: EntranceEntry; exits: uint16_t.
    ShipNativeStatus(SHIP_NATIVE_CALL* get_list)(uint32_t kind, ShipOotSceneListV1* list);
} ShipOotSceneQueryV1;
