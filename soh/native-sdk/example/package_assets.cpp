#include "package_assets.h"

#include <filesystem>
#include <system_error>

// Isolado do provider.cpp: as macros do windows.h colidem com os headers do jogo.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

std::string ProviderAssetsDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ProviderAssetsDirectory), &module)) {
        return {};
    }
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    try {
        const std::filesystem::path assets = std::filesystem::path(path).parent_path().parent_path() / "assets";
        std::error_code error;
        // mount_archive recebe char* na página de código ativa, como std::filesystem no Windows.
        return std::filesystem::is_directory(assets, error) ? assets.string() : std::string{};
    } catch (const std::exception&) {
        return {};
    }
}
