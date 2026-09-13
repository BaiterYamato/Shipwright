#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "shiplua/input/HotkeyRegistry.h"

namespace ShipLuaHost {

class OotHotkeyRegistry final : public ShipLua::HotkeyRegistry {
  public:
    bool Register(const ShipLua::HotkeyBinding& binding, std::function<void()> onFire) override;
    void UnregisterMod(const std::string& modId) override;
    void Fire(const std::string& modId, const std::string& id) override;
    std::vector<ShipLua::HotkeyBinding> Registered() const override;

    void DispatchScancode(int32_t scancode);

  private:
    struct Binding {
        ShipLua::HotkeyBinding metadata;
        std::function<void()> onFire;
        std::string scancodeCvar;
        std::string enabledCvar;
        int32_t defaultScancode = -1;
    };

    static int32_t ParseKeyName(const std::string& name);
    std::vector<Binding> mBindings;
};

} // namespace ShipLuaHost
