#undef NDEBUG
#include <cassert>
#include <cstring>
#include "mods/nei_save.h"
#include "mods/items/custom_bottles.h"
static NeiSaveData save;
extern "C" NeiSaveData* Nei_Save(void) { return &save; }
static void Reset() {
    std::memset(&save,0,sizeof(save)); std::memset(save.bottleSlots,0xff,sizeof(save.bottleSlots));
    save.bottomlessContent=0xff; Bottle_WheelResetTracking();
    uint8_t w,it; Bottle_ConsumeCatchSync(&w,&it);
}
int main() {
    Reset();
    for(unsigned i=0;i<8;++i) assert(Bottle_GiveBottle(0x14));
    assert(!Bottle_GiveBottle(0x14)); assert(!Bottle_HasFreeSlot());
    assert(!Bottle_BottomlessOwned() && !Bottle_NetOwned());
    assert(Bottle_WheelBottleCount(0)==4 && Bottle_WheelBottleCount(1)==4);
    assert(Bottle_WheelCanCycle(0));
    // O índice, não só o conteúdo, distingue duas garrafas vazias.
    Bottle_WheelRecordActive(0,0x14); assert(Bottle_WheelStep(0,1)==0x14);
    Bottle_WheelRecordActive(0,0x14); Bottle_WheelPersist(0,0x15);
    assert(Bottle_GetSlot(0)==0x14 && Bottle_GetSlot(1)==0x15);
    assert(Bottle_GetSlot(8)==0xff); Bottle_SetSlot(8,0x15);
    assert(Bottle_GetSlot(7)==0x14);
    Reset(); Bottle_SetBottomlessOwned(1); Bottle_BottomlessFill(0x15);
    const auto charges=Bottle_ContentMaxUses(0x15); assert(charges>1);
    for(unsigned i=1;i<charges;++i) { assert(Bottle_BottomlessConsume()==charges-i); assert(Bottle_BottomlessContent()==0x15); }
    assert(Bottle_BottomlessConsume()==0 && Bottle_BottomlessIsEmpty());
    assert(Bottle_BottomlessConsume()==0); assert(Bottle_CatchIntoEmpty(0x19));
    assert(Bottle_BottomlessContent()==0x19 && Bottle_BottomlessCount()==Bottle_ContentMaxUses(0x19));
    Reset(); assert(Bottle_GiveBottle(0x14)); Bottle_WheelRecordActive(0,0x14);
    assert(Bottle_CatchIntoEmpty(0x18)); uint8_t w,it;
    assert(Bottle_ConsumeCatchSync(&w,&it) && w==0 && it==0x18); assert(!Bottle_ConsumeCatchSync(&w,&it));
    // Reinicializar o arquivo cancela o valor que a roda tinha observado antes.
    Bottle_WheelRecordActive(0,0x18); Reset(); Bottle_SetSlot(0,0x15);
    Bottle_WheelPersist(0,0x14); assert(Bottle_GetSlot(0)==0x15);
    return 0;
}
