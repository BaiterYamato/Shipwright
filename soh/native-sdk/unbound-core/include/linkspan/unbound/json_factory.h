#ifndef LINKSPAN_UNBOUND_JSON_FACTORY_H
#define LINKSPAN_UNBOUND_JSON_FACTORY_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_UNBOUND_JSON_FACTORY_SERVICE "linkspan.unbound.json_factory"
#define LINKSPAN_UNBOUND_JSON_FACTORY_VERSION 1u
#define LINKSPAN_UNBOUND_JSON_MAX_SCHEMA 255u
#define LINKSPAN_UNBOUND_JSON_MERGE_OBJECTS_RECURSIVE 0x1u
#define LINKSPAN_UNBOUND_JSON_REPLACE_ARRAYS 0x2u

typedef struct LinkSpanUnboundJsonMetadataV1 {
    uint32_t size;
    uint32_t layer_count;
    uint64_t merged_hash;
    uint32_t schema_length;
    uint32_t path_length;
} LinkSpanUnboundJsonMetadataV1;

/* Callback síncrono. Os textos pertencem ao coremod e só permanecem válidos
 * durante a chamada. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* LinkSpanUnboundJsonSchemaFn)(void* user, const char* schema,
                                                                        uint32_t schema_length, const char* type,
                                                                        uint32_t type_length, uint32_t merge_flags);

/* Factory JSON publicada pelo coremod Unbound. Todas as funções devem ser
 * chamadas na thread do jogo. Handles pertencem ao coremod e precisam ser
 * liberados antes do shutdown do consumidor. read_json aceita output NULL e
 * capacidade zero para consultar o tamanho, sem terminador NUL. */
typedef struct LinkSpanUnboundJsonFactoryV1 {
    uint32_t size;
    void* context;
    uint8_t(SHIP_NATIVE_CALL* has_schema)(void* context, const char* schema);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_schemas)(void* context, LinkSpanUnboundJsonSchemaFn callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* load_merged)(void* context, const char* schema, const char* path,
                                                    uint64_t* handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_json)(void* context, uint64_t handle, char* output, uint32_t capacity,
                                                  uint32_t* output_size);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_metadata)(void* context, uint64_t handle,
                                                     LinkSpanUnboundJsonMetadataV1* metadata, char* schema_output,
                                                     uint32_t schema_capacity, char* path_output,
                                                     uint32_t path_capacity);
    ShipNativeStatus(SHIP_NATIVE_CALL* release)(void* context, uint64_t handle);
} LinkSpanUnboundJsonFactoryV1;

#endif
