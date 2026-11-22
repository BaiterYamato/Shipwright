#include <algorithm>
#include <fast/FlyCameraInput.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <libultraship/bridge/consolevariablebridge.h>

#include <chrono>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include "FlyCamera.h"
#include "Mouse.h"
#include "soh/native/OotNativeView.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64player.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
s32 Camera_GetDbgCamEnabled(void);
}

static Ship::Coords mouseCoord = {};
static Ship::Coords mouseCoordRel = {};

#define CVAR_ENABLE_MOUSE_NAME CVAR_SETTING("EnableMouse")
#define CVAR_ENABLE_MOUSE_DEFAULT 0
#define CVAR_ENABLE_MOUSE_VALUE CVarGetInteger(CVAR_ENABLE_MOUSE_NAME, CVAR_ENABLE_MOUSE_DEFAULT)
#define MOUSE_ENABLED (CVAR_ENABLE_MOUSE_VALUE && GetWindow()->IsMouseCaptured() && flyCameraToken == 0)

std::shared_ptr<Ship::Window> GetWindow() {
    return OTRGlobals::Instance->context->GetWindow();
}


namespace {

using FlyClock = std::chrono::steady_clock;

uint64_t flyCameraToken = 0;
ShipFlyCamera::Pose flyPose;
ShipOotCameraViewV1 flyView{};
FlyClock::time_point lastFlyFrame;
bool flyChangedMouseCapture = false;
bool flyExitAwaitingKeys = false;

const ShipOotCameraV1& FlyCameraService() {
    return ShipLuaHost::GetOotNativeCameraService();
}

void StopFlyCamera() {
    if (flyChangedMouseCapture) {
        const auto window = GetWindow();
        if (window->IsMouseCaptured()) {
            window->SetMouseCapture(false);
        }
        flyChangedMouseCapture = false;
    }
    if (flyCameraToken != 0) {
        FlyCameraService().release(flyCameraToken);
        flyCameraToken = 0;
    }
}

void DestroyFlyCamera() {
    StopFlyCamera();
    flyExitAwaitingKeys = false;
}

void ClearFlyCameraPlayerInput(PlayState* play) {
    // This hook runs after GameState_ReqPadData and before gameState->main().
    Input& input = play->state.input[0];
    Input neutral{};
    neutral.cur.err_no = input.cur.err_no;
    neutral.prev.err_no = input.prev.err_no;
    input = neutral;
}

ShipFlyCamera::FrameInput ReadFlyCameraInput(uint32_t keys) {
    return {
        mouseCoordRel.x,
        mouseCoordRel.y,
        !!(keys & Fast::FlyForward) - !!(keys & Fast::FlyBackward),
        !!(keys & Fast::FlyRight) - !!(keys & Fast::FlyLeft),
        !!(keys & Fast::FlyUp) - !!(keys & (Fast::FlyDownLeft | Fast::FlyDownRight)),
        (keys & (Fast::FlyBoostLeft | Fast::FlyBoostRight)) != 0,
    };
}

const char* FlyCameraBlocked(PlayState* play, Player* player) {
    if (!play || !player) {
        return "gameplay/player unavailable";
    }
    if (!CVarGetInteger(CVAR_DEVELOPER_TOOLS("DebugEnabled"), 0) || !CVAR_ENABLE_MOUSE_VALUE) {
        return "debug mode or mouse input disabled";
    }
    if (GetWindow()->GetGui()->GetMenuOrMenubarVisible() || ImGui::GetIO().WantTextInput) {
        return "menu/text input open";
    }
    if (ImGui::GetIO().AppFocusLost) {
        return "game window unfocused";
    }
    if (Camera_GetDbgCamEnabled() || play->pauseCtx.state != 0 || play->csCtx.state != CS_STATE_IDLE ||
        play->transitionMode != TRANS_MODE_OFF || Player_InCsMode(play)) {
        return "debug camera, pause, cutscene, or transition";
    }
    return nullptr;
}

bool StartFlyCamera(PlayState* play, Player* player) {
    if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) || std::abs(player->linearVelocity) >= 0.01f) {
        SPDLOG_INFO("[FlyCamera] refused: Link is moving or airborne (velocity {}, bg flags {})",
                    player->linearVelocity, player->actor.bgCheckFlags);
        return false;
    }

    flyView = {};
    flyView.size = sizeof(flyView);
    const auto readStatus = FlyCameraService().get_view(&flyView);
    if (readStatus != SHIP_NATIVE_OK || !ShipFlyCamera::Seed(flyPose, flyView.eye, flyView.at)) {
        SPDLOG_INFO("[FlyCamera] refused: current view unavailable (status {})", static_cast<int>(readStatus));
        return false;
    }
    const auto acquireStatus = FlyCameraService().acquire("soh.debug.fly-camera", &flyCameraToken);
    if (acquireStatus != SHIP_NATIVE_OK) {
        SPDLOG_INFO("[FlyCamera] refused: camera lease (status {})", static_cast<int>(acquireStatus));
        return false;
    }
    const auto writeStatus = FlyCameraService().set_view(flyCameraToken, &flyView);
    if (writeStatus != SHIP_NATIVE_OK) {
        SPDLOG_INFO("[FlyCamera] refused: initial view (status {})", static_cast<int>(writeStatus));
        StopFlyCamera();
        return false;
    }

    const auto window = GetWindow();
    flyChangedMouseCapture = !window->IsMouseCaptured();
    if (flyChangedMouseCapture) {
        window->SetMouseCapture(true);
    }
    if (!window->IsMouseCaptured()) {
        SPDLOG_INFO("[FlyCamera] refused: backend did not capture the mouse");
        StopFlyCamera();
        return false;
    }

    lastFlyFrame = FlyClock::now();
    ClearFlyCameraPlayerInput(play);
    SPDLOG_INFO("[FlyCamera] enabled (mouse capture {})", flyChangedMouseCapture ? "acquired" : "existing");
    return true;
}

