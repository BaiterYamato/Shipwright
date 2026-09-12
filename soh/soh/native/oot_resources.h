#ifndef LINKSPAN_OOT_RESOURCES_H
#define LINKSPAN_OOT_RESOURCES_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_RESOURCES_SERVICE "linkspan.oot.resources"
#define LINKSPAN_OOT_RESOURCES_VERSION 1u

/* Callback síncrono. `path` pertence ao host e só permanece válido durante a
 * chamada. Retornar outro status interrompe a enumeração e o propaga. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotResourcePathFn)(
    void* user, const char* path, uint32_t path_length);

/* VFS do Shipwright, disponível apenas na thread do jogo.
 *
 * read_file e get_game_versions aceitam uma primeira chamada com output=NULL e
 * capacity=0 para consultar o tamanho necessário. Buffers pertencem ao chamador.
 * mount_archive aceita O2R/OTR/ZIP/pasta suportados pelo ArchiveManager e devolve
 * um handle opaco. Quem monta deve desmontar no shutdown do provider. */
typedef struct ShipOotResourcesV1 {
    uint32_t size;
    uint8_t(SHIP_NATIVE_CALL* has_file)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file)(
        const char* path, uint8_t* output, uint32_t capacity, uint32_t* output_size);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_files)(
        const char* search_mask, ShipOotResourcePathFn callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* dirty_resources)(const char* search_mask);
    ShipNativeStatus(SHIP_NATIVE_CALL* unload_resource)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* mount_archive)(const char* archive_path, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* unmount_archive)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_game_versions)(
        uint32_t* output, uint32_t capacity, uint32_t* output_count);
} ShipOotResourcesV1;

#endif
