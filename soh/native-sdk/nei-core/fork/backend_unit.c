// Rotinas do fork ativadas pelos controles próprios do NEI. Saves vanilla continuam com suas garrafas.
#include "backend_unit.h"
#include "mods/extended_equipment.h"
#include "mods/items/custom_bottles.h"
#include "mods/nei_save.h"
#include <stdio.h>
#include <string.h>

static const char* kEquipmentNames[4][3] = {
    { "Cane of Byrna", "Four Sword", "Trident" },
    { "Goddess Shield", "Kite Shield", "Shield of Ikana" },
    { "Champion's Tunic", "Magic Tunic", "Sage's Tunic" },
    { "Pegasus Boots", "Climb Boots", "Roc's Boots" }
};
uint32_t NeiEquipment_Catalog(char* out, uint32_t capacity) {
    uint32_t used = 0;
    for (unsigned type = 0; type < 4; ++type) for (unsigned index = 1; index <= 3; ++index) {
        if ((type == 0 && index == 1) || ExtEquip_SlotRetired((s16)type, (u8)index)) continue;
        const char* icon = (const char*)ExtEquip_GetIcon((s16)type, (u8)index);
        if (icon && !strncmp(icon, "__OTR__", 7)) icon += 7;
        int n = snprintf(out + used, capacity - used, "%u.%u\t%s\t%s\t%u\n", type, index,
            kEquipmentNames[type][index-1], icon ? icon : "", ExtEquip_HasItem((s16)type, (u8)index) ? 1 : 0);
        if (n < 0 || (uint32_t)n >= capacity - used) break;
        used += (uint32_t)n;
    }
    return used;
}
int NeiEquipment_Toggle(unsigned type, unsigned index) {
    if (type >= 4 || index < 1 || index > 3 || (type==0 && index==1) || ExtEquip_SlotRetired((s16)type, (u8)index)) return 0;
    if (ExtEquip_HasItem((s16)type, (u8)index)) ExtEquip_RemoveItem((s16)type, (u8)index);
    else {
        CVarSetInteger(CVAR_EXT_EQUIP_ENABLED, 1);
        ExtEquip_GiveItem((s16)type, (u8)index);
        ExtEquip_Init();
        if (ExtEquip_CheckAgeReq((s16)type, (u8)index)) ExtEquip_SetSlot((s16)type, (u8)index);
        CVarSave();
    }
    return 1;
}

// Player_Update remains in the host. Restore the equipment dispatch that the
// donor called after CustomItems_Update, including cooldowns and cleanup.
void NeiEquipment_Tick(void* player, void* play) {
    ExtEquip_Update();
    ExtEquip_UpdateBehavior(player, play);
}
int NeiEquipment_UsesShieldCombo(void) {
    return ExtEquip_IsEnabled() &&
        (gExtEquipState.currentExtSword == 2 || gExtEquipState.currentExtShield == 2);
}

u8 NeiBottles_WheelsEnabled(void) {
    for (u8 i=0;i<8;++i) if (Bottle_GetSlot(i)!=BOTTLE_SLOT_EMPTY) return 1;
    return 0;
}
static u8 IsBottle(u8 item) { return item >= ITEM_BOTTLE && item <= ITEM_POE; }
static void SyncSlot(PlayState* play, u8 slot, u8 item) {
    gSaveContext.inventory.items[slot] = item;
    for (u8 button=1;button<=3;++button) {
        if (gSaveContext.equips.cButtonSlots[button-1]==slot && gSaveContext.equips.buttonItems[button]!=item) {
            gSaveContext.equips.buttonItems[button]=item;
            if (play) Interface_LoadItemIcon1(play, button);
        }
    }
}
static u8 MigrateBottle(u8 item, u8 wheel) {
    for (u8 k=0;k<4;++k) {
        u8 i=(u8)(wheel*4+k);
        if (Bottle_GetSlot(i)==BOTTLE_SLOT_EMPTY) { Bottle_SetSlot(i,item); return 1; }
    }
    return 0;
}
void NeiBottles_Project(void* state) {
    PlayState* play=(PlayState*)state;
    u8 wheel,item;
    if (Bottle_ConsumeCatchSync(&wheel,&item)) SyncSlot(play, wheel?SLOT_BOTTLE_2:SLOT_BOTTLE_1,item);
    if (NeiBottles_WheelsEnabled() && (!play || play->pauseCtx.state==0)) for (u8 w=0;w<2;++w) {
        u8 slot=w?SLOT_BOTTLE_2:SLOT_BOTTLE_1;
        u8 current=gSaveContext.inventory.items[slot];
        Bottle_WheelPersist(w,current);
        u8 unmanaged=IsBottle(current) && !Bottle_WheelContains(w,current);
        if (unmanaged && MigrateBottle(current,w)) unmanaged=0;
        u16 first=Bottle_WheelFirstItem(w);
        if (!unmanaged && first!=BOTTLE_SLOT_EMPTY && !Bottle_WheelContains(w,current)) {
            current=(u8)first; SyncSlot(play,slot,current);
        }
        Bottle_WheelRecordActive(w,current);
    }
    if (Bottle_BottomlessOwned()) {
        u8 current=gSaveContext.inventory.items[SLOT_BOTTLE_4];
        if (IsBottle(current) && current!=ITEM_BOTTLE && current!=Bottle_BottomlessContent()) Bottle_BottomlessFill(current);
        SyncSlot(play,SLOT_BOTTLE_4,Bottle_BottomlessIsEmpty()?ITEM_BOTTLE:Bottle_BottomlessContent());
    }
}
u8 NeiBottles_UpdateItem(u8 item, u8 button) {
    if (button<1 || button>3 || gSaveContext.equips.cButtonSlots[button-1]!=SLOT_BOTTLE_4 || !Bottle_BottomlessOwned()) return item;
    if (item==ITEM_BOTTLE) {
        if (Bottle_BottomlessConsume()>0) return Bottle_BottomlessContent();
    } else Bottle_BottomlessFill(item);
    return item;
}
int NeiBottles_Add(void) {
    if (!NeiBottles_WheelsEnabled()) {
        for (u8 w=0;w<2;++w) {
            u8 item=gSaveContext.inventory.items[w?SLOT_BOTTLE_2:SLOT_BOTTLE_1];
            if (IsBottle(item)) Bottle_SetSlot((u8)(w*4),item);
        }
        Bottle_WheelResetTracking();
    }
    if (!Bottle_GiveBottle(ITEM_BOTTLE)) return 0;
    NeiBottles_Project(gPlayState); return 1;
}
int NeiBottles_ToggleBottomless(void) {
    if (Bottle_BottomlessOwned()) { Bottle_SetBottomlessOwned(0); return 1; }
    u8 current=gSaveContext.inventory.items[SLOT_BOTTLE_4];
    if (current!=ITEM_NONE && !IsBottle(current)) return 0;
    Bottle_SetBottomlessOwned(1);
    if (current!=ITEM_NONE && current!=ITEM_BOTTLE) Bottle_BottomlessFill(current);
    else Bottle_BottomlessEmpty();
    NeiBottles_Project(gPlayState); return 1;
}
uint32_t NeiBottles_Status(char* out, uint32_t capacity) {
    int n=snprintf(out,capacity,"Bottle wheels: %u / 8. Bottomless Bottle: %s, %u uses remaining.",
        Bottle_WheelBottleCount(0)+Bottle_WheelBottleCount(1), Bottle_BottomlessOwned()?"enabled":"disabled",
        Bottle_BottomlessCount());
    return n>0 && (uint32_t)n<capacity?(uint32_t)n:0;
}
