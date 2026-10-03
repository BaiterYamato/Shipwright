#ifndef LINKSPAN_NEI_PROGRESSION_H
#define LINKSPAN_NEI_PROGRESSION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Independent OoT powers; Sensor queries the current randomizer seed.
enum { NEI_POWER_WAND, NEI_POWER_SLATE, NEI_POWER_SEASONS, NEI_POWER_CANE, NEI_POWER_COUNT };
static inline uint8_t NeiProgress_Limit(unsigned family) {
    static const uint8_t limits[] = { 6, 5, 4, 6 };
    return family < NEI_POWER_COUNT ? limits[family] : 0;
}
static inline uint8_t NeiProgress_ValidMask(unsigned family) {
    const unsigned count = NeiProgress_Limit(family);
    return count ? (uint8_t)((1u << count) - 1u) : 0;
}
static inline int NeiProgress_NextMask(unsigned family, unsigned index, uint8_t current) {
    return index < NeiProgress_Limit(family)
               ? (current ^ (1u << index)) : -1;
}
uint8_t NeiProgress_GetMask(unsigned family);
void NeiProgress_SetMask(unsigned family, uint8_t mask);
const char* NeiProgress_ItemId(unsigned family);
uint32_t NeiProgress_Catalog(char* output, uint32_t capacity);
#ifdef __cplusplus
}
#endif
#endif
