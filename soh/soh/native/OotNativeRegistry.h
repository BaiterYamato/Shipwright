#pragma once

#include <thread>

#include "oot_registry.h"

namespace ShipLuaHost {

void InitializeOotNativeRegistry(std::thread::id ownerThread = std::this_thread::get_id());
void ResetOotNativeRegistry();
const ShipOotRegistryV1& GetOotNativeRegistryService();

} // namespace ShipLuaHost
