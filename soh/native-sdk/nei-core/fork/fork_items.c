// Itens do fork NEI que a DLL liga ao host. O registro linkspan.nei.items guarda cada um com um id namespaced
// e o linkspan.oot.items aloca o id runtime que fica no botão C; o código do fork compara botões com o id lógico
// dele. NeiFork_ToLogicalItem e NeiFork_ToRuntimeItem fazem a ponte (patch 0002 e extracted-fixes.txt).
//
// A descrição de cada item (slot, idade, ícone, texto do get-item) sai da linha dele em sNeiItems[], que é a
// fonte única do fork; o modelo do get-item sai de gNeiItemModels[], gerado do draw.cpp do fork.
#include <string.h>

#include "fork_items.h"
#include "fork_models.h"
#include "item_mapping.h"
#include "oot_items.h"
#include "mods/extended_inventory.h"
#include "mods/extended_equipment.h"

static NeiItemMapping sMapping;

void NeiFork_MapItem(u8 runtimeId, u16 logicalId) {
    NeiMapping_Set(&sMapping, runtimeId, logicalId);
}

void NeiFork_ClearItems(void) {
    NeiMapping_Clear(&sMapping);
}

// Os ids de outros mods podem coincidir com ids lógicos do fork. Na leitura interna
// do fork, não os trate como itens NEI; Player_ProcessItemButtons os entrega ao host.
u8 NeiFork_ToLogicalItem(u8 runtimeId) {
    const u16 logical = NeiMapping_Logical(&sMapping, runtimeId);
    if (logical != 0) {
        return logical > 0xFF ? ITEM_EXT_BUTTON : (u8)logical;
    }
    if (runtimeId >= LINKSPAN_OOT_ITEMS_FIRST_ID && runtimeId <= LINKSPAN_OOT_ITEMS_LAST_ID) {
        return ITEM_NONE;
    }
    return runtimeId;
}

u16 NeiFork_ExtendedItem(u8 runtimeId) {
    const u16 logical = NeiMapping_Logical(&sMapping, runtimeId);
    return logical > 0xFF ? logical : ITEM_NONE;
}

// Espadas do equipamento usam IDs lógicos próprios no botão B. Os botões C continuam
// usando somente IDs runtime, inclusive quando outro mod ocupa a mesma faixa numérica.
static u8 IsEquippedSword(s32 item) {
    const u8 sword = ExtEquip_GetCurrent(EQUIP_TYPE_SWORD);
    return sword >= 1 && sword <= 3 && ExtEquip_IsEnabled() &&
           ExtEquip_HasItem(EQUIP_TYPE_SWORD, sword) && item == ExtEquip_GetItemId(EQUIP_TYPE_SWORD,sword);
}
u8 NeiFork_BButtonItem(u8 storedId) { return IsEquippedSword(storedId) ? storedId : NeiFork_ToLogicalItem(storedId); }

// Chamado antes de Player_GetItemOnButton converter o id para o mundo do fork.
// O host valida registro e idade no hook VB_CHANGE_HELD_ITEM_AND_USE_ITEM.
u8 NeiFork_IsForeignRuntimeButton(s32 button) {
    if (button < 1 || button > 3) {
        return 0;
    }
    const u8 runtimeId = gSaveContext.equips.buttonItems[button];
    return runtimeId >= LINKSPAN_OOT_ITEMS_FIRST_ID && runtimeId <= LINKSPAN_OOT_ITEMS_LAST_ID &&
           NeiMapping_Logical(&sMapping, runtimeId) == 0;
}

// Id lógico de um item do fork registrado nesta sessão. O z_player da DLL lê o botão já traduzido (player_unit.c) e
// usa isto para tirar o item do fork dos hooks do linkspan.oot.items, que tratam a faixa 0xA0-0xEF como item de mod:
// o host bloqueia o uso vanilla (VB_CHANGE_HELD_ITEM_AND_USE_ITEM) e zera a ação (VB_ITEM_ACTION_BE_NONE), e o fork
// precisa do Player_UseItem dele para montar a ação do item (PLAYER_IA_ROD_FIRE golpeia com B).
u8 NeiFork_IsForkItem(s32 item) {
    if (IsEquippedSword(item)) return 1;
    if (item == ITEM_EXT_BUTTON) {
        for (u32 i = 0; i < 256; ++i) if (sMapping.logical[i] > 0xFF) return 1;
    }
    return item >= 0 && item <= 0xFFFF &&
           NeiMapping_Runtime(&sMapping, (u16)item, ITEM_NONE) != ITEM_NONE;
}

// O id de outro mod pode coincidir numericamente com um id lógico do NEI. O botão
// guarda o id runtime original, então compare os dois antes de pular o hook do host.
u8 NeiFork_IsForkButtonItem(s32 item, s32 button) {
    if (button < 0 || button >= 4) return 0;
    if (button == 0 && IsEquippedSword(item) && gSaveContext.equips.buttonItems[0] == item) return 1;
    const u8 runtime = gSaveContext.equips.buttonItems[button];
    const u16 logical = NeiMapping_Logical(&sMapping, runtime);
    return logical != 0 && (logical == item || (logical > 0xFF && item == ITEM_EXT_BUTTON));
}

