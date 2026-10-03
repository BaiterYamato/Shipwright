#include "fork/inventory_controls.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool condition, const char* label) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
int main() {
    NeiInventoryGesture a{};
    Check(NeiInventoryInput(&a, true, 46, 100) == NEI_INVENTORY_NONE, "press waits for tap/hold");
    Check(NeiInventoryInput(&a, false, 46, 250) == NEI_INVENTORY_EQUIP, "tap opens hotbar on release");
    Check(NeiInventoryInput(&a, false, 46, 251) == NEI_INVENTORY_NONE, "tap cannot repeat");
    NeiInventoryInput(&a, true, 46, 1000);
    Check(NeiInventoryInput(&a, true, 46, 1399) == NEI_INVENTORY_NONE, "hold threshold not reached");
    Check(NeiInventoryInput(&a, true, 46, 1400) == NEI_INVENTORY_VARIANT, "hold opens variant at 400ms");
    Check(NeiInventoryInput(&a, true, 46, 2000) == NEI_INVENTORY_NONE, "hold fires once");
    Check(NeiInventoryInput(&a, false, 46, 2010) == NEI_INVENTORY_NONE, "releasing hold never equips");
    NeiInventoryInput(&a, true, 46, 3000);
    Check(NeiInventoryInput(&a, true, 47, 3500) == NEI_INVENTORY_NONE, "slot change cancels hold");
    Check(NeiInventoryInput(&a, false, 47, 3600) == NEI_INVENTORY_NONE, "cancelled hold never equips new slot");
    NeiInventoryInput(&a, true, 47, 4000);
    NeiInventoryCancel(&a, true);
    Check(NeiInventoryInput(&a, true, 47, 4600) == NEI_INVENTORY_NONE, "closing menu requires A release");
    NeiInventoryInput(&a, false, 47, 4700);
    NeiInventoryInput(&a, true, 47, 4800);
    Check(NeiInventoryInput(&a, false, 47, 4900) == NEI_INVENTORY_EQUIP, "fresh tap works after cancel");
    std::puts("Inventory controls: A tap, hold, release, slot change and cancel passed.");
}
