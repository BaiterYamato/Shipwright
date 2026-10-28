#pragma once

#include <filesystem>
#include <vector>

namespace ShipLua {
class ModHost;
}

namespace ShipLuaHost {

class OotActorProvider;
class OotHotkeyRegistry;
class OotWorldAdapter;

// RFC 0032: pastas de --linkspan-tool, guardadas antes do Initialize. Caminho vazio = flag sem pasta.
void SetToolDirectories(std::vector<std::filesystem::path> directories);
void Initialize();
void Shutdown();
ShipLua::ModHost* GetModHost();
OotActorProvider* ActorProvider();
OotHotkeyRegistry* Hotkeys();
OotWorldAdapter* WorldAdapter();
} // namespace ShipLuaHost

namespace SohGui {
class SohMenuModRegistry;
}

namespace ShipLuaHost {
SohGui::SohMenuModRegistry* ModMenuRegistry();
void OpenLogWindow();

} // namespace ShipLuaHost
