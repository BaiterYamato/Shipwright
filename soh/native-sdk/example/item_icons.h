#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// The same live texture paths as the game's HUD, including registered mod items
// and upgrades. Keep empty slots in place so the selector never changes width.
inline std::string ItemSelectorIcons(uint8_t selected, const uint8_t* items, const void* const* icons) {
    std::string result = std::to_string(selected) + ";";
    for (uint8_t button = 1; button <= 3; ++button) {
        if (button != 1) result += '\t';
        result += std::to_string(button) + ":" + std::to_string(items[button]) + ":";
        const char* path = items[button] != 0xFF && icons
                               ? static_cast<const char*>(icons[items[button]]) : nullptr;
        if (!path) continue;
        std::string_view icon(path);
        if (icon.starts_with("__OTR__")) icon.remove_prefix(7);
        result += icon;
    }
    return result;
}
