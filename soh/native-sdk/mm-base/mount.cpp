#include "mount.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace MmBase {
ShipNativeStatus SHIP_NATIVE_CALL Transform(void*, const char*, const uint8_t* data, size_t size,
                                            ShipNativeWriteFn write, void* writer) {
    if ((!data && size) || !write)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (size > UINT32_MAX)
        return SHIP_NATIVE_LIMIT;
    if (size < 0x48 || std::memcmp(data + 4, "TLDO", 4) != 0)
        return write(writer, reinterpret_cast<const char*>(data), static_cast<uint32_t>(size));
    // Tradução do MmCrossWorldArchive: apenas neste mount, inclusive aliases.
    std::vector<uint8_t> bytes(data, data + size);
    for (size_t i = 0x40; i + 8 <= size;) {
        const uint8_t opcode = bytes[i + 3];
        size_t advance = 8;
        switch (opcode) {
            case 0x20:
            case 0x24:
            case 0x25:
            case 0x27:
            case 0x29:
            case 0x31:
            case 0x32:
            case 0x33:
            case 0x35:
            case 0x36:
            case 0x42:
                advance = 16;
                break;
            case 0x3D:
            case 0x43:
                std::memset(bytes.data() + i, 0, 8);
                break;
            case 0x44:
                bytes[i + 3] = 0x45;
                break;
            case 0x45:
                bytes[i + 3] = 0x46;
                break;
            default:
                break;
        }
        i += advance;
        if (opcode == 0xDF)
            break;
    }
    return write(writer, reinterpret_cast<const char*>(bytes.data()), static_cast<uint32_t>(bytes.size()));
}

ShipNativeStatus Mount(const ShipOotResourcesV4* resources, uint64_t* handle, ShipOotMountInfoV1* info) {
    namespace fs = std::filesystem;
    *handle = 0;
    std::error_code ec;
    std::vector<fs::path> candidates;
    if (const char* root = std::getenv("SHIPLUA_MM_ROOT"); root && *root) {
        candidates.emplace_back(root); // Exclusivo, como no bootstrap anterior.
    } else {
        const auto cwd = fs::current_path(ec);
        if (!ec) {
            candidates = { cwd,
                           cwd / "hosts/mm",
                           cwd.parent_path(),
                           cwd.parent_path() / "mm",
                           cwd.parent_path() / "MM",
                           cwd.parent_path().parent_path() / "hosts/mm" };
        }
        fs::path runtimeRoot;
        if (const char* root = std::getenv("LINKSPAN_ROOT"); root && *root) {
            runtimeRoot = root;
        } else {
#ifdef _WIN32
            wchar_t executable[32768];
            const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
            if (length && length < 32768)
                runtimeRoot = fs::path(std::wstring(executable, length)).parent_path();
#endif
        }
        if (!runtimeRoot.empty()) {
            candidates.insert(candidates.end(),
                              { runtimeRoot, runtimeRoot / "hosts/mm", runtimeRoot.parent_path(),
                                runtimeRoot.parent_path() / "mm", runtimeRoot.parent_path().parent_path(),
                                runtimeRoot.parent_path().parent_path() / "hosts/mm" });
        }
    }
    fs::path archive;
    for (const auto& candidate : candidates) {
        const auto probe = candidate / "mm.o2r";
        if (fs::is_regular_file(probe, ec)) {
            archive = probe;
            break;
        }
    }
    if (archive.empty())
        return SHIP_NATIVE_UNSUPPORTED;
    archive = fs::absolute(archive, ec);
    if (ec)
        return SHIP_NATIVE_FAILURE;
    const std::string path = archive.generic_string();
    const ShipOotMountAliasV1 aliases[] = { { sizeof(ShipOotMountAliasV1), 0, "objects/", "objects/" },
                                            { sizeof(ShipOotMountAliasV1), 0, "textures/", "textures/" } };
    const ShipOotPrefixedArchiveSpecV1 spec{
        sizeof(spec), 0, "linkspan.mm.base", path.c_str(), "mm/", 0, 0, aliases, 2, 0, Transform, nullptr
    };
    return resources->mount_archive_prefixed(&spec, handle, info);
}
} // namespace MmBase
