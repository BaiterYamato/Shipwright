#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "oot_engine.h"
#include "oot_registry.h"
#include "oot_resources.h"
#include "oot_layout_id.h"
#include "z64.h"

namespace {
// SDL usa posições Xbox. No Switch Pro: B->SDL A, A->SDL B, Y->SDL X, X->SDL Y.
constexpr uint8_t SDL_BUTTON_B_NINTENDO = 0;
constexpr uint8_t SDL_BUTTON_A_NINTENDO = 1;
constexpr uint8_t SDL_BUTTON_Y_NINTENDO = 2;
constexpr uint8_t SDL_BUTTON_X_NINTENDO = 3;
// Botão "−" (SDL_CONTROLLER_BUTTON_BACK): L do N64.
constexpr uint8_t SDL_BUTTON_MINUS = 4;
// Clique do analógico direito (SDL_CONTROLLER_BUTTON_RIGHTSTICK): atalho de lente e máscara.
constexpr uint8_t SDL_BUTTON_RIGHT_STICK = 8;
// L e R físicos (SDL_CONTROLLER_BUTTON_LEFTSHOULDER/RIGHTSHOULDER): escudo e seleção de item.
constexpr uint8_t SDL_BUTTON_L = 9;
constexpr uint8_t SDL_BUTTON_R = 10;
// D-pad direita (SDL_CONTROLLER_BUTTON_DPAD_RIGHT): C-Up nativo, Navi e primeira pessoa.
constexpr uint8_t SDL_BUTTON_DPAD_RIGHT = 14;
// ZR (SDL_CONTROLLER_AXIS_TRIGGERRIGHT): usa o item do botão C selecionado.
constexpr uint8_t SDL_AXIS_ZR = 5;
constexpr int16_t ZR_HELD = 8000;
constexpr uint32_t PhysicalButton(uint8_t button) { return uint32_t{ 1 } << button; }
constexpr const char* FREE_LOOK_SETTING = "gSettings.FreeLook.Enabled";
constexpr const char* PERSISTENT_MASKS_SETTING = "gEnhancements.PersistentMasks";
// Toque rápido no R3 alterna a lente; segurar por este tempo alterna a máscara.
constexpr long long SHORTCUT_HOLD_MILLISECONDS = 400;
// Botão virtual de cada índice de SaveContext.equips.buttonItems (1..3 = C).
constexpr uint16_t ITEM_BUTTONS[] = { 0, BTN_CLEFT, BTN_CDOWN, BTN_CRIGHT };

enum class Phase { Ready, Airborne, Rolling, Running };

struct Mod {
    const ShipOotEngineV1* engine;
    const ShipOotMovementV2* movement;
    const ShipOotResourcesV2* resources;
    const ShipOotRegistryV1* registry;
    uint64_t registrySpace = 0;
    uint64_t jumpEntry = 0;
    uint64_t sprintEntry = 0;
    uint16_t keyboardJumpMask = 0;
    Phase phase = Phase::Ready;
    bool jumpWasDown = false;
    bool observedRoll = false;
    bool faceBindingsApplied = false;
    bool freeLookApplied = false;
    int32_t previousFreeLook = 0;
    uint32_t cameraFollowDelayMilliseconds = 500;
    bool cameraFreeLookActive = false;
    std::chrono::steady_clock::time_point lastCameraInput = std::chrono::steady_clock::now();
    bool persistentMasksApplied = false;
    int32_t previousPersistentMasks = 0;
    bool shortcutWasDown = false;
    bool shortcutHoldFired = false;
    std::chrono::steady_clock::time_point shortcutPressedAt{};
    uint8_t selectedItemButton = LINKSPAN_OOT_ITEM_BUTTON_C_LEFT;
    bool selectWasDown = false;
};

enum class CameraChange { None, FreeLook, Automatic };

// Parado, a câmera livre mantém o ângulo escolhido. Quando Link volta a andar com
// o analógico direito parado há cameraFollowDelayMilliseconds, o FreeLook é
// desligado e o Camera_Normal1 do jogo assume a partir do eye atual: sem salto,
// girando para trás de Link com a suavização nativa. Desligar a CVar (em vez de
// só zerar play->manualCamera) impede que drift do analógico religue o modo manual.
CameraChange UpdateCamera(Mod& mod) {
    constexpr int CAMERA_DEADZONE = 12;
    constexpr int MOVE_DEADZONE = 20;
    const int x = mod.movement->get_right_stick_x(0);
    const int y = mod.movement->get_right_stick_y(0);
    const auto now = std::chrono::steady_clock::now();
    if ((x * x) + (y * y) >= CAMERA_DEADZONE * CAMERA_DEADZONE) {
        mod.lastCameraInput = now;
        if (!mod.cameraFreeLookActive && mod.movement->set_setting_int(FREE_LOOK_SETTING, 1) == SHIP_NATIVE_OK) {
            mod.cameraFreeLookActive = true;
            return CameraChange::FreeLook;
        }
        return CameraChange::None;
    }
    const int moveX = mod.movement->get_stick_x(0);
    const int moveY = mod.movement->get_stick_y(0);
    const bool moving = (moveX * moveX) + (moveY * moveY) >= MOVE_DEADZONE * MOVE_DEADZONE;
    if (mod.cameraFreeLookActive && moving &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - mod.lastCameraInput).count() >=
            mod.cameraFollowDelayMilliseconds &&
        mod.movement->set_setting_int(FREE_LOOK_SETTING, 0) == SHIP_NATIVE_OK) {
        mod.cameraFreeLookActive = false;
        return CameraChange::Automatic;
    }
    return CameraChange::None;
}

