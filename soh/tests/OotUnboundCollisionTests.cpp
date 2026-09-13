#include "soh/unbound/CollisionVertexWords.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// N64 / OTR v0 layout, as z64bgcheck.h spelled it before OOT-UNBOUND-003B
uint32_t LegacyIndex(uint16_t word) {
    return word & 0x1FFFu;
}

bool LegacyFlagTest(uint16_t word, uint32_t flags) {
    return (word & ((flags & 7u) << 13)) != 0;
}

bool LegacyConveyor(uint16_t word) {
    return (word & 0x2000u) != 0;
}

void TestLegacyWordsUnpackLosslessly() {
    for (uint32_t value = 0; value <= 0xFFFF; value++) {
        const uint16_t legacy = static_cast<uint16_t>(value);
        const uint32_t word = Unbound_UnpackLegacyVtxWord(legacy);

        Check(UNBOUND_COLPOLY_VTX_INDEX(word) == LegacyIndex(legacy), "legacy vertex index changed on unpack");
        for (uint32_t flags = 0; flags < 16; flags++) {
            Check((UNBOUND_COLPOLY_FLAG_TEST(word, flags) != 0) == LegacyFlagTest(legacy, flags),
                  "legacy xpFlags test changed on unpack");
        }
        Check(((word & UNBOUND_COLPOLY_CONVEYOR) != 0) == LegacyConveyor(legacy),
              "legacy conveyor flag changed on unpack");
        Check(Unbound_PackVtxWord(UNBOUND_COLPOLY_VTX_INDEX(word), word >> UNBOUND_COLPOLY_INDEX_BITS) == word,
              "pack does not invert the legacy unpack");
    }
}

void TestWideIndicesKeepTheirFlags() {
    const uint32_t indices[] = { 0x1FFFu, 0x2000u, 0xFFFFu, 0x10000u, 0x1FFFFFFEu, 0x1FFFFFFFu };

    for (uint32_t index : indices) {
        for (uint32_t flags = 0; flags < 8; flags++) {
            const uint32_t word = Unbound_PackVtxWord(index, flags);

            Check(UNBOUND_COLPOLY_VTX_INDEX(word) == index, "wide vertex index truncated");
            Check((word & UNBOUND_COLPOLY_FLAGS_MASK) >> UNBOUND_COLPOLY_INDEX_BITS == flags,
                  "flags lost next to a wide vertex index");
            Check(((word & UNBOUND_COLPOLY_CONVEYOR) != 0) == ((flags & 1u) != 0), "conveyor bit moved");
            for (uint32_t tested = 0; tested < 8; tested++) {
                Check((UNBOUND_COLPOLY_FLAG_TEST(word, tested) != 0) == ((flags & tested) != 0),
                      "xpFlags test disagrees with the packed flags");
            }
        }
    }
    Check(Unbound_PackVtxWord(0x20000000u, 0) == 0, "vertex index above 29 bits leaked into the flags");
    Check(Unbound_PackVtxWord(0, 8) == 0, "flags above 3 bits leaked into the word");
}

void TestDynaRelocationKeepsFlags() {
    // DynaPoly_ExpandSRT: (COLPOLY_VTX_INDEX(word) + vtxStartIndex) | (word & COLPOLY_VIA_FLAGS_MASK)
    const uint32_t word = Unbound_UnpackLegacyVtxWord(0xDFFFu); // vertex 8191, flags 6
    const uint32_t vtxStartIndex = 16384;
    const uint32_t relocated = (UNBOUND_COLPOLY_VTX_INDEX(word) + vtxStartIndex) | (word & UNBOUND_COLPOLY_FLAGS_MASK);

    Check(UNBOUND_COLPOLY_VTX_INDEX(word) == 8191, "relocation fixture has the wrong vertex");
    Check(UNBOUND_COLPOLY_VTX_INDEX(relocated) == 8191 + vtxStartIndex, "relocated vertex index truncated");
    Check((relocated & UNBOUND_COLPOLY_FLAGS_MASK) == (word & UNBOUND_COLPOLY_FLAGS_MASK),
          "relocation changed the poly flags");
}

void TestNodeTablesGrowPastTheirCapacity() {
    const uint32_t staticMax = std::numeric_limits<uint32_t>::max();
    const uint32_t dynaMax = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    const size_t nodeSize = 8; // sizeof(SSNode), pinned by UnboundCollisionChecks.cpp

    Check(Unbound_GrowNodeCapacity(16384, 16384, dynaMax, nodeSize) == 32768, "dyna node table did not double");
    Check(Unbound_GrowNodeCapacity(4096, 100000, staticMax, nodeSize) == 131072,
          "static node table did not reach the requested index");
    Check(Unbound_GrowNodeCapacity(0, 0, staticMax, nodeSize) == 1, "empty node table cannot grow");
    for (uint32_t index = 0; index < 200000; index += 997) {
        const uint32_t capacity = Unbound_GrowNodeCapacity(4096, index, staticMax, nodeSize);

        Check(capacity > index && capacity >= 4096 && capacity % 4096 == 0, "grown capacity does not cover the index");
    }
    Check(Unbound_GrowNodeCapacity(1u << 30, 1u << 30, staticMax, nodeSize) == (1u << 31),
          "static node table stopped growing before 32 bits");
    Check(Unbound_GrowNodeCapacity(1u << 31, 1u << 31, staticMax, nodeSize) == 0,
          "static node capacity wrapped past 32 bits");
    Check(Unbound_GrowNodeCapacity(16, 0xFFFFFFFFu, staticMax, nodeSize) == 0, "SS_NULL accepted as a node index");
    Check(Unbound_GrowNodeCapacity(1u << 30, 1u << 30, dynaMax, nodeSize) == 0, "dyna node capacity exceeded s32");
    Check(Unbound_GrowNodeCapacity(16, 16, staticMax, std::numeric_limits<size_t>::max() / 16) == 0,
          "node table byte size overflowed");
    Check(Unbound_GrowNodeCapacity(16, 16, staticMax, 0) == 0, "zero-sized nodes accepted");
}

} // namespace

int main() {
    TestLegacyWordsUnpackLosslessly();
    TestWideIndicesKeepTheirFlags();
    TestDynaRelocationKeepsFlags();
    TestNodeTablesGrowPastTheirCapacity();
    std::cout << "oot_unbound_collision_tests: ok\n";
    return 0;
}
