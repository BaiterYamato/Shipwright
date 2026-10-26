#include "oot_item_table.h"
#include <stdio.h>
#include <string.h>

static unsigned gChecks;
static unsigned gFailures;

static void Check(int condition, const char* message) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        fprintf(stderr, "FALHA: %s\n", message);
    }
}

typedef struct Pair {
    const char* id;
    uint8_t when;
} Pair;

typedef struct Sink {
    Pair pairs[128];
    unsigned count;
    unsigned stop_at;
    ShipNativeStatus stop_status;
} Sink;

static ShipNativeStatus SHIP_NATIVE_CALL Emit(void* user, const char* id, uint8_t when) {
    Sink* sink = (Sink*)user;
    if (sink->count >= sizeof(sink->pairs) / sizeof(sink->pairs[0]))
        return SHIP_NATIVE_FAILURE;
    sink->pairs[sink->count].id = id;
    sink->pairs[sink->count].when = when;
    ++sink->count;
    if (sink->stop_at != 0 && sink->count == sink->stop_at)
        return sink->stop_status;
    return SHIP_NATIVE_OK;
}

static OotItemSnapshot Empty(void) {
    OotItemSnapshot snapshot = { 0 };
    memset(snapshot.items, 0xFF, sizeof(snapshot.items));
    memset(snapshot.button_items, 0xFF, sizeof(snapshot.button_items));
    return snapshot;
}

static void Expect(const Sink* sink, unsigned index, const char* id, uint8_t when) {
    Check(index < sink->count, "par esperado existe");
    if (index < sink->count) {
        Check(sink->pairs[index].id != NULL && strcmp(sink->pairs[index].id, id) == 0, "id e ordem do par");
        Check(sink->pairs[index].when == when, "estado do par");
    }
}

static void TestEmpty(void) {
    OotItemSnapshot snapshot = Empty();
    Sink sink = { 0 };
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "1: retrato vazio OK");
    Check(sink.count == 0, "1: retrato vazio sem itens");
    Check(OotItemSource_Emit(NULL, Emit, &sink) == SHIP_NATIVE_INVALID_ARGUMENT, "snapshot nulo");
    Check(OotItemSource_Emit(&snapshot, NULL, &sink) == SHIP_NATIVE_INVALID_ARGUMENT, "emit nulo");
}

static void TestEquipped(void) {
    OotItemSnapshot snapshot = Empty();
    Sink sink = { 0 };
    snapshot.equips_equipment = 0x1122;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "2: equipamento OK");
    Check(sink.count == 4, "2: quatro slots");
    Expect(&sink, 0, "oot.sword.master", LINKSPAN_ATTR_WHEN_EQUIPPED);
    Expect(&sink, 1, "oot.shield.hylian", LINKSPAN_ATTR_WHEN_EQUIPPED);
    Expect(&sink, 2, "oot.tunic.kokiri", LINKSPAN_ATTR_WHEN_EQUIPPED);
    Expect(&sink, 3, "oot.boots.kokiri", LINKSPAN_ATTR_WHEN_EQUIPPED);
    sink.count = 0;
    snapshot.equips_equipment = 0xF840;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "nibbles inválidos OK");
    Check(sink.count == 0, "nibbles fora de 1..3 ignorados");
}

