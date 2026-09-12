#pragma once
#include <shiplua/native/NativeProvider.h>
#include "oot_resources.h"

namespace ShipLuaHost {
struct OotNativeGamepadBridge {
    uint8_t (*hasGamepad)(uint8_t port) = nullptr;
    uint32_t (*getButtons)(uint8_t port) = nullptr;
    ShipNativeStatus (*clearButtonBindings)(uint8_t port, uint16_t virtualButton) = nullptr;
    ShipNativeStatus (*bindButton)(uint8_t port, uint16_t virtualButton, uint8_t sdlButton) = nullptr;
    ShipNativeStatus (*reloadMappings)(uint8_t port) = nullptr;
    int32_t (*getSettingInt)(const char* name, int32_t fallback) = nullptr;
    ShipNativeStatus (*setSettingInt)(const char* name, int32_t value) = nullptr;
};
struct OotNativeResourceBridge {
    uint8_t (*hasFile)(const char* path) = nullptr;
    ShipNativeStatus (*readFile)(const char* path, uint8_t* output, uint32_t capacity,
                                 uint32_t* outputSize) = nullptr;
    ShipNativeStatus (*listFiles)(const char* searchMask, ShipOotResourcePathFn callback, void* user) = nullptr;
    ShipNativeStatus (*dirtyResources)(const char* searchMask) = nullptr;
    ShipNativeStatus (*unloadResource)(const char* path) = nullptr;
    ShipNativeStatus (*mountArchive)(const char* archivePath, uint64_t* handle) = nullptr;
    ShipNativeStatus (*unmountArchive)(uint64_t handle) = nullptr;
    ShipNativeStatus (*getGameVersions)(uint32_t* output, uint32_t capacity, uint32_t* outputCount) = nullptr;
    ShipNativeStatus (*readFileLayers)(const char* path, ShipOotResourceLayerFn callback, void* user) = nullptr;
};
void SetOotNativeGamepadBridge(OotNativeGamepadBridge bridge);
void SetOotNativeResourceBridge(OotNativeResourceBridge bridge);
ShipLua::NativeProviderPolicy CreateOotNativePolicy();
}
