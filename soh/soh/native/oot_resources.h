#ifndef LINKSPAN_OOT_RESOURCES_H
#define LINKSPAN_OOT_RESOURCES_H

#include <shiplua/native/ship_native_abi.h>
#include <stddef.h>

#define LINKSPAN_OOT_RESOURCES_SERVICE "linkspan.oot.resources"
#define LINKSPAN_OOT_RESOURCES_VERSION 1u
#define LINKSPAN_OOT_RESOURCES_VERSION_2 2u

/* Callback síncrono. `path` pertence ao host e só permanece válido durante a
 * chamada. Retornar outro status interrompe a enumeração e o propaga. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotResourcePathFn)(void* user, const char* path, uint32_t path_length);

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
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotResourceLayerFn)(void* user, const ShipOotResourceLayerV2* layer,
                                                                   const char* archive_path, const uint8_t* data);

/* VFS do Shipwright, disponível apenas na thread do jogo.
 *
 * read_file e get_game_versions aceitam uma primeira chamada com output=NULL e
 * capacity=0 para consultar o tamanho necessário. Buffers pertencem ao chamador.
 * mount_archive aceita O2R/OTR/ZIP/pasta suportados pelo ArchiveManager e devolve
 * um handle opaco. Quem monta deve desmontar no shutdown do provider. */
typedef struct ShipOotResourcesV1 {
    uint32_t size;
    uint8_t(SHIP_NATIVE_CALL* has_file)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file)(const char* path, uint8_t* output, uint32_t capacity,
                                                  uint32_t* output_size);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_files)(const char* search_mask, ShipOotResourcePathFn callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* dirty_resources)(const char* search_mask);
    ShipNativeStatus(SHIP_NATIVE_CALL* unload_resource)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* mount_archive)(const char* archive_path, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* unmount_archive)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_game_versions)(uint32_t* output, uint32_t capacity, uint32_t* output_count);
} ShipOotResourcesV1;

/* Prefixo binário compatível com ShipOotResourcesV1. A regra de merge pertence
 * ao provider; o host apenas enumera as cópias montadas em ordem estável. */
typedef struct ShipOotResourcesV2 {
    uint32_t size;
    uint8_t(SHIP_NATIVE_CALL* has_file)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file)(const char* path, uint8_t* output, uint32_t capacity,
                                                  uint32_t* output_size);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_files)(const char* search_mask, ShipOotResourcePathFn callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* dirty_resources)(const char* search_mask);
    ShipNativeStatus(SHIP_NATIVE_CALL* unload_resource)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* mount_archive)(const char* archive_path, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* unmount_archive)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_game_versions)(uint32_t* output, uint32_t capacity, uint32_t* output_count);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file_layers)(const char* path, ShipOotResourceLayerFn callback, void* user);
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
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file)(const char* path, uint8_t* output, uint32_t capacity,
                                                  uint32_t* output_size);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_files)(const char* search_mask, ShipOotResourcePathFn callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* dirty_resources)(const char* search_mask);
    ShipNativeStatus(SHIP_NATIVE_CALL* unload_resource)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* mount_archive)(const char* archive_path, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* unmount_archive)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_game_versions)(uint32_t* output, uint32_t capacity, uint32_t* output_count);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file_layers)(const char* path, ShipOotResourceLayerFn callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_json_type)(const ShipOotJsonTypeSpecV1* spec, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_json_type)(uint64_t handle);
} ShipOotResourcesV3;

#define LINKSPAN_OOT_RESOURCES_VERSION_4 4u
#define LINKSPAN_OOT_MOUNT_STRICT 1u

/* Callbacks podem rodar em workers: reentrantes, sem serviços da thread do jogo.
 * Montar ou desmontar de dentro do transform devolve LIMIT.
 * Ponteiros e writer só valem na chamada. Sem transform, os bytes passam intactos.
 * Com transform, emitir a saída inteira, inclusive passthrough, e retornar OK.
 * O writer real usa const char* e uint32_t; dividir saídas maiores em pedaços. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotArchiveTransformFn)(void* user, const char* original_path,
                                                                      const uint8_t* data, size_t size,
                                                                      ShipNativeWriteFn write, void* writer);

typedef struct ShipOotMountAliasV1 {
    uint32_t size;
    uint32_t reserved_align;
    const char* source_prefix;
    const char* target_prefix;
} ShipOotMountAliasV1;

/* Prefixos relativos normalizados com / final, sem .., absoluto ou __OTR__.
 * Alias mapeia prefix + source_prefix para target_prefix; destino raiz usa "".
 * O host copia spec/strings/aliases; callback e user vivem até unmount drenado.
 * owner é o id canônico do manifesto, com o mesmo limite de 128 bytes da F2.
 * STRICT proíbe fallback raiz para dependência interna ausente. */
typedef struct ShipOotPrefixedArchiveSpecV1 {
    uint32_t size;
    uint32_t reserved_align;
    const char* owner;
    const char* archive_path;
    const char* prefix;
    uint32_t flags;
    uint32_t reserved_flags;
    const ShipOotMountAliasV1* aliases;
    uint32_t alias_count;
    uint32_t reserved_aliases;
    ShipOotArchiveTransformFn transform;
    void* transform_user;
} ShipOotPrefixedArchiveSpecV1;

/* Contagens uint32_t; exceder o limite falha com LIMIT, sem publicação parcial.
 * file_count conta arquivos canônicos; aliases contam regras, não arquivos.
 * Um alias só é aceito se todos os seus destinos estiverem livres. */
typedef struct ShipOotMountInfoV1 {
    uint32_t size;
    uint32_t file_count;
    uint32_t aliases_accepted;
    uint32_t aliases_rejected;
} ShipOotMountInfoV1;

/* Prefixo binário V3 literal. Montar/desmontar só na thread do jogo.
 * spec, handle e info obrigatórios; informar size=sizeof e zerar reservas.
 * Colisão canônica ou falha: handle zero e VFS/cache sem alteração.
 * Reusar unmount_archive; drenar workers antes do unload do owner.
 * Não há layout_id: este serviço transporta bytes, sem tipos do jogo. */
typedef struct ShipOotResourcesV4 {
    uint32_t size;
    uint8_t(SHIP_NATIVE_CALL* has_file)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file)(const char* path, uint8_t* output, uint32_t capacity,
                                                  uint32_t* output_size);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_files)(const char* search_mask, ShipOotResourcePathFn callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* dirty_resources)(const char* search_mask);
    ShipNativeStatus(SHIP_NATIVE_CALL* unload_resource)(const char* path);
    ShipNativeStatus(SHIP_NATIVE_CALL* mount_archive)(const char* archive_path, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* unmount_archive)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_game_versions)(uint32_t* output, uint32_t capacity, uint32_t* output_count);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_file_layers)(const char* path, ShipOotResourceLayerFn callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_json_type)(const ShipOotJsonTypeSpecV1* spec, uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_json_type)(uint64_t handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* mount_archive_prefixed)(const ShipOotPrefixedArchiveSpecV1* spec,
                                                               uint64_t* handle, ShipOotMountInfoV1* info);
} ShipOotResourcesV4;

#endif