void TickFlyCamera() {
    const bool keyboardToggle = Fast::TakeFlyCameraToggle();
    const uint32_t flightKeys = Fast::TakeFlyCameraKeys();
    const char* requestCvar = CVAR_DEVELOPER_TOOLS("FlyCameraToggle");
    const bool consoleToggle = CVarGetInteger(requestCvar, 0) != 0;
    if (consoleToggle)
        CVarClear(requestCvar);
    const bool togglePressed = keyboardToggle || consoleToggle;
    if (!togglePressed && flyCameraToken == 0 && !flyExitAwaitingKeys) {
        return;
    }
    if (!ImGui::GetCurrentContext()) {
        SPDLOG_INFO("[FlyCamera] unavailable: no UI input context");
        DestroyFlyCamera();
        return;
    }

    PlayState* play = gPlayState;
    Player* player = play ? GET_PLAYER(play) : nullptr;
    const char* blocked = FlyCameraBlocked(play, player);
    if (flyCameraToken != 0) {
        if (togglePressed || blocked || !GetWindow()->IsMouseCaptured() || !FlyCameraService().is_owned(flyCameraToken)) {
            SPDLOG_INFO("[FlyCamera] disabled ({})", togglePressed ? "toggle" : (blocked ? blocked : "capture/lease lost"));
            StopFlyCamera();
            flyExitAwaitingKeys = player && Fast::FlyCameraKeysHeld();
        } else {
            const auto now = FlyClock::now();
            // Capped so a hitch does not teleport the camera.
            constexpr float kMaxFlyStepSeconds = 0.1f;
            const float seconds = std::min(std::chrono::duration<float>(now - lastFlyFrame).count(), kMaxFlyStepSeconds);
            lastFlyFrame = now;
            ShipFlyCamera::Update(flyPose, ReadFlyCameraInput(flightKeys), seconds,
                                  CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) != 0, flyView.at);
            std::copy(flyPose.eye.begin(), flyPose.eye.end(), flyView.eye);
            const auto status = FlyCameraService().set_view(flyCameraToken, &flyView);
            if (status == SHIP_NATIVE_OK) {
                ClearFlyCameraPlayerInput(play);
                return;
            }
            SPDLOG_INFO("[FlyCamera] disabled: view update (status {})", static_cast<int>(status));
            StopFlyCamera();
            flyExitAwaitingKeys = Fast::FlyCameraKeysHeld();
        }
    } else if (togglePressed) {
        SPDLOG_INFO("[FlyCamera] toggle requested by {}", keyboardToggle ? "F8" : "console");
        if (blocked) {
            SPDLOG_INFO("[FlyCamera] refused: {}", blocked);
        } else if (StartFlyCamera(play, player)) {
            return;
        }
    }

    // A held flight key must not become a movement or action on exit.
    flyExitAwaitingKeys = flyExitAwaitingKeys && player && Fast::FlyCameraKeysHeld();
    if (flyExitAwaitingKeys) {
        ClearFlyCameraPlayerInput(play);
    }
}


} // namespace

