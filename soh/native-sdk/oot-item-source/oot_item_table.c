#include "oot_item_table.h"
#include <stddef.h>

static const char* const kEquipment[4][3] = {
    { "oot.sword.kokiri", "oot.sword.master", "oot.sword.biggoron" },
    { "oot.shield.deku", "oot.shield.hylian", "oot.shield.mirror" },
    { "oot.tunic.kokiri", "oot.tunic.goron", "oot.tunic.zora" },
    { "oot.boots.kokiri", "oot.boots.iron", "oot.boots.hover" },
};

static const struct {
    uint8_t shift;
    uint8_t mask;
    const char* ids[3];
} kUpgrades[] = {
    { 0, 7, { "oot.quiver.30", "oot.quiver.40", "oot.quiver.50" } },
    { 3, 7, { "oot.bomb_bag.20", "oot.bomb_bag.30", "oot.bomb_bag.40" } },
    { 6, 7, { "oot.strength.goron_bracelet", "oot.strength.silver_gauntlets", "oot.strength.golden_gauntlets" } },
    { 9, 7, { "oot.scale.silver", "oot.scale.golden", NULL } },
    { 12, 3, { "oot.wallet.adult", "oot.wallet.giant", "oot.wallet.tycoon" } },
    { 14, 7, { "oot.bullet_bag.30", "oot.bullet_bag.40", "oot.bullet_bag.50" } },
};

/* Índices ITEM_* do host; aliases e lacuna definidos pela T9. */
static const char* const kItems[0x3B] = {
    /* 0x00 */ "oot.item.stick",
    "oot.item.nut",
    "shared.bombs",
    "shared.bow",
    /* 0x04 */ "oot.item.arrow_fire",
    "oot.item.dins_fire",
    "oot.item.slingshot",
    "oot.item.ocarina_fairy",
    /* 0x08 */ "shared.ocarina",
    "shared.bombchu",
    "shared.hookshot",
    "oot.item.longshot",
    /* 0x0C */ "oot.item.arrow_ice",
    "oot.item.farores_wind",
    "oot.item.boomerang",
    "oot.item.lens",
    /* 0x10 */ "oot.item.bean",
    "oot.item.hammer",
    "oot.item.arrow_light",
    "oot.item.nayrus_love",
    /* 0x14 */ "oot.item.bottle",
    "oot.item.potion_red",
    "oot.item.potion_green",
    "oot.item.potion_blue",
    /* 0x18 */ "oot.item.fairy",
    "oot.item.fish",
    "oot.item.milk_bottle",
    "oot.item.letter_ruto",
    /* 0x1C */ "oot.item.blue_fire",
    "oot.item.bug",
    "oot.item.big_poe",
    "oot.item.milk_half",
    /* 0x20 */ "oot.item.poe",
    "oot.item.weird_egg",
    "oot.item.chicken",
    "oot.item.letter_zelda",
    /* 0x24 */ "oot.mask.keaton",
    "oot.mask.skull",
    "oot.mask.spooky",
    "oot.mask.bunny",
    /* 0x28 */ "oot.mask.goron",
    "oot.mask.zora",
    "oot.mask.gerudo",
    "oot.mask.truth",
    /* 0x2C */ NULL,
    "oot.item.pocket_egg",
    "oot.item.pocket_cucco",
    "oot.item.cojiro",
    /* 0x30 */ "oot.item.odd_mushroom",
    "oot.item.odd_potion",
    "oot.item.saw",
    "oot.item.sword_broken",
    /* 0x34 */ "oot.item.prescription",
    "oot.item.frog",
    "oot.item.eyedrops",
    "oot.item.claim_check",
    /* 0x38 */ "shared.bow",
    "shared.bow",
    "shared.bow",
};

const char* OotItemSource_ItemId(uint8_t item) {
    return item < sizeof(kItems) / sizeof(kItems[0]) ? kItems[item] : NULL;
}

ShipNativeStatus OotItemSource_Emit(const OotItemSnapshot* snapshot, LinkSpanAttrEmitFn emit, void* sink) {
    unsigned type, piece, index;
    ShipNativeStatus status;
    const char* id;
    if (snapshot == NULL || emit == NULL)
        return SHIP_NATIVE_INVALID_ARGUMENT;

    for (type = 0; type < 4; ++type) {
        unsigned value = (snapshot->equips_equipment >> (type * 4)) & 15u;
        if (value >= 1 && value <= 3) {
            status = emit(sink, kEquipment[type][value - 1], LINKSPAN_ATTR_WHEN_EQUIPPED);
            if (status != SHIP_NATIVE_OK)
                return status;
        }
    }
    for (type = 0; type < 4; ++type) {
        for (piece = 0; piece < 3; ++piece) {
            if ((snapshot->inventory_equipment & (1u << (type * 4 + piece))) != 0) {
                status = emit(sink, kEquipment[type][piece], LINKSPAN_ATTR_WHEN_OWNED);
                if (status != SHIP_NATIVE_OK)
                    return status;
            }
        }
    }
    for (index = 0; index < sizeof(kUpgrades) / sizeof(kUpgrades[0]); ++index) {
        unsigned value = (snapshot->upgrades >> kUpgrades[index].shift) & kUpgrades[index].mask;
        if (value >= 1 && value <= 3 && kUpgrades[index].ids[value - 1] != NULL) {
            status = emit(sink, kUpgrades[index].ids[value - 1], LINKSPAN_ATTR_WHEN_OWNED);
            if (status != SHIP_NATIVE_OK)
                return status;
        }
    }
    for (index = 0; index < sizeof(snapshot->items); ++index) {
        id = OotItemSource_ItemId(snapshot->items[index]);
        if (id != NULL) {
            status = emit(sink, id, LINKSPAN_ATTR_WHEN_OWNED);
            if (status != SHIP_NATIVE_OK)
                return status;
        }
    }
    /* B é reservado à espada: só os sete botões C/D-pad entram aqui. */
    for (index = 1; index < sizeof(snapshot->button_items); ++index) {
        id = OotItemSource_ItemId(snapshot->button_items[index]);
        if (id != NULL) {
            status = emit(sink, id, LINKSPAN_ATTR_WHEN_EQUIPPED);
            if (status != SHIP_NATIVE_OK)
                return status;
        }
    }
    if (snapshot->current_mask >= 1 && snapshot->current_mask <= 8)
        return emit(sink, kItems[0x24 + snapshot->current_mask - 1], LINKSPAN_ATTR_WHEN_EQUIPPED);
    return SHIP_NATIVE_OK;
}
