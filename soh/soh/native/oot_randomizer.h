#ifndef LINKSPAN_OOT_RANDOMIZER_H
#define LINKSPAN_OOT_RANDOMIZER_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_RANDOMIZER_SERVICE "linkspan.oot.randomizer"
#define LINKSPAN_OOT_RANDOMIZER_VERSION 1u
/* Itens de mod distintos que cabem numa seed (faixa RG_LINKSPAN_ITEM_* do randomizer). */
#define LINKSPAN_OOT_RANDOMIZER_MAX_ITEMS 64u
/* Cópias de um item no pool. */
#define LINKSPAN_OOT_RANDOMIZER_MAX_COPIES 4u

/* Item do linkspan.oot.items oferecido ao randomizer. O item precisa de get-item (set_get_item): é por ele que a
 * check entrega. `copies` vai de 1 a LINKSPAN_OOT_RANDOMIZER_MAX_COPIES. O item entra na seed pelo nome do
 * registro (id namespaced), que o spoiler e o arquivo guardam: numa seed carregada sem o mod, a check dá uma
 * rupia azul e o host registra o nome no log. */
typedef struct ShipOotRandomizerItemSpecV1 {
    uint32_t size;
    uint8_t item;
    uint8_t copies;
    uint8_t reserved[2];
} ShipOotRandomizerItemSpecV1;

/* Randomizer do SoH, na thread do jogo.
 *
 * add_item: oferece o item às seeds geradas depois com a opção "Link-Span Mod Items" ligada. Os itens entram no
 *   pool no lugar de junk, sem peso de lógica. Chamar de novo troca `copies`. LIMIT com a faixa cheia.
 * remove_item: retira a oferta (seeds já geradas não mudam); FAILURE se não havia.
 * seed_item_count: quantos itens de mod a seed do arquivo carregado tem (0 fora do randomizer).
 * seed_item_name: nome do registro do índice `index` da seed carregada; INVALID_ARGUMENT fora da faixa, LIMIT se
 *   o buffer não cabe o nome com o terminador. */
typedef struct ShipOotRandomizerV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* add_item)(const ShipOotRandomizerItemSpecV1* spec);
    ShipNativeStatus(SHIP_NATIVE_CALL* remove_item)(uint8_t item);
    uint32_t(SHIP_NATIVE_CALL* seed_item_count)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* seed_item_name)(uint32_t index, char* name, uint32_t capacity);
} ShipOotRandomizerV1;

#endif
