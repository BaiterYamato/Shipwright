#include "../example/item_icons.h"
#include "dmr_public_fixture.h"
#include <cstdlib>

int main() {
    const void* icons[256]{};
    FillDmrPublicIcons(icons);
    uint8_t items[4]{ 0, 3, 0xFF, 10 };
    if (ItemSelectorIcons(1, items, icons) != "1;1:3:textures/f5/bow\t2:255:\t3:10:textures/f5/hookshot")
        return EXIT_FAILURE;
    items[1] = 0xA2;
    items[2] = 7;
    if (ItemSelectorIcons(3, items, icons) !=
        "3;1:162:textures/f5/extended\t2:7:textures/f5/ocarina\t3:10:textures/f5/hookshot")
        return EXIT_FAILURE;
    // A mesma lógica real precisa distinguir o fallback e o slot vazio.
    if (ItemSelectorIcons(3, items, nullptr) != "3;1:162:\t2:7:\t3:10:")
        return EXIT_FAILURE;
    icons[3] = nullptr;
    items[1] = 3;
    return ItemSelectorIcons(1, items, icons).starts_with("1;1:3:\t2:7:textures/f5/ocarina") ? EXIT_SUCCESS
                                                                                             : EXIT_FAILURE;
}
