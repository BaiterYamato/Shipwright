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
void OpenLogWindow();

} // namespace ShipLuaHost
