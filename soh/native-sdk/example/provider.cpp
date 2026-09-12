#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "oot_engine.h"
#include "oot_resources.h"
#include "oot_layout_id.h"
#include "z64.h"

namespace {
// SDL usa posições Xbox. No Switch Pro: B->SDL A, A->SDL B, Y->SDL X, X->SDL Y.
constexpr uint8_t SDL_BUTTON_B_NINTENDO = 0;
constexpr uint8_t SDL_BUTTON_A_NINTENDO = 1;
constexpr uint8_t SDL_BUTTON_Y_NINTENDO = 2;
constexpr uint8_t SDL_BUTTON_X_NINTENDO = 3;
constexpr uint32_t PhysicalButton(uint8_t button) { return uint32_t{ 1 } << button; }
constexpr const char* FREE_LOOK_SETTING = "gSettings.FreeLook.Enabled";

enum class Phase { Ready, Airborne, Rolling, Running };

struct Mod {
    const ShipOotEngineV1* engine;
    const ShipOotMovementV1* movement;
    const ShipOotResourcesV1* resources;
    uint16_t keyboardJumpMask = 0;
    Phase phase = Phase::Ready;
    bool jumpWasDown = false;
    bool observedRoll = false;
    bool faceBindingsApplied = false;
    bool freeLookApplied = false;
    int32_t previousFreeLook = 0;
    uint32_t cameraIdleMilliseconds = 10000;
    bool cameraFreeLookActive = false;
    std::chrono::steady_clock::time_point lastCameraInput = std::chrono::steady_clock::now();
};

enum class CameraChange { None, FreeLook, Automatic };

CameraChange UpdateCamera(Mod& mod) {
    constexpr int CAMERA_DEADZONE = 12;
    const int x = mod.movement->get_right_stick_x(0);
    const int y = mod.movement->get_right_stick_y(0);
    const auto now = std::chrono::steady_clock::now();
    if ((x * x) + (y * y) >= CAMERA_DEADZONE * CAMERA_DEADZONE) {
        mod.lastCameraInput = now;
        if (!mod.cameraFreeLookActive && mod.movement->set_setting_int(FREE_LOOK_SETTING, 1) == SHIP_NATIVE_OK) {
            mod.cameraFreeLookActive = true;
            return CameraChange::FreeLook;
        }
    } else if (mod.cameraFreeLookActive &&
               std::chrono::duration_cast<std::chrono::milliseconds>(now - mod.lastCameraInput).count() >=
                   mod.cameraIdleMilliseconds &&
               mod.movement->set_setting_int(FREE_LOOK_SETTING, 0) == SHIP_NATIVE_OK) {
        mod.cameraFreeLookActive = false;
        return CameraChange::Automatic;
    }
    return CameraChange::None;
}

ShipNativeStatus Write(ShipNativeWriteFn write, void* writer, const char* text) {
    return write(writer, text, static_cast<uint32_t>(std::strlen(text)));
}

ShipNativeStatus ApplyNintendoFaceBindings(Mod& mod) {
    // A físico vira a ação contextual N64 A. Y e B alimentam N64 B: ataque e
    // cancelar/guardar continuam sendo decididos pelo estado normal do jogo.
    const uint16_t remappedButtons[] = { BTN_A, BTN_B, BTN_CUP, BTN_CDOWN, BTN_CLEFT, BTN_CRIGHT };
    for (const uint16_t button : remappedButtons) {
        if (mod.movement->clear_gamepad_button_bindings(0, button) != SHIP_NATIVE_OK) {
            mod.movement->reload_gamepad_mappings(0);
            return SHIP_NATIVE_FAILURE;
        }
    }
    if (
        mod.movement->bind_gamepad_button(0, BTN_A, SDL_BUTTON_A_NINTENDO) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_B, SDL_BUTTON_Y_NINTENDO) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_B, SDL_BUTTON_B_NINTENDO) != SHIP_NATIVE_OK) {
        mod.movement->reload_gamepad_mappings(0);
        return SHIP_NATIVE_FAILURE;
    }
    mod.faceBindingsApplied = true;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Status(void* user, const char*, uint32_t length,
                                        ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    char result[224];
    const int size = std::snprintf(result, sizeof(result),
        "perfil=nintendo; X=pulo %.2f; A=contexto/rolar/sprint; Y=espada; B=cancelar; câmera=stick direito/auto %.1fs; gamepad=%s",
        double(JUMP_VELOCITY), double(mod.cameraIdleMilliseconds) / 1000.0,
        mod.movement->has_gamepad(0) ? "conectado" : "ausente");
    return size > 0 && size < int(sizeof(result)) ? write(writer, result, uint32_t(size)) : SHIP_NATIVE_FAILURE;
}

