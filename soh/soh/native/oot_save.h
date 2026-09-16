#ifndef LINKSPAN_OOT_SAVE_H
#define LINKSPAN_OOT_SAVE_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_SAVE_SERVICE "linkspan.oot.save"
#define LINKSPAN_OOT_SAVE_VERSION 1u
#define LINKSPAN_OOT_SAVE_MAX_NAME 128u
#define LINKSPAN_OOT_SAVE_MAX_BYTES 1048576u

/* Blocos JSON por namespace, gravados na seção "linkspan" do arquivo de save.
 *
 * Só na thread do jogo. Os dados pertencem ao save carregado: começar um jogo
 * novo ou carregar outro arquivo troca o conteúdo. Uma escrita vale na memória e
 * chega ao disco no próximo save do jogo. Blocos de namespaces que nenhum mod
 * abriu nesta sessão são preservados no arquivo.
 *
 * open_namespace: nome com ponto ("autor.mod"), só [A-Za-z0-9._-], até 128
 *   bytes; o prefixo "linkspan." é do host. `version` é a versão do schema que o
 *   mod escreve. Abrir o mesmo nome de novo com a mesma versão devolve o mesmo
 *   handle; versão diferente = INVALID_ARGUMENT.
 * read: JSON compacto. Sem bloco = UNSUPPORTED com output_size 0. Aceita
 *   output NULL com capacity 0 para consultar o tamanho.
 * write: qualquer valor JSON válido de até LINKSPAN_OOT_SAVE_MAX_BYTES; grava
 *   stored_version = versão do handle.
 * get_stored_version: versão com que o bloco foi escrito (0 sem bloco). Use no
 *   hook oot.save.loaded para migrar blocos antigos.
 * set_required: o arquivo passa a registrar que depende do namespace; carregar
 *   o save sem mod que o abra gera aviso no log.
 * begin/commit/rollback: transação de um namespace. Durante a transação o save do
 *   jogo grava o estado anterior ao begin.
 * get_slot: arquivo carregado (0 a 2) ou -1. */
typedef struct ShipOotSaveV1 {
    uint32_t size;
    ShipNativeStatus (SHIP_NATIVE_CALL *open_namespace)(const char* name, uint32_t version, uint64_t* handle);
    ShipNativeStatus (SHIP_NATIVE_CALL *read)(uint64_t handle, char* output, uint32_t capacity,
                                              uint32_t* output_size);
    ShipNativeStatus (SHIP_NATIVE_CALL *write)(uint64_t handle, const char* json, uint32_t length);
    ShipNativeStatus (SHIP_NATIVE_CALL *erase)(uint64_t handle);
    ShipNativeStatus (SHIP_NATIVE_CALL *get_stored_version)(uint64_t handle, uint32_t* version);
    ShipNativeStatus (SHIP_NATIVE_CALL *set_required)(uint64_t handle, uint8_t required);
    ShipNativeStatus (SHIP_NATIVE_CALL *begin)(uint64_t handle);
    ShipNativeStatus (SHIP_NATIVE_CALL *commit)(uint64_t handle);
    ShipNativeStatus (SHIP_NATIVE_CALL *rollback)(uint64_t handle);
    int32_t (SHIP_NATIVE_CALL *get_slot)(void);
} ShipOotSaveV1;

#endif
