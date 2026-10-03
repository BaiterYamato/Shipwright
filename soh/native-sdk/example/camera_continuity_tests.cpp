// Exercise the provider's real camera update with controlled engine/input services.
#include "provider.cpp"
#include <limits>
#include <memory>

std::string ProviderAssetsDirectory() { return {}; }

namespace {
PlayState* testPlay;
Player* testPlayer;
SaveContext* testSave;
int8_t rightX = 0, rightY = 0, moveX = 0;
int freeLook = 0;
uint32_t testPhysical = 0;
uint8_t SHIP_NATIVE_CALL HasPad(uint8_t) { return 1; }
uint32_t SHIP_NATIVE_CALL Buttons(uint8_t) { return testPhysical; }
ShipNativeStatus SHIP_NATIVE_CALL Response(void* out, const char* data, uint32_t length) {
    static_cast<std::string*>(out)->append(data, length); return SHIP_NATIVE_OK;
}
void* SHIP_NATIVE_CALL TestPlay() { return testPlay; }
void* SHIP_NATIVE_CALL TestPlayer() { return testPlayer; }
void* SHIP_NATIVE_CALL TestSave() { return testSave; }
int8_t SHIP_NATIVE_CALL RightX(uint8_t) { return rightX; }
int8_t SHIP_NATIVE_CALL RightY(uint8_t) { return rightY; }
int8_t SHIP_NATIVE_CALL MoveX(uint8_t) { return moveX; }
int8_t SHIP_NATIVE_CALL Zero(uint8_t) { return 0; }
ShipNativeStatus SHIP_NATIVE_CALL Setting(const char* name, int value) {
    if (std::strcmp(name, FREE_LOOK_SETTING) == 0) freeLook = value;
    return SHIP_NATIVE_OK;
}

VecSph* Geometry(VecSph* pose, Vec3f* at, Vec3f* eye) {
    const float x = eye->x - at->x, y = eye->y - at->y, z = eye->z - at->z;
    const float horizontal = std::hypot(x, z);
    pose->r = std::hypot(horizontal, y);
    pose->yaw = static_cast<s16>(std::atan2(x, z) * (32768.0 / 3.141592653589793));
    pose->pitch = static_cast<s16>(std::atan2(y, horizontal) * (32768.0 / 3.141592653589793));
    return pose;
}

void Check(bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(EXIT_FAILURE); }
}
bool Equal(Vec3f a, Vec3f b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
void ViewAt(float x, float y, float z) {
    testPlay->view.lookAt = { x, y, z };
    testPlay->view.eye = { x - 120, y + 50, z };
    testPlay->view.fovy = 60;
}
void CheckSeed(const char* label) {
    const auto& camera = *testPlay->cameraPtrs[CAM_ID_MAIN];
    Check(Equal(camera.at, testPlay->view.lookAt) && Equal(camera.eye, testPlay->view.eye) &&
          Equal(camera.eyeNext, testPlay->view.eye), label);
    Check(std::abs(camera.dist - 130) < 0.001f && camera.fov == 60 &&
          testPlay->camX == -16384 && testPlay->camY > 4100 && testPlay->camY < 4120,
          "Resumption uses displayed distance, field of view and angles");
    Check(freeLook == 1 && testPlay->manualCamera, "Resumption enables manual camera after seeding");
}
}

