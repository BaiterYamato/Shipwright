#pragma once

namespace ShipLua {
class ModHost;
}

namespace ShipLuaHost {

class OotActorProvider;
class OotHotkeyRegistry;
class OotWorldAdapter;

void Initialize();
void Shutdown();
ShipLua::ModHost* GetModHost();
OotActorProvider* ActorProvider();
OotHotkeyRegistry* Hotkeys();
OotWorldAdapter* WorldAdapter();
} // namespace ShipLuaHost

namespace SohGui { class SohMenuModRegistry; }

namespace ShipLuaHost {
SohGui::SohMenuModRegistry* ModMenuRegistry();
void OpenLogWindow();

} // namespace ShipLuaHost
