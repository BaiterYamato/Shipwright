// Fixtures mínimas: os corpos sob teste vêm dos fontes reais, extraídos pelo CMake.
#pragma once
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "z64item.h"
#include "mods/nei_save.h"
#include "mods/items/custom_bottles.h"
#include "fork/progression.h"
#include "rc11_inventory_constants.inc"

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using s16 = int16_t;
struct SaveContext {
    struct { u8 items[24]; u32 questItems; } inventory;
    struct { u8 cButtonSlots[3]; u8 buttonItems[4]; } equips;
} gSaveContext;
struct PlayState { struct { int state; } pauseCtx; };
static PlayState* gPlayState;
static NeiSaveData sRc11Save;
static u8 sRc11Rule = WAND_RANDO_ELEMENTAL;
static bool sRc11Rando;
#define IS_RANDO sRc11Rando
#define CHECK_QUEST_ITEM(q) (gSaveContext.inventory.questItems & (1u << (q)))
#define EXT_EQUIP_OWNED_SHIFT 16
#define CVAR_EXT_EQUIP_ENABLED "gCheats.ExtEquip.Enabled"
static const u8 sWandQuest[WAND_MODE_COUNT] = { 0, 1, 2, 3, 4, 5 };
static const u32 gBitFlags[] = { 1, 2, 4, 8, 16, 32 };
extern "C" NeiSaveData* Nei_Save(void) { return &sRc11Save; }
extern "C" u16 Nei_GetOwnedItem(u8 slot) {
    return slot >= 24 && slot < 72 ? sRc11Save.ownedItems[slot - 24] : ITEM_NONE;
}
extern "C" void Nei_SetOwnedItem(u8 slot, u16 item) {
    if (slot >= 24 && slot < 72) sRc11Save.ownedItems[slot - 24] = item;
}
extern "C" u8 Picto_IsOwned(void) { return 0; }
extern "C" u8 PowerKeg_IsOwned(void) { return 0; }
static int CVarGetInteger(const char* key, int fallback) {
    return !std::strcmp(key, "gRandoSettings.ElementalWandShuffle") ? sRc11Rule : fallback;
}
u8 Wand_RandoMode(void) { return (u8)CVarGetInteger("gRandoSettings.ElementalWandShuffle", WAND_RANDO_MEDALLIONS); }
static void CVarSetInteger(const char*, int) {}
static void CVarSave(void) {}
static void Interface_LoadItemIcon1(PlayState*, u8) {}
u8 Wand_ModeOwned(u8);
u8 Wand_ModeCount(void);
u8 Wand_ModeAt(u8);
u8 Wand_GetMode(void);
void Wand_SetMode(u8);
static void* Wand_ModeIcon(u8) { return (void*)"native"; }
static void* Slate_RuneIcon(u8) { return (void*)"native"; }
static void* Seasons_SeasonIcon(u8) { return (void*)"native"; }
static void* ExtInv_GetInventoryItemIcon(u16) { return (void*)"native"; }
static void Slate_GrantRune(u8 rune) { sRc11Save.slateRunesOwned |= (u8)(1u << rune); }
static void Seasons_GrantSeason(u8 season) { sRc11Save.seasonsOwned |= (u8)(1u << season); }
static u8 Slate_GetRune(void) { return 0; }
static void Slate_SetRune(u8) {}
static u8 Seasons_GetSeason(void) { return 0; }
static void Seasons_SetSeason(u8) {}
static u8 ExtInv_GetItemSlot(u16 item) {
    switch (item) {
        case ITEM_CANE_OF_SOMARIA: return SLOT_CANE_OF_SOMARIA;
        case ITEM_ELEMENTAL_WAND: return SLOT_ELEMENTAL_WAND;
        default: return 0xFF;
    }
}
struct {
    u8 currentExtSword, currentExtShield, currentExtTunic, currentExtBoots;
} gExtEquipState;
static u8 ExtEquip_GetCurrent(s16 type) {
    switch (type) {
        case EQUIP_TYPE_SWORD: return gExtEquipState.currentExtSword;
        case EQUIP_TYPE_SHIELD: return gExtEquipState.currentExtShield;
        case EQUIP_TYPE_TUNIC: return gExtEquipState.currentExtTunic;
        default: return gExtEquipState.currentExtBoots;
    }
}
void ExtEquip_SetSlot(s16, u8);
static void ExtEquip_CleanupSlot(s16, u8) {}
static void ExtEquip_ApplyVanillaBase(s16, u8, u8) {}
static void ExtEquip_ApplyTridentShieldPolicy(void) {}
static void ExtEquip_RefreshPlayer(void) {}
static int ExtEquip_CheckAgeReq(s16, u8) { return 1; }
static void* ExtEquip_GetIcon(s16, u8) { return (void*)"native"; }
static void ExtEquip_Init(void);
#include "rc11_inventory_under_test.inc"
// Só o trecho de ExtEquip_Init que migra o layout antigo das botas.
static void ExtEquip_Init(void) {
    gExtEquipState.currentExtBoots = Nei_Save()->extEquipBoots;
#include "rc11_boots_migration.inc"
}

