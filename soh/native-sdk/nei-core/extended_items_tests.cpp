#include <cstdio>
#include <cstdlib>
#include "fork/item_mapping.h"
#include "fork/progression.h"
static int failures;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); ++failures; } } while (0)
int main() {
    NeiItemMapping map{};
    NeiMapping_Set(&map, 0xA0, 0x9E);
    NeiMapping_Set(&map, 0xA1, 0x0220);
    NeiMapping_Set(&map, 0xA2, 0x0221);
    NeiMapping_Set(&map, 0xA3, 0x0223);
    CHECK(NeiMapping_Logical(&map, 0xA1) == 0x0220);
    CHECK(NeiMapping_Runtime(&map, 0x0220, 0xFF) == 0xA1);
    CHECK(NeiMapping_Runtime(&map, 0x20, 0xFF) == 0xFF); // no truncation/vanilla collision
    CHECK(NeiMapping_Runtime(&map, 0, 0xFF) == 0xFF);
    CHECK(NeiMapping_Logical(&map, 0xA4) == 0); // foreign mod id remains foreign
    NeiMapping_Set(&map, 0xA1, 0x0223);
    CHECK(NeiMapping_Runtime(&map, 0x0220, 0xFF) == 0xFF);
    NeiMapping_Clear(&map);
    CHECK(NeiMapping_Logical(&map, 0xA1) == 0);
    for (unsigned family = 0; family < NEI_POWER_COUNT; ++family) {
        uint8_t mask = 0;
        for (unsigned i = 0; i < NeiProgress_Limit(family); ++i) mask = NeiProgress_NextMask(family, i, mask);
        CHECK(mask == NeiProgress_ValidMask(family));
        for (unsigned i = 0; i < NeiProgress_Limit(family); ++i) mask = NeiProgress_NextMask(family, i, mask);
        CHECK(mask == 0);
        CHECK(NeiProgress_NextMask(family, NeiProgress_Limit(family), mask) == -1);
    }
    CHECK(NeiProgress_Limit(NEI_POWER_SLATE) == 5);
    CHECK(NeiProgress_NextMask(99, 0, 0) == -1);
    CHECK(NeiProgress_NextMask(NEI_POWER_SLATE, 0, 0xFF) == 0xFE); // preserve unavailable/imported flags
    std::puts(failures ? "extended items: FAILED" : "extended items: ok");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
