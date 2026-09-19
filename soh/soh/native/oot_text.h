#ifndef LINKSPAN_OOT_TEXT_H
#define LINKSPAN_OOT_TEXT_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_TEXT_SERVICE "linkspan.oot.text"
#define LINKSPAN_OOT_TEXT_VERSION 1u
/* Tamanho máximo de uma mensagem, com o terminador. */
#define LINKSPAN_OOT_TEXT_MAX_MESSAGE 8192u
#define LINKSPAN_OOT_TEXT_MAX_ID 0xFFFEu

/* Tabelas de mensagens do jogo, na ordem das pastas de texto do SoH. */
#define LINKSPAN_OOT_TEXT_ENGLISH 0u
#define LINKSPAN_OOT_TEXT_GERMAN 1u
#define LINKSPAN_OOT_TEXT_FRENCH 2u
#define LINKSPAN_OOT_TEXT_JAPANESE 3u
#define LINKSPAN_OOT_TEXT_STAFF 4u
#define LINKSPAN_OOT_TEXT_LANGUAGES 5u

/* Callback síncrono de list_messages; `bytes` pertence ao host e só vale durante a
 * chamada. Outro status que não OK interrompe a enumeração e o propaga. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotMessageFn)(void* user, uint32_t id, uint8_t box_type,
                                                             uint8_t box_pos, const uint8_t* bytes, uint32_t length);

/* Mensagens do jogo, disponível apenas na thread do jogo e fora da exibição de uma
 * caixa de texto (entre frames).
 *
 * set_message: cria ou troca a mensagem `id` (0..LINKSPAN_OOT_TEXT_MAX_ID). `bytes`
 *   são os bytes crus do jogo, códigos de controle incluídos; o terminador 0x02 é
 *   acrescentado se faltar. box_type e box_pos vão de 0 a 15.
 * remove_message: tira o id da tabela (vanilla ou acrescentado); FAILURE se não
 *   existia.
 * clear_language: esvazia a tabela do idioma, para um mod que traz a tabela base
 *   inteira.
 * reset_language: volta a tabela ao estado do boot (recurso vanilla + override/).
 * list_messages: mensagens atuais em ordem de id. */
typedef struct ShipOotTextV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* set_message)(uint32_t language, uint32_t id, uint8_t box_type, uint8_t box_pos,
                                                    const uint8_t* bytes, uint32_t length);
    ShipNativeStatus(SHIP_NATIVE_CALL* remove_message)(uint32_t language, uint32_t id);
    ShipNativeStatus(SHIP_NATIVE_CALL* clear_language)(uint32_t language);
    ShipNativeStatus(SHIP_NATIVE_CALL* reset_language)(uint32_t language);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_messages)(uint32_t language, ShipOotMessageFn callback, void* user);
} ShipOotTextV1;

#endif
