#undef NDEBUG
#include <cassert>
#include <cstring>
#include "mods/nei_save.h"
#include "mods/items/custom_bottles.h"
static NeiSaveData save;
extern "C" NeiSaveData* Nei_Save(void) {
    return &save;
}
static void Reset() {
    std::memset(&save, 0, sizeof(save));
    std::memset(save.bottleSlots, 0xff, sizeof(save.bottleSlots));
    save.bottomlessContent = 0xff;
    Bottle_WheelResetTracking();
    uint8_t w, it;
    Bottle_ConsumeCatchSync(&w, &it);
}
int main() {
    Reset();
    for (unsigned i = 0; i < 8; ++i)
        assert(Bottle_GiveBottle(0x14));
    assert(!Bottle_GiveBottle(0x14));
    assert(!Bottle_HasFreeSlot());
    assert(!Bottle_BottomlessOwned() && !Bottle_NetOwned());
    assert(Bottle_WheelBottleCount(0) == 4 && Bottle_WheelBottleCount(1) == 4);
    assert(Bottle_WheelCanCycle(0));
    // O índice, não só o conteúdo, distingue duas garrafas vazias.
    Bottle_WheelRecordActive(0, 0x14);
    assert(Bottle_WheelStep(0, 1) == 0x14);
    Bottle_WheelRecordActive(0, 0x14);
    Bottle_WheelPersist(0, 0x15);
    assert(Bottle_GetSlot(0) == 0x14 && Bottle_GetSlot(1) == 0x15);
    assert(Bottle_GetSlot(8) == 0xff);
    Bottle_SetSlot(8, 0x15);
    assert(Bottle_GetSlot(7) == 0x14);
    Reset();
    Bottle_SetBottomlessOwned(1);
    Bottle_BottomlessFill(0x15);
    const auto charges = Bottle_ContentMaxUses(0x15);
    assert(charges > 1);
    for (unsigned i = 1; i < charges; ++i) {
        assert(Bottle_BottomlessConsume() == charges - i);
        assert(Bottle_BottomlessContent() == 0x15);
    }
    assert(Bottle_BottomlessConsume() == 0 && Bottle_BottomlessIsEmpty());
    assert(Bottle_BottomlessConsume() == 0);
    assert(Bottle_CatchIntoEmpty(0x19));
    assert(Bottle_BottomlessContent() == 0x19 && Bottle_BottomlessCount() == Bottle_ContentMaxUses(0x19));
    Reset();
    assert(Bottle_GiveBottle(0x14));
    Bottle_WheelRecordActive(0, 0x14);
    assert(Bottle_CatchIntoEmpty(0x18));
    uint8_t w, it;
    assert(Bottle_ConsumeCatchSync(&w, &it) && w == 0 && it == 0x18);
    assert(!Bottle_ConsumeCatchSync(&w, &it));
    // Reinicializar o arquivo cancela o valor que a roda tinha observado antes.
    Bottle_WheelRecordActive(0, 0x18);
    Reset();
    Bottle_SetSlot(0, 0x15);
    Bottle_WheelPersist(0, 0x14);
    assert(Bottle_GetSlot(0) == 0x15);
    // P1-04/P2-01/P2-02/P2-06: cada conteúdo adiado continua ocupando espaço físico.
    const uint8_t deferred[] = { 0xB6, 0xDD, 0xDE, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3 };
    for (uint8_t mm : deferred) {
        Reset();
        save.bottleSlots[0] = mm;
        for (unsigned i = 1; i < 8; ++i)
            save.bottleSlots[i] = 0x14;
        assert(Bottle_GetSlot(0) == 0xFF && Bottle_WheelBottleCount(0) == 3);
        assert(!Bottle_HasFreeSlot() && !Bottle_GiveBottle(0x15));
        Bottle_SetSlot(0, 0x15);
        assert(save.bottleSlots[0] == mm);
        save.bottleSlots[2] = 0xFF;
        assert(Bottle_HasFreeSlot() && Bottle_GiveBottle(0x15));
        assert(save.bottleSlots[0] == mm && save.bottleSlots[2] == 0x15);
        save.bottomlessContent = mm;
        save.bottomlessCount = 7;
        Bottle_SetBottomlessOwned(1);
        Bottle_BottomlessFill(0x15);
        Bottle_BottomlessEmpty();
        Bottle_BottomlessSetCount(0);
        assert(Bottle_BottomlessConsume() == 0);
        Bottle_SetBottomlessOwned(0);
        assert(!Bottle_BottomlessOwned() && save.bottomlessContent == mm && save.bottomlessCount == 7);
        Bottle_SetBottomlessOwned(1);
        Bottle_WheelRecordActive(0, 0x14);
        assert(Bottle_CatchIntoEmpty(0x19));
        assert(save.bottleSlots[1] == 0x19 && save.bottomlessContent == mm && save.bottomlessCount == 7);
        for (unsigned i = 1; i < 8; ++i)
            save.bottleSlots[i] = 0x15;
        assert(!Bottle_CatchIntoEmpty(0x19));
        assert(Bottle_ContentFromItemId(mm) == BOTTLE_C_COUNT);
        assert(!Bottle_GiveBottle(mm) && !Bottle_CatchIntoEmpty(mm));
    }
    return 0;
}
