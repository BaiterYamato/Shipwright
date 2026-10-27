#pragma once
#include "oot_resources.h"

namespace MmBase {
ShipNativeStatus Mount(const ShipOotResourcesV4* resources, uint64_t* handle, ShipOotMountInfoV1* info);
ShipNativeStatus SHIP_NATIVE_CALL Transform(void*, const char*, const uint8_t*, size_t, ShipNativeWriteFn, void*);
} // namespace MmBase