// R3: soltar antes de SHORTCUT_HOLD_MILLISECONDS alterna a lente; segurar até o
// limite alterna a máscara uma única vez e ignora a soltura. As regras do jogo
// (estado do Player, cena, idade, magia) ficam no host (movement v2).
const char* UpdateShortcut(Mod& mod, const Player& player, uint32_t physical) {
    const bool down = (physical & PhysicalButton(SDL_BUTTON_RIGHT_STICK)) != 0;
    const auto now = std::chrono::steady_clock::now();
    if (down && !mod.shortcutWasDown) {
        mod.shortcutPressedAt = now;
        mod.shortcutHoldFired = false;
    }
    const char* result = nullptr;
    const auto* play = static_cast<const PlayState*>(mod.engine->get_play_state());
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (play && save) {
        if (down && !mod.shortcutHoldFired &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - mod.shortcutPressedAt).count() >=
                SHORTCUT_HOLD_MILLISECONDS) {
            mod.shortcutHoldFired = true;
            // Máscara em uso: qualquer item de máscara a tira, como o botão C nativo.
            const uint8_t mask = player.currentMask != PLAYER_MASK_NONE
                ? static_cast<uint8_t>(ITEM_MASK_KEATON + player.currentMask - PLAYER_MASK_KEATON)
                : save->inventory.items[SLOT_TRADE_CHILD];
            result = mod.movement->player_use_item_shortcut(mask) != SHIP_NATIVE_OK ? "mask-blocked"
                     : player.currentMask != PLAYER_MASK_NONE                      ? "mask-on"
                                                                                    : "mask-off";
        } else if (!down && mod.shortcutWasDown && !mod.shortcutHoldFired) {
            result = mod.movement->player_use_item_shortcut(ITEM_LENS) != SHIP_NATIVE_OK ? "lens-blocked"
                     : play->actorCtx.lensActive                                           ? "lens-on"
                                                                                           : "lens-off";
        }
    }
    mod.shortcutWasDown = down;
    return result;
}

ShipNativeStatus Write(ShipNativeWriteFn write, void* writer, const char* text) {
    return write(writer, text, static_cast<uint32_t>(std::strlen(text)));
}