int main() {
    const auto play = std::make_unique<PlayState>();
    const auto player = std::make_unique<Player>();
    const auto save = std::make_unique<SaveContext>();
    const auto camera = std::make_unique<Camera>();
    testPlay = play.get(); testPlayer = player.get(); testSave = save.get();
    play->cameraPtrs[CAM_ID_MAIN] = camera.get();
    play->activeCamera = CAM_ID_MAIN;
    camera->status = CAM_STAT_ACTIVE;
    save->gameMode = GAMEMODE_NORMAL;
    ViewAt(500, 50, 100);
    ShipOotEngineV1 engine{};
    engine.get_play_state = TestPlay; engine.get_player = TestPlayer; engine.get_save_context = TestSave;
    ShipOotMovementV2 movement{};
    movement.get_right_stick_x = RightX; movement.get_right_stick_y = RightY;
    movement.get_stick_x = MoveX; movement.get_stick_y = Zero; movement.set_setting_int = Setting;
    Mod mod{ &engine, &movement, nullptr, nullptr };
    mod.cameraGeometry = Geometry;

    Check(UpdateCamera(mod, play.get(), false) == CameraChange::None && freeLook == 0,
          "Loading a scene does not start manual camera before input");
    rightX = 30;
    Check(UpdateCamera(mod, play.get(), false) == CameraChange::FreeLook, "First right-stick input resumes");
    CheckSeed("First input starts at displayed view, despite a stale main camera");

    rightX = 0; moveX = 50;
    mod.lastCameraInput -= std::chrono::seconds(1);
    Check(UpdateCamera(mod, play.get(), false) == CameraChange::Automatic && freeLook == 0,
          "Original automatic follow delay remains functional");
    ViewAt(-200, 15, 700);
    rightX = 30;
    Check(UpdateCamera(mod, play.get(), false) == CameraChange::FreeLook, "Automatic to manual resumes");
    CheckSeed("Automatic to manual starts at the new view");

    play->transitionTrigger = TRANS_TRIGGER_START;
    UpdateCamera(mod, play.get(), false);
    Check(freeLook == 0 && !play->manualCamera, "Transitions release manual camera even with stick held");
    play->transitionTrigger = TRANS_TRIGGER_OFF;
    play->sceneNum = 1; play->state.frames = 200;
    ViewAt(1000, -5, -850);
    Check(UpdateCamera(mod, play.get(), false) == CameraChange::FreeLook, "New scene resumes without stale state");
    CheckSeed("Cross-scene resumption uses new scene view");

    // Same PlayState allocation and scene ID: only the frame reset identifies the reload.
    play->state.frames = 1;
    ViewAt(50, 10, 1000);
    Check(UpdateCamera(mod, play.get(), false) == CameraChange::FreeLook, "Same-scene reload resets camera epoch");
    CheckSeed("Same-scene reload does not retain old pose");

    play->activeCamera = CAM_ID_SUB_FIRST;
    const Vec3f previousEye = camera->eye;
    ViewAt(-900, 100, 0);
    UpdateCamera(mod, play.get(), false);
    Check(freeLook == 0 && Equal(camera->eye, previousEye), "Subcamera remains in control");
    play->activeCamera = CAM_ID_MAIN;
    Check(UpdateCamera(mod, play.get(), false) == CameraChange::FreeLook, "Subcamera exit resumes");
    CheckSeed("Subcamera exit seeds from its final displayed view");

    UpdateCamera(mod, play.get(), true);
    Check(freeLook == 0, "Menus/aiming suspend manual camera");
    UpdateCamera(mod, play.get(), false);
    Check(freeLook == 0, "Menus retain recenter requirement");
    rightX = 0; moveX = 0;
    UpdateCamera(mod, play.get(), false);
    rightX = 30;
    Check(UpdateCamera(mod, play.get(), false) == CameraChange::FreeLook, "Recentered stick resumes");

    play->manualCamera = false;
    ViewAt(500, 40, -200);
    Check(UpdateCamera(mod, play.get(), false) == CameraChange::FreeLook, "Native camera reset reseeds");
    CheckSeed("Native camera reset uses visible pose");
    const Vec3f goodEye = camera->eye;
    play->manualCamera = false;
    play->view.eye.x = std::numeric_limits<float>::quiet_NaN();
    UpdateCamera(mod, play.get(), false);
    Check(freeLook == 0 && Equal(camera->eye, goodEye), "Invalid view never corrupts camera or enables stale fallback");
    play->view.eye = play->view.lookAt;
    UpdateCamera(mod, play.get(), false);
    Check(freeLook == 0, "Zero-distance view is rejected");
    ViewAt(0, 0, 0);
    UpdateCamera(mod, play.get(), false);
    CheckSeed("Camera recovers when valid view returns");

    // R and X are ocarina notes. Their movement shortcuts must yield immediately.
    movement.has_gamepad = HasPad; movement.get_gamepad_buttons = Buttons;
    mod.faceBindingsApplied = true; mod.itemMenu.open = true;
    testPhysical = PhysicalButton(SDL_BUTTON_R) | PhysicalButton(SDL_BUTTON_X_NINTENDO);
    player->stateFlags2 |= PLAYER_STATE2_OCARINA_PLAYING;
    std::string response;
    Update(&mod, "", 0, Response, &response);
    Check(response == "ocarina" && !mod.itemMenu.open && mod.selectWasDown && mod.jumpWasDown &&
          mod.phase == Phase::Ready && !mod.tunic.quickSwap && !mod.boots.quickSwap && freeLook == 0,
          "Ocarina owns R/X, closes selectors, suppresses queued jumps and releases manual camera");
    player->stateFlags2 &= ~PLAYER_STATE2_OCARINA_PLAYING;

    testPlayer = nullptr;
    UpdateCamera(mod, play.get(), false);
    Check(freeLook == 0, "No player suspends manual camera");
    testPlay = nullptr;
    UpdateCamera(mod, nullptr, false);
    Check(!mod.cameraEpoch.play && !mod.cameraFreeLookActive, "Title/file select clears scene identity");
    std::puts("Camera continuity: scene/reload, automatic/manual, subcamera, menu and invalid-view regressions passed.");
    return EXIT_SUCCESS;
}
