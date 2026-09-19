// Itens do fork NEI que a DLL já liga ao host, com o id lógico do fork (nei_compat.h) e o id namespaced no
// registro linkspan.nei.items. O código do fork compara botões com o id lógico; o host guarda nos botões o
// id runtime que o linkspan.oot.items alocou. NeiFork_ToLogicalItem faz a ponte (patch 0002 no equip_helper).
#include "fork_items.h"
#include "oot_items.h"

const NeiForkItem gNeiForkItems[] = {
    { ITEM_ROCS_FEATHER_SKIJER, "skijer.nei.rocs_feather", "Roc's Feather" },
    { ITEM_ROCS_CAPE, "skijer.nei.rocs_cape", "Roc's Cape" },
};
const u32 gNeiForkItemCount = ARRAY_COUNT(gNeiForkItems);

static u8 sLogicalByRuntime[256];

void NeiFork_MapItem(u8 runtimeId, u8 logicalId) {
    sLogicalByRuntime[runtimeId] = logicalId;
}

void NeiFork_ClearItems(void) {
    memset(sLogicalByRuntime, 0, sizeof(sLogicalByRuntime));
}

// Um id runtime da faixa de itens de mod que não é deste fork vira ITEM_NONE: a faixa do linkspan.oot.items
// (0xA0-0xEF) cruza os ids lógicos do fork, e o item de outro mod não pode disparar um item do fork.
u8 NeiFork_ToLogicalItem(u8 runtimeId) {
    if (sLogicalByRuntime[runtimeId] != 0) {
        return sLogicalByRuntime[runtimeId];
    }
    if (runtimeId >= LINKSPAN_OOT_ITEMS_FIRST_ID && runtimeId <= LINKSPAN_OOT_ITEMS_LAST_ID) {
        return ITEM_NONE;
    }
    return runtimeId;
}
