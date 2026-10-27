#pragma once

#include <shiplua/native/ship_native_abi.h>
#include "oot_actors.h"
#include "oot_engine.h"
#include "oot_resources.h"

extern "C" {
#include "functions.h"
#include "variables.h"
}

namespace ShipLuaHost {

struct PuppetBindings {
    const ShipNativeRuntime* runtime = nullptr;
    const ShipOotEngineV1* engine = nullptr;
    const ShipOotActorsV1* actors = nullptr;
    const ShipOotResourcesV1* resources = nullptr;
    decltype(&::Actor_SetScale) Actor_SetScale = nullptr;
    decltype(&::DmaMgr_SendRequest1) DmaMgr_SendRequest1 = nullptr;
    decltype(&::Gfx_SetupDL_25Opa) Gfx_SetupDL_25Opa = nullptr;
    decltype(&::LinkAnimation_Update) LinkAnimation_Update = nullptr;
    decltype(&::Player_DrawImpl) Player_DrawImpl = nullptr;
    decltype(&::SkelAnime_InitLink) SkelAnime_InitLink = nullptr;
    decltype(&::func_80093C80) func_80093C80 = nullptr;
    decltype(&::gSPSegment) gSPSegment = nullptr;
    s16* gLinkObjectIds = nullptr;
    RomFile* gObjectTable = nullptr;
    FlexSkeletonHeader** gPlayerSkelHeaders = nullptr;
    uintptr_t* gSegments = nullptr;
    s32 (*Player_OverrideLimbDrawPause)(PlayState*, s32, Gfx**, Vec3f*, Vec3s*, void*) = nullptr;
    void (*FrameInterpolation_RecordOpenChild)(const void*, int) = nullptr;
    void (*FrameInterpolation_RecordCloseChild)() = nullptr;
};

extern PuppetBindings gHost;
bool StatueAvailable();

} // namespace ShipLuaHost
