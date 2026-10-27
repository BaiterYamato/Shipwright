#define MM_FORMS_BINDINGS_IMPL
#include "game_bindings.h"
#include <cstring>
namespace MMFormsGame {
namespace {
bool ready = false;
const ShipOotEngineV1* engine = nullptr;
const ShipOotResourcesV1* resources = nullptr;
const ShipOotRenderV3* render = nullptr;
} // namespace
PlayState* Play() {
    return engine ? static_cast<PlayState*>(engine->get_play_state()) : nullptr;
}
uint8_t HasFile(const char* path) {
    if (!path || !resources)
        return 0;
    return resources->has_file(std::strncmp(path, "__OTR__", 7) == 0 ? path + 7 : path);
}
int ResourceSignature(char* path) {
    return path && std::strncmp(path, "__OTR__", 7) == 0 && HasFile(path) ? 1 : 0;
}
InterpolationScope::InterpolationScope(const void* key, int child) : open(false) {
    open = render && render->interpolation_begin(key, child) == SHIP_NATIVE_OK;
}
InterpolationScope::~InterpolationScope() {
    if (open)
        render->interpolation_end();
}
decltype(p_Actor_SpawnFloorDustRing) p_Actor_SpawnFloorDustRing = nullptr;
decltype(p_Animation_GetLastFrame) p_Animation_GetLastFrame = nullptr;
decltype(p_AudioOcarina_GetPlayingStaff) p_AudioOcarina_GetPlayingStaff = nullptr;
decltype(p_AudioOcarina_SetInstrument) p_AudioOcarina_SetInstrument = nullptr;
decltype(p_BgCheck_EntityRaycastFloor3) p_BgCheck_EntityRaycastFloor3 = nullptr;
decltype(p_Camera_GetInputDirYaw) p_Camera_GetInputDirYaw = nullptr;
decltype(p_Collider_UpdateCylinder) p_Collider_UpdateCylinder = nullptr;
decltype(p_CollisionCheck_SetAT) p_CollisionCheck_SetAT = nullptr;
decltype(p_EffectSsBlast_SpawnWhiteShockwave) p_EffectSsBlast_SpawnWhiteShockwave = nullptr;
decltype(p_EffectSsHahen_SpawnBurst) p_EffectSsHahen_SpawnBurst = nullptr;
decltype(p_Gfx_SetupDL_25Xlu) p_Gfx_SetupDL_25Xlu = nullptr;
decltype(p_Gfx_SetupDL_44Xlu) p_Gfx_SetupDL_44Xlu = nullptr;
decltype(p_Gfx_TwoTexScroll) p_Gfx_TwoTexScroll = nullptr;
decltype(p_LightContext_InsertLight) p_LightContext_InsertLight = nullptr;
decltype(p_LightContext_RemoveLight) p_LightContext_RemoveLight = nullptr;
decltype(p_Lights_PointNoGlowSetInfo) p_Lights_PointNoGlowSetInfo = nullptr;
decltype(p_LinkAnimation_Change) p_LinkAnimation_Change = nullptr;
decltype(p_LinkAnimation_Update) p_LinkAnimation_Update = nullptr;
decltype(p_Magic_Reset) p_Magic_Reset = nullptr;
decltype(p_Math_AsymStepToF) p_Math_AsymStepToF = nullptr;
decltype(p_Math_Atan2S) p_Math_Atan2S = nullptr;
decltype(p_Math_CosS) p_Math_CosS = nullptr;
decltype(p_Math_ScaledStepToS) p_Math_ScaledStepToS = nullptr;
decltype(p_Math_SinS) p_Math_SinS = nullptr;
decltype(p_Math_StepToF) p_Math_StepToF = nullptr;
decltype(p_Math_StepToS) p_Math_StepToS = nullptr;
decltype(p_Math_StepUntilF) p_Math_StepUntilF = nullptr;
decltype(p_Math_Vec3f_Copy) p_Math_Vec3f_Copy = nullptr;
decltype(p_Matrix_NewMtx) p_Matrix_NewMtx = nullptr;
decltype(p_Matrix_Pop) p_Matrix_Pop = nullptr;
decltype(p_Matrix_Push) p_Matrix_Push = nullptr;
decltype(p_Matrix_RotateX) p_Matrix_RotateX = nullptr;
decltype(p_Matrix_RotateY) p_Matrix_RotateY = nullptr;
decltype(p_Matrix_RotateZYX) p_Matrix_RotateZYX = nullptr;
decltype(p_Matrix_Scale) p_Matrix_Scale = nullptr;
decltype(p_Matrix_Translate) p_Matrix_Translate = nullptr;
decltype(p_Play_TriggerVoidOut) p_Play_TriggerVoidOut = nullptr;
decltype(p_Player_PlaySfx) p_Player_PlaySfx = nullptr;
decltype(p_Quake_Add) p_Quake_Add = nullptr;
decltype(p_Quake_SetCountdown) p_Quake_SetCountdown = nullptr;
decltype(p_Quake_SetQuakeValues) p_Quake_SetQuakeValues = nullptr;
decltype(p_Quake_SetSpeed) p_Quake_SetSpeed = nullptr;
decltype(p_Rand_CenteredFloat) p_Rand_CenteredFloat = nullptr;
decltype(p_ResourceMgr_LoadAnimByName) p_ResourceMgr_LoadAnimByName = nullptr;
decltype(p_Rumble_Request) p_Rumble_Request = nullptr;
decltype(p_SkinMatrix_SetTranslateRotateYXZScale) p_SkinMatrix_SetTranslateRotateYXZScale = nullptr;
decltype(p_func_8002829C) p_func_8002829C = nullptr;
decltype(p_gSPDisplayList) p_gSPDisplayList = nullptr;
decltype(p_gSPSegment) p_gSPSegment = nullptr;
decltype(p_gSaveContext) p_gSaveContext = nullptr;
bool Bind(const ShipNativeRuntime* runtime) {
    Reset();
    engine = static_cast<const ShipOotEngineV1*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, 1, sizeof(ShipOotEngineV1)));
    resources = static_cast<const ShipOotResourcesV1*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, 1, sizeof(ShipOotResourcesV1)));
    render = static_cast<const ShipOotRenderV3*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_RENDER_SERVICE, 3, sizeof(ShipOotRenderV3)));
    if (!engine || engine->size < sizeof(*engine) || !engine->get_play_state || !engine->layout_id ||
        std::strcmp(engine->layout_id, MM_FORMS_LAYOUT_ID) != 0 || engine->play_state_size != sizeof(PlayState) ||
        engine->player_size != sizeof(Player) || engine->save_context_size != sizeof(SaveContext) || !resources ||
        resources->size < sizeof(*resources) || !resources->has_file || !render || render->size < sizeof(*render) ||
        !render->interpolation_begin || !render->interpolation_end) {
        Reset();
        return false;
    }
    uintptr_t address = 0;
    if (runtime->resolve_symbol(runtime->context, "Actor_SpawnFloorDustRing", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Actor_SpawnFloorDustRing = reinterpret_cast<decltype(p_Actor_SpawnFloorDustRing)>(address);
    if (runtime->resolve_symbol(runtime->context, "Animation_GetLastFrame", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Animation_GetLastFrame = reinterpret_cast<decltype(p_Animation_GetLastFrame)>(address);
    if (runtime->resolve_symbol(runtime->context, "AudioOcarina_GetPlayingStaff", &address) != SHIP_NATIVE_OK ||
        !address) {
        Reset();
        return false;
    }
    p_AudioOcarina_GetPlayingStaff = reinterpret_cast<decltype(p_AudioOcarina_GetPlayingStaff)>(address);
    if (runtime->resolve_symbol(runtime->context, "AudioOcarina_SetInstrument", &address) != SHIP_NATIVE_OK ||
        !address) {
        Reset();
        return false;
    }
    p_AudioOcarina_SetInstrument = reinterpret_cast<decltype(p_AudioOcarina_SetInstrument)>(address);
    if (runtime->resolve_symbol(runtime->context, "BgCheck_EntityRaycastFloor3", &address) != SHIP_NATIVE_OK ||
        !address) {
        Reset();
        return false;
    }
    p_BgCheck_EntityRaycastFloor3 = reinterpret_cast<decltype(p_BgCheck_EntityRaycastFloor3)>(address);
    if (runtime->resolve_symbol(runtime->context, "Camera_GetInputDirYaw", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Camera_GetInputDirYaw = reinterpret_cast<decltype(p_Camera_GetInputDirYaw)>(address);
    if (runtime->resolve_symbol(runtime->context, "Collider_UpdateCylinder", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Collider_UpdateCylinder = reinterpret_cast<decltype(p_Collider_UpdateCylinder)>(address);
    if (runtime->resolve_symbol(runtime->context, "CollisionCheck_SetAT", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_CollisionCheck_SetAT = reinterpret_cast<decltype(p_CollisionCheck_SetAT)>(address);
    if (runtime->resolve_symbol(runtime->context, "EffectSsBlast_SpawnWhiteShockwave", &address) != SHIP_NATIVE_OK ||
        !address) {
        Reset();
        return false;
    }
    p_EffectSsBlast_SpawnWhiteShockwave = reinterpret_cast<decltype(p_EffectSsBlast_SpawnWhiteShockwave)>(address);
    if (runtime->resolve_symbol(runtime->context, "EffectSsHahen_SpawnBurst", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_EffectSsHahen_SpawnBurst = reinterpret_cast<decltype(p_EffectSsHahen_SpawnBurst)>(address);
    if (runtime->resolve_symbol(runtime->context, "Gfx_SetupDL_25Xlu", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Gfx_SetupDL_25Xlu = reinterpret_cast<decltype(p_Gfx_SetupDL_25Xlu)>(address);
    if (runtime->resolve_symbol(runtime->context, "Gfx_SetupDL_44Xlu", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Gfx_SetupDL_44Xlu = reinterpret_cast<decltype(p_Gfx_SetupDL_44Xlu)>(address);
    if (runtime->resolve_symbol(runtime->context, "Gfx_TwoTexScroll", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Gfx_TwoTexScroll = reinterpret_cast<decltype(p_Gfx_TwoTexScroll)>(address);
    if (runtime->resolve_symbol(runtime->context, "LightContext_InsertLight", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_LightContext_InsertLight = reinterpret_cast<decltype(p_LightContext_InsertLight)>(address);
    if (runtime->resolve_symbol(runtime->context, "LightContext_RemoveLight", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_LightContext_RemoveLight = reinterpret_cast<decltype(p_LightContext_RemoveLight)>(address);
    if (runtime->resolve_symbol(runtime->context, "Lights_PointNoGlowSetInfo", &address) != SHIP_NATIVE_OK ||
        !address) {
        Reset();
        return false;
    }
    p_Lights_PointNoGlowSetInfo = reinterpret_cast<decltype(p_Lights_PointNoGlowSetInfo)>(address);
    if (runtime->resolve_symbol(runtime->context, "LinkAnimation_Change", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_LinkAnimation_Change = reinterpret_cast<decltype(p_LinkAnimation_Change)>(address);
    if (runtime->resolve_symbol(runtime->context, "LinkAnimation_Update", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_LinkAnimation_Update = reinterpret_cast<decltype(p_LinkAnimation_Update)>(address);
    if (runtime->resolve_symbol(runtime->context, "Magic_Reset", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Magic_Reset = reinterpret_cast<decltype(p_Magic_Reset)>(address);
    if (runtime->resolve_symbol(runtime->context, "Math_AsymStepToF", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Math_AsymStepToF = reinterpret_cast<decltype(p_Math_AsymStepToF)>(address);
    if (runtime->resolve_symbol(runtime->context, "Math_Atan2S", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Math_Atan2S = reinterpret_cast<decltype(p_Math_Atan2S)>(address);
    if (runtime->resolve_symbol(runtime->context, "Math_CosS", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Math_CosS = reinterpret_cast<decltype(p_Math_CosS)>(address);
    if (runtime->resolve_symbol(runtime->context, "Math_ScaledStepToS", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Math_ScaledStepToS = reinterpret_cast<decltype(p_Math_ScaledStepToS)>(address);
    if (runtime->resolve_symbol(runtime->context, "Math_SinS", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Math_SinS = reinterpret_cast<decltype(p_Math_SinS)>(address);
    if (runtime->resolve_symbol(runtime->context, "Math_StepToF", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Math_StepToF = reinterpret_cast<decltype(p_Math_StepToF)>(address);
    if (runtime->resolve_symbol(runtime->context, "Math_StepToS", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Math_StepToS = reinterpret_cast<decltype(p_Math_StepToS)>(address);
    if (runtime->resolve_symbol(runtime->context, "Math_StepUntilF", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Math_StepUntilF = reinterpret_cast<decltype(p_Math_StepUntilF)>(address);
    if (runtime->resolve_symbol(runtime->context, "Math_Vec3f_Copy", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Math_Vec3f_Copy = reinterpret_cast<decltype(p_Math_Vec3f_Copy)>(address);
    if (runtime->resolve_symbol(runtime->context, "Matrix_NewMtx", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Matrix_NewMtx = reinterpret_cast<decltype(p_Matrix_NewMtx)>(address);
    if (runtime->resolve_symbol(runtime->context, "Matrix_Pop", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Matrix_Pop = reinterpret_cast<decltype(p_Matrix_Pop)>(address);
    if (runtime->resolve_symbol(runtime->context, "Matrix_Push", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Matrix_Push = reinterpret_cast<decltype(p_Matrix_Push)>(address);
    if (runtime->resolve_symbol(runtime->context, "Matrix_RotateX", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Matrix_RotateX = reinterpret_cast<decltype(p_Matrix_RotateX)>(address);
    if (runtime->resolve_symbol(runtime->context, "Matrix_RotateY", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Matrix_RotateY = reinterpret_cast<decltype(p_Matrix_RotateY)>(address);
    if (runtime->resolve_symbol(runtime->context, "Matrix_RotateZYX", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Matrix_RotateZYX = reinterpret_cast<decltype(p_Matrix_RotateZYX)>(address);
    if (runtime->resolve_symbol(runtime->context, "Matrix_Scale", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Matrix_Scale = reinterpret_cast<decltype(p_Matrix_Scale)>(address);
    if (runtime->resolve_symbol(runtime->context, "Matrix_Translate", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Matrix_Translate = reinterpret_cast<decltype(p_Matrix_Translate)>(address);
    if (runtime->resolve_symbol(runtime->context, "Play_TriggerVoidOut", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Play_TriggerVoidOut = reinterpret_cast<decltype(p_Play_TriggerVoidOut)>(address);
    if (runtime->resolve_symbol(runtime->context, "Player_PlaySfx", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Player_PlaySfx = reinterpret_cast<decltype(p_Player_PlaySfx)>(address);
    if (runtime->resolve_symbol(runtime->context, "Quake_Add", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Quake_Add = reinterpret_cast<decltype(p_Quake_Add)>(address);
    if (runtime->resolve_symbol(runtime->context, "Quake_SetCountdown", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Quake_SetCountdown = reinterpret_cast<decltype(p_Quake_SetCountdown)>(address);
    if (runtime->resolve_symbol(runtime->context, "Quake_SetQuakeValues", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Quake_SetQuakeValues = reinterpret_cast<decltype(p_Quake_SetQuakeValues)>(address);
    if (runtime->resolve_symbol(runtime->context, "Quake_SetSpeed", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Quake_SetSpeed = reinterpret_cast<decltype(p_Quake_SetSpeed)>(address);
    if (runtime->resolve_symbol(runtime->context, "Rand_CenteredFloat", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Rand_CenteredFloat = reinterpret_cast<decltype(p_Rand_CenteredFloat)>(address);
    if (runtime->resolve_symbol(runtime->context, "ResourceMgr_LoadAnimByName", &address) != SHIP_NATIVE_OK ||
        !address) {
        Reset();
        return false;
    }
    p_ResourceMgr_LoadAnimByName = reinterpret_cast<decltype(p_ResourceMgr_LoadAnimByName)>(address);
    if (runtime->resolve_symbol(runtime->context, "Rumble_Request", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_Rumble_Request = reinterpret_cast<decltype(p_Rumble_Request)>(address);
    if (runtime->resolve_symbol(runtime->context, "SkinMatrix_SetTranslateRotateYXZScale", &address) !=
            SHIP_NATIVE_OK ||
        !address) {
        Reset();
        return false;
    }
    p_SkinMatrix_SetTranslateRotateYXZScale =
        reinterpret_cast<decltype(p_SkinMatrix_SetTranslateRotateYXZScale)>(address);
    if (runtime->resolve_symbol(runtime->context, "func_8002829C", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_func_8002829C = reinterpret_cast<decltype(p_func_8002829C)>(address);
    if (runtime->resolve_symbol(runtime->context, "gSPDisplayList", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_gSPDisplayList = reinterpret_cast<decltype(p_gSPDisplayList)>(address);
    if (runtime->resolve_symbol(runtime->context, "gSPSegment", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_gSPSegment = reinterpret_cast<decltype(p_gSPSegment)>(address);
    if (runtime->resolve_symbol(runtime->context, "gSaveContext", &address) != SHIP_NATIVE_OK || !address) {
        Reset();
        return false;
    }
    p_gSaveContext = reinterpret_cast<decltype(p_gSaveContext)>(address);
    ready = true;
    return true;
}
void Reset() {
    ready = false;
    engine = nullptr;
    resources = nullptr;
    render = nullptr;
    p_Actor_SpawnFloorDustRing = nullptr;
    p_Animation_GetLastFrame = nullptr;
    p_AudioOcarina_GetPlayingStaff = nullptr;
    p_AudioOcarina_SetInstrument = nullptr;
    p_BgCheck_EntityRaycastFloor3 = nullptr;
    p_Camera_GetInputDirYaw = nullptr;
    p_Collider_UpdateCylinder = nullptr;
    p_CollisionCheck_SetAT = nullptr;
    p_EffectSsBlast_SpawnWhiteShockwave = nullptr;
    p_EffectSsHahen_SpawnBurst = nullptr;
    p_Gfx_SetupDL_25Xlu = nullptr;
    p_Gfx_SetupDL_44Xlu = nullptr;
    p_Gfx_TwoTexScroll = nullptr;
    p_LightContext_InsertLight = nullptr;
    p_LightContext_RemoveLight = nullptr;
    p_Lights_PointNoGlowSetInfo = nullptr;
    p_LinkAnimation_Change = nullptr;
    p_LinkAnimation_Update = nullptr;
    p_Magic_Reset = nullptr;
    p_Math_AsymStepToF = nullptr;
    p_Math_Atan2S = nullptr;
    p_Math_CosS = nullptr;
    p_Math_ScaledStepToS = nullptr;
    p_Math_SinS = nullptr;
    p_Math_StepToF = nullptr;
    p_Math_StepToS = nullptr;
    p_Math_StepUntilF = nullptr;
    p_Math_Vec3f_Copy = nullptr;
    p_Matrix_NewMtx = nullptr;
    p_Matrix_Pop = nullptr;
    p_Matrix_Push = nullptr;
    p_Matrix_RotateX = nullptr;
    p_Matrix_RotateY = nullptr;
    p_Matrix_RotateZYX = nullptr;
    p_Matrix_Scale = nullptr;
    p_Matrix_Translate = nullptr;
    p_Play_TriggerVoidOut = nullptr;
    p_Player_PlaySfx = nullptr;
    p_Quake_Add = nullptr;
    p_Quake_SetCountdown = nullptr;
    p_Quake_SetQuakeValues = nullptr;
    p_Quake_SetSpeed = nullptr;
    p_Rand_CenteredFloat = nullptr;
    p_ResourceMgr_LoadAnimByName = nullptr;
    p_Rumble_Request = nullptr;
    p_SkinMatrix_SetTranslateRotateYXZScale = nullptr;
    p_func_8002829C = nullptr;
    p_gSPDisplayList = nullptr;
    p_gSPSegment = nullptr;
    p_gSaveContext = nullptr;
}
bool Ready() {
    return ready;
}
} // namespace MMFormsGame