static void TestOwned(void) {
    static const char* const all[] = {
        "oot.sword.kokiri",  "oot.sword.master",  "oot.sword.biggoron", "oot.shield.deku",
        "oot.shield.hylian", "oot.shield.mirror", "oot.tunic.kokiri",   "oot.tunic.goron",
        "oot.tunic.zora",    "oot.boots.kokiri",  "oot.boots.iron",     "oot.boots.hover",
    };
    OotItemSnapshot snapshot = Empty();
    Sink sink = { 0 };
    unsigned i;
    snapshot.inventory_equipment = 0xCBA5;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "3: bits esparsos OK");
    Check(sink.count == 6, "3: bits esparsos, sem bit 3");
    Expect(&sink, 0, "oot.sword.kokiri", LINKSPAN_ATTR_WHEN_OWNED);
    Expect(&sink, 1, "oot.sword.biggoron", LINKSPAN_ATTR_WHEN_OWNED);
    Expect(&sink, 2, "oot.shield.hylian", LINKSPAN_ATTR_WHEN_OWNED);
    Expect(&sink, 3, "oot.tunic.kokiri", LINKSPAN_ATTR_WHEN_OWNED);
    Expect(&sink, 4, "oot.tunic.goron", LINKSPAN_ATTR_WHEN_OWNED);
    Expect(&sink, 5, "oot.boots.hover", LINKSPAN_ATTR_WHEN_OWNED);
    sink.count = 0;
    snapshot.inventory_equipment = 0xFFFF;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "todas as peças OK");
    Check(sink.count == 12, "somente três bits por tipo");
    for (i = 0; i < 12; ++i)
        Expect(&sink, i, all[i], LINKSPAN_ATTR_WHEN_OWNED);
    sink.count = 0;
    snapshot.inventory_equipment = 0x8888;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "bits 3 OK");
    Check(sink.count == 0, "faca quebrada e bits 3 ignorados");
}

static void TestUpgrades(void) {
    static const char* const expected[] = {
        "oot.quiver.50",    "oot.bomb_bag.30",  "oot.strength.goron_bracelet",
        "oot.scale.golden", "oot.wallet.adult", "oot.bullet_bag.30",
    };
    OotItemSnapshot snapshot = Empty();
    Sink sink = { 0 };
    unsigned i;
    snapshot.upgrades = 3u | (2u << 3) | (1u << 6) | (2u << 9) | (1u << 12) | (1u << 14);
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "4: upgrades OK");
    Check(sink.count == 6, "4: seis upgrades");
    for (i = 0; i < 6; ++i)
        Expect(&sink, i, expected[i], LINKSPAN_ATTR_WHEN_OWNED);
    sink.count = 0;
    snapshot.upgrades = 7u | (7u << 3) | (7u << 6) | (3u << 9) | (7u << 14) | 0xFFFE0000u;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "upgrades inválidos OK");
    Check(sink.count == 0, "níveis inválidos, escama 3 e upgrades alheios ignorados");
    sink.count = 0;
    snapshot.upgrades = 3u << 12;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "carteira 3 OK");
    Check(sink.count == 1, "carteira 3 isolada");
    Expect(&sink, 0, "oot.wallet.tycoon", LINKSPAN_ATTR_WHEN_OWNED);
}

static void TestItems(void) {
    OotItemSnapshot snapshot = Empty();
    Sink sink = { 0 };
    snapshot.items[0] = 3;
    snapshot.items[1] = 8;
    snapshot.items[2] = 0x0B;
    snapshot.items[3] = 0x28;
    snapshot.items[4] = 0x2C;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "5: inventário OK");
    Check(sink.count == 4, "5: sold out e vazios ignorados");
    Expect(&sink, 0, "shared.bow", LINKSPAN_ATTR_WHEN_OWNED);
    Expect(&sink, 1, "shared.ocarina", LINKSPAN_ATTR_WHEN_OWNED);
    Expect(&sink, 2, "oot.item.longshot", LINKSPAN_ATTR_WHEN_OWNED);
    Expect(&sink, 3, "oot.mask.goron", LINKSPAN_ATTR_WHEN_OWNED);
    sink.count = 0;
    snapshot = Empty();
    snapshot.items[23] = 0;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "último item OK");
    Check(sink.count == 1, "slot 23 incluído");
    Expect(&sink, 0, "oot.item.stick", LINKSPAN_ATTR_WHEN_OWNED);
}

static void TestButtons(void) {
    OotItemSnapshot snapshot = Empty();
    Sink sink = { 0 };
    snapshot.button_items[0] = 0x3B;
    snapshot.button_items[1] = 0x38;
    snapshot.button_items[2] = 0x0E;
    snapshot.button_items[3] = 0x99;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "6: botões OK");
    Check(sink.count == 2, "6: botões inválidos ignorados");
    Expect(&sink, 0, "shared.bow", LINKSPAN_ATTR_WHEN_EQUIPPED);
    Expect(&sink, 1, "oot.item.boomerang", LINKSPAN_ATTR_WHEN_EQUIPPED);
    sink.count = 0;
    snapshot = Empty();
    snapshot.button_items[0] = 3; /* Id válido também deve ser ignorado no B. */
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "B válido OK");
    Check(sink.count == 0, "B nunca emite");
    snapshot.button_items[7] = 0;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "último botão OK");
    Check(sink.count == 1, "D-pad slot 7 incluído");
    Expect(&sink, 0, "oot.item.stick", LINKSPAN_ATTR_WHEN_EQUIPPED);
}

