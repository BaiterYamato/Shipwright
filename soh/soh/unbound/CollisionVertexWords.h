#ifndef SOH_UNBOUND_COLLISION_VERTEX_WORDS_H
#define SOH_UNBOUND_COLLISION_VERTEX_WORDS_H

// SOH [Unbound] CollisionPoly vertex words and collision node-table growth, kept free of game headers so
// ROM-free tests can include them. UnboundCollisionChecks.cpp pins these constants to the COLPOLY_* macros
// of z64bgcheck.h. See docs/architecture/unbound-collision-layout.md.

#include <stddef.h>
#include <stdint.h>

#define UNBOUND_COLPOLY_INDEX_BITS 29
#define UNBOUND_COLPOLY_INDEX_MASK 0x1FFFFFFFu
#define UNBOUND_COLPOLY_FLAGS_MASK 0xE0000000u
#define UNBOUND_COLPOLY_LEGACY_INDEX_MASK 0x1FFFu
#define UNBOUND_COLPOLY_LEGACY_FLAGS_SHIFT 13

// Same spelling as COLPOLY_VTX_INDEX, COLPOLY_VIA_FLAG_TEST and COLPOLY_VIB_CONVEYOR
#define UNBOUND_COLPOLY_VTX_INDEX(word) ((word) & UNBOUND_COLPOLY_INDEX_MASK)
#define UNBOUND_COLPOLY_FLAG_TEST(word, flags) ((word) & (((uint32_t)(flags) & 7u) << UNBOUND_COLPOLY_INDEX_BITS))
#define UNBOUND_COLPOLY_CONVEYOR (1u << UNBOUND_COLPOLY_INDEX_BITS)

// N64 / OTR v0 word: 13-bit vertex index with the 3 flag bits on top of the u16
static inline uint32_t Unbound_UnpackLegacyVtxWord(uint16_t packed) {
    return (uint32_t)(packed & UNBOUND_COLPOLY_LEGACY_INDEX_MASK) |
           ((uint32_t)(packed >> UNBOUND_COLPOLY_LEGACY_FLAGS_SHIFT) << UNBOUND_COLPOLY_INDEX_BITS);
}

static inline uint32_t Unbound_PackVtxWord(uint32_t index, uint32_t flags3) {
    return (index & UNBOUND_COLPOLY_INDEX_MASK) | ((flags3 & 7u) << UNBOUND_COLPOLY_INDEX_BITS);
}

// Capacity a node table needs to store `index`: doubles `capacity` (at least 1) until the index fits.
// Returns 0 when that capacity would exceed `maxCapacity` or its byte size would not fit in size_t.
static inline uint32_t Unbound_GrowNodeCapacity(uint32_t capacity, uint32_t index, uint32_t maxCapacity,
                                                size_t nodeSize) {
    uint64_t grown = capacity != 0 ? capacity : 1;

    while (grown <= index) {
        grown *= 2;
    }
    if (nodeSize == 0 || grown > maxCapacity || grown > SIZE_MAX / nodeSize) {
        return 0;
    }
    return (uint32_t)grown;
}

#endif
