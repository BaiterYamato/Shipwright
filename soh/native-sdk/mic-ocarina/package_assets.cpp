#include "package_assets.h"
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::string MicAssetsDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&MicAssetsDirectory), &module)) return {};
    wchar_t path[MAX_PATH];
    const DWORD size = GetModuleFileNameW(module, path, MAX_PATH);
    if (!size || size >= MAX_PATH) return {};
    try {
        const auto assets = std::filesystem::path(path).parent_path().parent_path() / "assets";
        return std::filesystem::is_directory(assets) ? assets.string() : std::string{};
    } catch (...) { return {}; }
}
