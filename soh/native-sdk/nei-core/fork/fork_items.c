// Itens do fork NEI que a DLL liga ao host. O registro linkspan.nei.items guarda cada um com um id namespaced
// e o linkspan.oot.items aloca o id runtime que fica no botão C; o código do fork compara botões com o id lógico
// dele. NeiFork_ToLogicalItem e NeiFork_ToRuntimeItem fazem a ponte (patch 0002 e extracted-fixes.txt).
//
// A descrição de cada item (slot, idade, ícone, texto do get-item) sai da linha dele em sNeiItems[], que é a
// fonte única do fork; o modelo do get-item sai de gNeiItemModels[], gerado do draw.cpp do fork.
#include <string.h>

#include "fork_items.h"
#include "fork_models.h"
#include "oot_items.h"
#include "mods/extended_inventory.h"

static u8 sLogicalByRuntime[256];
static u8 sRuntimeByLogical[256];

void NeiFork_MapItem(u8 runtimeId, u8 logicalId) {
    sLogicalByRuntime[runtimeId] = logicalId;
    sRuntimeByLogical[logicalId] = runtimeId;
}

void NeiFork_ClearItems(void) {
    memset(sLogicalByRuntime, 0, sizeof(sLogicalByRuntime));
    memset(sRuntimeByLogical, 0, sizeof(sRuntimeByLogical));
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

// Id lógico de um item do fork registrado nesta sessão. O z_player da DLL lê o botão já traduzido (player_unit.c) e
// usa isto para tirar o item do fork dos hooks do linkspan.oot.items, que tratam a faixa 0xA0-0xEF como item de mod:
// o host bloqueia o uso vanilla (VB_CHANGE_HELD_ITEM_AND_USE_ITEM) e zera a ação (VB_ITEM_ACTION_BE_NONE), e o fork
// precisa do Player_UseItem dele para montar a ação do item (PLAYER_IA_ROD_FIRE golpeia com B).
u8 NeiFork_IsForkItem(s32 item) {
    return item >= 0 && item < 256 && sRuntimeByLogical[item] != 0;
}

// O contrário, para o kaleido do fork gravar no botão C o id que o host entende. Item vanilla passa direto; item
// do fork sem registro (não definido nesta sessão) vira ITEM_NONE em vez de um id que o host não conhece.
u8 NeiFork_ToRuntimeItem(u8 logicalId) {
    if (sRuntimeByLogical[logicalId] != 0) {
        return sRuntimeByLogical[logicalId];
    }
    if (Nei_FindByItem(logicalId) != NULL) {
        return ITEM_NONE;
    }
    return logicalId;
}

// Itens que não são célula da página do NEI mas entram no registro.
static const u8 sExtraItems[] = { ITEM_ROCS_CAPE };

u32 NeiFork_ListItems(u8* out, u32 capacity) {
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

static const char* ComponentOf(u8 item) {
    switch (item) {
        case ITEM_POKEBALL:
            return "expansion.ssbb";
        case ITEM_MARIO_MASK:
            return "expansion.sm64";
        default:
            return "core";
    }
}

int NeiFork_DescribeItem(u8 logicalId, NeiForkItemInfo* info) {
    const NeiItem* row = Nei_FindByItem(logicalId);
    if (row == NULL || info == NULL) {
        return 0;
    }
    memset(info, 0, sizeof(*info));
    info->logicalId = logicalId;
    info->slot = row->slot;
    info->age = row->ageReq == AGE_REQ_ADULT   ? LINKSPAN_OOT_ITEM_AGE_ADULT
                : row->ageReq == AGE_REQ_CHILD ? LINKSPAN_OOT_ITEM_AGE_CHILD
                                               : LINKSPAN_OOT_ITEM_AGE_ANY;
    info->icon = StripOtr(row->iconTex);
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

int NeiFork_NeedsAssets(u8 logicalId) {
    return logicalId != ITEM_ROCS_FEATHER_SKIJER && logicalId != ITEM_ROCS_CAPE;
}

const char* NeiFork_FallbackName(u8 logicalId) {
    return logicalId == ITEM_ROCS_FEATHER_SKIJER ? "Roc's Feather" : NULL;
}
