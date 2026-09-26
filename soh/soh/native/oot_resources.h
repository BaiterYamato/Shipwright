#ifndef LINKSPAN_OOT_RESOURCES_H
#define LINKSPAN_OOT_RESOURCES_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_RESOURCES_SERVICE "linkspan.oot.resources"
#define LINKSPAN_OOT_RESOURCES_VERSION 1u
#define LINKSPAN_OOT_RESOURCES_VERSION_2 2u

/* Callback síncrono. `path` pertence ao host e só permanece válido durante a
 * chamada. Retornar outro status interrompe a enumeração e o propaga. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotResourcePathFn)(
    void* user, const char* path, uint32_t path_length);

typedef struct ShipOotResourceLayerV2 {
    uint32_t size;
    uint32_t layer_index;
    uint32_t layer_count;
    uint32_t game_version;
    uint64_t content_hash;
    uint32_t data_size;
    uint32_t archive_path_length;
} ShipOotResourceLayerV2;

/* Callback síncrono, da menor para a maior prioridade. Todos os ponteiros
 * pertencem ao host e permanecem válidos somente durante a chamada. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotResourceLayerFn)(
    void* user, const ShipOotResourceLayerV2* layer, const char* archive_path,
    const uint8_t* data);

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

/* Prefixo binário compatível com ShipOotResourcesV1. A regra de merge pertence
 * ao provider; o host apenas enumera as cópias montadas em ordem estável. */
typedef struct ShipOotResourcesV2 {
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
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file_layers)(
        const char* path, ShipOotResourceLayerFn callback, void* user);
} ShipOotResourcesV2;

#define LINKSPAN_OOT_RESOURCES_VERSION_3 3u
#define LINKSPAN_OOT_RESOURCES_MAX_JSON_TYPE 128u

/* Transcodifica um documento JSON para um recurso XML do SoH, dentro do
 * carregamento do recurso e na thread que o carrega: o resource manager usa um
 * pool de threads, então o callback não chama serviços restritos à thread do jogo
 * e protege o próprio estado. `path` é o caminho pedido ao resource
 * manager; o provider lê as camadas que quiser (read_file_layers) e aplica a regra
 * de merge dele. `version` é o número depois da última barra do "$schema" da
 * camada de maior prioridade que o traz. O XML escrito por `write` (pode chegar
 * em pedaços) é um documento que as fábricas XML do host aceitam: a raiz decide o
 * tipo e a versão (<Scene>, <Room>, <CollisionHeader>, <Path>...). Retornar outro
 * status que não OK falha o carregamento do recurso, com o status no log. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotJsonTranscodeFn)(void* user, const char* path, uint32_t version,
                                                                   ShipNativeWriteFn write, void* writer);

typedef struct ShipOotJsonTypeSpecV1 {
    uint32_t size;
    /* Parte do "$schema" antes da versão ("unbound/scene"); [A-Za-z0-9._/-], até
     * LINKSPAN_OOT_RESOURCES_MAX_JSON_TYPE bytes, sem colidir com tipo do host. */
    const char* type;
    uint32_t min_version;
    uint32_t max_version;
    ShipOotJsonTranscodeFn transcode;
    void* user;
} ShipOotJsonTypeSpecV1;

/* Prefixo binário compatível com ShipOotResourcesV2.
 *
 * register_json_type: um recurso JSON (primeiro byte '{') cujo "$schema" tem
 *   esse tipo passa pelo transcodificador e pela fábrica XML do host. Um tipo
 *   tem um dono por vez: registrar de novo o mesmo tipo = INVALID_ARGUMENT.
 *   Versão fora da faixa falha o carregamento. Recursos já criados continuam no
 *   cache do resource manager; use dirty_resources para recarregar.
 * unregister_json_type: o tipo volta a não ter fábrica (o recurso falha ao
 *   carregar, como sem o mod). Quem registra deve remover no shutdown. */
typedef struct ShipOotResourcesV3 {
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
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file_layers)(
        const char* path, ShipOotResourceLayerFn callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_json_type)(const ShipOotJsonTypeSpecV1* spec, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_json_type)(uint64_t handle);
} ShipOotResourcesV3;

#endif