ShipNativeStatus SHIP_NATIVE_CALL Configure(void* user, const char* payload, uint32_t length,
                                           ShipNativeWriteFn write, void* writer) {
    if (!payload || !length || length >= 16) return SHIP_NATIVE_INVALID_ARGUMENT;
    char value[16]{};
    std::memcpy(value, payload, length);
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    if (end == value || parsed > 0xFFFFu) return SHIP_NATIVE_INVALID_ARGUMENT;
    unsigned long cameraTimeout = 10000;
    if (*end == ',') {
        char* timeoutEnd = nullptr;
        cameraTimeout = std::strtoul(end + 1, &timeoutEnd, 10);
        if (timeoutEnd == end + 1 || *timeoutEnd != '\0' || cameraTimeout < 1 || cameraTimeout > 60000)
            return SHIP_NATIVE_INVALID_ARGUMENT;
    } else if (*end != '\0') {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& mod = *static_cast<Mod*>(user);
    mod.keyboardJumpMask = static_cast<uint16_t>(parsed);
    mod.cameraIdleMilliseconds = static_cast<uint32_t>(cameraTimeout);
    if (!mod.freeLookApplied) {
        mod.previousFreeLook = mod.movement->get_setting_int(FREE_LOOK_SETTING, 0);
        if (mod.movement->set_setting_int(FREE_LOOK_SETTING, 1) != SHIP_NATIVE_OK) {
            return SHIP_NATIVE_FAILURE;
        }
        mod.freeLookApplied = true;
        mod.cameraFreeLookActive = true;
        mod.lastCameraInput = std::chrono::steady_clock::now();
    }
    return Write(write, writer, "configured");
}

ShipNativeStatus SHIP_NATIVE_CALL Jump(void* user, const char*, uint32_t length,
                                      ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    const ShipNativeStatus result = mod.movement->player_jump(float(JUMP_VELOCITY));
    return result == SHIP_NATIVE_OK ? Write(write, writer, "jump") : result;
}

ShipNativeStatus SHIP_NATIVE_CALL ResourceProbe(void* user, const char*, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    uint32_t size = 0;
    if (mod.resources->read_file("test/core.json", nullptr, 0, &size) != SHIP_NATIVE_OK ||
        !size || size > SHIP_NATIVE_MAX_BYTES) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* bytes = new (std::nothrow) uint8_t[size];
    if (!bytes) return SHIP_NATIVE_FAILURE;
    const auto read = mod.resources->read_file("test/core.json", bytes, size, &size);
    const auto written = read == SHIP_NATIVE_OK
        ? write(writer, reinterpret_cast<const char*>(bytes), size)
        : read;
    delete[] bytes;
    return written;
}

ShipNativeStatus SHIP_NATIVE_CALL Update(void* user, const char*, uint32_t length,
                                        ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    auto* player = static_cast<Player*>(mod.engine->get_player());
    if (!player) {
        mod.phase = Phase::Ready;
        mod.jumpWasDown = false;
        mod.observedRoll = false;
        return Write(write, writer, "unavailable");
    }
    const CameraChange cameraChange = UpdateCamera(mod);

    const bool hasGamepad = mod.movement->has_gamepad(0) != 0;
    if (hasGamepad && !mod.faceBindingsApplied && ApplyNintendoFaceBindings(mod) != SHIP_NATIVE_OK) {
        return Write(write, writer, "mapping-error");
    }
    const uint32_t physical = hasGamepad ? mod.movement->get_gamepad_buttons(0) : 0;
    const uint16_t current = mod.movement->get_input_current(0);
    const bool jumpDown = hasGamepad
        ? (physical & PhysicalButton(SDL_BUTTON_X_NINTENDO)) != 0
        : mod.keyboardJumpMask && (current & mod.keyboardJumpMask) != 0;
    const bool actionDown = hasGamepad
        ? (physical & PhysicalButton(SDL_BUTTON_A_NINTENDO)) != 0
        : (current & BTN_A) != 0;
    const bool justJumpPressed = jumpDown && !mod.jumpWasDown;
    mod.jumpWasDown = jumpDown;

    const bool grounded = mod.movement->is_player_grounded() != 0;
    const bool rolling = mod.movement->is_player_rolling() != 0;
    if (justJumpPressed && grounded && mod.movement->player_jump(float(JUMP_VELOCITY)) == SHIP_NATIVE_OK) {
        mod.phase = Phase::Airborne;
        mod.observedRoll = false;
        return Write(write, writer, "jump");
    }

    if (mod.phase == Phase::Airborne) {
        if (!grounded) return Write(write, writer, "airborne");
        mod.phase = Phase::Ready;
        return Write(write, writer, "landed");
    }

    if (rolling) {
        const bool firstRollFrame = mod.phase != Phase::Rolling;
        mod.phase = Phase::Rolling;
        mod.observedRoll = true;
        return Write(write, writer, firstRollFrame ? "roll" : "rolling");
    }

    if (mod.phase == Phase::Rolling && mod.observedRoll) {
        mod.observedRoll = false;
        if (actionDown) {
            mod.phase = Phase::Running;
            return Write(write, writer, "run");
        }
        mod.phase = Phase::Ready;
        return Write(write, writer, "idle");
    }

    if (mod.phase == Phase::Running) {
        if (!actionDown) {
            mod.phase = Phase::Ready;
            return Write(write, writer, "idle");
        }
        const int x = mod.movement->get_stick_x(0);
        const int y = mod.movement->get_stick_y(0);
        if ((x * x) + (y * y) >= 20 * 20 && player->linearVelocity < 8.0f) {
            player->linearVelocity = 8.0f;
        }
        return Write(write, writer, "running");
    }

    if (cameraChange == CameraChange::Automatic) return Write(write, writer, "camera-auto");
    if (cameraChange == CameraChange::FreeLook) return Write(write, writer, "camera-free");
    return Write(write, writer, actionDown ? "context" : "idle");
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    const auto* movement = static_cast<const ShipOotMovementV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_MOVEMENT_SERVICE, LINKSPAN_OOT_MOVEMENT_VERSION,
        sizeof(ShipOotMovementV1)));
    const auto* resources = static_cast<const ShipOotResourcesV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION,
        sizeof(ShipOotResourcesV1)));
    if (!engine || engine->size < sizeof(ShipOotEngineV1) || !engine->layout_id ||
        std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) ||
        engine->player_size != sizeof(Player) || engine->play_state_size != sizeof(PlayState) ||
        engine->save_context_size != sizeof(SaveContext) || !engine->get_player || !movement ||
        movement->size < sizeof(ShipOotMovementV1) || !movement->layout_id ||
        std::strcmp(movement->layout_id, LINKSPAN_OOT_LAYOUT_ID) ||
        !movement->get_input_current || !movement->get_stick_x || !movement->get_stick_y ||
        !movement->get_right_stick_x || !movement->get_right_stick_y ||
        !movement->has_gamepad || !movement->get_gamepad_buttons ||
        !movement->clear_gamepad_button_bindings || !movement->bind_gamepad_button ||
        !movement->reload_gamepad_mappings || !movement->get_setting_int || !movement->set_setting_int ||
        !movement->is_player_grounded ||
        !movement->is_player_rolling || !movement->player_jump || !movement->player_roll)
        return SHIP_NATIVE_UNSUPPORTED;
    if (!resources || resources->size < sizeof(ShipOotResourcesV1) || !resources->has_file ||
        !resources->read_file || !resources->list_files || !resources->dirty_resources ||
        !resources->unload_resource || !resources->mount_archive || !resources->unmount_archive ||
        !resources->get_game_versions) return SHIP_NATIVE_UNSUPPORTED;
    auto* mod = new (std::nothrow) Mod{engine, movement, resources};
    if (!mod) return SHIP_NATIVE_FAILURE;
    *instance = mod;
    if (runtime->register_function(runtime->context, "status", Status, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "configure", Configure, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "jump", Jump, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "resource_probe", ResourceProbe, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "update", Update, mod) != SHIP_NATIVE_OK) {
        delete mod;
        *instance = nullptr;
        return SHIP_NATIVE_FAILURE;
    }
    return SHIP_NATIVE_OK;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* mod = static_cast<Mod*>(instance);
    if (mod && mod->faceBindingsApplied) mod->movement->reload_gamepad_mappings(0);
    if (mod && mod->freeLookApplied) mod->movement->set_setting_int(FREE_LOOK_SETTING, mod->previousFreeLook);
    delete mod;
}
}

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, SHIP_NATIVE_ABI_MINOR, Init, Shutdown
    };
    return &descriptor;
}
