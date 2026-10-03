#ifndef LINKSPAN_NEI_ITEM_MAPPING_H
#define LINKSPAN_NEI_ITEM_MAPPING_H
#include <stdint.h>
#include <string.h>

// Runtime ids remain bytes; fork inventory ids may be u16. Never truncate an EXT id.
typedef struct NeiItemMapping { uint16_t logical[256]; } NeiItemMapping;
static inline void NeiMapping_Clear(NeiItemMapping* map) { memset(map, 0, sizeof(*map)); }
static inline void NeiMapping_Set(NeiItemMapping* map, uint8_t runtime, uint16_t logical) {
    map->logical[runtime] = logical;
}
static inline uint16_t NeiMapping_Logical(const NeiItemMapping* map, uint8_t runtime) {
    return map->logical[runtime];
}
static inline uint8_t NeiMapping_Runtime(const NeiItemMapping* map, uint16_t logical, uint8_t missing) {
    if (!logical) return missing;
    for (unsigned i = 0; i < 256; ++i) if (map->logical[i] == logical) return (uint8_t)i;
    return missing;
}
#endif