// O contrário, para o kaleido do fork gravar no botão C o id que o host entende. Item vanilla passa direto; item
// do fork sem registro (não definido nesta sessão) vira ITEM_NONE em vez de um id que o host não conhece.
u8 NeiFork_ToRuntimeItem(u16 logicalId) {
    const u8 runtime = NeiMapping_Runtime(&sMapping, logicalId, ITEM_NONE);
    if (runtime != ITEM_NONE) {
        return runtime;
    }
    if (logicalId > 0xFF || Nei_FindByItem(logicalId) != NULL) {
        return ITEM_NONE;
    }
    return logicalId;
}

// Itens que não são célula da página do NEI mas entram no registro.
static const u16 sExtraItems[] = { ITEM_ROCS_CAPE, EXT_ITEM_SHEIKAH_SLATE,
                                  EXT_ITEM_PHANTOM_HOURGLASS, EXT_ITEM_ROD_OF_SEASONS };

u32 NeiFork_ListItems(u16* out, u32 capacity) {
    u32 count = 0;
    for (u32 i = 0; i < ARRAY_COUNT(gPage2Items) && count < capacity; i++) {
        if (gPage2Items[i] != ITEM_NONE) {
            out[count++] = gPage2Items[i];
        }
    }
    for (u32 i = 0; i < ARRAY_COUNT(sExtraItems) && count < capacity; i++) {
        out[count++] = sExtraItems[i];
    }
    return count;
}

static const char* StripOtr(const void* path) {
    const char* text = (const char*)path;
    if (text == NULL) {
        return NULL;
    }
    return strncmp(text, "__OTR__", 7) == 0 ? text + 7 : text;
}

static const char* ComponentOf(u16 item) {
    switch (item) {
        case ITEM_POKEBALL:
            return "expansion.ssbb";
        case ITEM_MARIO_MASK:
            return "expansion.sm64";
        default:
            return "core";
    }
}

int NeiFork_DescribeItem(u16 logicalId, NeiForkItemInfo* info) {
    if (info == NULL) return 0;
    if (logicalId == EXT_ITEM_SHEIKAH_SLATE || logicalId == EXT_ITEM_PHANTOM_HOURGLASS ||
        logicalId == EXT_ITEM_ROD_OF_SEASONS) {
        memset(info, 0, sizeof(*info));
        info->logicalId = logicalId;
        info->age = LINKSPAN_OOT_ITEM_AGE_ANY;
        info->component = "core";
        info->icon = StripOtr(ExtInv_GetInventoryItemIcon(logicalId));
        info->modelScale = 0.015f;
        if (logicalId == EXT_ITEM_SHEIKAH_SLATE) {
            info->slot = SLOT_SHEIKAH_SLATE;
            info->message = "You got the %bSheikah Slate%w!";
            info->modelPath = "objects/object_nei_sheikah_slate/gNeiSheikahSlateDL";
        } else if (logicalId == EXT_ITEM_ROD_OF_SEASONS) {
            info->slot = SLOT_ROD_OF_SEASONS;
            info->message = "You got the %gRod of Seasons%w!";
            info->modelPath = "objects/object_nei_rod_of_seasons/gNeiRodOfSeasonsDL";
        } else {
            info->slot = SLOT_PHANTOM_HOURGLASS;
            info->message = "You got the %yPhantom Hourglass%w!";
            info->modelPath = "objects/object_nei_phantom_hourglass/gNeiPhantomHourglassDL";
        }
        return 1;
    }
    const NeiItem* row = Nei_FindByItem(logicalId);
    if (row == NULL) {
        return 0;
    }
    memset(info, 0, sizeof(*info));
    info->logicalId = logicalId;
    info->slot = row->slot;
    info->age = row->ageReq == AGE_REQ_ADULT   ? LINKSPAN_OOT_ITEM_AGE_ADULT
                : row->ageReq == AGE_REQ_CHILD ? LINKSPAN_OOT_ITEM_AGE_CHILD
                                               : LINKSPAN_OOT_ITEM_AGE_ANY;
    // Lantern's row icon is intentionally NULL: its artwork depends on the flame.
    // The host C-button registry still needs a real initial icon.
    info->icon = StripOtr(logicalId == ITEM_LANTERN ? ExtInv_GetInventoryItemIcon(logicalId) : row->iconTex);
    info->message = row->nameEn;
    info->component = ComponentOf(logicalId);
    for (u32 i = 0; i < gNeiItemModelCount; i++) {
        if (gNeiItemModels[i].item == logicalId) {
            info->modelPath = gNeiItemModels[i].path;
            info->modelScale = gNeiItemModels[i].scale;
            info->modelLayer = gNeiItemModels[i].layer;
            break;
        }
    }
    return 1;
}

int NeiFork_NeedsAssets(u16 logicalId) {
    return logicalId != ITEM_ROCS_FEATHER_SKIJER && logicalId != ITEM_ROCS_CAPE;
}

const char* NeiFork_FallbackName(u16 logicalId) {
    return logicalId == ITEM_ROCS_FEATHER_SKIJER ? "Roc's Feather" : NULL;
}