static void TestMasks(void) {
    static const char* const masks[] = {
        "oot.mask.keaton", "oot.mask.skull", "oot.mask.spooky", "oot.mask.bunny",
        "oot.mask.goron",  "oot.mask.zora",  "oot.mask.gerudo", "oot.mask.truth",
    };
    OotItemSnapshot snapshot = Empty();
    Sink sink = { 0 };
    unsigned i;
    snapshot.current_mask = 5;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "7: máscara Goron OK");
    Check(sink.count == 1, "7: uma máscara");
    Expect(&sink, 0, "oot.mask.goron", LINKSPAN_ATTR_WHEN_EQUIPPED);
    for (i = 0; i < 256; ++i) {
        sink.count = 0;
        snapshot.current_mask = (uint8_t)i;
        Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "máscara OK");
        Check(sink.count == (i >= 1 && i <= 8 ? 1u : 0u), "somente máscaras 1..8");
        if (i >= 1 && i <= 8)
            Expect(&sink, 0, masks[i - 1], LINKSPAN_ATTR_WHEN_EQUIPPED);
    }
}

static void TestStopAndOrder(void) {
    static const Pair expected[] = {
        { "oot.sword.master", LINKSPAN_ATTR_WHEN_EQUIPPED }, { "oot.shield.hylian", LINKSPAN_ATTR_WHEN_EQUIPPED },
        { "oot.tunic.kokiri", LINKSPAN_ATTR_WHEN_EQUIPPED }, { "oot.boots.kokiri", LINKSPAN_ATTR_WHEN_EQUIPPED },
        { "oot.sword.kokiri", LINKSPAN_ATTR_WHEN_OWNED },    { "oot.quiver.30", LINKSPAN_ATTR_WHEN_OWNED },
        { "shared.bow", LINKSPAN_ATTR_WHEN_OWNED },          { "oot.item.stick", LINKSPAN_ATTR_WHEN_OWNED },
        { "shared.bow", LINKSPAN_ATTR_WHEN_EQUIPPED },       { "oot.item.boomerang", LINKSPAN_ATTR_WHEN_EQUIPPED },
        { "oot.mask.goron", LINKSPAN_ATTR_WHEN_EQUIPPED },
    };
    OotItemSnapshot snapshot = Empty();
    Sink sink = { 0 };
    unsigned i;
    ShipNativeStatus status;
    snapshot.equips_equipment = 0x1122;
    snapshot.inventory_equipment = 1;
    snapshot.upgrades = 1;
    snapshot.items[0] = 3;
    snapshot.items[23] = 0;
    snapshot.button_items[1] = 0x38;
    snapshot.button_items[7] = 0x0E;
    snapshot.current_mask = 5;
    sink.stop_at = 2;
    sink.stop_status = SHIP_NATIVE_LIMIT;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_LIMIT, "8: LIMIT na segunda chamada");
    Check(sink.count == 2, "8: para na segunda chamada");
    sink.stop_at = 0;
    sink.count = 0;
    Check(OotItemSource_Emit(&snapshot, Emit, &sink) == SHIP_NATIVE_OK, "ordem completa OK");
    Check(sink.count == sizeof(expected) / sizeof(expected[0]), "quantidade completa");
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        Expect(&sink, i, expected[i].id, expected[i].when);
    /* Falha em cada etapa, inclusive no último emit; nenhum status é engolido. */
    for (status = SHIP_NATIVE_INVALID_ARGUMENT; status <= SHIP_NATIVE_LIMIT; ++status) {
        for (i = 1; i <= sizeof(expected) / sizeof(expected[0]); ++i) {
            sink.count = 0;
            sink.stop_at = i;
            sink.stop_status = status;
            Check(OotItemSource_Emit(&snapshot, Emit, &sink) == status, "propaga primeiro status não OK");
            Check(sink.count == i, "não chama emit após falha");
        }
    }
}

