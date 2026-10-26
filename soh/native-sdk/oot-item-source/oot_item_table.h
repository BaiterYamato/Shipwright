#ifndef OOT_ITEM_TABLE_H
#define OOT_ITEM_TABLE_H
/* Itens ativos do OoT para linkspan.attributes (RFC 0030, anexo A), a partir de um retrato do save. */
#include <stdint.h>

#include "linkspan/attributes/linkspan_attributes.h"

typedef struct OotItemSnapshot {
    uint16_t equips_equipment;    /* SaveContext.equips.equipment: um nibble por tipo, valor 1..3 */
    uint16_t inventory_equipment; /* SaveContext.inventory.equipment: um bit por peça possuída */
    uint32_t upgrades;            /* SaveContext.inventory.upgrades */
    uint8_t items[24];            /* SaveContext.inventory.items; 0xFF = vazio */
    uint8_t button_items[8];      /* SaveContext.equips.buttonItems: [0] = B, [1..7] = C e D-pad */
    uint8_t current_mask;         /* Player.currentMask (PLAYER_MASK_*); 0 sem jogador */
} OotItemSnapshot;

/* Id do item de inventário ou de botão (ITEM_* 0x00..0x3A), ou NULL fora da tabela. */
const char* OotItemSource_ItemId(uint8_t item);
/* Emite os itens ativos. Para no primeiro status não OK e o devolve. */
ShipNativeStatus OotItemSource_Emit(const OotItemSnapshot* snapshot, LinkSpanAttrEmitFn emit, void* sink);

#endif
