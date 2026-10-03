#pragma once
#include <stdint.h>
#include "../../example/menu_input.h"

typedef enum NeiInventoryAction {
    NEI_INVENTORY_NONE,
    NEI_INVENTORY_EQUIP,
    NEI_INVENTORY_VARIANT
} NeiInventoryAction;

typedef struct NeiInventoryGesture {
    uint64_t pressedAt;
    uint32_t context;
    uint8_t down;
    uint8_t fired;
    uint8_t waitRelease;
} NeiInventoryGesture;

static inline void NeiInventoryCancel(NeiInventoryGesture* gesture, int down) {
    gesture->down = 0;
    gesture->fired = 0;
    gesture->waitRelease = (uint8_t)down;
}

/* A tap is committed on release. A hold fires once, and can never become an
 * equip tap when released. Changing slot/page cancels the pending gesture. */
static inline NeiInventoryAction NeiInventoryInput(NeiInventoryGesture* gesture, int down,
                                                   uint32_t context, uint64_t now) {
    if (gesture->waitRelease) {
        if (!down) gesture->waitRelease = 0;
        return NEI_INVENTORY_NONE;
    }
    if (gesture->down && gesture->context != context) {
        NeiInventoryCancel(gesture, down);
        return NEI_INVENTORY_NONE;
    }
    if (down && !gesture->down) {
        gesture->down = 1;
        gesture->fired = 0;
        gesture->pressedAt = now;
        gesture->context = context;
    }
    if (down && !gesture->fired && now - gesture->pressedAt >= 400) {
        gesture->fired = 1;
        return NEI_INVENTORY_VARIANT;
    }
    if (!down && gesture->down) {
        const int tap = !gesture->fired;
        gesture->down = gesture->fired = 0;
        return tap ? NEI_INVENTORY_EQUIP : NEI_INVENTORY_NONE;
    }
    return NEI_INVENTORY_NONE;
}

uint64_t NeiInventory_Milliseconds(void);
uint16_t NeiInventory_PadButtons(void);
