#pragma once

#include <string_view>
#include <thread>
#include "oot_actor_models.h"

namespace ShipLuaHost {
void InitializeOotNativeActorModels(std::thread::id ownerThread);
const ShipOotActorModelsV1& GetOotNativeActorModelsService();
void ReleaseOotActorModelOwner(std::string_view owner);
}
