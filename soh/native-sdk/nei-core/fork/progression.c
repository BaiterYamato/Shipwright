#include <stdio.h>
#include <string.h>
#include "mods/extended_inventory.h"
#include "mods/nei_save.h"
#include "progression.h"

uint8_t NeiProgress_GetMask(unsigned family) {
    switch (family) {
        case NEI_POWER_WAND: return Nei_Save()->wandRodsOwned;
        case NEI_POWER_SLATE: return Nei_Save()->slateRunesOwned;
        case NEI_POWER_SEASONS: return Nei_Save()->seasonsOwned;
        case NEI_POWER_CANE: return Nei_Save()->caneSkills;
        default: return 0;
    }
}
void NeiProgress_SetMask(unsigned family, uint8_t mask) {
    const uint8_t supported = NeiProgress_ValidMask(family);
    // Keep flags belonging to unavailable/future abilities in imported saves.
    mask = (NeiProgress_GetMask(family) & ~supported) | (mask & supported);
    switch (family) {
        case NEI_POWER_WAND:
            Nei_Save()->wandRodsOwned = mask;
            if (mask) { ExtInv_SetSlotItem(SLOT_ELEMENTAL_WAND, ITEM_ELEMENTAL_WAND); Wand_SetMode(Wand_GetMode()); }
            break;
        case NEI_POWER_SLATE:
            Nei_Save()->slateRunesOwned = mask;
            if (mask) { ExtInv_SetSlotItem(SLOT_SHEIKAH_SLATE, EXT_ITEM_SHEIKAH_SLATE); Slate_SetRune(Slate_GetRune()); }
            break;
        case NEI_POWER_SEASONS:
            Nei_Save()->seasonsOwned = mask;
            if (mask) { ExtInv_SetSlotItem(SLOT_ROD_OF_SEASONS, EXT_ITEM_ROD_OF_SEASONS); Seasons_SetSeason(Seasons_GetSeason()); }
            else Nei_Save()->season = SEASON_OFF;
            break;
        case NEI_POWER_CANE:
            Nei_Save()->caneSkills = mask;
            if (mask) { ExtInv_SetSlotItem(SLOT_CANE_OF_SOMARIA, ITEM_CANE_OF_SOMARIA); Nei_CaneSetType(Nei_CaneGetType()); }
            break;
    }
}
const char* NeiProgress_ItemId(unsigned family) {
    static const char* ids[] = { "skijer.nei.elemental_wand", "skijer.nei.sheikah_slate",
                                "skijer.nei.rod_of_seasons", "skijer.nei.cane_of_somaria" };
    return family < NEI_POWER_COUNT ? ids[family] : NULL;
}
uint32_t NeiProgress_Catalog(char* output, uint32_t capacity) {
    static const char* names[NEI_POWER_COUNT][6] = {
        { "Sand Rod", "Tornado Rod", "Water Rod", "Meteor Rod", "Storm Rod", "Shadow Scepter" },
        { "Remote Bomb", "Stasis", "Cryonis", "Master Cycle Zero", "Sheikah Sensor" },
        { "Spring", "Summer", "Autumn", "Winter" },
        { "Somaria: Statue", "Somaria: Block", "Trirod: Echoes", "Pacci: Flip", "Pacci: Stone", "Ultrahand" },
    };
    uint32_t used = 0;
    if (!output || !capacity) return 0;
    output[0] = 0;
    for (unsigned family = 0; family < NEI_POWER_COUNT; ++family) {
        for (unsigned i = 0; i < NeiProgress_Limit(family); ++i) {
            const char* icon = family == NEI_POWER_WAND ? (const char*)Wand_ModeIcon(i)
                               : family == NEI_POWER_SLATE ? (const char*)Slate_RuneIcon(i)
                               : family == NEI_POWER_SEASONS ? (const char*)Seasons_SeasonIcon(i)
                               : (const char*)ExtInv_GetInventoryItemIcon(ITEM_CANE_OF_SOMARIA);
            if (!icon) icon = "";
            if (!strncmp(icon, "__OTR__", 7)) icon += 7;
            int n = snprintf(output + used, capacity - used, "%u.%u\t%s\t%s\t%s\t%u\n", family, i,
                             NeiProgress_ItemId(family), names[family][i], icon,
                             !!(NeiProgress_GetMask(family) & (1u << i)));
            if (n < 0 || (uint32_t)n >= capacity - used) { output[used] = 0; return used; }
            used += (uint32_t)n;
        }
    }
    return used;
}