static int ValidId(const char* id) {
    size_t i, length;
    if (id == NULL)
        return 0;
    length = strlen(id);
    if (length == 0 || length > LINKSPAN_ATTR_MAX_ID || id[0] == '.' || id[length - 1] == '.')
        return 0;
    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)id[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' ||
              ch == '-' || ch == '.'))
            return 0;
        if (ch == '.' && i > 0 && id[i - 1] == '.')
            return 0;
    }
    return 1;
}

static void TestTable(void) {
    /* Oráculo explícito: trava também as entradas sem alias da RFC. */
    static const char* const expected[] = {
        "oot.item.stick",
        "oot.item.nut",
        "shared.bombs",
        "shared.bow",
        "oot.item.arrow_fire",
        "oot.item.dins_fire",
        "oot.item.slingshot",
        "oot.item.ocarina_fairy",
        "shared.ocarina",
        "shared.bombchu",
        "shared.hookshot",
        "oot.item.longshot",
        "oot.item.arrow_ice",
        "oot.item.farores_wind",
        "oot.item.boomerang",
        "oot.item.lens",
        "oot.item.bean",
        "oot.item.hammer",
        "oot.item.arrow_light",
        "oot.item.nayrus_love",
        "oot.item.bottle",
        "oot.item.potion_red",
        "oot.item.potion_green",
        "oot.item.potion_blue",
        "oot.item.fairy",
        "oot.item.fish",
        "oot.item.milk_bottle",
        "oot.item.letter_ruto",
        "oot.item.blue_fire",
        "oot.item.bug",
        "oot.item.big_poe",
        "oot.item.milk_half",
        "oot.item.poe",
        "oot.item.weird_egg",
        "oot.item.chicken",
        "oot.item.letter_zelda",
        "oot.mask.keaton",
        "oot.mask.skull",
        "oot.mask.spooky",
        "oot.mask.bunny",
        "oot.mask.goron",
        "oot.mask.zora",
        "oot.mask.gerudo",
        "oot.mask.truth",
        NULL,
        "oot.item.pocket_egg",
        "oot.item.pocket_cucco",
        "oot.item.cojiro",
        "oot.item.odd_mushroom",
        "oot.item.odd_potion",
        "oot.item.saw",
        "oot.item.sword_broken",
        "oot.item.prescription",
        "oot.item.frog",
        "oot.item.eyedrops",
        "oot.item.claim_check",
        "shared.bow",
        "shared.bow",
        "shared.bow",
    };
    unsigned i, j;
    Check(sizeof(expected) / sizeof(expected[0]) == 0x3B, "9: oráculo 0x00..0x3A completo");
    for (i = 0; i < 256; ++i) {
        const char* id = OotItemSource_ItemId((uint8_t)i);
        if (i == 0x2C || i > 0x3A) {
            Check(id == NULL, "9: sold out e fora da tabela nulos");
            continue;
        }
        Check(id != NULL, "9: entrada válida não nula");
        if (id == NULL)
            continue;
        Check(ValidId(id), "9: padrão do id");
        Check(strcmp(id, expected[i]) == 0, "9: mapeamento exato");
        for (j = 0; j < i; ++j) {
            const char* previous = OotItemSource_ItemId((uint8_t)j);
            if (previous != NULL)
                Check(strcmp(id, previous) != 0 ||
                          (strcmp(id, "shared.bow") == 0 && (j == 3 || j >= 0x38) && (i == 3 || i >= 0x38)),
                      "9: único alias repetido é shared.bow");
        }
    }
}

int main(void) {
    TestEmpty();
    TestEquipped();
    TestOwned();
    TestUpgrades();
    TestItems();
    TestButtons();
    TestMasks();
    TestStopAndOrder();
    TestTable();
    printf("oot_item_table_tests: 9 casos, %u checks, %u falhas\n", gChecks, gFailures);
    return gFailures != 0;
}
