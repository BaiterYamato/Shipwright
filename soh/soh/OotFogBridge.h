#ifndef LINKSPAN_OOT_FOG_BRIDGE_H
#define LINKSPAN_OOT_FOG_BRIDGE_H

#include "fast/fog_volume.h"

#ifdef __cplusplus
#include "../native-sdk/include/oot_fog.h"
namespace ShipLuaHost {
const ShipOotFogV1& GetOotNativeFogService();
const ShipOotFogV2& GetOotNativeFogFlowService();
}
extern "C" {
#endif
void LinkSpan_BeginFogFrame(void* play_state);
void LinkSpan_EndFogFrame(void);
void LinkSpan_ResetFogScene(void);
int LinkSpan_TakeFogGrid(void* play_state, FastFogVolume* volume, const FastFogFlow** flow);
#ifdef __cplusplus
}
#endif

#endif
