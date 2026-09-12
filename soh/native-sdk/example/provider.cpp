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

// Decodifica o arquivo "version" do archive: byte 0 = endianness
// (0 = little, 1 = big), seguido de uint32 com a versão do jogo.
uint32_t ParseVersionFile(const uint8_t* bytes, uint32_t size) {
    if (!bytes || size != 5) return 0;
    if (bytes[0] == 1) {
        return (uint32_t(bytes[1]) << 24) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 8) |
               uint32_t(bytes[4]);
    }
    return uint32_t(bytes[1]) | (uint32_t(bytes[2]) << 8) | (uint32_t(bytes[3]) << 16) |
           (uint32_t(bytes[4]) << 24);
}

struct ListCounter {
    uint32_t count = 0;
    char first[96]{};
};

ShipNativeStatus SHIP_NATIVE_CALL CountPath(void* user, const char* path, uint32_t pathLength) {
    auto& counter = *static_cast<ListCounter*>(user);
    if (!counter.count && path && pathLength < sizeof(counter.first)) {
        std::memcpy(counter.first, path, pathLength);
        counter.first[pathLength] = '\0';
    }
    ++counter.count;
    return SHIP_NATIVE_OK;
}

// Prova de runtime do OOT-CORE-001: exercita o serviço linkspan.oot.resources
// v1 contra o VFS real do jogo (versions, has_file, read_file, list_files e
// montagem/desmontagem de um archive auxiliar com marcador conhecido).
ShipNativeStatus SHIP_NATIVE_CALL ResourceRuntimeProbe(void* user, const char*, uint32_t length,
                                                       ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    const char* step = "versions";
    char report[768];
    int used = 0;

    uint32_t versionCount = 0;
    if (mod.resources->get_game_versions(nullptr, 0, &versionCount) != SHIP_NATIVE_OK || !versionCount ||
        versionCount > 8) {
        return Write(write, writer, "fail@versions-count");
    }
    uint32_t versions[8]{};
    if (mod.resources->get_game_versions(versions, 8, &versionCount) != SHIP_NATIVE_OK) {
        return Write(write, writer, "fail@versions-read");
    }
    used = std::snprintf(report, sizeof(report), "versions=%u", versionCount);
    for (uint32_t i = 0; i < versionCount && used > 0 && used < int(sizeof(report)) - 24; ++i) {
        used += std::snprintf(report + used, sizeof(report) - used, "%s0x%08X", i ? "," : ":", versions[i]);
    }

    step = "has_file";
    const uint8_t hasReal = mod.resources->has_file("objects/gameplay_keep/gArrow1Anim");
    const uint8_t hasMissing = mod.resources->has_file("linkspan/definitely-missing.bin");
    if (hasReal != 1 || hasMissing != 0) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@has_file real=%u missing=%u", hasReal,
                      hasMissing);
        return Write(write, writer, report);
    }
    used += std::snprintf(report + used, sizeof(report) - used, "; has=1/0");

    step = "read_file";
    uint32_t versionSize = 0;
    if (mod.resources->read_file("version", nullptr, 0, &versionSize) != SHIP_NATIVE_OK || versionSize != 5) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@version-size %u", versionSize);
        return Write(write, writer, report);
    }
    uint8_t versionBytes[5]{};
    if (mod.resources->read_file("version", versionBytes, sizeof(versionBytes), &versionSize) != SHIP_NATIVE_OK) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@version-read");
        return Write(write, writer, report);
    }
    const uint32_t fileVersion = ParseVersionFile(versionBytes, versionSize);
    bool versionListed = false;
    for (uint32_t i = 0; i < versionCount; ++i) versionListed = versionListed || versions[i] == fileVersion;
    used += std::snprintf(report + used, sizeof(report) - used, "; version_file=0x%08X listed=%u", fileVersion,
                          versionListed ? 1u : 0u);

    step = "list_files";
    ListCounter counter;
    if (mod.resources->list_files("objects/gameplay_keep/gArrow*", CountPath, &counter) != SHIP_NATIVE_OK ||
        !counter.count) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@list_files");
        return Write(write, writer, report);
    }
    used += std::snprintf(report + used, sizeof(report) - used, "; arrows=%u first=%s", counter.count,
                          counter.first);

    step = "mount_archive";
    uint64_t handle = 0;
    if (mod.resources->mount_archive("mods/linkspan-resource-probe.zip", &handle) != SHIP_NATIVE_OK || !handle) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@mount");
        return Write(write, writer, report);
    }
    const char* markerPath = "test/linkspan-core-001-marker.txt";
    const char* markerExpected = "linkspan-oot-core-001-marker";
    char marker[64]{};
    uint32_t markerSize = 0;
    const bool mountedVisible = mod.resources->has_file(markerPath) == 1;
    const bool markerOk = mountedVisible &&
        mod.resources->read_file(markerPath, reinterpret_cast<uint8_t*>(marker), sizeof(marker) - 1,
                                 &markerSize) == SHIP_NATIVE_OK &&
        markerSize == std::strlen(markerExpected) && !std::memcmp(marker, markerExpected, markerSize);
    const auto unmount = mod.resources->unmount_archive(handle);
    const bool gone = mod.resources->has_file(markerPath) == 0;
    used += std::snprintf(report + used, sizeof(report) - used,
                          "; mount=ok handle=%llu marker=%s unmount=%s gone=%u",
                          static_cast<unsigned long long>(handle), markerOk ? "match" : "fail",
                          unmount == SHIP_NATIVE_OK ? "ok" : "fail", gone ? 1u : 0u);
    if (!markerOk || unmount != SHIP_NATIVE_OK || !gone) {
        std::snprintf(report + used, sizeof(report) - used, "; fail@%s", step);
    }
    return Write(write, writer, report);
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
        runtime->register_function(runtime->context, "resource_runtime_probe", ResourceRuntimeProbe, mod) !=
            SHIP_NATIVE_OK ||
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