extern "C" {
void Mouse_UpdatePos() {
    mouseCoord = GetWindow()->GetMousePos();
}

void Mouse_UpdatePosRel() {
    mouseCoordRel = GetWindow()->GetMouseDelta();
}

void Mouse_UpdateAll() {
    Mouse_UpdatePos();
    Mouse_UpdatePosRel();
}

void Mouse_HandleThirdPerson(f32* newCamX, f32* newCamY) {
    if (MOUSE_ENABLED && !CVarGetInteger(CVAR_SETTING("DisableThirdPersonMouse"), 0)) {
        *newCamX -= mouseCoordRel.x * 40.0f;
        *newCamY -= mouseCoordRel.y * 40.0f;
    }
}

void Mouse_HandleFirstPerson(Player* player) {
    f32 xAxisMulti = CVarGetFloat(CVAR_SETTING("FirstPersonCameraSensitivity.X"), 1.0f);
    f32 yAxisMulti = CVarGetFloat(CVAR_SETTING("FirstPersonCameraSensitivity.Y"), 1.0f);
    s8 invertXAxisMulti = ((CVarGetInteger(CVAR_SETTING("Controls.InvertAimingXAxis"), 0) &&
                            !CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0)) ||
                           (!CVarGetInteger(CVAR_SETTING("Controls.InvertAimingXAxis"), 0) &&
                            CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0)))
                              ? -1
                              : 1;
    s8 invertYAxisMulti = CVarGetInteger(CVAR_SETTING("Controls.InvertAimingYAxis"), 1) ? -1 : 1;
    if (MOUSE_ENABLED) {
        player->actor.focus.rot.y -= static_cast<int16_t>(mouseCoordRel.x * 6.0f * xAxisMulti * invertXAxisMulti);
        player->actor.focus.rot.x += static_cast<int16_t>(mouseCoordRel.y * 6.0f * yAxisMulti * invertYAxisMulti);
    }
}

void Mouse_RecenterCursor() {
    u32 width = GetWindow()->GetWidth();
    u32 height = GetWindow()->GetHeight();
    if (MOUSE_ENABLED) {
        GetWindow()->SetMousePos({ static_cast<s32>(width / 2), static_cast<s32>(height / 2) });
    }
}

void Mouse_HandleShield(f32* sp50, f32* sp54) {
    if (MOUSE_ENABLED) {
        s32 width = GetWindow()->GetWidth();
        s32 height = GetWindow()->GetHeight();
        f32 xBound = 7200 / (width / 2.0f);
        f32 yBound = 6000 / (height / 2.0f);
        *sp50 +=
            (mouseCoord.x - (width / 2)) * xBound * (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) ? 1 : -1);
        *sp54 += (mouseCoord.y - (height / 2)) * yBound;
        *sp50 = CLAMP(*sp50, -7200, 7200);
        *sp54 = CLAMP(*sp54, -6000, 6000);
    }
}

