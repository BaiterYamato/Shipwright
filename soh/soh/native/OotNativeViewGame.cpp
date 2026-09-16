// Liga linkspan.oot.camera/render (OotNativeView.cpp) ao jogo: subcâmera da PlayState, pilha de
// matrizes, display lists por caminho e o hook oot.player.limb_draw.
#include "OotNativeView.h"

#include "OotNativeHooks.h"
#include "oot_hooks.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ResourceManagerHelpers.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace {

constexpr size_t kOtrPrefixLength = 7; // "__OTR__"

ShipNativeStatus CameraCreate(int16_t* camera, void** play) {
    PlayState* state = gPlayState;
    if (!state || !GET_PLAYER(state) || state->activeCamera != CAM_ID_MAIN || !state->cameraPtrs[CAM_ID_MAIN]) {
        return SHIP_NATIVE_LIMIT;
    }
    const s16 sub = Play_CreateSubCamera(state);
    if (sub == SUBCAM_NONE) {
        return SHIP_NATIVE_LIMIT;
    }
    Camera* main = Play_GetCamera(state, CAM_ID_MAIN);
    Vec3f at = main->at;
    Vec3f eye = main->eye;
    const f32 fov = main->fov;
    Play_ChangeCameraStatus(state, CAM_ID_MAIN, CAM_STAT_WAIT);
    Play_ChangeCameraStatus(state, sub, CAM_STAT_ACTIVE);
    Play_CameraSetAtEye(state, sub, &at, &eye);
    Play_CameraSetFov(state, sub, fov);
    *camera = sub;
    *play = state;
    return SHIP_NATIVE_OK;
}

bool CameraValid(int16_t camera, void* play) {
    const PlayState* state = gPlayState;
    return state && state == play && camera > CAM_ID_MAIN && camera < NUM_CAMS && state->cameraPtrs[camera] &&
           state->activeCamera == camera;
}

// Na mesma cena: remove a subcâmera; só devolve a principal se a nossa ainda era a ativa.
void CameraDestroy(int16_t camera, void* play) {
    PlayState* state = gPlayState;
    if (!state || state != play || camera <= CAM_ID_MAIN || camera >= NUM_CAMS || !state->cameraPtrs[camera]) {
        return;
    }
    const bool wasActive = state->activeCamera == camera;
    Play_ClearCamera(state, camera);
    if (wasActive && state->cameraPtrs[CAM_ID_MAIN]) {
        Play_ChangeCameraStatus(state, CAM_ID_MAIN, CAM_STAT_ACTIVE);
    }
}

void CameraApply(int16_t camera, void* play, const ShipOotCameraViewV1& view) {
    auto* state = static_cast<PlayState*>(play);
    Vec3f eye{ view.eye[0], view.eye[1], view.eye[2] };
    Vec3f at{ view.at[0], view.at[1], view.at[2] };
    Play_CameraSetAtEye(state, camera, &at, &eye);
    Play_CameraSetFov(state, camera, view.fov);
}

ShipNativeStatus CameraRead(ShipOotCameraViewV1* view) {
    PlayState* state = gPlayState;
    if (!state) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    Camera* camera = Play_GetCamera(state, state->activeCamera);
    if (!camera) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    view->size = sizeof(ShipOotCameraViewV1);
    view->eye[0] = camera->eye.x;
    view->eye[1] = camera->eye.y;
    view->eye[2] = camera->eye.z;
    view->at[0] = camera->at.x;
    view->at[1] = camera->at.y;
    view->at[2] = camera->at.z;
    view->fov = camera->fov;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus DrawDisplayList(void* play, const char* path, uint8_t layer) {
    if (!ResourceMgr_FileExists(path + kOtrPrefixLength)) {
        return SHIP_NATIVE_FAILURE;
    }
    auto* state = static_cast<PlayState*>(play);
    auto* dlist = reinterpret_cast<Gfx*>(const_cast<char*>(path));
    if (layer == LINKSPAN_OOT_RENDER_TRANSLUCENT) {
        Gfx_DrawDListXlu(state, dlist);
    } else {
        Gfx_DrawDListOpa(state, dlist);
    }
    return SHIP_NATIVE_OK;
}

void MatrixTranslateApply(float x, float y, float z) {
    Matrix_Translate(x, y, z, MTXMODE_APPLY);
}

void MatrixScaleApply(float x, float y, float z) {
    Matrix_Scale(x, y, z, MTXMODE_APPLY);
}

void MatrixRotateApply(int16_t x, int16_t y, int16_t z) {
    Matrix_RotateZYX(x, y, z, MTXMODE_APPLY);
}

} // namespace

namespace ShipLuaHost {

void RegisterOotViewGameHooks() {
    static bool registered = false;
    if (registered || !GameInteractor::Instance) {
        return;
    }
    registered = true;
    OotViewBridge bridge;
    bridge.cameraCreate = CameraCreate;
    bridge.cameraValid = CameraValid;
    bridge.cameraDestroy = CameraDestroy;
    bridge.cameraApply = CameraApply;
    bridge.cameraRead = CameraRead;
    bridge.drawDisplayList = DrawDisplayList;
    bridge.matrixPush = Matrix_Push;
    bridge.matrixPop = Matrix_Pop;
    bridge.matrixTranslate = MatrixTranslateApply;
    bridge.matrixScale = MatrixScaleApply;
    bridge.matrixRotateZYX = MatrixRotateApply;
    SetOotViewBridge(bridge);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnCameraState>(
        [](PlayState*) { ShipLuaHost::UpdateOotCamera(); });
}

} // namespace ShipLuaHost

// Chamada no início de Player_PostLimbDrawGameplay (z_player_lib.c), na thread do jogo.
extern "C" void LinkSpan_PlayerLimbDraw(PlayState* play, s32 limbIndex, Actor* actor) {
    const auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().playerLimbDraw;
    if (!registry || !registry->HasHooks(point)) {
        return;
    }
    ShipOotPlayerLimbHookV1 payload{ sizeof(ShipOotPlayerLimbHookV1), play, actor, limbIndex };
    Matrix_Push();
    ShipLuaHost::EnterOotRenderScope();
    ShipLuaHost::GetOotHookRegistry()->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
    ShipLuaHost::LeaveOotRenderScope();
    Matrix_Pop();
}
