/* Inventário estendido do fork NEI (NEI-003) como unidade de compilação da DLL.
 *
 * O extended_inventory.c é quem sabe quantas páginas existem, qual slot visual corresponde a qual
 * slot real (0..23 vanilla, 24..71 no gNeiSave), qual ícone e qual nome cada item mostra e quais
 * slots estão bloqueados por idade ou por forma. O kaleido extraído (extracted/z_kaleido_item.c)
 * chama tudo isso; antes do NEI-003 essas funções eram stub de "recurso ausente".
 *
 * Fica numa unidade própria, e não colado no player_unit.c, porque o arquivo do fork já é uma TU
 * completa com os próprios includes — o z_player.c do fork nunca o inclui. */
#include "mods/extended_inventory.c"

/* Slots ocupados fora da página vanilla, isto é, de 24 até o fim das páginas do NEI. */
uint32_t NeiInv_OccupiedSlots(void) {
    uint32_t total = 0;
    const int fim = ExtInv_GetMaxPages() * 24;
    for (int slot = 24; slot < fim; ++slot) {
        if (ExtInv_GetSlotItem(slot) != ITEM_NONE) {
            ++total;
        }
    }
    return total;
}

/* Posse na página do NEI, pelo mesmo caminho que o Item_Give do fork usa (ExtInv_SetItemById). A célula do Roc's
 * Feather é progressiva: o Cape a toma, e o Feather dado depois do Cape não volta para ela. Item sem célula
 * própria (0xFF) não tem o que pôr aqui. */
u8 Cane_GiveSkill(u8 skill); /* item_cane_of_somaria.c, no player_unit.c */

void NeiInv_PlaceItem(uint8_t logicalId) {
    const uint8_t slot = ExtInv_GetItemSlot(logicalId);
    if (slot == 0xFF || slot < 24) {
        return;
    }
    /* A Cane é seis skills numa célula só, e a posse é o bit da skill: o Cane_GiveSkill da primeira é que põe a
     * Cane na célula. Sem skill, a Cane na célula não lança nada. */
    if (logicalId == ITEM_CANE_OF_SOMARIA) {
        if (!Nei_CaneOwned()) {
            Cane_GiveSkill(0);
        }
        return;
    }
    const uint16_t current = ExtInv_GetSlotItem(slot);
    if (current == logicalId || (current == ITEM_ROCS_CAPE && logicalId == ITEM_ROCS_FEATHER_SKIJER)) {
        return;
    }
    ExtInv_SetSlotItem(slot, logicalId);
}

/* Cada get-item do registro (callback `received`). É o RG_CANE_OF_SOMARIA do randomizer do fork: cada cópia
 * recebida acende a próxima skill, alternando as duas canes (Statue, Flip, Block, Stone, Platform, Ultrahand). */
void NeiInv_ReceiveItem(uint8_t logicalId) {
    if (logicalId == ITEM_CANE_OF_SOMARIA) {
        static const u8 kCaneOrder[6] = { 0, 3, 1, 4, 2, 5 };
        for (int i = 0; i < 6; i++) {
            if (Cane_GiveSkill(kCaneOrder[i])) {
                break;
            }
        }
        return;
    }
    NeiInv_PlaceItem(logicalId);
}

void NeiInv_RemoveItem(uint8_t logicalId) {
    const uint8_t slot = ExtInv_GetItemSlot(logicalId);
    if (slot != 0xFF && slot >= 24 && ExtInv_GetSlotItem(slot) == logicalId) {
        ExtInv_SetSlotItem(slot, ITEM_NONE);
    }
}

int NeiInv_HasItem(uint8_t logicalId) {
    const uint8_t slot = ExtInv_GetItemSlot(logicalId);
    return slot != 0xFF && slot >= 24 && ExtInv_GetSlotItem(slot) == logicalId;
}

void NeiInv_ReadState(int32_t* pages, int32_t* currentPage, uint32_t* occupied) {
    *pages = ExtInv_GetMaxPages();
    *currentPage = ExtInv_GetCurrentPage();
    *occupied = NeiInv_OccupiedSlots();
}