static s8 iterMouse = 0;
static s32 mouseQuickspinX[5] = {};
static s32 mouseQuickspinY[5] = {};
static u8 quickspinCount = 0;

void Mouse_UpdateQuickspinCount() {
    if (MOUSE_ENABLED && !CVarGetInteger(CVAR_SETTING("DisableThirdPersonMouse"), 0)) {
        quickspinCount = (quickspinCount + 1) % 5;
        mouseQuickspinX[quickspinCount] = mouseCoord.x;
        mouseQuickspinY[quickspinCount] = mouseCoord.y;
    } else {
        quickspinCount = 0;
    }
}

bool Mouse_HandleQuickspin(bool* should, s8* iter2, s8* sp3C) {
    s8 temp1;
    s8 temp2;
    s32 i;
    if (!MOUSE_ENABLED || CVarGetInteger(CVAR_SETTING("DisableThirdPersonMouse"), 0)) {
        return *should = false;
    }

    for (i = 0; i < 4; i++, iter2++) {
        // Calculating angles as per z_lib.c:func_80077D10()
        s32 relY = mouseQuickspinY[i + 1] - mouseQuickspinY[i];
        s32 relX = mouseQuickspinX[i + 1] - mouseQuickspinX[i];
        s16 aTan = Math_Atan2S((f32)relY, (f32)-relX);
        iterMouse = (u16)(aTan + 0x2000) >> 9; // See z_player.c:Player_ProcessControlStick()
        if ((*iter2 = iterMouse) < 0) {
            return *should = false;
        }
        *iter2 *= 2;
    }
    temp1 = sp3C[0] - sp3C[1];
    if (ABS(temp1) < 10) {
        return *should = false;
    }
    iter2 = &sp3C[1];
    for (i = 1; i < 3; i++, iter2++) {
        temp2 = *iter2 - *(iter2 + 1);
        if ((ABS(temp2) < 10) || (temp2 * temp1 < 0)) {
            return *should = false;
        }
    }

    return *should = true;
}

// Hook handlers

void Mouse_RegisterRecenterCursorOnShield() {
    COND_HOOK(OnPlayerHoldUpShield, true, Mouse_RecenterCursor);
}

void Mouse_RegisterHandleFirstPerson() {
    COND_HOOK(OnPlayerFirstPersonControl, true, Mouse_HandleFirstPerson);
}

void Mouse_RegisterHandleShield() {
    COND_HOOK(OnPlayerShieldControl, true, Mouse_HandleShield);
}

void Mouse_RegisterUpdateQuickspinCount() {
    COND_HOOK(OnPlayerProcessStick, true, Mouse_UpdateQuickspinCount);
}

void Mouse_RegisterHandleQuickspin() {
    REGISTER_VB_SHOULD(VB_SHOULD_QUICKSPIN, { Mouse_HandleQuickspin(should, va_arg(args, s8*), va_arg(args, s8*)); });
}

static RegisterShipInitFunc registerFlyCamera([] {
    SPDLOG_INFO("[FlyCamera] F8 hook installed");
    COND_HOOK(OnGameStateMainStart, true, TickFlyCamera);
    COND_HOOK(OnPlayDestroy, true, DestroyFlyCamera);
});

static RegisterShipInitFunc registerShieldRecenter(Mouse_RegisterRecenterCursorOnShield, { CVAR_ENABLE_MOUSE_NAME });
static RegisterShipInitFunc registerFirstPerson(Mouse_RegisterHandleFirstPerson, { CVAR_ENABLE_MOUSE_NAME });
static RegisterShipInitFunc registerQuickspinCount(Mouse_RegisterUpdateQuickspinCount, { CVAR_ENABLE_MOUSE_NAME });
static RegisterShipInitFunc registerQuickspin(Mouse_RegisterHandleQuickspin, { CVAR_ENABLE_MOUSE_NAME });
static RegisterShipInitFunc registerShieldMove(Mouse_RegisterHandleShield, { CVAR_ENABLE_MOUSE_NAME });
} // extern "C"
