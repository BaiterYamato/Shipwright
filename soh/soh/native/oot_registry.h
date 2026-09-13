#ifndef LINKSPAN_OOT_REGISTRY_H
#define LINKSPAN_OOT_REGISTRY_H

#include <limits.h>
#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_REGISTRY_SERVICE "linkspan.oot.registry"
#define LINKSPAN_OOT_REGISTRY_VERSION 1u
#define LINKSPAN_OOT_REGISTRY_AUTO_ID INT32_MIN
#define LINKSPAN_OOT_REGISTRY_MAX_NAME 255u
#define LINKSPAN_OOT_REGISTRY_MAX_SPACES 64u
#define LINKSPAN_OOT_REGISTRY_MAX_ENTRIES 65536u

/* Callback síncrono. `name` pertence ao host e só permanece válido durante a
 * chamada. Retornar outro status interrompe a enumeração e o propaga. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotRegistryEntryFn)(void* user, uint64_t entry_handle, int32_t id,
                                                                   const char* name, uint32_t name_length);

/* Registro genérico do host, disponível apenas na thread do jogo.
 *
 * O host copia nomes e payloads. Nomes não são caminhos C++ nem tipos do jogo;
 * o coremod define sua semântica. read_entry aceita uma primeira chamada com
 * buffers NULL e capacidades zero para consultar os tamanhos. Os bytes do nome
 * não incluem terminador NUL. Quem cria um espaço deve destruí-lo no shutdown. */
typedef struct ShipOotRegistryV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* create_space)(const char* name, int32_t first_id, int32_t last_id,
                                                     uint32_t stride, uint64_t* space_handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_space)(const char* name, uint64_t* space_handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* destroy_space)(uint64_t space_handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_entry)(uint64_t space_handle, const char* name, int32_t requested_id,
                                                       const uint8_t* payload, uint32_t payload_size,
                                                       uint64_t* entry_handle, int32_t* assigned_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_entry)(uint64_t entry_handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_entry_by_name)(uint64_t space_handle, const char* name,
                                                           uint64_t* entry_handle, int32_t* id);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_entry_by_id)(uint64_t space_handle, int32_t id, uint64_t* entry_handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* read_entry)(uint64_t entry_handle, char* name_output, uint32_t name_capacity,
                                                   uint32_t* name_size, uint8_t* payload_output,
                                                   uint32_t payload_capacity, uint32_t* payload_size, int32_t* id);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_entries)(uint64_t space_handle, ShipOotRegistryEntryFn callback,
                                                     void* user);
} ShipOotRegistryV1;

#endif
