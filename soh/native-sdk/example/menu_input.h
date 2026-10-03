#pragma once
#include <stdint.h>

/* A flick is one step until that stick returns to center. Both sticks are
 * sampled every time, so a held stick cannot repeat after a D-pad tap. */
typedef struct LinkSpanMenuInput {
    int8_t leftLatch;
    int8_t rightLatch;
    int8_t padLatch;
} LinkSpanMenuInput;

static inline int LinkSpanMenuStickStep(int x, int8_t* latch) {
    if (*latch) {
        if (x > -20 && x < 20) *latch = 0;
        return 0;
    }
    if (x >= 45) return *latch = 1;
    if (x <= -45) return *latch = -1;
    return 0;
}

static inline int LinkSpanMenuStep(LinkSpanMenuInput* state, int leftX, int rightX, int padDirection) {
    const int left = LinkSpanMenuStickStep(leftX, &state->leftLatch);
    const int right = LinkSpanMenuStickStep(rightX, &state->rightLatch);
    const int pad = padDirection && padDirection != state->padLatch ? padDirection : 0;
    state->padLatch = (int8_t)padDirection;
    return pad ? pad : right ? right : left;
}
