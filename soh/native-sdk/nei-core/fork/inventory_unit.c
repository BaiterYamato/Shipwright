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
#include "progression.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include <stdio.h>

unsigned NeiSensor_GetWish(unsigned slot) {
    if (slot >= 5) return 0;
    char key[32]; snprintf(key, sizeof(key), "gNei.SensorDesire%u", slot);
    const int item = CVarGetInteger(key, 0);
    return item > 0 ? (unsigned)item : 0;
}
void NeiSensor_SetWish(unsigned slot, unsigned item) {
    if (slot >= 5) return;
    char key[32]; snprintf(key, sizeof(key), "gNei.SensorDesire%u", slot);
    CVarSetInteger(key, item);
    CVarSave();
}

/* O macro vanilla CHECK_AGE_REQ_SLOT consulta gSlotAgeReqs, que tem só 24 entradas.
 * A página do NEI usa slots 24..71 e precisa da regra do inventário estendido. */
u8 NeiInv_CheckAgeReqSlot(u8 slot) {
    const u8 required = ExtInv_GetSlotAgeReq(slot);
    const bool allowed = required == AGE_REQ_NONE || required == gSaveContext.linkAge;
    return GameInteractor_Should(VB_SLOT_MEETS_AGE_REQ, allowed, slot);
}

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

static uint8_t LogicalSlot(uint16_t logicalId) {
    switch (logicalId) {
        case EXT_ITEM_SHEIKAH_SLATE: return SLOT_SHEIKAH_SLATE;
        case EXT_ITEM_PHANTOM_HOURGLASS: return SLOT_PHANTOM_HOURGLASS;
        case EXT_ITEM_ROD_OF_SEASONS: return SLOT_ROD_OF_SEASONS;
        default: return ExtInv_GetItemSlot((uint8_t)logicalId);
    }
}
void NeiInv_PlaceItem(uint16_t logicalId) {
    const uint8_t slot = LogicalSlot(logicalId);
    if (slot == 0xFF || slot < 24) {
        return;
    }
    if (logicalId == EXT_ITEM_SHEIKAH_SLATE && !Nei_Save()->slateRunesOwned) Slate_GrantRune(SLATE_RUNE_BOMB);
    if (logicalId == EXT_ITEM_ROD_OF_SEASONS && !Nei_Save()->seasonsOwned) Seasons_GrantSeason(SEASON_SPRING);
    if (logicalId == ITEM_ELEMENTAL_WAND && !Nei_Save()->wandRodsOwned) Wand_GrantMode(WAND_MODE_SAND);
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
void NeiInv_ReceiveItem(uint16_t logicalId) {
    if (logicalId == ITEM_CANE_OF_SOMARIA) {
        static const u8 kCaneOrder[6] = { 0, 3, 1, 4, 2, 5 };
        for (int i = 0; i < 6; i++) {
            if (Cane_GiveSkill(kCaneOrder[i])) {
                break;
            }
        }
        return;
    }
    if (logicalId == EXT_ITEM_SHEIKAH_SLATE || logicalId == EXT_ITEM_ROD_OF_SEASONS ||
        logicalId == ITEM_ELEMENTAL_WAND) {
        const unsigned family = logicalId == ITEM_ELEMENTAL_WAND ? NEI_POWER_WAND
                                : logicalId == EXT_ITEM_SHEIKAH_SLATE ? NEI_POWER_SLATE : NEI_POWER_SEASONS;
        const uint8_t mask = NeiProgress_GetMask(family);
        for (unsigned i = 0; i < NeiProgress_Limit(family); ++i) {
            if (!(mask & (1u << i))) { NeiProgress_SetMask(family, mask | (1u << i)); break; }
        }
    }
    NeiInv_PlaceItem(logicalId);
}

void NeiInv_RemoveItem(uint16_t logicalId) {
    /* A Cane guarda a posse em bits além da célula. Zerar esses bits permite concedê-la de novo depois
     * da desativação; a seleção da habilidade deixa de ter efeito enquanto a máscara estiver vazia. */
    if (logicalId == ITEM_CANE_OF_SOMARIA) {
        Nei_Save()->caneSkills = 0;
    }
    if (logicalId == ITEM_ELEMENTAL_WAND) Nei_Save()->wandRodsOwned = 0;
    if (logicalId == EXT_ITEM_SHEIKAH_SLATE) Nei_Save()->slateRunesOwned = 0;
    if (logicalId == EXT_ITEM_ROD_OF_SEASONS) { Nei_Save()->seasonsOwned = 0; Nei_Save()->season = SEASON_OFF; }
    const uint8_t slot = LogicalSlot(logicalId);
    if (slot != 0xFF && slot >= 24 && ExtInv_GetSlotItem(slot) == logicalId) {
        ExtInv_SetSlotItem(slot, ITEM_NONE);
    }
}

int NeiInv_HasItem(uint16_t logicalId) {
    const uint8_t slot = LogicalSlot(logicalId);
    return slot != 0xFF && slot >= 24 && ExtInv_GetSlotItem(slot) == logicalId;
}

const char* NeiInv_CurrentIcon(uint16_t logicalId) {
    const char* path = (const char*)ExtInv_GetInventoryItemIcon(logicalId);
    if (path && !strncmp(path, "__OTR__", 7)) path += 7;
    return path;
}

void NeiInv_ReadState(int32_t* pages, int32_t* currentPage, uint32_t* occupied) {
    *pages = ExtInv_GetMaxPages();
    *currentPage = ExtInv_GetCurrentPage();
    *occupied = NeiInv_OccupiedSlots();
}
