#pragma once
#include <shiplua/native/ship_native_abi.h>
#include "soh/ResourceManagerHelpers.h"
#include "soh/frame_interpolation.h"
#include "oot_engine.h"
#include "oot_render.h"
#include "oot_resources.h"
extern "C" {
#include "z64.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}
namespace MMFormsGame {
bool Bind(const ShipNativeRuntime* runtime);
void Reset();
bool Ready();
PlayState* Play();
uint8_t HasFile(const char* path);
int ResourceSignature(char* path);
struct InterpolationScope {
    bool open;
    InterpolationScope(const void* key, int child);
    ~InterpolationScope();
};
extern decltype(&::Actor_SpawnFloorDustRing) p_Actor_SpawnFloorDustRing;
extern decltype(&::Animation_GetLastFrame) p_Animation_GetLastFrame;
extern decltype(&::AudioOcarina_GetPlayingStaff) p_AudioOcarina_GetPlayingStaff;
extern decltype(&::AudioOcarina_SetInstrument) p_AudioOcarina_SetInstrument;
extern decltype(&::BgCheck_EntityRaycastFloor3) p_BgCheck_EntityRaycastFloor3;
extern decltype(&::Camera_GetInputDirYaw) p_Camera_GetInputDirYaw;
extern decltype(&::Collider_UpdateCylinder) p_Collider_UpdateCylinder;
extern decltype(&::CollisionCheck_SetAT) p_CollisionCheck_SetAT;
extern decltype(&::EffectSsBlast_SpawnWhiteShockwave) p_EffectSsBlast_SpawnWhiteShockwave;
extern decltype(&::EffectSsHahen_SpawnBurst) p_EffectSsHahen_SpawnBurst;
extern decltype(&::Gfx_SetupDL_25Xlu) p_Gfx_SetupDL_25Xlu;
extern decltype(&::Gfx_SetupDL_44Xlu) p_Gfx_SetupDL_44Xlu;
extern decltype(&::Gfx_TwoTexScroll) p_Gfx_TwoTexScroll;
extern decltype(&::LightContext_InsertLight) p_LightContext_InsertLight;
extern decltype(&::LightContext_RemoveLight) p_LightContext_RemoveLight;
extern decltype(&::Lights_PointNoGlowSetInfo) p_Lights_PointNoGlowSetInfo;
extern decltype(&::LinkAnimation_Change) p_LinkAnimation_Change;
extern decltype(&::LinkAnimation_Update) p_LinkAnimation_Update;
extern decltype(&::Magic_Reset) p_Magic_Reset;
extern decltype(&::Math_AsymStepToF) p_Math_AsymStepToF;
extern decltype(&::Math_Atan2S) p_Math_Atan2S;
extern decltype(&::Math_CosS) p_Math_CosS;
extern decltype(&::Math_ScaledStepToS) p_Math_ScaledStepToS;
extern decltype(&::Math_SinS) p_Math_SinS;
extern decltype(&::Math_StepToF) p_Math_StepToF;
extern decltype(&::Math_StepToS) p_Math_StepToS;
extern decltype(&::Math_StepUntilF) p_Math_StepUntilF;
extern decltype(&::Math_Vec3f_Copy) p_Math_Vec3f_Copy;
extern decltype(&::Matrix_NewMtx) p_Matrix_NewMtx;
extern decltype(&::Matrix_Pop) p_Matrix_Pop;
extern decltype(&::Matrix_Push) p_Matrix_Push;
extern decltype(&::Matrix_RotateX) p_Matrix_RotateX;
extern decltype(&::Matrix_RotateY) p_Matrix_RotateY;
extern decltype(&::Matrix_RotateZYX) p_Matrix_RotateZYX;
extern decltype(&::Matrix_Scale) p_Matrix_Scale;
extern decltype(&::Matrix_Translate) p_Matrix_Translate;
extern decltype(&::Play_TriggerVoidOut) p_Play_TriggerVoidOut;
extern decltype(&::Player_PlaySfx) p_Player_PlaySfx;
extern decltype(&::Quake_Add) p_Quake_Add;
extern decltype(&::Quake_SetCountdown) p_Quake_SetCountdown;
extern decltype(&::Quake_SetQuakeValues) p_Quake_SetQuakeValues;
extern decltype(&::Quake_SetSpeed) p_Quake_SetSpeed;
extern decltype(&::Rand_CenteredFloat) p_Rand_CenteredFloat;
extern decltype(&::ResourceMgr_LoadAnimByName) p_ResourceMgr_LoadAnimByName;
extern decltype(&::Rumble_Request) p_Rumble_Request;
extern decltype(&::SkinMatrix_SetTranslateRotateYXZScale) p_SkinMatrix_SetTranslateRotateYXZScale;
extern decltype(&::func_8002829C) p_func_8002829C;
extern decltype(&::gSPDisplayList) p_gSPDisplayList;
extern decltype(&::gSPSegment) p_gSPSegment;
extern decltype(::gSaveContext)* p_gSaveContext;
} // namespace MMFormsGame
#ifndef MM_FORMS_BINDINGS_IMPL
#define Actor_SpawnFloorDustRing MMFormsGame::p_Actor_SpawnFloorDustRing
#define Animation_GetLastFrame MMFormsGame::p_Animation_GetLastFrame
#define AudioOcarina_GetPlayingStaff MMFormsGame::p_AudioOcarina_GetPlayingStaff
#define AudioOcarina_SetInstrument MMFormsGame::p_AudioOcarina_SetInstrument
#define BgCheck_EntityRaycastFloor3 MMFormsGame::p_BgCheck_EntityRaycastFloor3
#define Camera_GetInputDirYaw MMFormsGame::p_Camera_GetInputDirYaw
#define Collider_UpdateCylinder MMFormsGame::p_Collider_UpdateCylinder
#define CollisionCheck_SetAT MMFormsGame::p_CollisionCheck_SetAT
#define EffectSsBlast_SpawnWhiteShockwave MMFormsGame::p_EffectSsBlast_SpawnWhiteShockwave
#define EffectSsHahen_SpawnBurst MMFormsGame::p_EffectSsHahen_SpawnBurst
#define Gfx_SetupDL_25Xlu MMFormsGame::p_Gfx_SetupDL_25Xlu
#define Gfx_SetupDL_44Xlu MMFormsGame::p_Gfx_SetupDL_44Xlu
#define Gfx_TwoTexScroll MMFormsGame::p_Gfx_TwoTexScroll
#define LightContext_InsertLight MMFormsGame::p_LightContext_InsertLight
#define LightContext_RemoveLight MMFormsGame::p_LightContext_RemoveLight
#define Lights_PointNoGlowSetInfo MMFormsGame::p_Lights_PointNoGlowSetInfo
#define LinkAnimation_Change MMFormsGame::p_LinkAnimation_Change
#define LinkAnimation_Update MMFormsGame::p_LinkAnimation_Update
#define Magic_Reset MMFormsGame::p_Magic_Reset
#define Math_AsymStepToF MMFormsGame::p_Math_AsymStepToF
#define Math_Atan2S MMFormsGame::p_Math_Atan2S
#define Math_CosS MMFormsGame::p_Math_CosS
#define Math_ScaledStepToS MMFormsGame::p_Math_ScaledStepToS
#define Math_SinS MMFormsGame::p_Math_SinS
#define Math_StepToF MMFormsGame::p_Math_StepToF
#define Math_StepToS MMFormsGame::p_Math_StepToS
#define Math_StepUntilF MMFormsGame::p_Math_StepUntilF
#define Math_Vec3f_Copy MMFormsGame::p_Math_Vec3f_Copy
#define Matrix_NewMtx MMFormsGame::p_Matrix_NewMtx
#define Matrix_Pop MMFormsGame::p_Matrix_Pop
#define Matrix_Push MMFormsGame::p_Matrix_Push
#define Matrix_RotateX MMFormsGame::p_Matrix_RotateX
#define Matrix_RotateY MMFormsGame::p_Matrix_RotateY
#define Matrix_RotateZYX MMFormsGame::p_Matrix_RotateZYX
#define Matrix_Scale MMFormsGame::p_Matrix_Scale
#define Matrix_Translate MMFormsGame::p_Matrix_Translate
#define Play_TriggerVoidOut MMFormsGame::p_Play_TriggerVoidOut
#define Player_PlaySfx MMFormsGame::p_Player_PlaySfx
#define Quake_Add MMFormsGame::p_Quake_Add
#define Quake_SetCountdown MMFormsGame::p_Quake_SetCountdown
#define Quake_SetQuakeValues MMFormsGame::p_Quake_SetQuakeValues
#define Quake_SetSpeed MMFormsGame::p_Quake_SetSpeed
#define Rand_CenteredFloat MMFormsGame::p_Rand_CenteredFloat
#define ResourceMgr_LoadAnimByName MMFormsGame::p_ResourceMgr_LoadAnimByName
#define Rumble_Request MMFormsGame::p_Rumble_Request
#define SkinMatrix_SetTranslateRotateYXZScale MMFormsGame::p_SkinMatrix_SetTranslateRotateYXZScale
#define func_8002829C MMFormsGame::p_func_8002829C
#define gSPDisplayList MMFormsGame::p_gSPDisplayList
#define gSPSegment MMFormsGame::p_gSPSegment
#define gSaveContext (*MMFormsGame::p_gSaveContext)
#define gPlayState MMFormsGame::Play()
#define ResourceMgr_FileExists MMFormsGame::HasFile
#define ResourceMgr_OTRSigCheck MMFormsGame::ResourceSignature
#endif
// As macros originais declaram funções em escopo de bloco; aqui a ligação já está resolvida.
#undef OPEN_DISPS
#undef CLOSE_DISPS
#define OPEN_DISPS(ctx)                                                 \
    {                                                                   \
        MMFormsGame::InterpolationScope formsScope(__FILE__, __LINE__); \
        GraphicsContext* __gfxCtx = (ctx);                              \
        (void)__gfxCtx;
#define CLOSE_DISPS(ctx) \
    }                    \
    (void)0
