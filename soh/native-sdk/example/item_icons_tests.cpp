#include "item_icons.h"
#include <array>
#include <cstdlib>

int main() {
    std::array<const void*, 256> icons{};
    uint8_t items[4] = { 0, 0xA2, 7, 0xFF };
    icons[0xA2] = "__OTR__textures/nei/gDekuLeafIconTex";
    icons[7] = "__OTR__textures/icon_item_static/gItemIconOcarinaFairyTex";
    if (ItemSelectorIcons(1, items, icons.data()) !=
        "1;1:162:textures/nei/gDekuLeafIconTex\t2:7:textures/icon_item_static/gItemIconOcarinaFairyTex\t3:255:")
        return EXIT_FAILURE;
    // An upgrade changes the texture at the same runtime ID immediately.
    icons[0xA2] = "__OTR__textures/nei/gRocsCapeIconTex";
    if (ItemSelectorIcons(2, items, icons.data()).find("gRocsCapeIconTex") == std::string::npos)
        return EXIT_FAILURE;
    icons[0xA2] = nullptr;
    if (ItemSelectorIcons(2, items, icons.data()).find("1:162:\t2:7:") == std::string::npos)
        return EXIT_FAILURE;
    items[1] = items[2] = 0xFF;
    return ItemSelectorIcons(1, items, nullptr) == "1;1:255:\t2:255:\t3:255:"
               ? EXIT_SUCCESS : EXIT_FAILURE;
}
