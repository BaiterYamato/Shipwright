#pragma once
#include <shiplua/native/NativeProvider.h>
#include "oot_resources.h"
#include <vector>
#include <utility>
#include <string>

namespace ShipLuaHost {
struct OotNativeGamepadBridge {
    uint8_t (*hasGamepad)(uint8_t port) = nullptr;
    uint32_t (*getButtons)(uint8_t port) = nullptr;
    ShipNativeStatus (*clearButtonBindings)(uint8_t port, uint16_t virtualButton) = nullptr;
    ShipNativeStatus (*bindButton)(uint8_t port, uint16_t virtualButton, uint8_t sdlButton) = nullptr;
    ShipNativeStatus (*reloadMappings)(uint8_t port) = nullptr;
    int32_t (*getSettingInt)(const char* name, int32_t fallback) = nullptr;
    ShipNativeStatus (*setSettingInt)(const char* name, int32_t value) = nullptr;
    int16_t (*getAxis)(uint8_t port, uint8_t sdlAxis) = nullptr;
    ShipNativeStatus (*bindAxis)(uint8_t port, uint16_t virtualButton, uint8_t sdlAxis, int8_t direction) = nullptr;
};
bool InOotArchiveTransform();
struct OotArchiveTransformScope {
    OotArchiveTransformScope();
    ~OotArchiveTransformScope();
};

// HOST-SLIM-001 (RFC 0029): pedido de linkspan.oot.resources V4, validado e copiado pelo engine.
struct OotPrefixedMountRequest {
    std::string owner;
    std::string archivePath;
    std::string prefix; // relativo, com '/' final
    // Guardado. A resolução das referências internas pelo prefixo entra na F3.
    bool strict = false;
    std::vector<std::pair<std::string, std::string>> aliases; // source_prefix, target_prefix ("" = raiz)
    ShipOotArchiveTransformFn transform = nullptr;
    void* transformUser = nullptr;
};

struct OotNativeResourceBridge {
    uint8_t (*hasFile)(const char* path) = nullptr;
    ShipNativeStatus (*readFile)(const char* path, uint8_t* output, uint32_t capacity, uint32_t* outputSize) = nullptr;
    ShipNativeStatus (*listFiles)(const char* searchMask, ShipOotResourcePathFn callback, void* user) = nullptr;
    ShipNativeStatus (*dirtyResources)(const char* searchMask) = nullptr;
    ShipNativeStatus (*unloadResource)(const char* path) = nullptr;
    ShipNativeStatus (*mountArchive)(const char* archivePath, uint64_t* handle) = nullptr;
    ShipNativeStatus (*unmountArchive)(uint64_t handle) = nullptr;
    ShipNativeStatus (*getGameVersions)(uint32_t* output, uint32_t capacity, uint32_t* outputCount) = nullptr;
    ShipNativeStatus (*readFileLayers)(const char* path, ShipOotResourceLayerFn callback, void* user) = nullptr;
    // V4: archive sob `prefix`. O handle é do espaço do mountArchive; unmountArchive desmonta os dois.
    ShipNativeStatus (*mountPrefixed)(const OotPrefixedMountRequest& request, uint64_t* handle,
                                      ShipOotMountInfoV1* info) = nullptr;
};
void SetOotNativeGamepadBridge(OotNativeGamepadBridge bridge);
void SetOotNativeResourceBridge(OotNativeResourceBridge bridge);
ShipLua::NativeProviderPolicy CreateOotNativePolicy();
} // namespace ShipLuaHost