static void Rc11Reset() {
    std::memset(&sRc11Save, 0, sizeof(sRc11Save));
    for (auto& item : sRc11Save.ownedItems) item = ITEM_NONE;
    std::memset(sRc11Save.bottleSlots, 0xFF, sizeof(sRc11Save.bottleSlots));
    sRc11Save.bottomlessContent = 0xFF;
    std::memset(&gSaveContext, 0xFF, sizeof(gSaveContext));
    gSaveContext.inventory.questItems = 0;
    gExtEquipState = {};
    sRc11Rule = WAND_RANDO_ELEMENTAL;
    sRc11Rando = false;
    ExtInv_ResetSessionItems();
    Bottle_WheelResetTracking();
    u8 wheel, item;
    Bottle_ConsumeCatchSync(&wheel, &item);
}
static void RunRc11InventoryRegressions() {
    Rc11Reset();
    // P1-01: inicialização/ciclagem projeta um trade OoT sem mudar o ID MM físico.
    gSaveContext.inventory.items[SLOT_TRADE_ADULT] = ITEM_MM_MOONS_TEAR;
    assert(ExtInv_GetSlotItem(SLOT_TRADE_ADULT) == ITEM_NONE);
    ExtInv_SetSlotItem(SLOT_TRADE_ADULT, ITEM_POCKET_EGG);
    assert(ExtInv_GetSlotItem(SLOT_TRADE_ADULT) == ITEM_POCKET_EGG);
    assert(ExtInv_GetPhysicalSlotItem(SLOT_TRADE_ADULT) == ITEM_MM_MOONS_TEAR);
    ExtInv_ResetSessionItems();
    assert(ExtInv_GetSlotItem(SLOT_TRADE_ADULT) == ITEM_NONE);
    gSaveContext.inventory.items[SLOT_BOTTLE_1] = ITEM_BOTTLE_WITH_MAGIC_MUSHROOM;
    gSaveContext.equips.cButtonSlots[0] = SLOT_BOTTLE_1;
    gSaveContext.equips.buttonItems[1] = ITEM_BOTTLE_WITH_MAGIC_MUSHROOM;
    SyncSlot(nullptr, SLOT_BOTTLE_1, ITEM_FISH);
    assert(ExtInv_GetSlotItem(SLOT_BOTTLE_1) == ITEM_FISH);
    assert(gSaveContext.inventory.items[SLOT_BOTTLE_1] == ITEM_BOTTLE_WITH_MAGIC_MUSHROOM);
    assert(gSaveContext.equips.buttonItems[1] == ITEM_BOTTLE_WITH_MAGIC_MUSHROOM);
    // Caminho vanilla continua atualizando a célula e seu botão C.
    gSaveContext.inventory.items[SLOT_BOTTLE_2] = ITEM_BOTTLE;
    gSaveContext.equips.cButtonSlots[1] = SLOT_BOTTLE_2;
    gSaveContext.equips.buttonItems[2] = ITEM_BOTTLE;
    SyncSlot(nullptr, SLOT_BOTTLE_2, ITEM_FISH);
    assert(gSaveContext.inventory.items[SLOT_BOTTLE_2] == ITEM_FISH && gSaveContext.equips.buttonItems[2] == ITEM_FISH);

    Rc11Reset();
    // P2-02: a migração pula o slot MM e conserva a poção ao projetar a wheel.
    sRc11Save.bottleSlots[0] = ITEM_CHATEAU_ROMANI;
    sRc11Save.bottleSlots[1] = ITEM_BOTTLE;
    gSaveContext.inventory.items[SLOT_BOTTLE_1] = ITEM_POTION_RED;
    NeiBottles_Project(nullptr);
    assert(sRc11Save.bottleSlots[0] == ITEM_CHATEAU_ROMANI);
    assert(sRc11Save.bottleSlots[2] == ITEM_POTION_RED);
    assert(gSaveContext.inventory.items[SLOT_BOTTLE_1] == ITEM_POTION_RED);
    for (unsigned i = 2; i < 4; ++i) sRc11Save.bottleSlots[i] = ITEM_FISH;
    assert(!MigrateBottle(ITEM_POTION_BLUE, 0));

    Rc11Reset();
    // P1-02/P1-03: leitura e toggles nativos não apagam Shadow nem sua seleção.
    sRc11Save.wandMode = WAND_MODE_SCEPTER;
    sRc11Save.wandRodsOwned = (1u << WAND_MODE_SCEPTER) | 1;
    assert(Wand_GetMode() == WAND_MODE_SAND && sRc11Save.wandMode == WAND_MODE_SCEPTER);
    NeiProgress_SetMask(NEI_POWER_WAND, 3);
    assert(sRc11Save.wandMode == WAND_MODE_SCEPTER && sRc11Save.wandRodsOwned == 0x23);
    NeiInv_RemoveItem(ITEM_ELEMENTAL_WAND);
    assert(sRc11Save.wandRodsOwned == 0x20);
    sRc11Rule = WAND_RANDO_SINGLE;
    assert(!Wand_ModeOwned(WAND_MODE_SAND));
    Wand_GrantMode(WAND_MODE_SAND);
    Wand_SetModeOwned(WAND_MODE_SAND, 0);
    assert(sRc11Save.wandRodsOwned == 0x20);
    Wand_GrantMode(WAND_MODE_TORNADO);
    Wand_SetMode(WAND_MODE_TORNADO);
    assert(sRc11Save.wandMode == WAND_MODE_TORNADO);
    char catalog[8192] = {};
    NeiProgress_Catalog(catalog, sizeof(catalog));
    assert(!std::strstr(catalog, "Shadow Scepter") && !std::strstr(catalog, "Somaria: Statue"));

    Rc11Reset();
    // P2-03: o fallback efetivo nunca seleciona Statue nem altera sua seleção salva.
    sRc11Save.caneSkills = 3;
    sRc11Save.caneSkillSel[0] = 0;
    assert(Nei_CaneActiveSkill() == CANE_SKILL_SOMARIA_BLOCK);
    assert(sRc11Save.caneSkillSel[0] == 0 && sRc11Save.caneSkills == 3);
    Nei_CaneSetSkillSlot(0, 0);
    assert(sRc11Save.caneSkillSel[0] == 0);
    NeiProgress_SetMask(NEI_POWER_CANE, 0);
    assert(sRc11Save.caneSkills == 1 && !Nei_CaneOwned());
    NeiInv_PlaceItem(ITEM_CANE_OF_SOMARIA);
    assert(sRc11Save.caneSkills == 3 && sRc11Save.caneSkillSel[0] == 0);
    NeiInv_RemoveItem(ITEM_CANE_OF_SOMARIA);
    assert(sRc11Save.caneSkills == 1);
    // P2-04: concessão pelo menu e primeira cópia progressiva começam em Block.
    Rc11Reset();
    NeiInv_PlaceItem(ITEM_CANE_OF_SOMARIA);
    assert(Nei_GetOwnedItem(SLOT_CANE_OF_SOMARIA) == ITEM_CANE_OF_SOMARIA);
    assert(Nei_CaneHasSkill(CANE_SKILL_SOMARIA_BLOCK));
    Rc11Reset();
    sRc11Save.caneSkills = 0x81;
    assert(!Nei_CaneOwned() && !Nei_CaneHasSkill(CANE_SKILL_SOMARIA_BLOCK));
    NeiInv_RemoveItem(ITEM_CANE_OF_SOMARIA);
    assert(sRc11Save.caneSkills == 0x81);
    Rc11Reset();
    NeiInv_ReceiveItem(ITEM_CANE_OF_SOMARIA);
    assert(sRc11Save.caneSkills == (1u << CANE_SKILL_SOMARIA_BLOCK));

    Rc11Reset();
    // P2-05: o layout antigo migra uma vez, como no fork (Pendant vai para a troca, Scale sai),
    // e depois as Climb/Roc Boots persistem entre recargas.
    sRc11Save.extEquipOwnedBits = ExtEquip_GetBit(EQUIP_TYPE_BOOTS, 2) | ExtEquip_GetBit(EQUIP_TYPE_BOOTS, 3);
    sRc11Save.extEquipBoots = 3;
    ExtEquip_Init();
    assert(sRc11Save.extBootsLayoutVersion == 1 && sRc11Save.extEquipOwnedBits == 0);
    assert(sRc11Save.extEquipBoots == 0 && gExtEquipState.currentExtBoots == 0);
    assert(sRc11Save.tradeAdultOwned == (1u << TRADE_ADULT_PENDANT));
    for (u8 boots : { (u8)2, (u8)3 }) {
        ExtEquip_GiveItem(EQUIP_TYPE_BOOTS, boots);
        ExtEquip_SetSlot(EQUIP_TYPE_BOOTS, boots);
        ExtEquip_Init();
        assert(ExtEquip_HasItem(EQUIP_TYPE_BOOTS, boots) && sRc11Save.extEquipBoots == boots);
        assert(gExtEquipState.currentExtBoots == boots);
        assert(sRc11Save.tradeAdultOwned == (1u << TRADE_ADULT_PENDANT));
    }
    // P3-01: catálogo e ação usam o mesmo bloqueio; o bit de Ikana sobrevive.
    sRc11Save.extEquipOwnedBits |= 1u << 21;
    const u32 before = sRc11Save.extEquipOwnedBits;
    assert(ExtEquip_SlotRetired(EQUIP_TYPE_SHIELD, 3));
    assert(!NeiEquipment_Toggle(EQUIP_TYPE_SHIELD, 3));
    assert(sRc11Save.extEquipOwnedBits == before);
    NeiEquipment_Catalog(catalog, sizeof(catalog));
    assert(!std::strstr(catalog, "Shield of Ikana"));
    assert(ExtInv_IsDeferredMmItem(ITEM_BOTTLE_WITH_MAGIC_MUSHROOM));
    Rc11Reset();
}