// ZR segura o botão C selecionado: o jogo o trata como o C nativo, inclusive
// mirar arco e gancho enquanto o gatilho continua pressionado.
ShipNativeStatus BindItemTrigger(Mod& mod, uint8_t itemButton) {
    for (uint8_t button = LINKSPAN_OOT_ITEM_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEM_BUTTON_C_RIGHT; ++button) {
        if (mod.movement->clear_gamepad_button_bindings(0, ITEM_BUTTONS[button]) != SHIP_NATIVE_OK)
            return SHIP_NATIVE_FAILURE;
    }
    return mod.movement->bind_gamepad_axis(0, ITEM_BUTTONS[itemButton], SDL_AXIS_ZR, 1);
}

ShipNativeStatus ApplyNintendoBindings(Mod& mod) {
    // A físico vira a ação contextual N64 A. Y e B alimentam N64 B: ataque e
    // cancelar/guardar continuam sendo decididos pelo estado normal do jogo.
    // D-pad direita alimenta o C-Up nativo (Navi e primeira pessoa), L físico o
    // escudo (R do N64) e "−" o L do N64. R3 e R são lidos fisicamente.
    const uint16_t remappedButtons[] = { BTN_A, BTN_B, BTN_CUP, BTN_DRIGHT, BTN_R, BTN_L };
    for (const uint16_t button : remappedButtons) {
        if (mod.movement->clear_gamepad_button_bindings(0, button) != SHIP_NATIVE_OK) {
            mod.movement->reload_gamepad_mappings(0);
            return SHIP_NATIVE_FAILURE;
        }
    }
    if (mod.movement->bind_gamepad_button(0, BTN_A, SDL_BUTTON_A_NINTENDO) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_B, SDL_BUTTON_Y_NINTENDO) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_B, SDL_BUTTON_B_NINTENDO) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_CUP, SDL_BUTTON_DPAD_RIGHT) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_R, SDL_BUTTON_L) != SHIP_NATIVE_OK ||
        mod.movement->bind_gamepad_button(0, BTN_L, SDL_BUTTON_MINUS) != SHIP_NATIVE_OK ||
        BindItemTrigger(mod, mod.selectedItemButton) != SHIP_NATIVE_OK) {
        mod.movement->reload_gamepad_mappings(0);
        return SHIP_NATIVE_FAILURE;
    }
    mod.faceBindingsApplied = true;
    return SHIP_NATIVE_OK;
}

// Próximo botão C com item, na ordem C-Left, C-Down, C-Right.
uint8_t NextItemButton(uint8_t current, const SaveContext& save) {
    for (uint8_t step = 1; step <= 3; ++step) {
        const uint8_t candidate = static_cast<uint8_t>((current - 1 + step) % 3 + 1);
        if (save.equips.buttonItems[candidate] != ITEM_NONE) return candidate;
    }
    return current;
}

// R troca o botão C usado pelo ZR. Com o ZR pressionado não troca: soltaria o item em uso.
const char* UpdateItemSelection(Mod& mod, uint32_t physical) {
    const bool down = (physical & PhysicalButton(SDL_BUTTON_R)) != 0;
    const bool pressed = down && !mod.selectWasDown;
    mod.selectWasDown = down;
    const auto* save = static_cast<const SaveContext*>(mod.engine->get_save_context());
    if (!pressed || !save || mod.movement->get_gamepad_axis(0, SDL_AXIS_ZR) > ZR_HELD) return nullptr;
    const uint8_t next = NextItemButton(mod.selectedItemButton, *save);
    if (next == mod.selectedItemButton) return nullptr;
    if (BindItemTrigger(mod, next) != SHIP_NATIVE_OK) {
        mod.movement->reload_gamepad_mappings(0);
        mod.faceBindingsApplied = false;
        return "mapping-error";
    }
    mod.selectedItemButton = next;
    return next == LINKSPAN_OOT_ITEM_BUTTON_C_LEFT   ? "item-c-left"
           : next == LINKSPAN_OOT_ITEM_BUTTON_C_DOWN ? "item-c-down"
                                                     : "item-c-right";
}

