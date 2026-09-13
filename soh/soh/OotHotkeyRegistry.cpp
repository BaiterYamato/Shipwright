#include "OotHotkeyRegistry.h"

#include <algorithm>
#include <iterator>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/controller/controldevice/controller/mapping/keyboard/KeyboardScancodes.h>
#include <spdlog/spdlog.h>

namespace ShipLuaHost {
namespace {

struct KeyNameEntry {
    const char* name;
    Ship::KbScancode scancode;
};

constexpr KeyNameEntry kKeyNames[] = {
    { "A", Ship::KbScancode::LUS_KB_A },
    { "B", Ship::KbScancode::LUS_KB_B },
    { "C", Ship::KbScancode::LUS_KB_C },
    { "D", Ship::KbScancode::LUS_KB_D },
    { "E", Ship::KbScancode::LUS_KB_E },
    { "F", Ship::KbScancode::LUS_KB_F },
    { "G", Ship::KbScancode::LUS_KB_G },
    { "H", Ship::KbScancode::LUS_KB_H },
    { "I", Ship::KbScancode::LUS_KB_I },
    { "J", Ship::KbScancode::LUS_KB_J },
    { "K", Ship::KbScancode::LUS_KB_K },
    { "L", Ship::KbScancode::LUS_KB_L },
    { "M", Ship::KbScancode::LUS_KB_M },
    { "N", Ship::KbScancode::LUS_KB_N },
    { "O", Ship::KbScancode::LUS_KB_O },
    { "P", Ship::KbScancode::LUS_KB_P },
    { "Q", Ship::KbScancode::LUS_KB_Q },
    { "R", Ship::KbScancode::LUS_KB_R },
    { "S", Ship::KbScancode::LUS_KB_S },
    { "T", Ship::KbScancode::LUS_KB_T },
    { "U", Ship::KbScancode::LUS_KB_U },
    { "V", Ship::KbScancode::LUS_KB_V },
    { "W", Ship::KbScancode::LUS_KB_W },
    { "X", Ship::KbScancode::LUS_KB_X },
    { "Y", Ship::KbScancode::LUS_KB_Y },
    { "Z", Ship::KbScancode::LUS_KB_Z },
    { "0", Ship::KbScancode::LUS_KB_0 },
    { "1", Ship::KbScancode::LUS_KB_1 },
    { "2", Ship::KbScancode::LUS_KB_2 },
    { "3", Ship::KbScancode::LUS_KB_3 },
    { "4", Ship::KbScancode::LUS_KB_4 },
    { "5", Ship::KbScancode::LUS_KB_5 },
    { "6", Ship::KbScancode::LUS_KB_6 },
    { "7", Ship::KbScancode::LUS_KB_7 },
    { "8", Ship::KbScancode::LUS_KB_8 },
    { "9", Ship::KbScancode::LUS_KB_9 },
    { "F1", Ship::KbScancode::LUS_KB_F1 },
    { "F2", Ship::KbScancode::LUS_KB_F2 },
    { "F3", Ship::KbScancode::LUS_KB_F3 },
    { "F4", Ship::KbScancode::LUS_KB_F4 },
    { "F5", Ship::KbScancode::LUS_KB_F5 },
    { "F6", Ship::KbScancode::LUS_KB_F6 },
    { "F7", Ship::KbScancode::LUS_KB_F7 },
    { "F8", Ship::KbScancode::LUS_KB_F8 },
    { "F9", Ship::KbScancode::LUS_KB_F9 },
    { "F10", Ship::KbScancode::LUS_KB_F10 },
    { "F11", Ship::KbScancode::LUS_KB_F11 },
    { "F12", Ship::KbScancode::LUS_KB_F12 },
    { "Space", Ship::KbScancode::LUS_KB_SPACE },
    { "Enter", Ship::KbScancode::LUS_KB_ENTER },
    { "Tab", Ship::KbScancode::LUS_KB_TAB },
    { "Shift", Ship::KbScancode::LUS_KB_SHIFT },
    { "Control", Ship::KbScancode::LUS_KB_CONTROL },
    { "Alt", Ship::KbScancode::LUS_KB_ALT },
    { "Up", Ship::KbScancode::LUS_KB_ARROWKEY_UP },
    { "Down", Ship::KbScancode::LUS_KB_ARROWKEY_DOWN },
    { "Left", Ship::KbScancode::LUS_KB_ARROWKEY_LEFT },
    { "Right", Ship::KbScancode::LUS_KB_ARROWKEY_RIGHT },
};

std::string CvarPrefix(const ShipLua::HotkeyBinding& binding) {
    return "gShipLua.Hotkey." + binding.modId + "." + binding.id;
}

} // namespace

int32_t OotHotkeyRegistry::ParseKeyName(const std::string& name) {
    const auto found = std::find_if(std::begin(kKeyNames), std::end(kKeyNames),
                                    [&](const KeyNameEntry& entry) { return name == entry.name; });
    return found == std::end(kKeyNames) ? -1 : static_cast<int32_t>(found->scancode);
}

bool OotHotkeyRegistry::Register(const ShipLua::HotkeyBinding& binding, std::function<void()> onFire) {
    const int32_t defaultScancode = binding.defaultKey.empty() ? -1 : ParseKeyName(binding.defaultKey);
    if (!binding.defaultKey.empty() && defaultScancode < 0) {
        SPDLOG_WARN("ShipLua [{}]: hotkey '{}': tecla padrão desconhecida '{}'", binding.modId, binding.id,
                    binding.defaultKey);
        return false;
    }

    const std::string prefix = CvarPrefix(binding);
    const auto found = std::find_if(mBindings.begin(), mBindings.end(), [&](const Binding& current) {
        return current.metadata.modId == binding.modId && current.metadata.id == binding.id;
    });
    Binding replacement{ binding, std::move(onFire), prefix + ".Scancode", prefix + ".Enabled", defaultScancode };
    if (found != mBindings.end()) {
        *found = std::move(replacement);
    } else {
        mBindings.push_back(std::move(replacement));
    }
    SPDLOG_INFO("ShipLua [{}]: hotkey '{}' registrada (default='{}')", binding.modId, binding.id, binding.defaultKey);
    return true;
}

void OotHotkeyRegistry::UnregisterMod(const std::string& modId) {
    std::erase_if(mBindings, [&](const Binding& binding) { return binding.metadata.modId == modId; });
}

void OotHotkeyRegistry::Fire(const std::string& modId, const std::string& id) {
    const auto found = std::find_if(mBindings.begin(), mBindings.end(), [&](const Binding& binding) {
        return binding.metadata.modId == modId && binding.metadata.id == id;
    });
    if (found != mBindings.end() && found->onFire) {
        const std::function<void()> callback = found->onFire;
        callback();
    }
}

std::vector<ShipLua::HotkeyBinding> OotHotkeyRegistry::Registered() const {
    std::vector<ShipLua::HotkeyBinding> result;
    result.reserve(mBindings.size());
    for (const Binding& binding : mBindings) {
        result.push_back(binding.metadata);
    }
    return result;
}

void OotHotkeyRegistry::DispatchScancode(int32_t scancode) {
    if (scancode <= 0) {
        return;
    }
    std::vector<std::function<void()>> callbacks;
    for (const Binding& binding : mBindings) {
        if (!binding.onFire || CVarGetInteger(binding.enabledCvar.c_str(), 1) == 0) {
            continue;
        }
        const int32_t configured = CVarGetInteger(binding.scancodeCvar.c_str(), binding.defaultScancode);
        if (configured >= 0 && configured == scancode) {
            callbacks.push_back(binding.onFire);
        }
    }
    for (const auto& callback : callbacks) {
        callback();
    }
}

} // namespace ShipLuaHost
