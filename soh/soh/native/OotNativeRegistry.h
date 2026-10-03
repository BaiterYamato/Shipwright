#pragma once

#include <thread>
#include <string_view>

#include "oot_registry.h"

namespace ShipLuaHost {

void InitializeOotNativeRegistry(std::thread::id ownerThread = std::this_thread::get_id());
void ResetOotNativeRegistry();
const ShipOotRegistryV1& GetOotNativeRegistryService();
const ShipOotRegistryV2& GetOotNativeRegistryServiceV2();
void ReleaseOotRegistryOwner(std::string_view owner);

} // namespace ShipLuaHost