ShipNativeStatus SHIP_NATIVE_CALL Status(void* user, const char*, uint32_t length,
                                        ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    char result[448];
    const int size = std::snprintf(result, sizeof(result),
        "perfil=nintendo; X=pulo %.2f; A=contexto/rolar/sprint; Y=espada; B=cancelar; "
        "ZR=item do C selecionado; R=troca o C; L=escudo; -=L do N64; "
        "R3=lente (toque)/máscara (segurar); D-pad direita=C-Up; "
        "câmera=stick direito, volta a seguir ao andar após %.2fs; gamepad=%s",
        double(JUMP_VELOCITY), double(mod.cameraFollowDelayMilliseconds) / 1000.0,
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
    // Segundo valor: atraso em ms (0..60000) sem analógico direito antes de a
    // câmera voltar a seguir Link quando ele anda.
    unsigned long followDelay = 500;
    if (*end == ',') {
        char* delayEnd = nullptr;
        followDelay = std::strtoul(end + 1, &delayEnd, 10);
        if (delayEnd == end + 1 || *delayEnd != '\0' || followDelay > 60000)
            return SHIP_NATIVE_INVALID_ARGUMENT;
    } else if (*end != '\0') {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& mod = *static_cast<Mod*>(user);
    mod.keyboardJumpMask = static_cast<uint16_t>(parsed);
    mod.cameraFollowDelayMilliseconds = static_cast<uint32_t>(followDelay);
    if (!mod.freeLookApplied) {
        mod.previousFreeLook = mod.movement->get_setting_int(FREE_LOOK_SETTING, 0);
        if (mod.movement->set_setting_int(FREE_LOOK_SETTING, 1) != SHIP_NATIVE_OK) {
            return SHIP_NATIVE_FAILURE;
        }
        mod.freeLookApplied = true;
        mod.cameraFreeLookActive = true;
        mod.lastCameraInput = std::chrono::steady_clock::now();
    }
    // A máscara colocada pelo R3 fica fora dos botões C; sem PersistentMasks o
    // jogo a tiraria (Player_ProcessItemButtons) e o host recusa o uso.
    if (!mod.persistentMasksApplied) {
        mod.previousPersistentMasks = mod.movement->get_setting_int(PERSISTENT_MASKS_SETTING, 0);
        if (mod.movement->set_setting_int(PERSISTENT_MASKS_SETTING, 1) != SHIP_NATIVE_OK) {
            return SHIP_NATIVE_FAILURE;
        }
        mod.persistentMasksApplied = true;
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

struct LayerProbeState {
    char archives[320]{};
    uint32_t used = 0;
    uint32_t count = 0;
    uint64_t mergedHash = 14695981039346656037ULL;
};

ShipNativeStatus SHIP_NATIVE_CALL CollectLayer(void* user, const ShipOotResourceLayerV2* layer,
                                               const char* archivePath, const uint8_t* data) {
    auto& state = *static_cast<LayerProbeState*>(user);
    if (!layer || layer->size < sizeof(ShipOotResourceLayerV2) || !archivePath ||
        (!data && layer->data_size) || layer->layer_index != state.count) {
        return SHIP_NATIVE_FAILURE;
    }
    const int written = std::snprintf(state.archives + state.used, sizeof(state.archives) - state.used,
                                      "%s%.*s", state.count ? ">" : "",
                                      static_cast<int>(layer->archive_path_length), archivePath);
    if (written < 0 || static_cast<uint32_t>(written) >= sizeof(state.archives) - state.used) {
        return SHIP_NATIVE_LIMIT;
    }
    state.used += static_cast<uint32_t>(written);
    for (uint32_t i = 0; i < layer->data_size; ++i) {
        state.mergedHash ^= data[i];
        state.mergedHash *= 1099511628211ULL;
    }
    ++state.count;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL LayerProbe(void* user, const char* payload, uint32_t length,
                                             ShipNativeWriteFn write, void* writer) {
    if ((!payload && length) || length > 4096) return SHIP_NATIVE_INVALID_ARGUMENT;
    char path[4097]{};
    if (length) std::memcpy(path, payload, length);
    else std::memcpy(path, "test/layers.json", sizeof("test/layers.json"));
    LayerProbeState state;
    const auto& mod = *static_cast<Mod*>(user);
    const auto status = mod.resources->read_file_layers(path, CollectLayer, &state);
    if (status != SHIP_NATIVE_OK) return status;
    char report[448];
    const int size = std::snprintf(report, sizeof(report), "layers=%u; order=%s; merged=%016llx",
                                   state.count, state.archives,
                                   static_cast<unsigned long long>(state.mergedHash));
    return size > 0 && size < int(sizeof(report)) ? write(writer, report, uint32_t(size)) : SHIP_NATIVE_FAILURE;
}

ShipNativeStatus SHIP_NATIVE_CALL LayerRuntimeProbe(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                                    void* writer) {
    if (length)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    uint64_t base = 0;
    uint64_t overrideLayer = 0;
    if (mod.resources->mount_archive("mods/linkspan-layer-base.zip", &base) != SHIP_NATIVE_OK || !base) {
        return Write(write, writer, "fail@mount-base");
    }
    const auto mountedOverride = mod.resources->mount_archive("mods/linkspan-layer-override.zip", &overrideLayer);
    if (mountedOverride != SHIP_NATIVE_OK || !overrideLayer) {
        mod.resources->unmount_archive(base);
        return Write(write, writer, "fail@mount-override");
    }

    LayerProbeState state;
    const auto read = mod.resources->read_file_layers("unbound/layer-probe.json", CollectLayer, &state);
    const auto unmountOverride = mod.resources->unmount_archive(overrideLayer);
    const auto unmountBase = mod.resources->unmount_archive(base);
    if (read != SHIP_NATIVE_OK || unmountOverride != SHIP_NATIVE_OK || unmountBase != SHIP_NATIVE_OK) {
        return Write(write, writer, "fail@read-or-cleanup");
    }
    char report[448];
    const int size = std::snprintf(report, sizeof(report), "layers=%u; order=%s; merged=%016llx; cleanup=ok",
                                   state.count, state.archives, static_cast<unsigned long long>(state.mergedHash));
    return size > 0 && size < int(sizeof(report)) ? write(writer, report, uint32_t(size)) : SHIP_NATIVE_FAILURE;
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

struct RegistryCounter {
    uint32_t count = 0;
    int32_t firstId = LINKSPAN_OOT_REGISTRY_AUTO_ID;
};

ShipNativeStatus SHIP_NATIVE_CALL CountRegistryEntry(void* user, uint64_t, int32_t id,
                                                     const char*, uint32_t) {
    auto& counter = *static_cast<RegistryCounter*>(user);
    if (!counter.count) counter.firstId = id;
    ++counter.count;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL RegistryProbe(void* user, const char*, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    uint64_t space = 0;
    if (mod.registry->find_space("example/dynamic_movement/actions", &space) != SHIP_NATIVE_OK ||
        space != mod.registrySpace) {
        return Write(write, writer, "fail@space");
    }
    uint64_t entry = 0;
    int32_t id = 0;
    if (mod.registry->find_entry_by_name(space, "example/dynamic_movement/jump", &entry, &id) !=
            SHIP_NATIVE_OK ||
        entry != mod.jumpEntry) {
        return Write(write, writer, "fail@jump");
    }
    char name[64]{};
    uint8_t payload[64]{};
    uint32_t nameSize = 0;
    uint32_t payloadSize = 0;
    int32_t readId = 0;
    if (mod.registry->read_entry(entry, name, sizeof(name), &nameSize, payload, sizeof(payload),
                                 &payloadSize, &readId) != SHIP_NATIVE_OK) {
        return Write(write, writer, "fail@read");
    }
    RegistryCounter counter;
    if (mod.registry->list_entries(space, CountRegistryEntry, &counter) != SHIP_NATIVE_OK) {
        return Write(write, writer, "fail@list");
    }
    char report[256];
    const int size = std::snprintf(
        report, sizeof(report), "space=ok; entries=%u; first=%d; jump=%.*s:%.*s; id=%d",
        counter.count, counter.firstId, static_cast<int>(nameSize), name,
        static_cast<int>(payloadSize), reinterpret_cast<const char*>(payload), readId);
    return size > 0 && size < int(sizeof(report)) ? write(writer, report, uint32_t(size)) : SHIP_NATIVE_FAILURE;
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

// Posição do botão C selecionado para o anel do HUD Lua: "x,y,lado,alpha" ou "none".
ShipNativeStatus SHIP_NATIVE_CALL HudSelection(void* user, const char*, uint32_t length,
                                              ShipNativeWriteFn write, void* writer) {
    if (length) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& mod = *static_cast<Mod*>(user);
    int16_t x = 0;
    int16_t y = 0;
    int16_t side = 0;
    uint8_t alpha = 0;
    if (!mod.faceBindingsApplied || !mod.movement->has_gamepad(0) ||
        mod.movement->get_item_button_rect(mod.selectedItemButton, &x, &y, &side, &alpha) != SHIP_NATIVE_OK ||
        !alpha) {
        return Write(write, writer, "none");
    }
    char result[48];
    const int size = std::snprintf(result, sizeof(result), "%d,%d,%d,%u", x, y, side, unsigned(alpha));
    return size > 0 && size < int(sizeof(result)) ? write(writer, result, uint32_t(size)) : SHIP_NATIVE_FAILURE;
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
        mod.shortcutWasDown = false;
        mod.shortcutHoldFired = false;
        mod.selectWasDown = false;
        return Write(write, writer, "unavailable");
    }
    const CameraChange cameraChange = UpdateCamera(mod);

    const bool hasGamepad = mod.movement->has_gamepad(0) != 0;
    if (hasGamepad && !mod.faceBindingsApplied && ApplyNintendoBindings(mod) != SHIP_NATIVE_OK) {
        return Write(write, writer, "mapping-error");
    }
    const uint32_t physical = hasGamepad ? mod.movement->get_gamepad_buttons(0) : 0;
    if (const char* selection = UpdateItemSelection(mod, physical)) {
        return Write(write, writer, selection);
    }
    if (const char* shortcut = UpdateShortcut(mod, *player, physical)) {
        return Write(write, writer, shortcut);
    }
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
    const auto* movement = static_cast<const ShipOotMovementV2*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_MOVEMENT_SERVICE, LINKSPAN_OOT_MOVEMENT_VERSION_2,
        sizeof(ShipOotMovementV2)));
    const auto* resources = static_cast<const ShipOotResourcesV2*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION_2,
        sizeof(ShipOotResourcesV2)));
    const auto* registry = static_cast<const ShipOotRegistryV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_REGISTRY_SERVICE, LINKSPAN_OOT_REGISTRY_VERSION,
        sizeof(ShipOotRegistryV1)));
    if (!engine || engine->size < sizeof(ShipOotEngineV1) || !engine->layout_id ||
        std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) ||
        engine->player_size != sizeof(Player) || engine->play_state_size != sizeof(PlayState) ||
        engine->save_context_size != sizeof(SaveContext) || !engine->get_player || !movement ||
        movement->size < sizeof(ShipOotMovementV2) || !movement->layout_id ||
        std::strcmp(movement->layout_id, LINKSPAN_OOT_LAYOUT_ID) ||
        !movement->get_input_current || !movement->get_stick_x || !movement->get_stick_y ||
        !movement->get_right_stick_x || !movement->get_right_stick_y ||
        !movement->has_gamepad || !movement->get_gamepad_buttons ||
        !movement->clear_gamepad_button_bindings || !movement->bind_gamepad_button ||
        !movement->reload_gamepad_mappings || !movement->get_setting_int || !movement->set_setting_int ||
        !movement->is_player_grounded ||
        !movement->is_player_rolling || !movement->player_jump || !movement->player_roll ||
        !movement->get_gamepad_axis || !movement->bind_gamepad_axis || !movement->player_use_item_shortcut ||
        !movement->get_item_button_rect)
        return SHIP_NATIVE_UNSUPPORTED;
    if (!resources || resources->size < sizeof(ShipOotResourcesV2) || !resources->has_file ||
        !resources->read_file || !resources->list_files || !resources->dirty_resources ||
        !resources->unload_resource || !resources->mount_archive || !resources->unmount_archive ||
        !resources->get_game_versions || !resources->read_file_layers) return SHIP_NATIVE_UNSUPPORTED;
    if (!registry || registry->size < sizeof(ShipOotRegistryV1) || !registry->create_space ||
        !registry->find_space || !registry->destroy_space || !registry->register_entry ||
        !registry->unregister_entry || !registry->find_entry_by_name || !registry->find_entry_by_id ||
        !registry->read_entry || !registry->list_entries) return SHIP_NATIVE_UNSUPPORTED;
    auto* mod = new (std::nothrow) Mod{engine, movement, resources, registry};
    if (!mod) return SHIP_NATIVE_FAILURE;
    *instance = mod;
    if (registry->create_space("example/dynamic_movement/actions", 0x80, 0xFF, 1,
                               &mod->registrySpace) != SHIP_NATIVE_OK) {
        delete mod;
        *instance = nullptr;
        return SHIP_NATIVE_FAILURE;
    }
    int32_t assignedId = 0;
    static const char jumpPayload[] = "action=jump";
    static const char sprintPayload[] = "action=sprint";
    if (registry->register_entry(mod->registrySpace, "example/dynamic_movement/jump",
                                 LINKSPAN_OOT_REGISTRY_AUTO_ID,
                                 reinterpret_cast<const uint8_t*>(jumpPayload), sizeof(jumpPayload) - 1,
                                 &mod->jumpEntry, &assignedId) != SHIP_NATIVE_OK ||
        registry->register_entry(mod->registrySpace, "example/dynamic_movement/sprint",
                                 LINKSPAN_OOT_REGISTRY_AUTO_ID,
                                 reinterpret_cast<const uint8_t*>(sprintPayload), sizeof(sprintPayload) - 1,
                                 &mod->sprintEntry, &assignedId) != SHIP_NATIVE_OK) {
        registry->destroy_space(mod->registrySpace);
        delete mod;
        *instance = nullptr;
        return SHIP_NATIVE_FAILURE;
    }
    if (runtime->register_function(runtime->context, "status", Status, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "configure", Configure, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "jump", Jump, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "resource_probe", ResourceProbe, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "layer_probe", LayerProbe, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "layer_runtime_probe", LayerRuntimeProbe, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "resource_runtime_probe", ResourceRuntimeProbe, mod) !=
            SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "registry_probe", RegistryProbe, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "hud_selection", HudSelection, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "update", Update, mod) != SHIP_NATIVE_OK) {
        registry->destroy_space(mod->registrySpace);
        delete mod;
        *instance = nullptr;
        return SHIP_NATIVE_FAILURE;
    }
    return SHIP_NATIVE_OK;
}

bool LensOnItemButton(const Mod& mod, const SaveContext& save) {
    const int count = mod.movement->get_setting_int("gEnhancements.DpadEquips", 0) != 0 ? 8 : 4;
    for (int button = 1; button < count; ++button) {
        if (save.equips.buttonItems[button] == ITEM_LENS) return true;
    }
    return false;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* mod = static_cast<Mod*>(instance);
    if (mod && mod->faceBindingsApplied) mod->movement->reload_gamepad_mappings(0);
    if (mod && mod->freeLookApplied) mod->movement->set_setting_int(FREE_LOOK_SETTING, mod->previousFreeLook);
    if (mod && mod->persistentMasksApplied)
        mod->movement->set_setting_int(PERSISTENT_MASKS_SETTING, mod->previousPersistentMasks);
    // Lente fora dos botões só seguia ligada pelo atalho: desligá-la deixa o
    // Magic_Update encerrar o consumo com o som nativo.
    if (mod) {
        auto* play = static_cast<PlayState*>(mod->engine->get_play_state());
        const auto* save = static_cast<const SaveContext*>(mod->engine->get_save_context());
        if (play && save && play->actorCtx.lensActive && !LensOnItemButton(*mod, *save))
            play->actorCtx.lensActive = false;
    }
    if (mod && mod->registrySpace) mod->registry->destroy_space(mod->registrySpace);
    delete mod;
}
}

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, SHIP_NATIVE_ABI_MINOR, Init, Shutdown
    };
    return &descriptor;
}
