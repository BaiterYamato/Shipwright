#include "ShipLuaBootstrap.h"
#include "native/OotNativeEngine.h"
#include "native/OotNativeRegistry.h"
#include "native/OotNativeScenes.h"
#include "native/OotNativeHooks.h"
#include "native/OotNativeItems.h"
#include "native/OotNativeView.h"
#include "native/OotNativeLights.h"
#include "native/OotNativeWorld.h"
#include "native/OotNativeSkeletons.h"
#include "native/OotNativeJsonTypes.h"
#include "native/OotNativeText.h"
#include "native/OotNativeSave.h"
#include "OotActorProvider.h"
#include "OotHotkeyRegistry.h"
#include "OotWorldAdapter.h"
#include "SohGui/SohMenuModRegistry.h"
#include "ShipLuaPuppet.h"
#include "mmform/MmGoronForm.h"
#include "soh/Enhancements/item-tables/ItemTableTypes.h"
#include "soh/Enhancements/item-tables/ItemTableManager.h"
// gMagicMeterFillTex: textura de preenchimento sólido reaproveitada por
// ship.hud.draw_rect (mesma que o medidor de magia usa).
#include "textures/parameter_static/parameter_static.h"

#include <filesystem>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <cstring>

#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToButtonMapping.h>
#include <ship/controller/controldevice/controller/mapping/sdl/SDLButtonToButtonMapping.h>
#include <ship/debug/Console.h>
#include <ship/resource/File.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/Archive.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/resource/archive/O2rArchive.h>
#include <shiplua/generated/ApiBindings.h>
#include <shiplua/host/ModHost.h>
#include <shiplua/manifest/ManifestParser.h>
#include <shiplua/runtime/LuaRuntime.h>
#include <shiplua/storage/AtomicFile.h>
#include <shiplua/storage/KeyValueStorage.h>
#include <shiplua/timer/FrameTimerScheduler.h>
#include <shiplua/world/WorldHandoff.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageInterfaceAddon.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/ShipUtils.h"
#include "soh/mmaudio/MmSoundFont.h"
#include "soh/mmaudio/MmSfxPlayer.h"
#include "soh/mmaudio/mmseq/MmAudioEngine.h"
#include "align_asset_macro.h"
#include "soh/ShipInit.hpp"
// OPEN_DISPS declara FrameInterpolation_* em escopo de bloco com linkage C++;
// este header traz as declarações extern "C" corretas.
#include "soh/frame_interpolation.h"
#include "soh/cvar_prefixes.h"
#include <libultraship/bridge.h>

extern "C" {
#include "functions.h"
#include "lauxlib.h"
#include "lua.h"
#include "macros.h"
#include "variables.h"
#include "z64.h"
extern PlayState* gPlayState;
extern u8 gWalkSpeedToggle;

// Emite uma display list arbitrária no buffer opaco. Precisa de linkage C: o
// macro OPEN_DISPS declara FrameInterpolation_* em escopo de bloco, e dentro
// de código C++ isso vira um símbolo mangled que não existe.
// Declaradas em z_player.c; iniciam e identificam o rolamento nativo do OoT.
// O parâmetro se chama "this" no C original — aqui precisa de outro nome.
void Player_SetupRoll(Player* player, PlayState* play);
void Player_Action_Roll(Player* player, PlayState* play);
void Player_Action_Idle(Player* player, PlayState* play);
// Animacao de espera corrente do Player (z_player.c:2113, nao e static). Usada
// para devolver a pose quando a cutscene da mascara solta o corpo.
LinkAnimationHeader* Player_GetIdleAnim(Player* player);
// Solta o jogador da escada e o deixa cair: limpa CLIMBING_LADDER, transiciona
// a ação e dá um empurrão para trás. É o mesmo caminho que o engine usa quando
// o jogador larga a escada (z_player.c:7890) — não é invenção nossa.
void func_8083FB7C(Player* player, PlayState* play);
void Player_Action_80845EF8(Player* player, PlayState* play);
void FileChoose_LoadGame(GameState* gameState);
u32 Save_Exist(int fileNum);

static void ShipLuaEmitDisplayList(PlayState* play, const char* path) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)path);
    CLOSE_DISPS(play->state.gfxCtx);
}

static void ShipLuaEmitScaledDisplayList(PlayState* play, const char* path, f32 scaleX, f32 scaleY, f32 scaleZ) {
    Matrix_Push();
    Matrix_Scale(scaleX, scaleY, scaleZ, MTXMODE_APPLY);
    OPEN_DISPS(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)path);
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Pop();
}
}

// OOT-VANILLA-001: lida pelas funções inline de soh/src/code/linkspan_vanilla.h; 1 quando algum mod carregou no boot.
extern "C" {
uint8_t gLinkSpanEngineExtended = 0;
void Fast_SetQuantizeFloatMtx(bool on);
}

namespace ShipLuaHost {
namespace {

std::unique_ptr<ShipLua::ModHost> gModHost;
std::shared_ptr<OotActorProvider> gActorProvider;
std::shared_ptr<ShipLua::CapabilityRegistry> gCapabilityRegistry;
std::shared_ptr<OotHotkeyRegistry> gHotkeys;
std::shared_ptr<SohGui::SohMenuModRegistry> gMenuRegistry;
std::map<uint64_t, std::shared_ptr<Ship::Archive>> gNativeResourceArchives;
uint64_t gNextNativeResourceArchive = 1;

std::shared_ptr<Ship::ResourceManager> NativeResourceManager() {
    auto* context = Ship::Context::GetRawInstance();
    return context ? context->GetResourceManager() : nullptr;
}

uint8_t NativeHasResourceFile(const char* path) {
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    return archives && archives->HasFile(path) ? 1 : 0;
}

ShipNativeStatus NativeReadResourceFile(const char* path, uint8_t* output, uint32_t capacity,
                                        uint32_t* outputSize) {
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    const auto file = archives ? archives->LoadFile(path) : nullptr;
    if (!file || !file->IsLoaded || !file->Buffer || file->Buffer->size() > UINT32_MAX) {
        *outputSize = 0;
        return SHIP_NATIVE_UNSUPPORTED;
    }
    *outputSize = static_cast<uint32_t>(file->Buffer->size());
    if (!output && capacity == 0) return SHIP_NATIVE_OK;
    if (capacity < *outputSize) return SHIP_NATIVE_LIMIT;
    if (*outputSize) std::memcpy(output, file->Buffer->data(), *outputSize);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeListResourceFiles(const char* mask, ShipOotResourcePathFn callback, void* user) {
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    const auto files = archives ? archives->ListFiles(mask) : nullptr;
    if (!files) return SHIP_NATIVE_UNSUPPORTED;
    auto ordered = *files;
    std::sort(ordered.begin(), ordered.end());
    for (const auto& path : ordered) {
        if (path.size() > UINT32_MAX) return SHIP_NATIVE_LIMIT;
        const auto status = callback(user, path.c_str(), static_cast<uint32_t>(path.size()));
        if (status != SHIP_NATIVE_OK) return status;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeDirtyResources(const char* mask) {
    const auto manager = NativeResourceManager();
    if (!manager) return SHIP_NATIVE_UNSUPPORTED;
    manager->DirtyResources(mask);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeUnloadResource(const char* path) {
    const auto manager = NativeResourceManager();
    if (!manager) return SHIP_NATIVE_UNSUPPORTED;
    manager->UnloadResource(path);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeMountResourceArchive(const char* archivePath, uint64_t* handle) {
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    if (!archives) return SHIP_NATIVE_UNSUPPORTED;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(archivePath, error).lexically_normal();
    if (error || (!std::filesystem::is_regular_file(absolute, error) &&
                  !std::filesystem::is_directory(absolute, error))) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    // Barras normais: o FolderArchive casa o caminho base com a listagem em generic_string, e com barra
    // invertida a pasta montava sem nenhum arquivo.
    const auto mounted = archives->AddArchive(absolute.generic_string());
    if (!mounted) return SHIP_NATIVE_FAILURE;
    for (const auto& [hash, path] : *mounted->ListFiles()) {
        (void)hash;
        manager->UnloadResource(path);
    }
    const uint64_t next = gNextNativeResourceArchive++;
    if (!next) return SHIP_NATIVE_LIMIT;
    gNativeResourceArchives.emplace(next, mounted);
    *handle = next;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeUnmountResourceArchive(uint64_t handle) {
    const auto entry = gNativeResourceArchives.find(handle);
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    if (entry == gNativeResourceArchives.end()) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!archives) return SHIP_NATIVE_UNSUPPORTED;
    for (const auto& [hash, path] : *entry->second->ListFiles()) {
        (void)hash;
        manager->UnloadResource(path);
    }
    if (archives->RemoveArchive(entry->second) != 1) return SHIP_NATIVE_FAILURE;
    gNativeResourceArchives.erase(entry);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeGetResourceGameVersions(uint32_t* output, uint32_t capacity, uint32_t* outputCount) {
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    if (!archives) return SHIP_NATIVE_UNSUPPORTED;
    auto versions = archives->GetGameVersions();
    std::sort(versions.begin(), versions.end());
    versions.erase(std::unique(versions.begin(), versions.end()), versions.end());
    if (versions.size() > UINT32_MAX) return SHIP_NATIVE_LIMIT;
    *outputCount = static_cast<uint32_t>(versions.size());
    if (!output && capacity == 0) return SHIP_NATIVE_OK;
    if (capacity < *outputCount) return SHIP_NATIVE_LIMIT;
    std::copy(versions.begin(), versions.end(), output);
    return SHIP_NATIVE_OK;
}

uint64_t NativeResourceContentHash(const uint8_t* data, size_t size) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

ShipNativeStatus NativeReadResourceFileLayers(const char* path, ShipOotResourceLayerFn callback, void* user) {
    const auto manager = NativeResourceManager();
    const auto archiveManager = manager ? manager->GetArchiveManager() : nullptr;
    const auto archives = archiveManager ? archiveManager->GetArchives() : nullptr;
    if (!archives) return SHIP_NATIVE_UNSUPPORTED;

    struct Layer {
        std::shared_ptr<Ship::Archive> archive;
        std::shared_ptr<Ship::File> file;
    };
    std::vector<Layer> layers;
    for (const auto& archive : *archives) {
        if (!archive || !archive->HasFile(path)) continue;
        auto file = archive->LoadFile(path);
        if (file && file->IsLoaded && file->Buffer && file->Buffer->size() <= UINT32_MAX) {
            layers.push_back({ archive, std::move(file) });
        }
    }
    if (layers.empty() || layers.size() > UINT32_MAX) return SHIP_NATIVE_UNSUPPORTED;

    const uint32_t layerCount = static_cast<uint32_t>(layers.size());
    for (uint32_t i = 0; i < layerCount; ++i) {
        const auto& archive = layers[i].archive;
        const auto& buffer = *layers[i].file->Buffer;
        const auto* data = reinterpret_cast<const uint8_t*>(buffer.data());
        const auto& archivePath = archive->GetPath();
        if (archivePath.size() > UINT32_MAX) return SHIP_NATIVE_LIMIT;
        const ShipOotResourceLayerV2 info{
            sizeof(ShipOotResourceLayerV2), i, layerCount,
            archive->HasGameVersion() ? archive->GetGameVersion() : 0,
            NativeResourceContentHash(data, buffer.size()), static_cast<uint32_t>(buffer.size()),
            static_cast<uint32_t>(archivePath.size())
        };
        const auto status = callback(user, &info, archivePath.c_str(), data);
        if (status != SHIP_NATIVE_OK) return status;
    }
    return SHIP_NATIVE_OK;
}

void ClearNativeResourceArchives() {
    std::vector<uint64_t> handles;
    for (const auto& [handle, archive] : gNativeResourceArchives) {
        (void)archive;
        handles.push_back(handle);
    }
    for (const auto handle : handles) NativeUnmountResourceArchive(handle);
    gNativeResourceArchives.clear();
}

std::shared_ptr<Ship::Controller> GetNativeController(uint8_t port) {
    auto* context = Ship::Context::GetRawInstance();
    auto deck = context ? context->GetControlDeck() : nullptr;
    return deck && port < 4 ? deck->GetControllerByPort(port) : nullptr;
}

uint8_t NativeHasGamepad(uint8_t port) {
    auto* context = Ship::Context::GetRawInstance();
    auto deck = context ? context->GetControlDeck() : nullptr;
    auto manager = deck ? deck->GetConnectedPhysicalDeviceManager() : nullptr;
    return manager && !manager->GetConnectedSDLGamepadsForPort(port).empty() ? 1 : 0;
}

uint32_t NativeGamepadButtons(uint8_t port) {
    auto* context = Ship::Context::GetRawInstance();
    auto deck = context ? context->GetControlDeck() : nullptr;
    auto manager = deck ? deck->GetConnectedPhysicalDeviceManager() : nullptr;
    if (!manager || deck->GamepadGameInputBlocked()) {
        return 0;
    }
    uint32_t buttons = 0;
    for (const auto& [instanceId, gamepad] : manager->GetConnectedSDLGamepadsForPort(port)) {
        (void)instanceId;
        for (uint8_t button = 0; button < SDL_CONTROLLER_BUTTON_MAX && button < 32; ++button) {
            if (SDL_GameControllerGetButton(gamepad, static_cast<SDL_GameControllerButton>(button))) {
                buttons |= uint32_t{ 1 } << button;
            }
        }
    }
    return buttons;
}

ShipNativeStatus NativeClearGamepadButtonBindings(uint8_t port, uint16_t virtualButton) {
    auto controller = GetNativeController(port);
    auto button = controller ? controller->GetButtonByBitmask(virtualButton) : nullptr;
    if (!button) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    button->ClearAllButtonMappingsForDeviceTypeTransient(Ship::PhysicalDeviceType::SDLGamepad);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeBindGamepadButton(uint8_t port, uint16_t virtualButton, uint8_t sdlButton) {
    auto controller = GetNativeController(port);
    auto button = controller ? controller->GetButtonByBitmask(virtualButton) : nullptr;
    if (!button || sdlButton >= SDL_CONTROLLER_BUTTON_MAX) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    button->AddButtonMapping(std::make_shared<Ship::SDLButtonToButtonMapping>(
        port, virtualButton, static_cast<SDL_GameControllerButton>(sdlButton)));
    return SHIP_NATIVE_OK;
}

int16_t NativeGamepadAxis(uint8_t port, uint8_t axis) {
    auto* context = Ship::Context::GetRawInstance();
    auto deck = context ? context->GetControlDeck() : nullptr;
    auto manager = deck ? deck->GetConnectedPhysicalDeviceManager() : nullptr;
    if (!manager || deck->GamepadGameInputBlocked() || axis >= SDL_CONTROLLER_AXIS_MAX) {
        return 0;
    }
    int16_t strongest = 0;
    for (const auto& [instanceId, gamepad] : manager->GetConnectedSDLGamepadsForPort(port)) {
        (void)instanceId;
        const int16_t value = SDL_GameControllerGetAxis(gamepad, static_cast<SDL_GameControllerAxis>(axis));
        if (std::abs(static_cast<int>(value)) > std::abs(static_cast<int>(strongest))) {
            strongest = value;
        }
    }
    return strongest;
}

// Transitório como NativeBindGamepadButton: sem SaveToConfig, o unload recarrega o perfil.
ShipNativeStatus NativeBindGamepadAxis(uint8_t port, uint16_t virtualButton, uint8_t axis, int8_t direction) {
    auto controller = GetNativeController(port);
    auto button = controller ? controller->GetButtonByBitmask(virtualButton) : nullptr;
    if (!button || axis >= SDL_CONTROLLER_AXIS_MAX || (direction != 1 && direction != -1)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    button->AddButtonMapping(
        std::make_shared<Ship::SDLAxisDirectionToButtonMapping>(port, virtualButton, axis, direction));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeReloadGamepadMappings(uint8_t port) {
    auto controller = GetNativeController(port);
    if (!controller) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    controller->ReloadAllMappingsFromConfig();
    return SHIP_NATIVE_OK;
}

int32_t NativeGetSettingInt(const char* name, int32_t fallback) {
    return CVarGetInteger(name, fallback);
}

ShipNativeStatus NativeSetSettingInt(const char* name, int32_t value) {
    CVarSetInteger(name, value);
    return SHIP_NATIVE_OK;
}
// Timers por frame (ship.timer.after/every). Precisa de Tick() todo frame —
// ver o hook OnGameFrameUpdate no Initialize. Sem isto, core.timers fica
// indisponível e mods que dependem de sequenciamento (por exemplo a animação
// de colocar máscara antes de trocar o corpo) degradam para ação instantânea.
std::shared_ptr<ShipLua::FrameTimerScheduler> gTimers;
// Stores persistentes. Guardados aqui para o flush periódico e o do shutdown —
// a gravação é adiada de propósito (ver KeyValueStorage::EnablePersistence).
std::shared_ptr<ShipLua::KeyValueStorage> gStorage;
std::shared_ptr<ShipLua::KeyValueStorage> gSharedStorage;
std::shared_ptr<OotWorldAdapter> gWorldAdapter;
HOOK_ID gLoadGameHook = 0;
HOOK_ID gImportTickHook = 0;
std::optional<int32_t> gPendingSaveLoadedSlot;
std::optional<ShipLua::WorldHandoff> gPendingHandoff;
HOOK_ID gActorDestroyHook = 0;
HOOK_ID gPlayDestroyHook = 0;
HOOK_ID gCustomBodyRestoreHook = 0;

constexpr int kSwitchWorldExitCode = 73;

struct BridgeConfig {
    std::filesystem::path sessionDirectory;
    std::filesystem::path handoffPath;
    std::array<std::byte, 16> sessionId{};
    std::array<std::byte, 32> authenticationKey{};
    std::uint64_t sequence = 0;
};

int HexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

template <std::size_t Size> bool ParseHex(const char* text, std::array<std::byte, Size>& output) {
    if (text == nullptr || std::char_traits<char>::length(text) != Size * 2) {
        return false;
    }
    for (std::size_t index = 0; index < Size; ++index) {
        const int high = HexDigit(text[index * 2]);
        const int low = HexDigit(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[index] = static_cast<std::byte>((high << 4) | low);
    }
    return true;
}

std::optional<BridgeConfig> GetBridgeConfig() {
    const char* sessionDirectory = std::getenv("LINKSPAN_SESSION_DIR");
    const char* handoffPath = std::getenv("LINKSPAN_HANDOFF_PATH");
    const char* sequence = std::getenv("LINKSPAN_SEQUENCE");
    if (sessionDirectory == nullptr || handoffPath == nullptr || sequence == nullptr) {
        return std::nullopt;
    }
    BridgeConfig config;
    config.sessionDirectory = sessionDirectory;
    config.handoffPath = handoffPath;
    if (!ParseHex(std::getenv("LINKSPAN_SESSION_ID"), config.sessionId) ||
        !ParseHex(std::getenv("LINKSPAN_AUTH_KEY"), config.authenticationKey)) {
        return std::nullopt;
    }
    char* end = nullptr;
    config.sequence = std::strtoull(sequence, &end, 10);
    if (end == sequence || *end != '\0' || config.sequence == 0) {
        return std::nullopt;
    }
    return config;
}

bool BothGamesAvailable() {
    const char* available = std::getenv("LINKSPAN_AVAILABLE_GAMES");
    if (available == nullptr) {
        return false;
    }
    const std::string games(available);
    return games.find("oot") != std::string::npos && games.find("mm") != std::string::npos;
}

[[noreturn]] void ExitForWorldSwitch() {
#ifdef _WIN32
    // RequestWorldTravel runs inside a Lua callback. std::exit executes CRT
    // teardown while that callback stack is still active and can trigger the
    // Windows fail-fast code 0xC0000409 before the launcher receives code 73.
    ::ExitProcess(static_cast<UINT>(kSwitchWorldExitCode));
#else
    std::_Exit(kSwitchWorldExitCode);
#endif
}

ShipLua::Result<void> RequestWorldTravel(const ShipLua::WorldDestination& destination) {
    const auto config = GetBridgeConfig();
    if (!config.has_value() || !BothGamesAvailable() || destination.world != ShipLua::WorldId::Mm ||
        gWorldAdapter == nullptr) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::Unsupported, "ponte Link-Span para MM indisponível");
    }
    if (gPlayState == nullptr || gPlayState->sceneNum != SCENE_LINKS_HOUSE) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::InvalidState,
                                          "o teleporte deve ser usado dentro da casa do Link");
    }
    const auto player = gWorldAdapter->CapturePlayerState();
    if (!player.isOk()) {
        return ShipLua::Result<void>::err(player.code, player.message);
    }
    ShipLua::WorldHandoff handoff;
    handoff.sessionId = config->sessionId;
    handoff.sequence = config->sequence;
    handoff.source = ShipLua::WorldId::Oot;
    handoff.destination = destination;
    handoff.player = *player.value;
    const auto written = ShipLua::WorldHandoffCodec::WriteFile(config->handoffPath, handoff, config->authenticationKey);
    if (!written.isOk()) {
        return written;
    }
    const auto requested = ShipLua::AtomicFile::Write(config->sessionDirectory / "next-world", "mm\n");
    if (!requested.isOk()) {
        std::error_code ignored;
        std::filesystem::remove(config->handoffPath, ignored);
        return requested;
    }
    SPDLOG_INFO("Link-Span exportou o estado OoT e solicitou troca para MM ({})", destination.id);
    spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& logger) { logger->flush(); });
    ExitForWorldSwitch();
}

void TryConsumeWorldHandoff() {
    const auto config = GetBridgeConfig();
    if (!config.has_value() || gWorldAdapter == nullptr || !std::filesystem::is_regular_file(config->handoffPath)) {
        return;
    }
    const auto handoff = ShipLua::WorldHandoffCodec::ReadFile(config->handoffPath, config->authenticationKey);
    if (!handoff.isOk()) {
        SPDLOG_ERROR("Link-Span rejeitou o handoff OoT: {}", handoff.message);
        return;
    }
    if (handoff.value->sessionId != config->sessionId || handoff.value->sequence + 1 != config->sequence ||
        handoff.value->destination.world != ShipLua::WorldId::Oot) {
        SPDLOG_ERROR("Link-Span rejeitou um handoff destinado a outra sessão ou jogo");
        return;
    }
    // Nada é aplicado aqui: o hook de load também dispara fora de um jogo
    // carregado (intro/arquivo novo), e escrever o save + forçar entrance
    // nesse ponto corrompe o estado. Guarda o handoff — e o arquivo em disco —
    // até um frame realmente jogável.
    gPendingHandoff = *handoff.value;
    SPDLOG_INFO("Link-Span recebeu um handoff para OoT ({}) — aguardando um save carregado",
                handoff.value->destination.id);
}

// Só importa com um jogo de verdade rodando: PlayState ativo, save com vida
// (arquivo carregado, não a intro) e nenhuma transição em curso.
bool WorldImportIsSafe() {
    if (gPlayState == nullptr) {
        return false;
    }
    if (gSaveContext.healthCapacity <= 0) {
        return false;
    }
    return gPlayState->transitionTrigger == TRANS_TRIGGER_OFF;
}

void TickWorldImport() {
    if (!gPendingHandoff.has_value() || gWorldAdapter == nullptr || !WorldImportIsSafe()) {
        return;
    }
    const auto config = GetBridgeConfig();
    if (!config.has_value()) {
        gPendingHandoff.reset();
        return;
    }

    const ShipLua::WorldHandoff handoff = *gPendingHandoff;
    const auto prepared = gWorldAdapter->PrepareImport(handoff.player, handoff.destination);
    if (!prepared.isOk()) {
        gPendingHandoff.reset();
        SPDLOG_ERROR("Link-Span não preparou a importação OoT: {}", prepared.message);
        return;
    }
    const auto committed = gWorldAdapter->CommitImport();
    if (!committed.isOk()) {
        gWorldAdapter->AbortImport();
        gPendingHandoff.reset();
        SPDLOG_ERROR("Link-Span não confirmou a importação OoT: {}", committed.message);
        return;
    }
    // Só agora o handoff pode sumir do disco: a viagem foi mesmo aplicada.
    gPendingHandoff.reset();
    std::error_code error;
    std::filesystem::remove(config->handoffPath, error);
    SPDLOG_INFO("Link-Span importou o estado compartilhado em OoT ({})", handoff.destination.id);
}

#ifdef _WIN32
std::filesystem::path RuntimeRoot() {
    if (const wchar_t* configured = _wgetenv(L"LINKSPAN_ROOT"); configured != nullptr && *configured != L'\0') {
        return configured;
    }
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        return std::filesystem::current_path();
    }
    executable.resize(length);
    return std::filesystem::path(executable).parent_path();
}

std::wstring QuotePowerShellLiteral(std::wstring value) {
    std::size_t position = 0;
    while ((position = value.find(L'\'', position)) != std::wstring::npos) {
        value.insert(position, 1, L'\'');
        position += 2;
    }
    return L"'" + value + L"'";
}
#endif

ShipLua::Logger CreateLogger() {
    return ShipLua::Logger([](ShipLua::LogLevel level, const std::string& modId, const std::string& message) {
        switch (level) {
            case ShipLua::LogLevel::Debug:
                SPDLOG_DEBUG("ShipLua [{}]: {}", modId, message);
                break;
            case ShipLua::LogLevel::Info:
                SPDLOG_INFO("ShipLua [{}]: {}", modId, message);
                break;
            case ShipLua::LogLevel::Warn:
                SPDLOG_WARN("ShipLua [{}]: {}", modId, message);
                break;
            case ShipLua::LogLevel::Error:
                SPDLOG_ERROR("ShipLua [{}]: {}", modId, message);
                break;
        }
    });
}

std::string GetHostVersion() {
    return std::to_string(gBuildVersionMajor) + "." + std::to_string(gBuildVersionMinor) + "." +
           std::to_string(gBuildVersionPatch);
}

std::int16_t DegreesToBinang(double degrees) {
    const double normalized = std::remainder(degrees, 360.0);
    return static_cast<std::int16_t>(std::lround(normalized * (65536.0 / 360.0)));
}

std::shared_ptr<OotActorProvider> CreateActorProvider() {
    OotActorProviderHooks hooks;
    hooks.objectReady = [](std::int16_t objectId) {
        if (gPlayState == nullptr) {
            return false;
        }
        const s32 objectIndex = Object_GetIndex(&gPlayState->objectCtx, objectId);
        return objectIndex >= 0 && Object_IsLoaded(&gPlayState->objectCtx, objectIndex);
    };
    hooks.spawn = [](const OotActorDefinition& definition, const ShipLua::ActorSpawnRequest& request) -> void* {
        if (gPlayState == nullptr) {
            return nullptr;
        }
        float x = static_cast<float>(request.x);
        float y = static_cast<float>(request.y);
        float z = static_cast<float>(request.z);
        std::int16_t rotationY = DegreesToBinang(request.rotationY);
        // Posição (0,0,0) significa "na frente do player": os mods ainda não
        // têm ship.transform.relative_to_player (plan-sdk §8.4).
        if (request.x == 0 && request.y == 0 && request.z == 0) {
            if (Player* player = GET_PLAYER(gPlayState); player != nullptr) {
                const float forward = 60.0f;
                x = player->actor.world.pos.x + Math_SinS(player->actor.shape.rot.y) * forward;
                y = player->actor.world.pos.y;
                z = player->actor.world.pos.z + Math_CosS(player->actor.shape.rot.y) * forward;
                rotationY = static_cast<std::int16_t>(player->actor.shape.rot.y + 0x8000);
            }
        }
        Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, definition.actorId, x, y, z,
                                     DegreesToBinang(request.rotationX), rotationY,
                                     DegreesToBinang(request.rotationZ), definition.params);
        if (spawned != nullptr && definition.key == "oot.link_child_puppet") {
            if (!ShipLuaPuppet_Attach(spawned, gPlayState)) {
                Actor_Kill(spawned);
                return nullptr;
            }
        }
        if (spawned != nullptr && definition.key == "oot.mm_elegy_statue") {
            if (!ShipLuaPuppet_AttachStatue(spawned)) {
                Actor_Kill(spawned);
                return nullptr;
            }
        }
        return spawned;
    };
    hooks.kill = [](void* actor) {
        if (actor != nullptr) {
            Actor_Kill(static_cast<Actor*>(actor));
        }
    };
    std::vector<OotActorDefinition> allowlist{
        { "oot.en_dog", ACTOR_EN_DOG, OBJECT_DOG, static_cast<std::int16_t>(0x8000) },
        { "oot.en_torch2", ACTOR_EN_TORCH2, OBJECT_TORCH2, 0 },
        // Host: En_Item00 (gameplay_keep, sempre carregado); update/draw são
        // substituídos por ShipLuaPuppet_Attach logo após o spawn.
        { "oot.link_child_puppet", ACTOR_EN_ITEM00, OBJECT_GAMEPLAY_KEEP, 0 },
        // Estátua da Elegia do MM: assets transplantados via mm-elegy-assets.o2r.
        { "oot.mm_elegy_statue", ACTOR_EN_ITEM00, OBJECT_GAMEPLAY_KEEP, 0 },
    };
    return std::make_shared<OotActorProvider>(std::move(allowlist), std::move(hooks), CreateLogger(), ACTOR_PLAYER);
}

ShipLua::Result<void> RegisterHostCapability(const std::string& id, const std::string& description) {
    if (gCapabilityRegistry == nullptr) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::InvalidState, "capability registry is unavailable");
    }
    const auto providerVersion = ShipLua::SemVersion::Parse(GetHostVersion());
    const auto capabilityVersion = ShipLua::SemVersion::Parse(std::string(ShipLua::Generated::kApiVersion));
    if (!providerVersion.isOk() || !capabilityVersion.isOk()) {
        return ShipLua::Result<void>::err(ShipLua::ErrorCode::HostFailure, "invalid Shipwright or ShipLua version");
    }
    ShipLua::CapabilityProvider offer;
    offer.name = "shipwright-native";
    offer.providerVersion = *providerVersion.value;
    offer.capabilityVersion = *capabilityVersion.value;
    offer.games = { "oot" };
    offer.stability = ShipLua::CapabilityStability::Experimental;
    offer.description = description;
    return gCapabilityRegistry->Register(id, std::move(offer));
}

ShipLua::Result<bool> ApplyPlayerSpeedMultiplier(double requested);

ShipLua::Result<ShipLua::LuaApiHostContext> CreateHostContext() {
    ShipLua::LuaApiHostContext context;
    context.gameId = "oot";
    context.resourceExists = [](const std::string& path) { return NativeHasResourceFile(path.c_str()) != 0; };
    context.hostVersion = GetHostVersion();
    context.capabilities = { "oot.player.jump",         "oot.spawn_dog",      "oot.player.bunny_hood",
                             "oot.player.mask",         "player.speed",       "player.fields",
                             "oot.player.attach_model", "mod.assets",         "oot.player.immunity",
                             "oot.player.weight",       "oot.player.roll",    "hooks.bridge",
                             "oot.player.custom_body",  "oot.player.held_item_model",
                             "hud.draw",                "hud.icons",          "game.state",
                             "input.actions",           "oot.env",             "save.events",
                             "oot.audio",
                             "oot.cutscene" };
    context.hotkeys = gHotkeys;
    context.capabilityRegistry = gCapabilityRegistry;
    context.actors = gActorProvider;
    context.timers = gTimers;
    context.setSpeedMultiplier = ApplyPlayerSpeedMultiplier;
    context.capabilities.push_back("core.timers");
    auto registered = RegisterHostCapability("oot.player.jump", "Apply a validated jump impulse to OoT Link.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("core.timers", "Per-frame timers owned by each mod (ship.timer).");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered =
        RegisterHostCapability("save.events", "Publish save.loaded after the loaded Player becomes available.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "hud.draw", "Draw arbitrary rectangles and text over the HUD from hook.<game>.hud.draw.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "hud.icons", "Draw validated resource textures as bounded HUD icons.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "game.state", "Read a stable gameplay, pause, dialog, cutscene, transition, death or loading state.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "input.actions", "Observe directional controller actions and consume only explicitly accepted presses.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("oot.env", "Read ambient world state: time of day, night flag and scene id.");
    registered = RegisterHostCapability("oot.audio",
                                        "Redirect the player voice sfx block to another form's voices.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "oot.cutscene", "Take over the camera with a dedicated sub-camera for a bounded number of frames.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("oot.spawn_dog", "Spawn the legacy OoT dog demo actor.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("oot.player.bunny_hood",
                                        "Equip the OoT Bunny Hood with Majora's Mask speed and jump behaviour.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("oot.player.mask", "Equip any OoT mask by logical name.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "oot.player.custom_body",
        "Replace the player's visual body with any mod-provided skeleton and named animations, surviving scene "
        "changes automatically.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "oot.player.held_item_model",
        "Draw an arbitrary display list anchored to the player's hand bones, updated every frame.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("player.speed", "Scale the player's movement speed by a validated factor.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("player.fields", "Read and write named player fields with range validation.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("oot.player.attach_model",
                                        "Draw an arbitrary display list on the player by resource path.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered =
        RegisterHostCapability("mod.assets", "Mount mod-provided archives under the mod/<name>/ namespace.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("oot.player.immunity", "Grant the player immunity to a damage kind.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("oot.player.weight", "Switch the player between normal and heavy weight.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("oot.player.roll", "Enable continuous, steerable chained rolling.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "hooks.bridge", "Generic bridge to native VB_*/On* GameInteractor hooks via ship.events.on(\"hook.*\").");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    if (const char* available = std::getenv("LINKSPAN_AVAILABLE_GAMES"); available != nullptr) {
        const std::string games(available);
        if (games.find("oot") != std::string::npos) {
            context.availableGames.push_back("oot");
        }
        if (games.find("mm") != std::string::npos) {
            context.availableGames.push_back("mm");
        }
    }
    if (BothGamesAvailable() && GetBridgeConfig().has_value()) {
        context.capabilities.push_back("world.travel");
        context.worldTravel = RequestWorldTravel;
        registered =
            RegisterHostCapability("world.travel", "Travel to a logical destination in the other Link-Span host.");
        if (!registered.isOk()) {
            return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
        }
    }

    // ship.storage: store persistente por mod, gravado no diretório do app (um
    // arquivo para todos os mods, namespaced internamente por mod). Antes disto
    // o host real nunca conectava um KeyValueStorage — ship.storage.set caía em
    // Unsupported e nada sobrevivia à sessão. Um arquivo corrompido não trava o
    // host (começa vazio e é sobrescrito na próxima gravação).
    {
        auto storage = std::make_shared<ShipLua::KeyValueStorage>();
        const std::filesystem::path storagePath = Ship::Context::GetPathRelativeToAppDirectory(
            "shiplua-storage.bin", Ship::Context::GetRawInstance()->GetShortName());
        const auto loaded = storage->EnablePersistence(storagePath);
        if (!loaded.isOk()) {
            SPDLOG_WARN("ShipLua: storage persistente corrompido ({}), recomeçando vazio: {}",
                        storagePath.string(), loaded.message);
        }
        gStorage = storage;
        context.storage = storage;
        context.capabilities.push_back("core.storage");
        registered = RegisterHostCapability(
            "core.storage", "Per-mod key-value storage persisted to disk across sessions.");
        if (!registered.isOk()) {
            return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
        }
    }

    if (gMenuRegistry != nullptr && context.storage != nullptr) {
        context.menu = gMenuRegistry;
        context.capabilities.push_back("core.menu");
        registered = RegisterHostCapability(
            "core.menu", "Declarative per-mod menu pages backed exclusively by ship.storage.");
        if (!registered.isOk()) {
            return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
        }
    }

    // ship.storage.shared: store COMPARTILHADO entre os dois jogos, num arquivo
    // no diretório de sessão do launcher. Só existe sob o launcher (BridgeConfig);
    // standalone não tem para onde compartilhar, então fica ausente e degrada.
    if (const auto& bridge = GetBridgeConfig(); bridge.has_value()) {
        auto shared = std::make_shared<ShipLua::KeyValueStorage>();
        const std::filesystem::path sharedPath = bridge->sessionDirectory / "shared-storage.bin";
        const auto loaded = shared->EnablePersistence(sharedPath);
        if (!loaded.isOk()) {
            SPDLOG_WARN("ShipLua: storage compartilhado corrompido ({}), recomeçando vazio: {}",
                        sharedPath.string(), loaded.message);
        }
        gSharedStorage = shared;
        context.sharedStorage = shared;
        context.capabilities.push_back("core.storage.shared");
        registered = RegisterHostCapability(
            "core.storage.shared", "Cross-game key-value storage shared between OoT and MM under the launcher.");
        if (!registered.isOk()) {
            return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
        }
    }
    return ShipLua::Result<ShipLua::LuaApiHostContext>::ok(std::move(context));
}

int LuaPlayerJump(lua_State* state) {
    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr || (player->stateFlags1 & PLAYER_STATE1_DEAD) != 0 || (player->actor.bgCheckFlags & 1) == 0) {
        lua_pushboolean(state, 0);
        return 1;
    }
    player->actor.velocity.y = 6.34375f;
    lua_pushboolean(state, 1);
    return 1;
}

// ship.player.set_speed_multiplier(factor): primitiva comum aos dois jogos.
// Dirige o SpeedModifier que o host já implementa, mas em modo incondicional
// (o padrão exige segurar um botão). Passar 1.0 restaura o estado anterior.
bool gSpeedForced = false;
float gSpeedPreviousValue = 1.0f;
int gSpeedPreviousToggleMode = 0;
u8 gSpeedPreviousToggleState = 0;

ShipLua::Result<bool> ApplyPlayerSpeedMultiplier(double requested) {
    if (!std::isfinite(requested) || requested < 0.1 || requested > 5.0) {
        SPDLOG_WARN("ShipLua set_speed_multiplier: fator fora da faixa 0.1–5.0");
        return ShipLua::Result<bool>::ok(false);
    }

    const bool restore = std::fabs(requested - 1.0) < 0.0001;
    if (!gSpeedForced && !restore) {
        gSpeedPreviousValue = CVarGetFloat(CVAR_CHEAT("SpeedModifier.Value"), 1.0f);
        gSpeedPreviousToggleMode = CVarGetInteger(CVAR_CHEAT("SpeedModifier.SpeedToggle"), 0);
        gSpeedPreviousToggleState = gWalkSpeedToggle;
        gSpeedForced = true;
    }

    if (restore) {
        if (gSpeedForced) {
            CVarSetFloat(CVAR_CHEAT("SpeedModifier.Value"), gSpeedPreviousValue);
            CVarSetInteger(CVAR_CHEAT("SpeedModifier.SpeedToggle"), gSpeedPreviousToggleMode);
            gWalkSpeedToggle = gSpeedPreviousToggleState;
            gSpeedForced = false;
        }
        SPDLOG_INFO("ShipLua set_speed_multiplier: velocidade restaurada");
    } else {
        CVarSetFloat(CVAR_CHEAT("SpeedModifier.Value"), static_cast<float>(requested));
        // Modo toggle ligado + toggle ativo = multiplicador sempre valendo,
        // sem depender de o jogador segurar o botão modificador.
        CVarSetInteger(CVAR_CHEAT("SpeedModifier.SpeedToggle"), 1);
        gWalkSpeedToggle = 1;
        SPDLOG_INFO("ShipLua set_speed_multiplier: velocidade x{:.2f}", requested);
    }
    // Os hooks COND_VB_SHOULD do SpeedModifiers avaliam a condição no momento
    // do REGISTRO — e só os widgets do menu chamam ShipInit::Init ao mudar o
    // CVar. Sem esta chamada, mudar o valor programaticamente não liga nada.
    ShipInit::Init(CVAR_CHEAT("SpeedModifier.Value"));

    return ShipLua::Result<bool>::ok(true);
}

// ---------------------------------------------------------------------------
// Primitivas de habilidade. Genéricas de propósito: servem para a forma Goron,
// mas também para qualquer outra forma, item ou mod de dificuldade.
// ---------------------------------------------------------------------------

// ship.oot.player.set_damage_immunity(kind, enabled): hoje só "fire". Apaga o
// corpo em chamas a cada frame, o que anula o dano contínuo de queimadura.
bool gFireImmunity = false;

// ship.oot.player.set_weight(kind): "heavy" usa a mecânica das botas de ferro
// (afunda na água, resiste a vento/empurrão); "normal" devolve as botas
// anteriores. É a tradução honesta de "peso de Goron" para os sistemas do OoT.
bool gHeavyWeight = false;
int8_t gPreviousBoots = 0;

// ship.oot.player.set_roll_mode(mode): "chain" encadeia rolamentos
// indefinidamente e permite dirigir durante o rolamento — a aproximação do
// rolamento contínuo do Goron usando a instrumentação que o engine já tem.
bool gChainRoll = false;
constexpr s16 kRollSteerStep = 0x400;   // giro por frame durante o rolamento
constexpr s32 kRollStickDeadzone = 10;  // abaixo disso, o rolamento acaba

int LuaSetDamageImmunity(lua_State* state) {
    const char* kind = luaL_checkstring(state, 1);
    const bool enabled = lua_toboolean(state, 2) != 0;
    if (std::strcmp(kind, "fire") != 0) {
        SPDLOG_WARN("ShipLua set_damage_immunity: tipo '{}' n\xC3\xA3o suportado (use 'fire')", kind);
        lua_pushboolean(state, 0);
        return 1;
    }
    gFireImmunity = enabled;
    SPDLOG_INFO("ShipLua set_damage_immunity: fogo {}", enabled ? "imune" : "normal");
    lua_pushboolean(state, 1);
    return 1;
}

int LuaSetWeight(lua_State* state) {
    const char* kind = luaL_optstring(state, 1, "normal");
    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr) {
        SPDLOG_WARN("ShipLua set_weight: fora de gameplay");
        lua_pushboolean(state, 0);
        return 1;
    }

    if (std::strcmp(kind, "heavy") == 0) {
        if (!gHeavyWeight) {
            gPreviousBoots = player->currentBoots;
            gHeavyWeight = true;
        }
        player->currentBoots = PLAYER_BOOTS_IRON;
        Player_SetBootData(play, player);
        SPDLOG_INFO("ShipLua set_weight: peso pesado (mec\xC3\xA2nica das botas de ferro)");
    } else if (std::strcmp(kind, "normal") == 0) {
        if (gHeavyWeight) {
            player->currentBoots = gPreviousBoots;
            Player_SetBootData(play, player);
            gHeavyWeight = false;
        }
        SPDLOG_INFO("ShipLua set_weight: peso normal");
    } else {
        SPDLOG_WARN("ShipLua set_weight: valor '{}' desconhecido (use 'heavy' ou 'normal')", kind);
        lua_pushboolean(state, 0);
        return 1;
    }

    lua_pushboolean(state, 1);
    return 1;
}

int LuaSetRollMode(lua_State* state) {
    const char* mode = luaL_optstring(state, 1, "vanilla");
    if (std::strcmp(mode, "chain") == 0) {
        gChainRoll = true;
    } else if (std::strcmp(mode, "vanilla") == 0) {
        gChainRoll = false;
    } else {
        SPDLOG_WARN("ShipLua set_roll_mode: modo '{}' desconhecido (use 'chain' ou 'vanilla')", mode);
        lua_pushboolean(state, 0);
        return 1;
    }
    SPDLOG_INFO("ShipLua set_roll_mode: {}", mode);
    lua_pushboolean(state, 1);
    return 1;
}

// ---------------------------------------------------------------------------
// Corpo visual customizado do player — v2, generalizada a partir da v1 (que
// só sabia fazer Goron). O mecanismo inteiro já era genérico; só a API não
// era. Nenhum nome de forma é conhecido pelo host: skeleton e animações são
// caminhos de resource arbitrários que o MOD fornece via ship.oot.player.
// set_body({skeleton=..., anims={nome=path,...}, default_anim=...}) — Zora,
// Deku, um lobo (Wolfos/Wolf Link), ou qualquer skeleton compatível com
// SkelAnime_InitLink cabem sem tocar em C++ de novo.
//
// O bloqueio original era real: skeletons com mais limbs que PLAYER_LIMB_MAX
// (22) não cabem nos buffers fixos do Player. A saída (achada minerando um
// fork de referência que já resolveu isto em produção) é NÃO reaproveitar o
// skelAnime do Player: usar um SkelAnime independente, inicializado com
// jointTable/morphTable NULL — SkelAnime_InitLink então aloca do tamanho real
// do header, seja lá qual for. Para esconder o corpo vanilla enquanto o nosso
// desenha, usamos PLAYER_STATE2_DISABLE_DRAW — flag vanilla já existente (o
// balconista a usa para esconder o Link atrás do balcão), não invenção nossa.
// O ator hospedeiro (En_Item00) segue a posição do player a cada frame; seu
// próprio draw roda pelo Actor_Draw normal, então a matriz de mundo já vem
// pronta — mesmo padrão do puppet do Kafei/Rauru, só que seguindo o player.
//
// Qual animação toca a cada frame é decidido pelo hook.oot.player.
// body_anim_select (Transform): o mod recebe speed/on_ground e devolve o
// NOME da animação (uma chave da tabela `anims` que ele mesmo definiu) — o
// host não sabe o que "andar" ou "nadar" significa para a forma de cada mod.
// Sem callback (ou resultado inválido), cai num fallback só quando o mod usa
// as chaves convencionais "idle"/"walk"/"run" (compat com a v1 e o caminho
// mais comum).
//
// Nota: "misc/link_animetion/*_Data" é o blob bruto de keyframes; o
// LinkAnimationHeader de verdade (o que SkelAnime_InitLink/LinkAnimation_Change
// esperam) é o símbolo SEM o sufixo _Data, em objects/gameplay_keep/. Usar o
// _Data direto faz o parser ler a estrutura errada e crashar dentro do
// próprio SkelAnime_InitLink (achado por um crash real, não suposição).
// Definida mais abaixo (seção "ponte de hooks") — declarada aqui porque
// CustomBodyActorUpdate, logo abaixo, precisa dela antes dessa seção existir
// no arquivo.
std::optional<ShipLua::EventValue> DispatchHookTransform(const char* name, ShipLua::EventPayload payload);

// ATENÇÃO ao mexer aqui: LinkAnimation_Change guarda o ponteiro ORIGINAL que
// recebe (z_skelanime.c:1292, `skelAnime->animation = ogAnim`) e o re-resolve
// a cada frame dentro de LinkAnimation_Update. O mesmo vale para o skeleton.
// Ou seja: as strings de caminho passadas ao engine precisam sobreviver
// enquanto o corpo estiver ativo — passar uma std::string temporária dá
// use-after-free no frame SEGUINTE (crash real em AnimationContext_SetLoadFrame,
// não teórico). Por isso os caminhos já prefixados vivem aqui, na spec global,
// e nunca em locais. std::map é node-based: o endereço de um value é estável
// enquanto o elemento não for removido/reatribuído.
struct CustomBodySpec {
    std::string skeletonPath;                 // como o mod informou (para get_body)
    std::map<std::string, std::string> anims; // nome lógico (do mod) -> path do mod
    // Segmentos N64 que a display list do esqueleto lê diretamente (por exemplo,
    // 0x08 para os olhos dos modelos de Player). O host só aceita 1..15.
    std::map<int, std::string> segments;
    // Chaves em `anims` que devem executar em loop reverso. O asset continua
    // único; isso representa fielmente ações como descida de escada, onde MM
    // usa o mesmo header de subida com playSpeed negativo.
    std::set<std::string> reverseAnims;
    // Modelos alternativos associados a estados nativos do Player. Hoje "roll"
    // é usado quando Player_Action_Roll está ativo; a chave continua explícita
    // para não amarrar o contrato à forma Goron.
    std::map<std::string, std::string> models;
    // Postura opcional que pode usar outro skeleton e outro formato de animação
    // (por exemplo, a defesa enrolada do Goron de MM). Diferente de `anims`,
    // estes assets não precisam usar LinkAnimationHeader nem o mesmo número
    // de membros do corpo principal.
    std::string shieldSkeletonPath;
    std::string shieldAnimationPath;
    std::string defaultAnim;
    f32 groundOffset = 0.0f;
    f32 rollOffset = 0.0f;
    // Opt-in de semântica de forma: em água funda, o corpo enrola, afunda e
    // usa o respawn/void-out nativo. O padrão é manter a natação normal do OoT.
    bool waterVoid = false;
    // Goron não agarra bordas no Player de MM. É opt-in para não mudar a ação
    // humana de outros corpos externos.
    bool blockLedgeGrab = false;
    // Espelhos com "__OTR__" já aplicado — é DESTES que os ponteiros passados
    // ao engine saem, e é por isso que eles precisam ser membros persistentes.
    std::string skeletonPathPrefixed;
    std::map<std::string, std::string> animsPrefixed;
    std::map<int, std::string> segmentsPrefixed;
    std::map<std::string, std::string> modelsPrefixed;
    std::string shieldSkeletonPathPrefixed;
    std::string shieldAnimationPathPrefixed;
};

bool gCustomBodyActive = false;
Actor* gCustomBodyActor = nullptr;
// PlayState do frame corrente. A maquina de acoes portada pede animacao pelo
// nome e nao carrega PlayState na assinatura; o update do ator o publica aqui.
PlayState* gCustomBodyPlay = nullptr;
SkelAnime gCustomBodySkelAnime{};
SkelAnime gCustomBodyShieldSkelAnime{};
std::string gCustomBodyCurrentAnim;
CustomBodySpec gCustomBodySpec;

// Chamado pelo gate nativo de Player_ActionHandler_12. A função tem linkage C
// porque z_player.c é compilado como C; não reaproveita o toggle global de
// Crowd Control, portanto não altera nem é alterada por esse efeito externo.
extern "C" u8 ShipLua_ShouldBlockLedgeGrabs(void) {
    return gCustomBodyActive && gCustomBodySpec.blockLedgeGrab;
}

// ship.oot.player.set_roll_blocked(bool): veta o rolamento no portão nativo
// (Player_TryRoll). Um sistema de stamina precisa disto — só drenar o medidor
// não impede a ação; sem bloquear, rolar com a barra vazia continua saindo.
// Angulo acumulado da bola durante o rolamento. Vive aqui, e nao numa estatica
// dentro do draw, para poder ser zerado ao sair do rolamento.
s16 gCustomBodyRollSpin = 0;

bool gRollBlocked = false;

// "mask" reproduz Camera_Demo4/CAM_SET_MASK_TRANSFORMATION do MM: subcâmera
// frontal, aproximação até o rosto, roll oscilante, mergulho de 45 graus e
// retorno. "jump" e "orbit" continuam disponíveis para a API genérica.
enum class CutsceneStyle { Jump, Orbit, Mask };
CutsceneStyle gCutsceneStyle = CutsceneStyle::Jump;
s16 gCutsceneSavedCamMode = 0;

extern "C" u8 ShipLua_ShouldBlockRoll(void) {
    return gRollBlocked ? 1 : 0;
}

int LuaSetRollBlocked(lua_State* state) {
    gRollBlocked = lua_toboolean(state, 1) != 0;
    lua_pushboolean(state, 1);
    return 1;
}

// ---------------------------------------------------------------------------
// Cutscene de transformação: subcâmera orbitando o jogador.
//
// É a peça que faltava para a troca de máscara parecer a de MM/OoTMM. Até
// aqui a transformação congelava o input e cobria a troca com um flash, mas a
// câmera continuava normal — o momento não tinha peso nenhum.
//
// O ciclo de vida da câmera é a parte delicada (o design de providers nativos
// listava justamente isto como risco): a arbitragem entre câmera principal e
// subcâmera precisa ser exata, senão o renderer fica com estado inconsistente.
// A sequência abaixo é a mesma que os chefes do próprio jogo usam
// (z_boss_dodongo.c): entra em modo cutscene, congela atores, limpa
// subcâmeras, cria a nova, põe a principal em WAIT e a nova em ACTIVE. Na
// saída, devolve com func_800C08AC + func_80064534 + solta os atores.
bool gCutsceneActive = false;
s16 gCutsceneCamId = 0;
int gCutsceneFrames = 0;
int gCutsceneElapsed = 0;
float gCutsceneStartDist = 160.0f;
float gCutsceneEndDist = 62.0f;
float gCutsceneHeight = 28.0f;
float gCutsceneSpin = 0.0f;
Vec3f gMaskCutsceneOriginalAt{};
Vec3f gMaskCutsceneOriginalEye{};
f32 gMaskCutsceneOriginalFov = 60.0f;
s16 gMaskCutsceneOriginalRoll = 0;
f32 gMaskCutsceneDistance = 40.0f;
f32 gMaskCutsceneFov = 80.0f;
f32 gMaskCutsceneYaw = 0.0f;
f32 gMaskCutsceneRollSign = 1.0f;
// ---------------------------------------------------------------------------
// Linha de tempo da transformacao.
//
// As duas primeiras constantes sao os frameCounts REAIS lidos do `mm.o2r`
// (`objects/gameplay_keep/gPlayerAnim_*`, campo u16 no offset 68 do resource):
// `cl_setmask` tem 66 frames e `al_hensin` tem 51. Nao sao estimativas.
//
// `al_hensin` ("hensin" = 変身, transformacao) e `al_hensin_loop` existem SO no
// mm.o2r — o OoT nao as tem — e sao a contorcao que faltava. Ate aqui a cena
// usava `cl_setmaskend`, que tem TRES frames e e a pose de encerrar o gesto de
// vestir uma mascara qualquer: em loop por dezoito frames ela parece uma pose
// parada, nao sofrimento. A referencia skijer nunca menciona `al_hensin`, por
// isso ninguem a tinha ligado.
//
// INFERENCIA REGISTRADA: a divisao vestir -> contorcer vem do encadeamento
// obvio dos frameCounts, nao do decomp do MM. Os frames de SFX (2/4/11/20/30)
// continuam sendo os de `D_8085D8F0`, esses sim vindos do decomp, e por isso
// nao foram movidos.
constexpr int kMaskSetmaskFrames = 66;
constexpr int kMaskHensinFrames = 51;
constexpr int kMaskHensinStartFrame = kMaskSetmaskFrames;
constexpr int kMaskTransitionPoseFrames = kMaskHensinStartFrame + kMaskHensinFrames;
constexpr int kMaskTransitionFlashBuildFrames = 6;
constexpr int kMaskTransitionPeakFrame = kMaskTransitionPoseFrames + kMaskTransitionFlashBuildFrames;
constexpr int kMaskTransitionRevealFrames = 13;
constexpr int kMaskTransitionTotalFrames = kMaskTransitionPeakFrame + kMaskTransitionRevealFrames;

// As rampas de ambiente e luz eram numeros absolutos calibrados para uma pose
// de 84 frames. Derivar do fim da pose impede que deslizem toda vez que a
// duracao mudar — foi exatamente esse tipo de constante solta que fez o
// desenho da mascara e o relogio da cena se separarem.
constexpr int kMaskEnvRampStart = 16;
constexpr int kMaskEnvRampSurge = (kMaskTransitionPoseFrames * 59) / 84;
constexpr int kMaskEnvRampPeak = (kMaskTransitionPoseFrames * 63) / 84;
constexpr int kMaskPointRampPeak = (kMaskTransitionPoseFrames * 64) / 84;

// Quando true, a cutscene também congelou atores/entrou em csMode e precisa
// desfazer isso na saída. Por padrão a primitiva é SÓ câmera.
bool gCutsceneFroze = false;

void CutsceneUpdate(PlayState* play); // definida abaixo; start() já posiciona

void CutsceneStop(PlayState* play) {
    if (!gCutsceneActive) {
        return;
    }
    gCutsceneActive = false;
    if (play == nullptr) {
        gCutsceneCamId = 0;
        gCutsceneFroze = false;
        return;
    }
    if (gCutsceneStyle == CutsceneStyle::Jump) {
        // Devolve o modo que estava antes. MM volta para CAM_MODE_NORMAL, mas
        // restaurar o modo salvo respeita quem já estivesse num modo especial.
        if (Camera* active = GET_ACTIVE_CAM(play); active != nullptr) {
            Camera_RequestMode(active, gCutsceneSavedCamMode);
        }
    } else if (gCutsceneCamId != 0) {
        const bool restoreMaskCamera = gCutsceneStyle == CutsceneStyle::Mask;
        // Devolve o controle à câmera principal antes de descartar a nossa.
        func_800C08AC(play, gCutsceneCamId, 0);
        gCutsceneCamId = 0;
        if (restoreMaskCamera) {
            // A câmera principal ficou em WAIT durante toda a transformação.
            // Apenas reativá-la conserva o último enquadramento da subcâmera em
            // algumas cenas internas. Reponha explicitamente o snapshot de
            // entrada e force a câmera normal a recalcular seu estado.
            if (Camera* main = Play_GetCamera(play, CAM_ID_MAIN); main != nullptr) {
                main->at = gMaskCutsceneOriginalAt;
                main->eye = gMaskCutsceneOriginalEye;
                main->eyeNext = gMaskCutsceneOriginalEye;
                main->dist = std::sqrt(
                    std::pow(gMaskCutsceneOriginalEye.x - gMaskCutsceneOriginalAt.x, 2.0f) +
                    std::pow(gMaskCutsceneOriginalEye.y - gMaskCutsceneOriginalAt.y, 2.0f) +
                    std::pow(gMaskCutsceneOriginalEye.z - gMaskCutsceneOriginalAt.z, 2.0f));
                main->fov = gMaskCutsceneOriginalFov;
                main->roll = gMaskCutsceneOriginalRoll;
                Camera_RequestMode(main, gCutsceneSavedCamMode);
                Camera_ResetAnim(main);
            }
        }
    }
    if (gCutsceneFroze) {
        gCutsceneFroze = false;
        func_80064534(play, &play->csCtx);
        Player* player = GET_PLAYER(play);
        Player_SetCsActionWithHaltedActors(play, player != nullptr ? &player->actor : nullptr, 7);
    }
}

// ship.oot.cutscene.start(frames, opções): assume a câmera por N frames.
int LuaCutsceneStart(lua_State* state) {
    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr || gSaveContext.gameMode != GAMEMODE_NORMAL) {
        SPDLOG_WARN("ShipLua cutscene.start: fora de gameplay");
        lua_pushboolean(state, 0);
        return 1;
    }
    if (gCutsceneActive) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int frames = static_cast<int>(luaL_checkinteger(state, 1));
    if (frames <= 0 || frames > 600) {
        SPDLOG_WARN("ShipLua cutscene.start: frames fora da faixa 1..600");
        lua_pushboolean(state, 0);
        return 1;
    }
    // Opções são todas opcionais; os padrões dão uma aproximação frontal.
    if (lua_istable(state, 2)) {
        lua_getfield(state, 2, "start_distance");
        gCutsceneStartDist = static_cast<float>(luaL_optnumber(state, -1, 160.0));
        lua_pop(state, 1);
        lua_getfield(state, 2, "end_distance");
        gCutsceneEndDist = static_cast<float>(luaL_optnumber(state, -1, 62.0));
        lua_pop(state, 1);
        lua_getfield(state, 2, "height");
        gCutsceneHeight = static_cast<float>(luaL_optnumber(state, -1, 28.0));
        lua_pop(state, 1);
        lua_getfield(state, 2, "spin");
        gCutsceneSpin = static_cast<float>(luaL_optnumber(state, -1, 0.0));
        lua_pop(state, 1);
        lua_getfield(state, 2, "freeze_player");
        gCutsceneFroze = lua_toboolean(state, -1) != 0;
        lua_pop(state, 1);
        lua_getfield(state, 2, "style");
        {
            const char* style = luaL_optstring(state, -1, "jump");
            gCutsceneStyle = (std::strcmp(style, "orbit") == 0) ? CutsceneStyle::Orbit : CutsceneStyle::Jump;
        }
        lua_pop(state, 1);
    } else {
        gCutsceneStartDist = 160.0f;
        gCutsceneEndDist = 62.0f;
        gCutsceneHeight = 28.0f;
        gCutsceneSpin = 0.0f;
        gCutsceneFroze = false;
        gCutsceneStyle = CutsceneStyle::Jump;
    }

    // Por padrão a primitiva é SÓ câmera. Congelar o jogador aqui atropelaria
    // qualquer animação que o mod tenha acabado de tocar — foi exatamente o
    // que quebrou a primeira versão desta cutscene: Player_SetCsActionWithHaltedActors
    // substitui a ação do Link, apagando a animação de colocar a máscara.
    // Quem quiser o congelamento completo pede freeze_player = true.
    if (gCutsceneFroze) {
        func_80064520(play, &play->csCtx);
        Player_SetCsActionWithHaltedActors(play, &player->actor, 1);
    }
    if (gCutsceneStyle == CutsceneStyle::Jump) {
        // Caminho do MM: sem subcâmera. Só troca o modo da câmera ativa; o
        // enquadramento passa a ser o nativo do jogo, e é por isso que em MM a
        // transformação "encaixa" enquanto a nossa órbita destoava.
        Camera* active = GET_ACTIVE_CAM(play);
        if (active == nullptr) {
            SPDLOG_WARN("ShipLua cutscene.start: sem cÃ¢mera ativa");
            lua_pushboolean(state, 0);
            return 1;
        }
        gCutsceneSavedCamMode = active->mode;
        Camera_RequestMode(active, CAM_MODE_JUMP);
        gCutsceneCamId = 0;
        gCutsceneFrames = frames;
        gCutsceneElapsed = 0;
        gCutsceneActive = true;
        CutsceneUpdate(play);
        SPDLOG_INFO("ShipLua cutscene.start: modo de cÃ¢mera JUMP por {} frames", frames);
        lua_pushboolean(state, 1);
        return 1;
    }

    Play_ClearAllSubCameras(play);
    gCutsceneCamId = Play_CreateSubCamera(play);
    if (gCutsceneCamId == SUBCAM_NONE) {
        SPDLOG_WARN("ShipLua cutscene.start: sem slot de subc\xC3\xA2mera dispon\xC3\xADvel");
        lua_pushboolean(state, 0);
        return 1;
    }
    // Semeia com a visão atual antes de ativar, como z_onepointdemo.c faz —
    // sem isto o primeiro frame pode sair de uma posição indefinida.
    if (Camera* seeded = Play_GetCamera(play, gCutsceneCamId); seeded != nullptr) {
        seeded->at = play->view.lookAt;
        seeded->eye = play->view.eye;
        seeded->eyeNext = play->view.eye;
        seeded->fov = play->view.fovy;
    }
    Play_ChangeCameraStatus(play, CAM_ID_MAIN, CAM_STAT_WAIT);
    Play_ChangeCameraStatus(play, gCutsceneCamId, CAM_STAT_ACTIVE);
    gCutsceneFrames = frames;
    gCutsceneElapsed = 0;
    gCutsceneActive = true;
    // Posiciona já neste frame: esperar o próximo update deixaria um frame com
    // a subcâmera em posição indefinida.
    CutsceneUpdate(play);
    SPDLOG_INFO("ShipLua cutscene.start: assumindo a c\xC3\xA2mera por {} frames", frames);
    lua_pushboolean(state, 1);
    return 1;
}

int LuaCutsceneStop(lua_State* state) {
    CutsceneStop(gPlayState);
    lua_pushboolean(state, 1);
    return 1;
}

int LuaCutsceneActive(lua_State* state) {
    lua_pushboolean(state, gCutsceneActive ? 1 : 0);
    return 1;
}


// ---------------------------------------------------------------------------
// Iluminação de cena e luz pontual no jogador.
//
// É o que falta para a transformação de máscara parecer a de MM. O que se
// costuma chamar de "efeito de partículas" ali é, no decomp, quase todo uma
// rampa de fog e ambiente mais uma luz pontual no Link (func_808550D0,
// z_player.c). Não há partícula nenhuma.
//
// Primitivas genéricas de propósito: o host não sabe o que é transformação.
// Um mod de tempestade, de caverna ou de poção usa as mesmas.
// ---------------------------------------------------------------------------

bool gEnvOverrideActive = false;
EnvLightSettings gEnvSaved{};

// ship.oot.env.set_light_override({ fog_near, fog_color = {r,g,b},
//                                  ambient_color = {r,g,b}, light1_color = {r,g,b} })
// Campos omitidos ficam como estão. A primeira chamada guarda o estado atual
// para clear_light_override() poder devolver.
int LuaEnvSetLightOverride(lua_State* state) {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    if (!gEnvOverrideActive) {
        gEnvSaved = play->envCtx.lightSettings;
        gEnvOverrideActive = true;
    }

    auto readColor = [&](const char* field, u8* dst) {
        lua_getfield(state, 1, field);
        if (lua_istable(state, -1)) {
            for (int i = 0; i < 3; i++) {
                lua_rawgeti(state, -1, i + 1);
                const int v = static_cast<int>(luaL_optinteger(state, -1, dst[i]));
                dst[i] = static_cast<u8>(v < 0 ? 0 : (v > 255 ? 255 : v));
                lua_pop(state, 1);
            }
        }
        lua_pop(state, 1);
    };

    if (lua_istable(state, 1)) {
        readColor("fog_color", play->envCtx.lightSettings.fogColor);
        readColor("ambient_color", play->envCtx.lightSettings.ambientColor);
        readColor("light1_color", play->envCtx.lightSettings.light1Color);
        lua_getfield(state, 1, "fog_near");
        if (!lua_isnil(state, -1)) {
            const int nearValue = static_cast<int>(luaL_checkinteger(state, -1));
            play->envCtx.lightSettings.fogNear = static_cast<s16>(nearValue < 0 ? 0 : (nearValue > 1000 ? 1000 : nearValue));
        }
        lua_pop(state, 1);
    }
    lua_pushboolean(state, 1);
    return 1;
}

int LuaEnvClearLightOverride(lua_State* state) {
    if (gEnvOverrideActive && gPlayState != nullptr) {
        gPlayState->envCtx.lightSettings = gEnvSaved;
    }
    gEnvOverrideActive = false;
    lua_pushboolean(state, 1);
    return 1;
}

// Luz pontual acompanhando o jogador. O Player do MM carrega um LightInfo
// embutido; o do OoT não, então mantemos o nosso e o registramos no contexto
// de luzes da cena.
bool gPointLightActive = false;
LightInfo gPointLightInfo{};
LightNode* gPointLightNode = nullptr;
f32 gPointLightOffY = 0.0f;
u8 gPointLightColor[3] = { 255, 255, 255 };
s16 gPointLightRadius = 0;

void PointLightDetach(PlayState* play) {
    if (gPointLightNode != nullptr && play != nullptr) {
        LightContext_RemoveLight(play, &play->lightCtx, gPointLightNode);
    }
    gPointLightNode = nullptr;
    gPointLightActive = false;
}

// ship.oot.player.set_point_light({ r, g, b, radius, offset_y }) — radius 0 apaga.
int LuaPlayerSetPointLight(lua_State* state) {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }

    s16 radius = 0;
    if (lua_istable(state, 1)) {
        lua_getfield(state, 1, "radius");
        radius = static_cast<s16>(luaL_optinteger(state, -1, 0));
        lua_pop(state, 1);
        const char* keys[3] = { "r", "g", "b" };
        for (int i = 0; i < 3; i++) {
            lua_getfield(state, 1, keys[i]);
            const int v = static_cast<int>(luaL_optinteger(state, -1, gPointLightColor[i]));
            gPointLightColor[i] = static_cast<u8>(v < 0 ? 0 : (v > 255 ? 255 : v));
            lua_pop(state, 1);
        }
        lua_getfield(state, 1, "offset_y");
        gPointLightOffY = static_cast<f32>(luaL_optnumber(state, -1, 0.0));
        lua_pop(state, 1);
    }

    if (radius <= 0) {
        PointLightDetach(play);
        lua_pushboolean(state, 1);
        return 1;
    }

    gPointLightRadius = radius > 5000 ? 5000 : radius;
    if (!gPointLightActive) {
        gPointLightNode = LightContext_InsertLight(play, &play->lightCtx, &gPointLightInfo);
        if (gPointLightNode == nullptr) {
            SPDLOG_WARN("ShipLua set_point_light: sem slot de luz disponÃ­vel");
            lua_pushboolean(state, 0);
            return 1;
        }
        gPointLightActive = true;
    }
    lua_pushboolean(state, 1);
    return 1;
}

// Reposiciona a luz por frame. Sem isto ela ficaria parada onde foi criada.
void PointLightUpdate(PlayState* play) {
    if (!gPointLightActive || play == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        return;
    }
    Lights_PointNoGlowSetInfo(&gPointLightInfo, static_cast<s16>(player->actor.world.pos.x),
                              static_cast<s16>(player->actor.world.pos.y + gPointLightOffY),
                              static_cast<s16>(player->actor.world.pos.z), gPointLightColor[0], gPointLightColor[1],
                              gPointLightColor[2], gPointLightRadius);
}


// ---------------------------------------------------------------------------
// Redirecionamento de voz do jogador.
//
// O Link tem um bloco de sfx de voz a partir de 0x6800 (NA_SE_VO_LI_SWORD_N), e
// cada ação — atacar, levar dano, cair, gritar — é um offset dentro dele. As
// formas do MM têm blocos próprios no MESMO formato, deslocados:
//
//   Fierce Deity 0x00   Garo 0x60   Deku 0x80   Zora 0xA0   GORON 0xC0
//
// Então trocar a voz inteira de uma forma é somar um offset — não é preciso
// mapear som por som. Os offsets foram levantados do fork skijer
// (transformation_masks.c:157-215) e estão na wiki, seção 10.
//
// Primitiva genérica de propósito: o host não sabe o que é "Goron". Quem fizer
// Zora ou Deku troca um número no Lua, sem C++ novo.
// ---------------------------------------------------------------------------

bool gVoiceMapActive = false;
u16 gVoiceBase = 0x6800;
s32 gVoiceOffset = 0;

extern "C" u8 ShipLua_TransformVoiceSfx(u16* sfxId) {
    if (!gVoiceMapActive || sfxId == nullptr) {
        return 0;
    }
    // Só redireciona o que está DENTRO do bloco de voz. Fora dele o id é outra
    // coisa (passo, roupa, item) e mexer nele daria som aleatório.
    if (*sfxId < gVoiceBase || *sfxId >= gVoiceBase + 0x100) {
        return 0;
    }

    const s32 action = *sfxId - gVoiceBase;
    const s32 mapped = gVoiceBase + gVoiceOffset + action;

    // O caminho pelo soundfont do MM: a voz da forma é uma entrada de SFX como
    // qualquer outra. Se a amostra não existir, devolvemos 0 e o jogo toca a voz
    // normal do Link — degradação silenciosa, não crash.
    if (!ShipLua::MmAudio_PlayVoiceSfx(mapped)) {
        return 0;
    }
    return 1;
}

// ship.oot.audio.play_sfx(index, font) — toca uma entrada de SFX do soundfont
// do MM. `font` é opcional e assume 0, que é onde vivem os efeitos do jogo.
//
// É o caminho curto: o interpretador de sequência seria o "certo", mas as
// amostras estão no soundfont e são alcançáveis por índice. Mesmo áudio, mesmo
// soundfont do MM — só disparado direto em vez de pelo script.
int LuaPlayFontSfx(lua_State* state) {
    const int index = static_cast<int>(luaL_checkinteger(state, 1));
    const int font = static_cast<int>(luaL_optinteger(state, 2, 0));
    if (index < 0 || index > 4095 || font < 0 || font > 40) {
        SPDLOG_WARN("ShipLua play_sfx: indice ou font fora da faixa");
        lua_pushboolean(state, 0);
        return 1;
    }
    lua_pushboolean(state, ShipLua::MmAudio_PlayFontSfx(font, index) ? 1 : 0);
    return 1;
}

// ship.oot.audio.dump_sfx_table(first, count, font) — lista no log tamanho e
// duração de cada entrada. Serve para descobrir QUAL índice é qual som sem
// adivinhar; sai quando a trilha estiver mapeada.
int LuaDumpSfxTable(lua_State* state) {
    const int first = static_cast<int>(luaL_optinteger(state, 1, 0));
    const int count = static_cast<int>(luaL_optinteger(state, 2, 64));
    const int font = static_cast<int>(luaL_optinteger(state, 3, 0));
    ShipLua::MmAudio_DumpSfxTable(font, first, count);
    lua_pushboolean(state, 1);
    return 1;
}

// ship.oot.audio.set_voice_map(base, offset) — offset 0 ou nil desliga.
int LuaSetVoiceMap(lua_State* state) {
    if (lua_isnoneornil(state, 1)) {
        gVoiceMapActive = false;
        lua_pushboolean(state, 1);
        return 1;
    }
    const int base = static_cast<int>(luaL_checkinteger(state, 1));
    const int offset = static_cast<int>(luaL_checkinteger(state, 2));
    if (base < 0 || base > 0xFFFF || offset < 0 || offset > 0xFF) {
        SPDLOG_WARN("ShipLua set_voice_map: base ou offset fora da faixa");
        lua_pushboolean(state, 0);
        return 1;
    }
    gVoiceBase = static_cast<u16>(base);
    gVoiceOffset = offset;
    gVoiceMapActive = (offset != 0);
    SPDLOG_INFO("ShipLua: voz mapeada para base 0x{:04x} offset 0x{:02x}", base, offset);
    lua_pushboolean(state, 1);
    return 1;
}

// Roda por frame: aproxima a câmera e encerra sozinha ao fim.
void CutsceneUpdate(PlayState* play) {
    if (!gCutsceneActive || play == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == nullptr || gSaveContext.gameMode != GAMEMODE_NORMAL) {
        CutsceneStop(play);
        return;
    }
    if (gCutsceneElapsed >= gCutsceneFrames) {
        CutsceneStop(play);
        return;
    }

    if (gCutsceneStyle == CutsceneStyle::Jump) {
        // Todo frame, como Player_Action_86 faz: reforça o modo (o jogo pode
        // reverter sozinho em transições) e gira o Link para encarar a câmera.
        // O +0x8000 é meia volta: Camera_GetCamDirYaw devolve a direção para
        // onde a câmera OLHA, e o Link precisa ficar de frente para ela.
        if (Camera* active = GET_ACTIVE_CAM(play); active != nullptr) {
            Camera_RequestMode(active, CAM_MODE_JUMP);
            player->actor.shape.rot.y = Camera_GetCamDirYaw(active) + 0x8000;
        }
        ++gCutsceneElapsed;
        return;
    }

    constexpr double kPi = 3.14159265358979323846;
    if (gCutsceneStyle == CutsceneStyle::Mask) {
        Camera* cam = Play_GetCamera(play, gCutsceneCamId);
        if (cam == nullptr) {
            CutsceneStop(play);
            return;
        }

        // Camera_Demo4 do MM usa o foco do ator, ligeiramente rebaixado em
        // direção à cintura. O deslocamento lateral e o roll começam no frame
        // 12, estabilizam no 38 e então a câmera mergulha 45 graus.
        Vec3f focus = player->actor.focus.pos;
        focus.y -= (focus.y - player->actor.world.pos.y) * 0.1f;
        const int frame = gCutsceneElapsed;
        f32 pitch = 0.0f;

        if (frame < 38) {
            f32 wave = 0.0f;
            if (frame >= 12) {
                const f32 waveDegrees = static_cast<f32>(frame - 12) * (135.0f / 13.0f);
                wave = std::sin(waveDegrees * static_cast<f32>(kPi / 180.0)) * gMaskCutsceneRollSign;
            }
            const f32 sway = static_cast<f32>(frame) * (6.0f / 19.0f) * wave;
            const f32 playerYaw =
                static_cast<f32>(player->actor.shape.rot.y) * static_cast<f32>(kPi / 32768.0);
            focus.x += std::sin(playerYaw + static_cast<f32>(kPi * 0.5)) * sway;
            focus.z += std::cos(playerYaw + static_cast<f32>(kPi * 0.5)) * sway;
            cam->at.x += (focus.x - cam->at.x) * 0.2f;
            cam->at.y += (focus.y - cam->at.y) * 0.2f;
            cam->at.z += (focus.z - cam->at.z) * 0.1f;
            const f32 distanceStep = 1.0f / static_cast<f32>(std::max(1, 38 - frame));
            gMaskCutsceneDistance += (30.0f - gMaskCutsceneDistance) * distanceStep;
            const f32 rollDegrees = static_cast<f32>(frame) * (30.0f / 19.0f) * wave;
            cam->roll = static_cast<s16>(std::lround(rollDegrees * (32768.0f / 180.0f)));
        } else if (frame < 62) {
            const int settleFrame = frame - 38;
            const f32 playerYaw =
                static_cast<f32>(player->actor.shape.rot.y) * static_cast<f32>(kPi / 32768.0);
            focus.x = player->actor.world.pos.x - std::sin(playerYaw) * 7.0f;
            focus.z = player->actor.world.pos.z - std::cos(playerYaw) * 7.0f;
            if (settleFrame == 0) {
                cam->at = focus;
            } else {
                cam->at.x += (focus.x - cam->at.x) * 0.25f;
                cam->at.y += (focus.y - cam->at.y) * 0.25f;
                cam->at.z += (focus.z - cam->at.z) * 0.1f;
            }
            gMaskCutsceneFov +=
                (32.0f - gMaskCutsceneFov) / static_cast<f32>(std::max(1, 24 - settleFrame));
            gMaskCutsceneDistance = 35.0f;
            pitch = static_cast<f32>(kPi * 0.25);
            cam->roll = static_cast<s16>(std::lround(static_cast<f32>(cam->roll) * 0.9f));
        } else if (frame < kMaskTransitionPeakFrame) {
            const int faceFrame = frame - 62;
            cam->at.x += (focus.x - cam->at.x) * 0.25f;
            cam->at.y += (focus.y - cam->at.y) * 0.25f;
            cam->at.z += (focus.z - cam->at.z) * 0.1f;
            // No MM a soma 1..35 divide por 630 e leva o FOV de 32 a 60.
            gMaskCutsceneFov += ((60.0f - 32.0f) / 630.0f) * static_cast<f32>(faceFrame + 1);
            gMaskCutsceneDistance = 35.0f;
            pitch = static_cast<f32>(kPi * 0.25);
            cam->roll = static_cast<s16>(std::lround(static_cast<f32>(cam->roll) * 0.9f));
        } else {
            const f32 reveal =
                std::clamp(static_cast<f32>(frame - kMaskTransitionPeakFrame + 1) /
                               static_cast<f32>(kMaskTransitionRevealFrames),
                           0.0f, 1.0f);
            cam->at.x += (gMaskCutsceneOriginalAt.x - cam->at.x) * reveal;
            cam->at.y += (gMaskCutsceneOriginalAt.y - cam->at.y) * reveal;
            cam->at.z += (gMaskCutsceneOriginalAt.z - cam->at.z) * reveal;
            cam->eye.x += (gMaskCutsceneOriginalEye.x - cam->eye.x) * reveal;
            cam->eye.y += (gMaskCutsceneOriginalEye.y - cam->eye.y) * reveal;
            cam->eye.z += (gMaskCutsceneOriginalEye.z - cam->eye.z) * reveal;
            cam->eyeNext = cam->eye;
            cam->fov += (gMaskCutsceneOriginalFov - cam->fov) * reveal;
            cam->roll = static_cast<s16>(std::lround(static_cast<f32>(cam->roll) * (1.0f - reveal)));
            player->actor.shape.rot.y = Camera_GetCamDirYaw(cam) + 0x8000;
            ++gCutsceneElapsed;
            return;
        }

        const f32 horizontal = std::cos(pitch) * gMaskCutsceneDistance;
        cam->eye.x = cam->at.x + std::sin(gMaskCutsceneYaw) * horizontal;
        cam->eye.y = cam->at.y + std::sin(pitch) * gMaskCutsceneDistance;
        cam->eye.z = cam->at.z + std::cos(gMaskCutsceneYaw) * horizontal;
        cam->eyeNext = cam->eye;
        cam->fov = gMaskCutsceneFov;
        player->actor.shape.rot.y = Camera_GetCamDirYaw(cam) + 0x8000;
        ++gCutsceneElapsed;
        return;
    }

    const double t = static_cast<double>(gCutsceneElapsed) / static_cast<double>(gCutsceneFrames);
    // Ease-out: aproxima rápido e desacelera, dando peso ao instante da troca.
    const double eased = 1.0 - (1.0 - t) * (1.0 - t);
    const double dist = gCutsceneStartDist + (gCutsceneEndDist - gCutsceneStartDist) * eased;

    // Ângulo: parte de frente para o Link e gira conforme "spin" (em voltas).
    const double baseYaw = static_cast<double>(player->actor.shape.rot.y) * (kPi / 32768.0);
    const double angle = baseYaw + kPi + gCutsceneSpin * 2.0 * kPi * eased;

    Vec3f at;
    at.x = player->actor.world.pos.x;
    at.y = player->actor.world.pos.y + gCutsceneHeight;
    at.z = player->actor.world.pos.z;

    Vec3f eye;
    eye.x = at.x + static_cast<f32>(std::sin(angle) * dist);
    eye.y = at.y + gCutsceneHeight * 0.6f;
    eye.z = at.z + static_cast<f32>(std::cos(angle) * dist);

    // Escreve direto na struct da câmera, como os chefes do jogo fazem
    // (z_boss_dodongo.c). NÃO usar Play_CameraSetAtEye aqui: ela termina
    // gravando atLERPStepScale = 0.01f, uma interpolação lentíssima — a câmera
    // se arrasta tão devagar rumo ao alvo que parece que nada aconteceu. Foi
    // por isso que a primeira versão desta cutscene não mostrava nada apesar
    // de iniciar corretamente.
    Camera* cam = Play_GetCamera(play, gCutsceneCamId);
    if (cam != nullptr) {
        cam->at = at;
        cam->eye = eye;
        cam->eyeNext = eye;
    }
    ++gCutsceneElapsed;
}

// ship.oot.env.get(campo): estado do AMBIENTE, não do jogador. Fica separado
// de player.get de propósito — hora do dia e cena não são propriedades do
// Link, e misturar as duas coisas envelhece mal.
//
//   time_of_day  0..1 (0 = meia-noite, 0.5 = meio-dia)
//   is_night     0/1, pela mesma flag que o engine usa (IS_NIGHT)
//   scene_id     número da cena atual
//
// Não há "is_indoors" aqui: o engine não expõe isso de forma direta e uma
// heurística chutada seria pior que nada — o mod já distingue ambientes pela
// própria tabela de cenas.
int LuaEnvGet(lua_State* state) {
    const char* field = luaL_checkstring(state, 1);
    PlayState* play = gPlayState;
    if (field == nullptr) {
        lua_pushnil(state);
        return 1;
    }
    if (std::strcmp(field, "time_of_day") == 0) {
        lua_pushnumber(state, static_cast<double>(gSaveContext.dayTime) / 65535.0);
        return 1;
    }
    if (std::strcmp(field, "is_night") == 0) {
        lua_pushnumber(state, IS_NIGHT ? 1 : 0);
        return 1;
    }
    if (std::strcmp(field, "scene_id") == 0) {
        lua_pushnumber(state, play != nullptr ? static_cast<double>(play->sceneNum) : -1.0);
        return 1;
    }
    lua_pushnil(state);
    return 1;
}

const char* CurrentGameStateMode() {
    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (play == nullptr || player == nullptr) {
        return "unavailable";
    }
    if (gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return "loading";
    }
    if (play->transitionTrigger != TRANS_TRIGGER_OFF || play->transitionMode != TRANS_MODE_OFF) {
        return "transition";
    }
    if ((player->stateFlags1 & PLAYER_STATE1_DEAD) != 0) {
        return "dead";
    }
    if (play->pauseCtx.state != 0) {
        return "paused";
    }
    if (play->msgCtx.msgMode != 0) {
        return "dialog";
    }
    if (play->csCtx.state != CS_STATE_IDLE || Player_InBlockingCsMode(play, player) || gCutsceneActive) {
        return "cutscene";
    }
    return "gameplay";
}

// ship.game.state(): snapshot pequeno, sem ponteiro nem layout nativo.
int LuaGameState(lua_State* state) {
    lua_newtable(state);
    lua_pushstring(state, CurrentGameStateMode());
    lua_setfield(state, -2, "mode");
    if (gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2) {
        lua_pushinteger(state, gSaveContext.fileNum);
        lua_setfield(state, -2, "save_slot");
    }
    return 1;
}

// hook.oot.item.give (transform). Chamado do funil GiveItemEntryFromActor
// (z_actor.c) logo antes do item ser entregue ao Player. O Lua recebe o item
// atual e pode devolver, via ship.hooks.result, um NOVO get_item_id — o host
// reconstrói o GetItemEntry pela tabela vanilla (MOD_NONE) e substitui in
// place. É o ponto que um randomizer em Lua precisa: "esta check dá o quê?".
// Ponto frio (só ao pegar item), mesma disciplina do bridge de ledge-grab.
extern "C" void ShipLua_TransformGivenItem(GetItemEntry* entry) {
    if (entry == nullptr || gModHost == nullptr) {
        return;
    }
    const auto result = DispatchHookTransform(
        "hook.oot.item.give",
        ShipLua::EventPayload{
            {"item_id", static_cast<std::int64_t>(entry->itemId)},
            {"get_item_id", static_cast<std::int64_t>(entry->getItemId)},
        });
    if (!result.has_value() || !std::holds_alternative<std::int64_t>(result->value)) {
        return;
    }
    const std::int64_t replacement = std::get<std::int64_t>(result->value);
    if (replacement == entry->getItemId || replacement <= 0 || replacement > 0xFFFF) {
        return;
    }
    GetItemEntry replaced =
        ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, static_cast<uint16_t>(replacement));
    if (replaced.itemId != ITEM_NONE) {
        *entry = replaced;
    }
}
std::optional<std::string> gCustomBodyOverrideAnim;
bool gCustomBodyOverrideOnce = false;
int gCustomBodyRollCharge = 0;
bool gCustomBodyRollSpikesActive = false;
int gCustomBodyRollMagicDrainTimer = 0;
int gCustomBodyRollWallBounceTimer = 0;
enum class CustomBodyRollPhase : uint8_t { None, Enter, Rolling, Exit };
CustomBodyRollPhase gCustomBodyRollPhase = CustomBodyRollPhase::None;
enum class CustomBodyWaterVoidPhase : uint8_t { None, Curl, Ball, Triggered };
CustomBodyWaterVoidPhase gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::None;
bool gCustomBodyWaterVoidOwnsInputDisable = false;
int gCustomBodyWaterVoidTimer = 0;
enum class CustomBodyGroundPoundPhase : uint8_t { None, Rising, Falling, Impact };
CustomBodyGroundPoundPhase gCustomBodyGroundPoundPhase = CustomBodyGroundPoundPhase::None;
int gCustomBodyGroundPoundTimer = 0;
// Verdadeiro somente no intervalo de dano de um soco especial. Exposto pelo
// getter para que a spec Lua possa observar o estado sem ter de inferi-lo da
// animação e de frames internos do engine.
bool gCustomBodyPunchHitActive = false;
// Igual a av2.actionVar2 do Player de MM: B durante punch A/B não reinicia a
// animação atual; apenas agenda o próximo golpe quando ela terminar.
bool gCustomBodyPunchQueued = false;
// Estado original do cylinder do Player antes da bola Goron assumir o ataque.
// O collider pertence ao Player e é reutilizado pelo engine; por isso não pode
// ficar com raio/dano de bola depois de qualquer saída de roll ou troca de cena.
bool gCustomBodyRollColliderArmed = false;
f32 gCustomBodyRollColliderRadius = 0;
f32 gCustomBodyRollColliderHeight = 0;
f32 gCustomBodyRollColliderYShift = 0;
bool gCustomBodyShieldActive = false;
// Retorno de `LinkAnimation_Update` do frame ANTERIOR. A maquina portada precisa
// do fim de one-shot para o curl virar bola e o desenrolar virar idle, mas ela
// roda antes do update da animacao neste frame — entao o sinal so pode ser o do
// frame passado. Um frame de atraso e o custo; travar o curl seria o preco de
// nao ter sinal nenhum.
bool gCustomBodyLastAnimFinished = false;

// A maquina portada e a dona do rolamento neste momento?
//
// Enquanto for false vale o caminho antigo — o sequestro do `Player_Action_Roll`
// do OoT, que e uma esquiva, encadeado por hooks para fingir a bola. Os dois
// NUNCA podem valer ao mesmo tempo: seriam duas bolas disputando o mesmo corpo.
//
// A condicao e "ha corpo externo E o mod pediu rolamento de Goron". Um mod que
// use `set_roll_mode("chain")` SEM corpo externo continua no caminho antigo: a
// maquina precisa do corpo para ter o que dirigir, e a capability publica nao
// pode deixar de funcionar so porque a forma existe.
bool MmFormOwnsRoll() {
    return gCustomBodyActive && ShipLua::MmForm::BallEnabled();
}

// Mesma pergunta para o combo de socos. Sem chave do mod: o soco nao e opcional
// como o rolamento — se a spec declara `punch_a`, a forma soca, e quem cuida
// disso passa a ser a maquina.
bool MmFormOwnsPunch() {
    return gCustomBodyActive && ShipLua::MmForm::PunchEnabled();
}
// ---------------------------------------------------------------------------
// Maquina de acoes do corpo externo.
//
// Ate aqui o corpo tinha UM caminho de animacao: perguntar ao Lua a cada frame
// qual nome tocar, com um "override" one-shot por cima. Isso serve para
// locomocao (idle/walk/run), e nao serve para as acoes que MM implementa como
// estado COM DONO — soco em combo, defesa e instrumento. Os tres sintomas
// medidos em jogo tem a mesma raiz:
//
//   - o soco nunca saia: a entrada dependia de `player->meleeWeaponState`, que
//     so e escrito pelo swing de espada do OoT e nunca pela forma;
//   - os tambores nunca apareciam e a pose congelava: o override so e liberado
//     quando a animacao termina, entao qualquer one-shot perdido deixava o
//     corpo preso naquela pose;
//   - a defesa ficava errada: a action func do Player continuava rodando por
//     baixo e reescrevia a pose no mesmo frame em que trocavamos o skeleton.
//
// A referencia (skijer/Shipwright @ Not-Enough-Items,
// soh/mods/transformation_masks/mm_player_form.cpp) resolve com uma action
// machine em C++ que toma posse do corpo e PAUSA a action func do Player
// enquanto esta ativa, via PLAYER_STATE3_PAUSE_ACTION_FUNC — flag que o nosso
// host ja possui e ja respeita (z_player.c:12099). E o que replicamos aqui.
enum class CustomBodyAction : uint8_t { None, Punch };
CustomBodyAction gCustomBodyAction = CustomBodyAction::None;
// 0 = punch_a, 1 = punch_b, 2 = punch_c. Igual ao comboStep da referencia.
uint8_t gCustomBodyComboStep = 0;
// Recuperacao tocando apos o ultimo golpe do combo; o combo ja acabou, mas a
// acao ainda e dona do corpo ate a animacao terminar.
bool gCustomBodyPunchRecovering = false;
// Guardado na ativacao para reiniciar o agachamento a cada defesa.
AnimationHeader* gCustomBodyShieldAnimation = nullptr;
// A troca de um corpo externo acontece depois de gPlayerAnim_cl_setmask. Sem
// uma transicao propria, o Player pode andar durante a animacao e a troca de
// skeleton fica exposta em um frame. OoTMM e o PR #1 congelam o Player e
// escondem esse instante sob um flash branco; mantemos a mesma semantica sem
// sequestrar camera ou iniciar uma cutscene do OoT.
enum class MaskTransitionPhase : uint8_t { None, Conceal, Reveal };
MaskTransitionPhase gMaskTransitionPhase = MaskTransitionPhase::None;
int gMaskTransitionFramesRemaining = 0;
int gMaskTransitionRevealTimeout = 0;
int gMaskTransitionElapsed = 0;
s16 gMaskTransitionFlashAlpha = 0;
bool gMaskTransitionOwnsInputDisable = false;
bool gMaskTransitionOwnsCutscene = false;
bool gMaskTransitionOwnsPointLight = false;
bool gMaskTransitionEnvSaved = false;
bool gMaskTransitionRemoving = false;
bool gMaskTransitionMaskHandDrawLogged = false;
bool gMaskTransitionMaskDrawLogged = false;
bool gMaskTransitionScreamDrawLogged = false;
EnvLightSettings gMaskTransitionSavedEnv{};
struct MaskTransitionSavedAdjustments {
    s16 ambient[3];
    s16 light1[3];
    s16 fog[3];
    s16 fogNear;
};
MaskTransitionSavedAdjustments gMaskTransitionSavedAdjustments{};

constexpr char kMaskOnAnimationPath[] = "__OTR__mm/objects/gameplay_keep/gPlayerAnim_cl_setmask";
constexpr char kMaskOnEndAnimationPath[] = "__OTR__mm/objects/gameplay_keep/gPlayerAnim_cl_setmaskend";
// A contorcao da transformacao e o loop que a sustenta. Ver a nota de linha de
// tempo junto de `kMaskHensinFrames`.
constexpr char kMaskHensinAnimationPath[] = "__OTR__mm/objects/gameplay_keep/gPlayerAnim_al_hensin";
constexpr char kMaskHensinLoopAnimationPath[] = "__OTR__mm/objects/gameplay_keep/gPlayerAnim_al_hensin_loop";
constexpr char kMmGoronMaskDListPath[] = "__OTR__mm/objects/gameplay_keep/gGoronMaskDL";
// A MASCARA GRITANDO. Nao e a mesma malha da mascara normal: vive num objeto
// proprio (`object_mask_goron`) e tem a boca escancarada.
//
// Fonte primaria, 2S2H `mm/src/code/z_player_lib.c:4049-4056`: a partir do
// frame 51 de `cl_setmask` — ou durante `cl_setmaskend` — o MM soma 4 ao indice
// da tabela `D_801C0B20` e desenha OUTRA display list. Na tabela (`:2911`), o
// indice de PLAYER_MASK_GORON e `gGoronMaskDL` e o mesmo indice mais quatro e
// `object_mask_goron_DL_0014A0`.
//
// Era esta a troca que faltava: a cena desenhava `gGoronMaskDL` do inicio ao
// fim, entao a mascara nunca abria a boca por mais bem cronometrado que o resto
// estivesse.
constexpr char kMmGoronMaskScreamDListPath[] =
    "__OTR__mm/objects/object_mask_goron/object_mask_goron_DL_0014A0";
// Frame de `cl_setmask` em que a troca acontece (z_player_lib.c:4050).
constexpr int kMaskScreamStartFrame = 51;

struct MaskTransitionLightStage {
    s16 fogNear;
    u8 fog[3];
    u8 ambient[3];
};

constexpr std::array<MaskTransitionLightStage, 3> kMaskTransitionLightStages = {{
    { 650, { 0, 0, 0 }, { 10, 0, 30 } },
    { 300, { 200, 200, 255 }, { 0, 0, 0 } },
    { 600, { 0, 0, 0 }, { 0, 0, 200 } },
}};

struct MaskTransitionPointStage {
    u8 color[3];
    s16 radius;
};

// Perfil humano de D_8085D848. A entrada Goron comeca em Link humano; o
// perfil nao-humano de raio 100 tornava a coloracao azul quase invisivel.
constexpr std::array<MaskTransitionPointStage, 3> kMaskTransitionPointStages = {{
    { { 120, 200, 255 }, 1000 },
    { { 255, 255, 255 }, 5000 },
    { { 200, 200, 255 }, 5000 },
}};

void MaskTransitionClearForcedMask(Player* player);
// true entre OnPlayDestroy e o próximo OnSceneInit quando o corpo estava
// ativo na cena anterior — sinaliza para religar automaticamente na cena
// nova, usando a spec já guardada em gCustomBodySpec (nunca limpa por troca
// de cena, só pelo toggle manual).
bool gCustomBodyPendingRestore = false;

void CustomBodyFreeSkelBuffers(SkelAnime& skelAnime) {
    if (skelAnime.jointTable != nullptr) {
        ZELDA_ARENA_FREE_DEBUG(skelAnime.jointTable);
        skelAnime.jointTable = nullptr;
    }
    if (skelAnime.morphTable != nullptr) {
        ZELDA_ARENA_FREE_DEBUG(skelAnime.morphTable);
        skelAnime.morphTable = nullptr;
    }
    // Sem uma postura opcional na próxima spec, o update não pode conservar
    // o ponteiro do skeleton anterior junto com as tabelas já liberadas.
    skelAnime.skeleton = nullptr;
    skelAnime.skeletonHeader = nullptr;
    skelAnime.dListCount = 0;
}

void CustomBodyFreeSkelBuffers() {
    CustomBodyFreeSkelBuffers(gCustomBodySkelAnime);
    CustomBodyFreeSkelBuffers(gCustomBodyShieldSkelAnime);
}

void CustomBodyWaterVoidReset(Player* player) {
    if (player != nullptr && gCustomBodyWaterVoidOwnsInputDisable) {
        player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
    }
    gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::None;
    gCustomBodyWaterVoidOwnsInputDisable = false;
    gCustomBodyWaterVoidTimer = 0;
}

u8 MaskTransitionLerpColor(u8 from, u8 to, float t) {
    return static_cast<u8>(std::clamp(static_cast<int>(std::lround(from + (to - from) * t)), 0, 255));
}

void MaskTransitionApplyEnvironment(PlayState* play, float phase, bool revealing) {
    if (play == nullptr || !gMaskTransitionEnvSaved) {
        return;
    }

    const float clamped = std::clamp(phase, 0.0f, 3.0f);
    const MaskTransitionLightStage* from = nullptr;
    const MaskTransitionLightStage* to = nullptr;
    float t = 0.0f;
    if (revealing) {
        from = &kMaskTransitionLightStages[2];
        t = clamped / 3.0f;
    } else if (clamped <= 1.0f) {
        to = &kMaskTransitionLightStages[0];
        t = clamped;
    } else if (clamped <= 2.0f) {
        from = &kMaskTransitionLightStages[0];
        to = &kMaskTransitionLightStages[1];
        t = clamped - 1.0f;
    } else {
        from = &kMaskTransitionLightStages[1];
        to = &kMaskTransitionLightStages[2];
        t = clamped - 2.0f;
    }

    EnvLightSettings& base = play->envCtx.lightSettings;
    if (revealing) {
        const s16 targetFogNear = static_cast<s16>(
            std::lround(base.fogNear + (from->fogNear - base.fogNear) * t));
        play->envCtx.adjFogNear = targetFogNear - base.fogNear;
        for (int i = 0; i < 3; ++i) {
            const u8 targetFog = MaskTransitionLerpColor(base.fogColor[i], from->fog[i], t);
            const u8 targetAmbient = MaskTransitionLerpColor(base.ambientColor[i], from->ambient[i], t);
            const u8 targetLight1 = MaskTransitionLerpColor(base.light1Color[i], 0, t);
            play->envCtx.adjFogColor[i] = static_cast<s16>(targetFog) - base.fogColor[i];
            play->envCtx.adjAmbientColor[i] = static_cast<s16>(targetAmbient) - base.ambientColor[i];
            play->envCtx.adjLight1Color[i] = static_cast<s16>(targetLight1) - base.light1Color[i];
        }
        return;
    }

    const s16 fromFogNear = from != nullptr ? from->fogNear : base.fogNear;
    const s16 toFogNear = to != nullptr ? to->fogNear : base.fogNear;
    const s16 targetFogNear = static_cast<s16>(std::lround(fromFogNear + (toFogNear - fromFogNear) * t));
    play->envCtx.adjFogNear = targetFogNear - base.fogNear;
    for (int i = 0; i < 3; ++i) {
        const u8 fromFog = from != nullptr ? from->fog[i] : base.fogColor[i];
        const u8 toFog = to != nullptr ? to->fog[i] : base.fogColor[i];
        const u8 fromAmbient = from != nullptr ? from->ambient[i] : base.ambientColor[i];
        const u8 toAmbient = to != nullptr ? to->ambient[i] : base.ambientColor[i];
        const u8 targetFog = MaskTransitionLerpColor(fromFog, toFog, t);
        const u8 targetAmbient = MaskTransitionLerpColor(fromAmbient, toAmbient, t);
        const u8 targetLight1 = MaskTransitionLerpColor(base.light1Color[i], 0, std::min(clamped, 1.0f));
        play->envCtx.adjFogColor[i] = static_cast<s16>(targetFog) - base.fogColor[i];
        play->envCtx.adjAmbientColor[i] = static_cast<s16>(targetAmbient) - base.ambientColor[i];
        play->envCtx.adjLight1Color[i] = static_cast<s16>(targetLight1) - base.light1Color[i];
    }
}

void MaskTransitionApplyPointLight(PlayState* play, Player* player, float phase, bool revealing) {
    if (!gMaskTransitionOwnsPointLight || play == nullptr || player == nullptr) {
        return;
    }

    const float clamped = std::clamp(phase, 0.0f, 3.0f);
    int stageIndex = 0;
    float radiusScale = clamped;
    if (clamped > 2.0f) {
        stageIndex = 2;
        radiusScale = clamped - 2.0f;
    } else if (clamped > 1.0f) {
        stageIndex = 1;
        radiusScale = clamped - 1.0f;
    }
    if (revealing) {
        stageIndex = 2;
        radiusScale = clamped / 3.0f;
    }

    const auto& stage = kMaskTransitionPointStages[stageIndex];
    gPointLightColor[0] = stage.color[0];
    gPointLightColor[1] = stage.color[1];
    gPointLightColor[2] = stage.color[2];
    gPointLightOffY = 28.0f;
    gPointLightRadius =
        static_cast<s16>(std::lround(stage.radius * std::clamp(radiusScale, 0.0f, 1.0f)));
    Lights_PointNoGlowSetInfo(&gPointLightInfo, static_cast<s16>(player->actor.world.pos.x),
                              static_cast<s16>(player->actor.world.pos.y + gPointLightOffY),
                              static_cast<s16>(player->actor.world.pos.z), gPointLightColor[0], gPointLightColor[1],
                              gPointLightColor[2], gPointLightRadius);
    if (!gPointLightActive && gPointLightRadius > 0) {
        gPointLightNode = LightContext_InsertLight(play, &play->lightCtx, &gPointLightInfo);
        gPointLightActive = gPointLightNode != nullptr;
    }
}

bool MaskTransitionShouldDraw(Player* player) {
    return player != nullptr && gMaskTransitionPhase == MaskTransitionPhase::Conceal &&
           !gMaskTransitionRemoving && player->currentMask == PLAYER_MASK_GORON;
}

// A cutscene da mascara roda em DOIS relogios: `gMaskTransitionElapsed`, que o
// host incrementa por frame, e `player->skelAnime.curFrame`, que o engine avanca
// pela animacao. Os SFX, as rampas e o flash pertencem ao primeiro — ele e
// monotonico e deterministico, e e o que garante que o pico do flash caia sempre
// no mesmo frame.
//
// O DESENHO da mascara, nao: ele precisa acompanhar a MAO e a CABECA do Link.
// Se `cl_setmask` atrasar, for reiniciada ou trocada por `Player_Action_Idle`, o
// contador do host segue correndo e a mascara aparece na mao antes de a mao
// chegar la. Por isso o desenho ancora no frame real enquanto a animacao de
// vestir estiver no ar, e so cai para o contador quando ela nao estiver — com
// aviso, porque isso significa que alguem trocou a animacao por baixo da cena.
int MaskTransitionDrawFrame(Player* player) {
    if (player == nullptr) {
        return gMaskTransitionElapsed;
    }
    const bool playingSetmask = reinterpret_cast<const void*>(player->skelAnime.animation) ==
                                reinterpret_cast<const void*>(kMaskOnAnimationPath);
    if (!playingSetmask) {
        static bool reportedDesync = false;
        if (!reportedDesync && gMaskTransitionElapsed > 0 && gMaskTransitionElapsed < kMaskSetmaskFrames) {
            reportedDesync = true;
            SPDLOG_WARN("ShipLua mascara: cl_setmask nao esta no ar no frame {} da cena; o desenho da mascara "
                        "voltou ao contador do host",
                        gMaskTransitionElapsed);
        }
        return gMaskTransitionElapsed;
    }
    return static_cast<int>(player->skelAnime.curFrame);
}

void DrawMaskTransitionHandImpl(PlayState* play, Player* player) {
    const int drawFrame = MaskTransitionDrawFrame(player);
    if (play == nullptr || !MaskTransitionShouldDraw(player) || drawFrame < 8 || drawFrame >= 12) {
        return;
    }

    // func_80128640 do MM: entre os frames 8 e 12 de cl_setmask, a máscara
    // ainda está na mão esquerda. Estes offsets são os valores originais do
    // draw, aplicados enquanto a matriz de PLAYER_LIMB_L_HAND está viva.
    Matrix_Push();
    Matrix_Translate(-323.67f, 412.15f, -969.96f, MTXMODE_APPLY);
    Matrix_RotateZYX(-0x32BE, -0x50DE, -0x7717, MTXMODE_APPLY);
    ShipLuaEmitScaledDisplayList(play, kMmGoronMaskDListPath, 1.0f, 1.0f, 1.0f);
    Matrix_Pop();
    if (!gMaskTransitionMaskHandDrawLogged) {
        gMaskTransitionMaskHandDrawLogged = true;
        SPDLOG_INFO("ShipLua mascara: gGoronMaskDL desenhada na mao esquerda (frame {} da animacao)", drawFrame);
    }
}

void DrawMaskTransitionHeadImpl(PlayState* play, Player* player) {
    // A mascara passa da mao para o rosto no frame 12 da animacao. Depois que
    // `cl_setmask` termina (66) a cena segura `cl_setmaskend`, e ai o relogio do
    // host volta a mandar — por isso o max com o contador: uma vez no rosto, a
    // mascara nao pode sumir so porque a animacao de vestir acabou.
    const int animFrame = MaskTransitionDrawFrame(player);
    const int drawFrame =
        gMaskTransitionElapsed >= kMaskSetmaskFrames ? std::max(animFrame, gMaskTransitionElapsed) : animFrame;
    if (play == nullptr || !MaskTransitionShouldDraw(player) || drawFrame < 12) {
        return;
    }

    // No rosto, Player_DrawTransformationMask do MM achata Z durante o
    // sofrimento e estica Y. Aqui a matriz ainda é a do PLAYER_LIMB_HEAD.
    float flattenZ = 0.0f;
    if (drawFrame >= 14 && drawFrame < 64) {
        flattenZ = 0.3f;
    } else if (drawFrame >= 64) {
        flattenZ = std::max(0.0f, 0.3f - (drawFrame - 63) * 0.015f);
    }
    float stretchY = 0.0f;
    if (drawFrame >= 16 && drawFrame <= 65) {
        stretchY = 0.1f;
    } else if (drawFrame > 65) {
        stretchY = std::max(0.0f, 0.1f - (drawFrame - 65) * 0.02f);
    }

    // A troca para a mascara de boca aberta (ver kMmGoronMaskScreamDListPath).
    const bool screaming = drawFrame >= kMaskScreamStartFrame;
    const char* maskPath = screaming ? kMmGoronMaskScreamDListPath : kMmGoronMaskDListPath;
    ShipLuaEmitScaledDisplayList(play, maskPath, 1.0f, 1.0f + stretchY, 1.0f - flattenZ);

    if (!gMaskTransitionMaskDrawLogged) {
        gMaskTransitionMaskDrawLogged = true;
        SPDLOG_INFO("ShipLua mascara: gGoronMaskDL desenhada no membro HEAD (frame {} da animacao, cena em {})",
                    drawFrame, gMaskTransitionElapsed);
    }
    if (screaming && !gMaskTransitionScreamDrawLogged) {
        gMaskTransitionScreamDrawLogged = true;
        SPDLOG_INFO("ShipLua mascara: troca para object_mask_goron_DL_0014A0 (boca aberta) no frame {} da animacao",
                    drawFrame);
    }
}

bool MaskTransitionStartCloseup(PlayState* play) {
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr || gCutsceneActive) {
        return false;
    }

    // No MM, func_808323C0 seleciona CAM_SET_MASK_TRANSFORMATION; só depois
    // Player_Action_86 escolhe CAM_MODE_NORMAL (humano -> forma). Trocar o modo
    // da câmera normal do OoT não reproduz Camera_Demo4, portanto criamos uma
    // subcâmera e executamos a trajetória correspondente em CutsceneUpdate.
    Camera* active = GET_ACTIVE_CAM(play);
    if (active == nullptr) {
        return false;
    }

    gMaskCutsceneOriginalAt = active->at;
    gMaskCutsceneOriginalEye = active->eye;
    gMaskCutsceneOriginalFov = active->fov;
    gMaskCutsceneOriginalRoll = active->roll;
    gCutsceneSavedCamMode = active->mode;
    const f32 deltaX = active->eye.x - active->at.x;
    const f32 deltaY = active->eye.y - active->at.y;
    const f32 deltaZ = active->eye.z - active->at.z;
    gMaskCutsceneDistance =
        std::min(40.0f, std::sqrt((deltaX * deltaX) + (deltaY * deltaY) + (deltaZ * deltaZ)));
    gMaskCutsceneDistance = std::max(gMaskCutsceneDistance, 1.0f);
    gMaskCutsceneYaw = std::atan2(deltaX, deltaZ);
    gMaskCutsceneFov = 80.0f;
    gMaskCutsceneRollSign = (play->gameplayFrames & 1) != 0 ? -1.0f : 1.0f;

    Play_ClearAllSubCameras(play);
    gCutsceneCamId = Play_CreateSubCamera(play);
    if (gCutsceneCamId == SUBCAM_NONE) {
        gCutsceneCamId = 0;
        return false;
    }
    if (Camera* cam = Play_GetCamera(play, gCutsceneCamId); cam != nullptr) {
        cam->at = gMaskCutsceneOriginalAt;
        cam->eye = gMaskCutsceneOriginalEye;
        cam->eyeNext = gMaskCutsceneOriginalEye;
        cam->fov = gMaskCutsceneFov;
        cam->roll = 0;
    }
    Play_ChangeCameraStatus(play, CAM_ID_MAIN, CAM_STAT_WAIT);
    Play_ChangeCameraStatus(play, gCutsceneCamId, CAM_STAT_ACTIVE);

    gCutsceneStyle = CutsceneStyle::Mask;
    gCutsceneFroze = false;
    gCutsceneFrames = kMaskTransitionTotalFrames;
    gCutsceneElapsed = 0;
    gCutsceneActive = true;
    CutsceneUpdate(play);
    return true;
}

void MaskTransitionReset(Player* player) {
    PlayState* play = gPlayState;
    const bool wasActive = gMaskTransitionPhase != MaskTransitionPhase::None;
    if (player != nullptr && gMaskTransitionOwnsInputDisable) {
        player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
    }
    // DEVOLVER A ANIMACAO. A cena deixa `al_hensin_loop` pendurada na skelAnime
    // do Player e, enquanto a forma Goron vive, a action func fica pausada e
    // ninguem a substitui. Ao tirar a mascara o Link humano voltava com a
    // contorcao ainda tocando — e como `Player_Action_Idle` so troca de animacao
    // quando ela TERMINA (`animDone`, z_player.c:8358), um LOOP nunca terminava
    // e o rosto ficava travado de dor para sempre.
    //
    // Restaurar aqui cobre as tres saidas de uma vez: fim normal, cancelamento e
    // troca de cena.
    if (wasActive && player != nullptr && play != nullptr) {
        if (LinkAnimationHeader* idle = Player_GetIdleAnim(player); idle != nullptr) {
            LinkAnimation_Change(play, &player->skelAnime, idle, 1.0f, 0.0f, Animation_GetLastFrame(idle),
                                 ANIMMODE_LOOP, 0.0f);
        }
    }
    if (gMaskTransitionOwnsCutscene) {
        CutsceneStop(play);
    }
    if (gMaskTransitionOwnsPointLight) {
        PointLightDetach(play);
    }
    if (gMaskTransitionEnvSaved && play != nullptr) {
        for (int i = 0; i < 3; ++i) {
            play->envCtx.adjAmbientColor[i] = gMaskTransitionSavedAdjustments.ambient[i];
            play->envCtx.adjLight1Color[i] = gMaskTransitionSavedAdjustments.light1[i];
            play->envCtx.adjFogColor[i] = gMaskTransitionSavedAdjustments.fog[i];
        }
        play->envCtx.adjFogNear = gMaskTransitionSavedAdjustments.fogNear;
    }
    gMaskTransitionPhase = MaskTransitionPhase::None;
    gMaskTransitionFramesRemaining = 0;
    gMaskTransitionRevealTimeout = 0;
    gMaskTransitionElapsed = 0;
    gMaskTransitionFlashAlpha = 0;
    gMaskTransitionOwnsInputDisable = false;
    gMaskTransitionOwnsCutscene = false;
    gMaskTransitionOwnsPointLight = false;
    gMaskTransitionEnvSaved = false;
    gMaskTransitionRemoving = false;
    gMaskTransitionMaskHandDrawLogged = false;
    gMaskTransitionMaskDrawLogged = false;
    gMaskTransitionScreamDrawLogged = false;
    if (wasActive) {
        SPDLOG_INFO("ShipLua mascara: camera, input, luz e ambiente restaurados");
    }
}

int MaskTransitionStart(Player* player, PlayState* play) {
    MaskTransitionReset(player);
    gMaskTransitionSavedEnv = play->envCtx.lightSettings;
    for (int i = 0; i < 3; ++i) {
        gMaskTransitionSavedAdjustments.ambient[i] = play->envCtx.adjAmbientColor[i];
        gMaskTransitionSavedAdjustments.light1[i] = play->envCtx.adjLight1Color[i];
        gMaskTransitionSavedAdjustments.fog[i] = play->envCtx.adjFogColor[i];
    }
    gMaskTransitionSavedAdjustments.fogNear = play->envCtx.adjFogNear;
    gMaskTransitionEnvSaved = true;
    gMaskTransitionPhase = MaskTransitionPhase::Conceal;
    gMaskTransitionFramesRemaining = kMaskTransitionPeakFrame;
    gMaskTransitionElapsed = 0;
    gMaskTransitionOwnsInputDisable = true;
    gMaskTransitionOwnsPointLight = !gPointLightActive;
    gMaskTransitionOwnsCutscene = MaskTransitionStartCloseup(play);
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->linearVelocity = 0.0f;
    player->actor.velocity.y = 0.0f;
    SPDLOG_INFO("ShipLua mascara: cutscene MM iniciada; troca no frame {} (camera={})", kMaskTransitionPeakFrame,
                gMaskTransitionOwnsCutscene ? "close-up" : "indisponivel");
    return kMaskTransitionPeakFrame;
}

void MaskTransitionStartRemoval(Player* player, int animationFrames) {
    MaskTransitionReset(player);
    gMaskTransitionPhase = MaskTransitionPhase::Conceal;
    gMaskTransitionFramesRemaining = std::max(animationFrames, 1);
    gMaskTransitionElapsed = 0;
    gMaskTransitionOwnsInputDisable = true;
    gMaskTransitionRemoving = true;
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->linearVelocity = 0.0f;
    player->actor.velocity.y = 0.0f;
    SPDLOG_INFO("ShipLua mascara: retirada iniciada; troca em {} frames", gMaskTransitionFramesRemaining);
}

void MaskTransitionBeginReveal(Player* player) {
    if (gMaskTransitionPhase != MaskTransitionPhase::Conceal) {
        return;
    }
    // A timer Lua troca o corpo no fim da animacao. Fixar alpha no pico neste
    // ponto garante que nenhum frame mostre Link e Goron simultaneamente.
    gMaskTransitionPhase = MaskTransitionPhase::Reveal;
    gMaskTransitionFramesRemaining = 0;
    gMaskTransitionRevealTimeout = 0;
    gMaskTransitionFlashAlpha = 255;
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    SPDLOG_INFO("ShipLua mascara: corpo trocado sob o pico branco; iniciando reveal");
}

void MaskTransitionUpdate(Player* player) {
    if (gMaskTransitionPhase == MaskTransitionPhase::None) {
        return;
    }

    PlayState* play = gPlayState;
    if (play == nullptr || gSaveContext.gameMode != GAMEMODE_NORMAL ||
        (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_WATER)) != 0) {
        MaskTransitionClearForcedMask(player);
        MaskTransitionReset(player);
        return;
    }

    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->linearVelocity = 0.0f;
    player->actor.velocity.y = 0.0f;

    // A CENA ASSUME A ANIMACAO DO PLAYER.
    //
    // `Player_Action_Idle` faz duas coisas que arruinavam a cutscene: e ela quem
    // chama `LinkAnimation_Update` (z_player.c:8349) e e ela quem TROCA a
    // animacao por uma pose de espera ou um fidget assim que a atual termina
    // (`Player_ChooseNextIdleAnim`, :8370). Com a cena prendendo o Link parado, o
    // seletor de idle roubava `cl_setmask` no meio — o aviso de dessincronizacao
    // pegou isso no frame 45 — e dai vinham o flicker e a volta repentina para a
    // pose padrao.
    //
    // Pausar sozinho nao resolve: pausar a action func congelaria a animacao,
    // porque e ela que avanca o frame. Entao a cena faz as duas pontas — pausa e
    // avanca por conta propria.
    //
    // Este hook roda em z_player.c:12335, DEPOIS do clear do flag em :12193, e
    // por isso o pause vale para o frame seguinte. E o mesmo padrao de
    // `CustomBodyHoldPlayerAction`.
    player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
    if (play != nullptr) {
        LinkAnimation_Update(play, &player->skelAnime);
    }

    if (gMaskTransitionPhase == MaskTransitionPhase::Conceal) {
        if (gMaskTransitionFramesRemaining > 0) {
            --gMaskTransitionFramesRemaining;
            ++gMaskTransitionElapsed;
        }

        if (gMaskTransitionRemoving) {
            if (gMaskTransitionElapsed == 8) {
                ShipLua::MmAudio_PlayVoiceSfx(0x1856); // NA_SE_IT_SET_TRANSFORM_MASK
            }
            if (gMaskTransitionFramesRemaining <= kMaskTransitionFlashBuildFrames) {
                const int builtFrames =
                    kMaskTransitionFlashBuildFrames - gMaskTransitionFramesRemaining + 1;
                gMaskTransitionFlashAlpha = static_cast<s16>(
                    std::clamp(builtFrames * 255 / kMaskTransitionFlashBuildFrames, 0, 255));
            }
            if (gMaskTransitionFramesRemaining == 0 && ++gMaskTransitionRevealTimeout >= 15) {
                MaskTransitionReset(player);
            }
            return;
        }

        // D_8085D8F0 / Player_Action_86. Os IDs usam as amostras reais do
        // soundfont do MM, sem substituicao por efeitos do OoT.
        switch (gMaskTransitionElapsed) {
            case 2:
                ShipLua::MmAudio_PlayVoiceSfx(0x0877); // NA_SE_PL_PUT_OUT_ITEM
                break;
            case 4:
                ShipLua::MmAudio_PlayVoiceSfx(0x1856); // NA_SE_IT_SET_TRANSFORM_MASK
                break;
            case 11:
                ShipLua::MmAudio_PlayVoiceSfx(0x0874); // NA_SE_PL_FREEZE_S
                break;
            case 20:
                ShipLua::MmAudio_PlayVoiceSfx(0x1858); // NA_SE_IT_TRANSFORM_MASK_BROKEN
                break;
            case 30:
                ShipLua::MmAudio_PlayVoiceSfx(0x09AA); // NA_SE_PL_TRANSFORM_VOICE
                break;
            case kMaskEnvRampSurge:
                // Unico SFX que nao vem de `D_8085D8F0`: e o raio que a cena
                // dispara junto da virada da rampa azul, entao acompanha ela.
                ShipLua::MmAudio_PlayVoiceSfx(0x2912); // NA_SE_EV_LIGHTNING_HARD
                break;
            case kMaskTransitionPoseFrames + 1:
                ShipLua::MmAudio_PlayVoiceSfx(0x484F); // NA_SE_SY_TRANSFORM_MASK_FLASH
                break;
            default:
                break;
        }

        // Fim do gesto de vestir -> comeca a CONTORCAO. Antes daqui a cena
        // trocava para `cl_setmaskend` (tres frames) e a segurava em loop, o
        // que le como pose parada. `al_hensin` e a transformacao de verdade.
        if (gMaskTransitionElapsed == kMaskHensinStartFrame) {
            if (auto* hensin = ResourceMgr_LoadAnimByName(kMaskHensinAnimationPath); hensin != nullptr) {
                const f32 lastFrame = Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(hensin));
                LinkAnimation_Change(play, &player->skelAnime,
                                     reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(kMaskHensinAnimationPath)),
                                     1.0f, 0.0f, lastFrame, ANIMMODE_ONCE, 0.0f);
                SPDLOG_INFO("ShipLua mascara: contorcao al_hensin iniciada no frame {} ({} frames)",
                            gMaskTransitionElapsed, kMaskHensinFrames);
            } else {
                // Sem saida silenciosa: se a contorcao nao carregar, a cena
                // continua com a pose curta antiga, mas o log diz o motivo.
                SPDLOG_WARN("ShipLua mascara: al_hensin nao carregou; caindo para cl_setmaskend (pose curta)");
                if (auto* endAnim = ResourceMgr_LoadAnimByName(kMaskOnEndAnimationPath); endAnim != nullptr) {
                    const f32 lastFrame = Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(endAnim));
                    LinkAnimation_Change(
                        play, &player->skelAnime,
                        reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(kMaskOnEndAnimationPath)), 1.0f, 0.0f,
                        lastFrame, ANIMMODE_LOOP, 0.0f);
                }
            }
        }

        // A contorcao acaba junto com a pose. Se o flash atrasar (timeout de
        // reveal, mod lento), `al_hensin_loop` segura o sofrimento em vez de
        // deixar o Link congelado no ultimo frame.
        //
        // ANIMMODE_ONCE, nao LOOP: em loop ela nunca termina, e uma animacao que
        // nunca termina e uma animacao que `Player_Action_Idle` nunca substitui.
        // Era assim que o rosto contorcido sobrevivia ate depois de tirar a
        // mascara. Os 48 frames dela cobrem de sobra os seis que faltam ate o
        // pico do flash.
        if (gMaskTransitionElapsed == kMaskTransitionPoseFrames) {
            if (auto* loop = ResourceMgr_LoadAnimByName(kMaskHensinLoopAnimationPath); loop != nullptr) {
                const f32 lastFrame = Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(loop));
                LinkAnimation_Change(
                    play, &player->skelAnime,
                    reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(kMaskHensinLoopAnimationPath)), 1.0f, 0.0f,
                    lastFrame, ANIMMODE_ONCE, 0.0f);
            }
        }

        float envPhase = 0.0f;
        float pointPhase = 0.0f;
        if (gMaskTransitionElapsed >= kMaskEnvRampStart) {
            if (gMaskTransitionElapsed < kMaskEnvRampSurge) {
                envPhase = std::min(1.0f, (gMaskTransitionElapsed - (kMaskEnvRampStart - 1)) * 0.1f);
            } else if (gMaskTransitionElapsed < kMaskEnvRampPeak) {
                envPhase = std::min(2.0f, 1.0f + (gMaskTransitionElapsed - (kMaskEnvRampSurge - 1)) * 0.5f);
            } else {
                envPhase = std::min(3.0f, 2.0f + (gMaskTransitionElapsed - (kMaskEnvRampPeak - 1)) * 0.2f);
            }
            if (gMaskTransitionElapsed < kMaskPointRampPeak) {
                pointPhase = std::min(1.0f, (gMaskTransitionElapsed - (kMaskEnvRampStart - 1)) * 0.2f);
            } else {
                pointPhase = std::min(3.0f, 1.0f + (gMaskTransitionElapsed - (kMaskPointRampPeak - 1)) * 0.55f);
            }
        }
        MaskTransitionApplyEnvironment(play, envPhase, false);
        MaskTransitionApplyPointLight(play, player, pointPhase, false);

        if (gMaskTransitionElapsed > kMaskTransitionPoseFrames) {
            const int builtFrames = gMaskTransitionElapsed - kMaskTransitionPoseFrames;
            gMaskTransitionFlashAlpha =
                static_cast<s16>(std::clamp(builtFrames * 255 / kMaskTransitionFlashBuildFrames, 0, 255));
        }
        if (gMaskTransitionFramesRemaining == 0) {
            // Uma falha no callback Lua nao pode deixar o jogador sem input.
            // O body normalmente e ativado no mesmo frame; esta margem so e
            // usada como escape seguro se o mod for descarregado no meio.
            if (++gMaskTransitionRevealTimeout >= 15) {
                MaskTransitionClearForcedMask(player);
                MaskTransitionReset(player);
            }
        }
        return;
    }

    gMaskTransitionFlashAlpha = std::max<s16>(0, gMaskTransitionFlashAlpha - 20);
    const float revealPhase = 3.0f * static_cast<float>(gMaskTransitionFlashAlpha) / 255.0f;
    MaskTransitionApplyEnvironment(play, revealPhase, true);
    MaskTransitionApplyPointLight(play, player, revealPhase, true);
    if (gMaskTransitionFlashAlpha == 0) {
        MaskTransitionReset(player);
    }
}

void CustomBodyClearRollHitbox(Player* player, PlayState* play);

// Desliga o corpo e destrói o ator hospedeiro se ainda existir. clearSpec
// distingue o toggle manual (limpa tudo, inclusive a spec guardada) da
// limpeza interna de troca de cena (mantém a spec para religar sozinho).
void CustomBodyDeactivate(PlayState* play, bool clearSpec) {
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (gCustomBodyActive) {
        if (player != nullptr) {
            CustomBodyClearRollHitbox(player, play);
            player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_DRAW;
            player->stateFlags1 &= ~PLAYER_STATE1_SHIELDING;
            CustomBodyWaterVoidReset(player);
        }
        if (gCustomBodyActor != nullptr && play != nullptr) {
            Actor_Kill(gCustomBodyActor);
        }
    }
    gCustomBodyActive = false;
    gCustomBodyActor = nullptr;
    CustomBodyFreeSkelBuffers();
    gCustomBodyCurrentAnim.clear();
    gCustomBodyOverrideAnim.reset();
    gCustomBodyOverrideOnce = false;
    gCustomBodyRollCharge = 0;
    gCustomBodyRollSpikesActive = false;
    gCustomBodyRollMagicDrainTimer = 0;
    gCustomBodyRollWallBounceTimer = 0;
    gCustomBodyRollPhase = CustomBodyRollPhase::None;
    CustomBodyWaterVoidReset(nullptr);
    gCustomBodyGroundPoundPhase = CustomBodyGroundPoundPhase::None;
    gCustomBodyGroundPoundTimer = 0;
    gCustomBodyPunchHitActive = false;
    gCustomBodyPunchQueued = false;
    gCustomBodyRollColliderArmed = false;
    gCustomBodyShieldActive = false;
    gCustomBodyAction = CustomBodyAction::None;
    gCustomBodyComboStep = 0;
    gCustomBodyPunchRecovering = false;
    gCustomBodyShieldAnimation = nullptr;
    // `Reset()` descarta o estado da maquina. Devolva antes tudo que ela possa
    // ter pendurado no Player, inclusive quando o corpo ja marcou inactive.
    ShipLua::MmForm::ReleaseBody(player);
    ShipLua::MmForm::Reset();
    if (clearSpec) {
        gCustomBodySpec = CustomBodySpec{};
        gCustomBodyPendingRestore = false;
    }
}

// Chamado em OnPlayDestroy: a troca de cena já descartou (ou está
// descartando) a arena Zelda de onde jointTable/morphTable foram alocados —
// chamar ZELDA_ARENA_FREE_DEBUG aqui seria double-free num ponteiro pendente
// (o mesmo bug que o fork de referência documentou e corrigiu). Só zera os
// ponteiros; o ator hospedeiro já foi destruído pela própria troca de cena.
// Marca pending-restore para OnSceneInit religar sozinho na cena nova.
void CustomBodyResetForSceneChange() {
    if (gMaskTransitionPhase != MaskTransitionPhase::None) {
        MaskTransitionClearForcedMask(nullptr);
    }
    // A cena nova cria câmeras próprias; segurar o id da subcâmera antiga
    // deixaria o jogador sem controle de câmera. Só descarta o estado — não
    // chama CutsceneStop, que mexeria em ponteiros já em teardown.
    gCutsceneActive = false;
    gCutsceneCamId = 0;
    gCutsceneElapsed = 0;
    // O Player antigo já está em teardown; só descartamos o estado do flash.
    // A nova cena cria um Player limpo, portanto não há flag de input a limpar.
    MaskTransitionReset(nullptr);
    gCustomBodyPendingRestore = gCustomBodyActive;
    gCustomBodyActive = false;
    gCustomBodyActor = nullptr;
    gCustomBodySkelAnime.jointTable = nullptr;
    gCustomBodySkelAnime.morphTable = nullptr;
    gCustomBodyShieldSkelAnime.jointTable = nullptr;
    gCustomBodyShieldSkelAnime.morphTable = nullptr;
    gCustomBodyShieldSkelAnime.skeleton = nullptr;
    gCustomBodyShieldSkelAnime.skeletonHeader = nullptr;
    gCustomBodyShieldSkelAnime.dListCount = 0;
    gCustomBodyCurrentAnim.clear();
    gCustomBodyOverrideAnim.reset();
    gCustomBodyOverrideOnce = false;
    gCustomBodyRollCharge = 0;
    gCustomBodyRollSpikesActive = false;
    gCustomBodyRollMagicDrainTimer = 0;
    gCustomBodyRollWallBounceTimer = 0;
    gCustomBodyRollPhase = CustomBodyRollPhase::None;
    CustomBodyWaterVoidReset(nullptr);
    gCustomBodyGroundPoundPhase = CustomBodyGroundPoundPhase::None;
    gCustomBodyGroundPoundTimer = 0;
    gCustomBodyPunchHitActive = false;
    gCustomBodyPunchQueued = false;
    gCustomBodyRollColliderArmed = false;
    gCustomBodyShieldActive = false;
    gCustomBodyAction = CustomBodyAction::None;
    gCustomBodyComboStep = 0;
    gCustomBodyPunchRecovering = false;
    gCustomBodyShieldAnimation = nullptr;
    gCustomBodyPlay = nullptr;
    // O Player antigo ja esta em teardown, mas a ordem do contrato continua:
    // ReleaseBody sempre precede Reset.
    ShipLua::MmForm::ReleaseBody(nullptr);
    ShipLua::MmForm::Reset();
}

const char* CustomBodyRollPhaseName() {
    switch (gCustomBodyRollPhase) {
        case CustomBodyRollPhase::Enter:
            return "enter";
        case CustomBodyRollPhase::Rolling:
            return "rolling";
        case CustomBodyRollPhase::Exit:
            return "exit";
        default:
            return "none";
    }
}

const char* CustomBodyGroundPoundPhaseName() {
    switch (gCustomBodyGroundPoundPhase) {
        case CustomBodyGroundPoundPhase::Rising:
            return "ground_pound_rise";
        case CustomBodyGroundPoundPhase::Falling:
            return "ground_pound_fall";
        case CustomBodyGroundPoundPhase::Impact:
            return "ground_pound_impact";
        default:
            return nullptr;
    }
}

// Fase do rolamento pela fonte que estiver no comando. Os nomes que a maquina
// devolve sao os MESMOS deste arquivo ("enter"/"rolling"/"exit" e os de ground
// pound), de proposito: o payload que o mod observa nao muda de vocabulario
// quando o dono troca.
const char* CustomBodyCurrentRollPhaseName() {
    if (MmFormOwnsRoll()) {
        const char* fromForm = ShipLua::MmForm::RollPhaseName();
        if (fromForm != nullptr) {
            return fromForm;
        }
        return "none";
    }
    const char* groundPound = CustomBodyGroundPoundPhaseName();
    return groundPound != nullptr ? groundPound : CustomBodyRollPhaseName();
}

// Nivel de carga e espinhos pela fonte no comando. As duas escalas convivem: o
// caminho antigo conta 0..60, a maquina 4..0x36 (54) — o desenho usa
// `charge >= 5` e `(charge - 4) * 0.02`, que sao os numeros do MM e por isso
// caem certo nos dois.
int CustomBodyRollChargeForDisplay() {
    return MmFormOwnsRoll() ? static_cast<int>(ShipLua::MmForm::RollChargeLevel()) : gCustomBodyRollCharge;
}

bool CustomBodyRollSpikesForDisplay() {
    return MmFormOwnsRoll() ? ShipLua::MmForm::RollSpikesActive() : gCustomBodyRollSpikesActive;
}

const char* CustomBodyWaterVoidPhaseName() {
    switch (gCustomBodyWaterVoidPhase) {
        case CustomBodyWaterVoidPhase::Curl:
            return "curl";
        case CustomBodyWaterVoidPhase::Ball:
            return "ball";
        case CustomBodyWaterVoidPhase::Triggered:
            return "triggered";
        default:
            return "none";
    }
}

// Player_Action_96 de MM reflete a bola em paredes quando ela já alcançou
// velocidade de ataque. O roll vanilla de OoT tem escala menor (e pode acabar
// antes do callback de ator), portanto o hook OnPlayerBonk chama este helper
// no mesmo frame. AT_HIT é preservado para dyna/objetos quebráveis receberem
// seu dano antes de qualquer reflexão.
void CustomBodyHandleRollWallBounce(Player* player, PlayState* play) {
    constexpr f32 kMinimumBounceSpeed = 6.0f;
    constexpr int kBounceCooldownFrames = 4;
    // A maquina portada tem quique proprio, dentro de `ActionGoronRoll` — com a
    // excecao de dyna que este caminho nao tem. Quando ela e a dona, este helper
    // sai de cena inteiro.
    if (MmFormOwnsRoll()) {
        return;
    }
    if (!gCustomBodyActive || !gChainRoll || player == nullptr || play == nullptr ||
        gCustomBodyRollPhase != CustomBodyRollPhase::Rolling || gCustomBodyRollWallBounceTimer > 0 ||
        fabsf(player->linearVelocity) < kMinimumBounceSpeed || (player->actor.bgCheckFlags & BGCHECKFLAG_WALL) == 0 ||
        (gCustomBodyRollColliderArmed && (player->cylinder.base.atFlags & AT_HIT) != 0)) {
        return;
    }

    // Mesma reflexão usada pelo enhancement BounceOffWalls do host e
    // equivalente à rotação por wallYaw do Player_Action_96.
    player->yaw = ((player->actor.wallYaw - player->yaw) + player->actor.wallYaw) - 0x8000;
    player->actor.shape.rot.y = player->yaw;
    player->actor.world.rot.y = player->yaw;
    player->linearVelocity = std::max(kMinimumBounceSpeed, fabsf(player->linearVelocity) * 0.85f);
    gCustomBodyRollWallBounceTimer = kBounceCooldownFrames;
    Player_PlaySfx(&player->actor, NA_SE_PL_BODY_HIT);
    Player_SetupRoll(player, play);
}

bool CustomBodyStartAnimation(PlayState* play, const std::string& name, f32 speed, bool once, bool reverse = false) {
    // As duas saidas abaixo eram silenciosas, e foi por isso que o instrumento
    // ficou sem diagnostico: a acao nao comecava e nada aparecia no log. Cada
    // motivo agora se identifica uma vez por nome.
    static std::set<std::string> reportedMissing;
    static std::set<std::string> reportedLoad;
    const auto path = gCustomBodySpec.animsPrefixed.find(name);
    if (play == nullptr || path == gCustomBodySpec.animsPrefixed.end()) {
        if (play != nullptr && reportedMissing.insert(name).second) {
            SPDLOG_WARN("ShipLua corpo: animacao '{}' nao esta declarada na spec", name);
        }
        return false;
    }
    auto* loaded = ResourceMgr_LoadAnimByName(path->second.c_str());
    if (loaded == nullptr) {
        if (reportedLoad.insert(name).second) {
            SPDLOG_WARN("ShipLua corpo: animacao '{}' declarada como '{}' nao carregou pelo Resource Manager", name,
                        path->second);
        }
        return false;
    }
    const f32 lastFrame = Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(loaded));
    const f32 startFrame = reverse ? lastFrame : 0.0f;
    const f32 endFrame = reverse ? 0.0f : lastFrame;
    LinkAnimation_Change(play, &gCustomBodySkelAnime,
                          reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(path->second.c_str())),
                          reverse ? -speed : speed, startFrame, endFrame, once ? ANIMMODE_ONCE : ANIMMODE_LOOP, 0.0f);
    // TODA troca de animacao: o primeiro frame apenas captura a origem, nunca
    // herda o `prevTransl` da animacao anterior. Sem isto a diferenca entre a
    // raiz da animacao velha e a da nova vira um deslocamento de um frame.
    // `SkelAnime_UpdateTranslation` limpa o flag sozinho na primeira chamada.
    gCustomBodySkelAnime.movementFlags |= ANIM_FLAG_NOMOVE;
    gCustomBodyCurrentAnim = name;
    gCustomBodyOverrideAnim = name;
    gCustomBodyOverrideOnce = once;
    return true;
}

// O Player de MM não permite que Goron nade. O fork de referência faz a
// sequência pg_maru_change -> bola afundando -> Play_TriggerVoidOut quando a
// profundidade passa de 20. Mantemos essa regra como opt-in da spec para que
// qualquer outro corpo externo continue usando a água normal do OoT.
void CustomBodyUpdateWaterVoid(Player* player, PlayState* play) {
    if (!gCustomBodySpec.waterVoid || player == nullptr || play == nullptr) {
        return;
    }
    // O estado precisa continuar valido por CONDICAO a cada frame, e nao ficar
    // travado por ter sido ligado uma vez — e assim que o Player do MM funciona.
    // Sem esta invalidacao, Curl/Ball sobrevivem ao jogador sair da agua e
    // Triggered nunca volta depois do void-out. Qualquer uma das duas deixa o
    // corpo preso: `waterBall` em CustomBodyActorDraw liga a display list
    // enrolada SEM checar actionFunc, entao o Goron vira bola permanente mesmo
    // andando, sacando a ocarina ou defendendo com R.
    if (gCustomBodyWaterVoidPhase != CustomBodyWaterVoidPhase::None && player->actor.yDistToWater <= 20.0f) {
        gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::None;
        gCustomBodyWaterVoidTimer = 0;
        if (gCustomBodyWaterVoidOwnsInputDisable) {
            player->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
            gCustomBodyWaterVoidOwnsInputDisable = false;
        }
        return;
    }
    if (gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::Triggered) {
        return;
    }
    if (gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::None) {
        if (player->actor.yDistToWater <= 20.0f || (player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) != 0 ||
            player->getItemId != GI_NONE) {
            return;
        }
        gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::Curl;
        gCustomBodyWaterVoidOwnsInputDisable = true;
        gCustomBodyWaterVoidTimer = 0;
        player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
        player->linearVelocity = 0.0f;
        player->actor.velocity.y = -2.0f;
        player->actor.gravity = -0.5f;
        CustomBodyClearRollHitbox(player, play);
        CustomBodyStartAnimation(play, "roll_enter", 0.67f, true);
        Player_PlaySfx(&player->actor, NA_SE_EV_DIVE_INTO_WATER);
        Player_PlaySfx(&player->actor, NA_SE_PL_BODY_HIT);
        return;
    }

    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    player->linearVelocity = 0.0f;
    if (gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::Curl) {
        player->actor.velocity.y = -2.0f;
        player->actor.gravity = -0.5f;
        const auto curl = gCustomBodySpec.animsPrefixed.find("roll_enter");
        auto* loaded = curl == gCustomBodySpec.animsPrefixed.end() ? nullptr : ResourceMgr_LoadAnimByName(curl->second.c_str());
        if (loaded == nullptr || gCustomBodySkelAnime.curFrame >=
                                  Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(loaded)) - 0.5f) {
            gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::Ball;
            gCustomBodyOverrideAnim.reset();
            gCustomBodyOverrideOnce = false;
            player->actor.gravity = -2.0f;
        }
        return;
    }

    // A bola não gira: ela só afunda por aproximadamente 15 frames antes da
    // transição. Isso replica a separação de fases do Player de MM.
    player->actor.velocity.y = -2.0f;
    player->actor.gravity = -2.0f;
    if (++gCustomBodyWaterVoidTimer >= 15) {
        gCustomBodyWaterVoidPhase = CustomBodyWaterVoidPhase::Triggered;
        Player_PlaySfx(&player->actor, NA_SE_OC_ABYSS);
        Play_TriggerVoidOut(play);
    }
}

// Perfil compacto e fechado para os socos de Goron. A forma usa os quads de
// ataque que o Player do OoT já possui; assim não há collider alocado por Lua
// nem parâmetros de dano arbitrários vindos de mod. Os nomes são convenções da
// spec, e só ganham colisão quando a animação correspondente foi declarada.
struct CustomBodyPunchInfo {
    const char* animation;
    f32 hitStart;
    f32 hitEnd;
    f32 nearDist;
    f32 farDist;
    f32 sideOffset;
    f32 halfWidth;
    f32 yBottom;
    f32 yTop;
};

constexpr std::array<CustomBodyPunchInfo, 3> kCustomBodyGoronPunches = {{
    // MM sMeleeAttackAnimInfo: punch A {6, 8}, punch B {12, 18}, punch C {8, 14}.
    { "punch_a", 6.0f, 8.0f, 20.0f, 55.0f, -15.0f, 15.0f, 20.0f, 55.0f },
    { "punch_b", 12.0f, 18.0f, 20.0f, 55.0f, 15.0f, 15.0f, 20.0f, 55.0f },
    { "punch_c", 8.0f, 14.0f, -10.0f, 30.0f, 0.0f, 30.0f, 5.0f, 30.0f },
}};

const CustomBodyPunchInfo* CustomBodyGetPunchInfo() {
    for (const auto& punch : kCustomBodyGoronPunches) {
        if (gCustomBodyCurrentAnim == punch.animation &&
            gCustomBodySpec.animsPrefixed.find(punch.animation) != gCustomBodySpec.animsPrefixed.end()) {
            return &punch;
        }
    }
    return nullptr;
}

const char* CustomBodyPunchRecoveryAnimation(const char* punchName, bool lockedOn) {
    if (std::strcmp(punchName, "punch_a") == 0) {
        return lockedOn ? "punch_a_end_run" : "punch_a_end";
    }
    if (std::strcmp(punchName, "punch_b") == 0) {
        return lockedOn ? "punch_b_end_run" : "punch_b_end";
    }
    if (std::strcmp(punchName, "punch_c") == 0) {
        return lockedOn ? "punch_c_end_run" : "punch_c_end";
    }
    return nullptr;
}

bool CustomBodyStartPunchRecovery(PlayState* play, const char* punchName, const Player* player) {
    // MM escolhe a variante `R` por Player_CheckHostileLockOn (Z travado num
    // inimigo), nao por estar em movimento — `sMeleeAttackAnimInfo` guarda o
    // par end/endR e o seletor e o lock-on. A regra por velocidade era nossa.
    const bool lockedOn = (player->stateFlags1 & PLAYER_STATE1_HOSTILE_LOCK_ON) != 0;
    const char* preferred = CustomBodyPunchRecoveryAnimation(punchName, lockedOn);
    if (preferred != nullptr && CustomBodyStartAnimation(play, preferred, 1.0f, true)) {
        return true;
    }
    // Um mod pode fornecer somente a recuperação parada; ela é melhor que
    // cortar o punch diretamente para idle quando o Player ainda se move.
    if (preferred != nullptr && std::strstr(preferred, "_run") != nullptr) {
        std::string fallback(preferred);
        fallback.erase(fallback.size() - 4);
        return CustomBodyStartAnimation(play, fallback, 1.0f, true);
    }
    return false;
}

// O encadeamento do combo passou a ser indexado por gCustomBodyComboStep na
// acao de soco; a versao por nome de animacao saiu junto com o caminho antigo.

// Os punches de Goron em MM usam a translacao da junta raiz como movimento
// real. LinkAnimation_Update apenas ENFILEIRA o carregamento da pose; a fila
// roda depois de Actor_UpdateAll. Portanto, chamar SkelAnime_UpdateTranslation
// diretamente aqui leria o frame anterior e a pose carregada depois tornaria a
// deslocar a malha. Enfileirar MOVEACTOR depois de LOADFRAME preserva a ordem
// nativa: carrega a pose, extrai o delta e neutraliza jointTable[0] antes do
// draw. O deslocamento real continua restrito ao perfil fechado dos socos.
void CustomBodyQueueRootMotion(PlayState* play, Player* player) {
    const bool moves = MmFormOwnsPunch() ? ShipLua::MmForm::UsesRootMotion() : (CustomBodyGetPunchInfo() != nullptr);

    // A raiz vertical dos punches e pose, nao movimento do Player. UPDATEY faz
    // o callback devolver jointTable[0].y a baseTransl; arg3=0 abaixo descarta
    // o delta Y para que o jogador continue preso ao piso.
    gCustomBodySkelAnime.movementFlags |= ANIM_FLAG_UPDATEY;
    if (!moves) {
        // Mesmo fora do root motion a raiz precisa ser neutralizada antes do
        // draw, mas uma animacao declarada por Lua nao pode mover o Player.
        gCustomBodySkelAnime.movementFlags |= ANIM_FLAG_NOMOVE;
    }
    AnimationContext_SetMoveActor(play, &player->actor, &gCustomBodySkelAnime, 0.0f);
}

void CustomBodySetPunchQuadVertices(Player* player, const CustomBodyPunchInfo& punch) {
    const f32 sinYaw = Math_SinS(player->yaw);
    const f32 cosYaw = Math_CosS(player->yaw);
    const f32 rightX = cosYaw;
    const f32 rightZ = -sinYaw;
    const Vec3f& pos = player->actor.world.pos;

    const f32 farX = pos.x + sinYaw * punch.farDist + rightX * punch.sideOffset;
    const f32 farZ = pos.z + cosYaw * punch.farDist + rightZ * punch.sideOffset;
    const f32 nearX = pos.x + sinYaw * punch.nearDist + rightX * punch.sideOffset;
    const f32 nearZ = pos.z + cosYaw * punch.nearDist + rightZ * punch.sideOffset;
    Vec3f a = { farX - rightX * punch.halfWidth, pos.y + punch.yTop, farZ - rightZ * punch.halfWidth };
    Vec3f b = { farX + rightX * punch.halfWidth, pos.y + punch.yTop, farZ + rightZ * punch.halfWidth };
    Vec3f c = { nearX + rightX * punch.halfWidth, pos.y + punch.yBottom, nearZ + rightZ * punch.halfWidth };
    Vec3f d = { nearX - rightX * punch.halfWidth, pos.y + punch.yBottom, nearZ - rightZ * punch.halfWidth };
    Collider_SetQuadVertices(&player->meleeWeaponQuads[0], &a, &b, &c, &d);
}

// Player_Update já produziu os quads da espada antes do ator hospedeiro
// atualizar (ACTORCAT_PLAYER vem antes de ACTORCAT_ITEMACTION). Enquanto a
// animação Goron é a dona visual do golpe, elimina ambos para a espada
// invisível não acertar junto do punho.
void CustomBodyUpdatePunchHitbox(Player* player, PlayState* play) {
    gCustomBodyPunchHitActive = false;
    const CustomBodyPunchInfo* punch = CustomBodyGetPunchInfo();
    if (punch == nullptr) {
        return;
    }

    ColliderQuad* primary = &player->meleeWeaponQuads[0];
    ColliderQuad* secondary = &player->meleeWeaponQuads[1];
    Collider_ResetQuadAT(play, &primary->base);
    Collider_ResetQuadAT(play, &secondary->base);
    primary->info.toucher.dmgFlags = 0;
    secondary->info.toucher.dmgFlags = 0;

    // MM usa deteccao antecipada para o Goron: o quad liga em 5.0 e nao no
    // hitStart da tabela (`earlyStart = isGoron ? 5.0f : hitStart`, de
    // func_8083FCF0). A janela por hitStart era mais estreita que a original.
    constexpr f32 kGoronPunchEarlyStart = 5.0f;
    const f32 frame = gCustomBodySkelAnime.curFrame;
    if (frame < std::min(kGoronPunchEarlyStart, punch->hitStart) || frame > punch->hitEnd) {
        return;
    }

    CustomBodySetPunchQuadVertices(player, *punch);
    primary->base.atFlags = AT_ON | AT_TYPE_PLAYER;
    // Equivalente OoT de DMG_GORON_PUNCH: impacto pesado, não um corte da
    // espada oculta. O valor 2 segue a força transformada do MM e é fechado
    // no host para não virar uma API de dano irrestrito para Lua.
    primary->info.toucher.dmgFlags = DMG_HAMMER_SWING;
    primary->info.toucher.damage = 2;
    primary->info.toucherFlags = TOUCH_ON | TOUCH_NEAREST;
    CollisionCheck_SetAT(play, &play->colChkCtx, &primary->base);
    gCustomBodyPunchHitActive = true;
}

void CustomBodyClearRollHitbox(Player* player, PlayState* play) {
    if (!gCustomBodyRollColliderArmed) {
        return;
    }
    Collider_ResetCylinderAT(play, &player->cylinder.base);
    player->cylinder.base.atFlags = AT_NONE;
    player->cylinder.info.toucherFlags = TOUCH_NONE;
    player->cylinder.info.toucher.dmgFlags = 0;
    player->cylinder.info.toucher.damage = 0;
    player->cylinder.dim.radius = gCustomBodyRollColliderRadius;
    player->cylinder.dim.height = gCustomBodyRollColliderHeight;
    player->cylinder.dim.yShift = gCustomBodyRollColliderYShift;
    Collider_UpdateCylinder(&player->actor, &player->cylinder);
    gCustomBodyRollColliderArmed = false;
}

// MM Player_Action_96 arma o cylinder do próprio Player para a bola (raio 25,
// dano 1). O equivalente OoT seguro do impacto é DMG_HAMMER_SWING. Só usamos
// este caminho depois da transição curl e acima da velocidade mínima, para que
// a animação de entrada/saída nunca cause dano invisível.
void CustomBodyUpdateRollHitbox(Player* player, PlayState* play, bool rolling) {
    const bool groundPoundImpact = gCustomBodyGroundPoundPhase == CustomBodyGroundPoundPhase::Impact;
    const bool attackActive = groundPoundImpact || (rolling && fabsf(player->linearVelocity) > 2.0f);
    if (!attackActive) {
        CustomBodyClearRollHitbox(player, play);
        return;
    }

    if (!gCustomBodyRollColliderArmed) {
        gCustomBodyRollColliderArmed = true;
        gCustomBodyRollColliderRadius = player->cylinder.dim.radius;
        gCustomBodyRollColliderHeight = player->cylinder.dim.height;
        gCustomBodyRollColliderYShift = player->cylinder.dim.yShift;
    }
    Collider_ResetCylinderAT(play, &player->cylinder.base);
    player->cylinder.base.atFlags = AT_ON | AT_TYPE_PLAYER;
    player->cylinder.info.elemType = ELEMTYPE_UNK2;
    player->cylinder.info.toucherFlags = TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL;
    player->cylinder.info.toucher.dmgFlags = DMG_HAMMER_SWING;
    player->cylinder.info.toucher.damage = groundPoundImpact ? 4 : 1;
    // 25 é o raio canônico de Player_SetCylinderForAttack durante a esfera
    // normal e durante os espinhos. O impacto usa 60, como DMG_GORON_POUND.
    player->cylinder.dim.radius = groundPoundImpact ? 60.0f : 25.0f;
    player->cylinder.dim.height = 34;
    player->cylinder.dim.yShift = 0;
    Collider_UpdateCylinder(&player->actor, &player->cylinder);
    CollisionCheck_SetAT(play, &play->colChkCtx, &player->cylinder.base);
}

// O MM não tem um "asset de ground pound": é uma fase física dentro de
// Player_Action_96. O host conserva Player_Action_Roll para o pipeline de
// movimento do OoT e injeta somente o arco vertical/impacto no frame posterior
// ao update vanilla. Assim não substituímos Player_Update nem o bg check.
void CustomBodyUpdateGroundPound(Player* player, PlayState* play, bool rolling) {
    if (gCustomBodyGroundPoundPhase == CustomBodyGroundPoundPhase::None) {
        if (rolling && !gCustomBodyRollSpikesActive && CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {
            gCustomBodyGroundPoundPhase = CustomBodyGroundPoundPhase::Rising;
            gCustomBodyGroundPoundTimer = 0;
            gCustomBodyRollCharge = 0;
            Player_SetupRoll(player, play);
            player->linearVelocity = 0.0f;
            player->actor.velocity.y = 14.0f;
            player->actor.gravity = -1.2f;
        }
        return;
    }

    // O roll vanilla termina sua animação enquanto o Goron ainda está no ar;
    // rearmá-lo impede que Idle assuma antes de a bola tocar o solo. O skeleton
    // humano continua oculto e a renderização externa permanece a bola.
    if (player->actionFunc != Player_Action_Roll) {
        Player_SetupRoll(player, play);
    }
    player->linearVelocity = 0.0f;

    if (gCustomBodyGroundPoundPhase == CustomBodyGroundPoundPhase::Rising) {
        if (player->actor.velocity.y <= 0.0f) {
            gCustomBodyGroundPoundPhase = CustomBodyGroundPoundPhase::Falling;
        }
    } else if (gCustomBodyGroundPoundPhase == CustomBodyGroundPoundPhase::Falling) {
        if (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND_TOUCH) {
            gCustomBodyGroundPoundPhase = CustomBodyGroundPoundPhase::Impact;
            gCustomBodyGroundPoundTimer = 6;
            player->actor.velocity.y = 0.0f;
            Actor_RequestQuake(play, 3, 12);
            Rumble_Request(0.0f, 180, 12, 100);
        } else if (++gCustomBodyGroundPoundTimer > 180) {
            // Mesma rede de seguranca do water void: se o toque no chao nunca
            // chega (queda na agua, empurrao, plataforma que some), a fase ficava
            // travada, o rearme de Player_SetupRoll continuava a cada frame e o
            // corpo seguia como bola indefinidamente. Voltar a None devolve o
            // controle em vez de exigir que o jogador recarregue a cena.
            gCustomBodyGroundPoundPhase = CustomBodyGroundPoundPhase::None;
            gCustomBodyGroundPoundTimer = 0;
        }
    } else {
        player->actor.velocity.y = 0.0f;
        if (--gCustomBodyGroundPoundTimer <= 0) {
            gCustomBodyGroundPoundPhase = CustomBodyGroundPoundPhase::None;
        }
    }
}

// MM ativa os espinhos com custo inicial 2 e, enquanto estão ativos, drena 1
// unidade a cada 10 frames. O save de OoT expõe o mesmo medidor; alterar o
// valor somente em MAGIC_STATE_IDLE evita disputar uma poção/flecha mágica ou
// outra transição de HUD em curso.
void CustomBodyUpdateRollSpikes(PlayState* play, bool rolling) {
    if (!rolling || gCustomBodyGroundPoundPhase != CustomBodyGroundPoundPhase::None) {
        gCustomBodyRollSpikesActive = false;
        gCustomBodyRollMagicDrainTimer = 0;
        if (!rolling) {
            gCustomBodyRollCharge = std::max(gCustomBodyRollCharge - 2, 0);
        }
        return;
    }
    const bool holdingA = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_A);
    if (gCustomBodyRollSpikesActive) {
        if (!holdingA || gSaveContext.magic <= 0) {
            gCustomBodyRollSpikesActive = false;
            gCustomBodyRollMagicDrainTimer = 0;
            gCustomBodyRollCharge = 4;
            return;
        }
        if (gSaveContext.magicState == MAGIC_STATE_IDLE && --gCustomBodyRollMagicDrainTimer <= 0) {
            gSaveContext.magic = std::max(0, static_cast<int>(gSaveContext.magic) - 1);
            gCustomBodyRollMagicDrainTimer = 10;
            if (gSaveContext.magic == 0) {
                gCustomBodyRollSpikesActive = false;
                gCustomBodyRollCharge = 4;
            }
        }
        return;
    }
    if (holdingA) {
        gCustomBodyRollCharge = std::min(gCustomBodyRollCharge + 1, 60);
    } else {
        gCustomBodyRollCharge = std::max(gCustomBodyRollCharge - 2, 0);
    }
    if (gCustomBodyRollCharge >= 60) {
        if (gSaveContext.magicState == MAGIC_STATE_IDLE && gSaveContext.magic >= 2) {
            gSaveContext.magic -= 2;
            gCustomBodyRollSpikesActive = true;
            gCustomBodyRollMagicDrainTimer = 10;
        } else {
            // Mantém a pré-carga visível, mas nunca ativa o estado de espinho
            // sem o custo de magia que o MM exige.
            gCustomBodyRollCharge = 59;
        }
    }
}

// Núcleo comum entre o toggle manual e a auto-restauração pós-troca-de-cena:
// aloca o skelAnime a partir de gCustomBodySpec e spawna o ator hospedeiro.
// Não mexe em gCustomBodySpec — quem chama já garantiu que está preenchida.
// A custom body hides Link's shield limb, so the native Player draw path never
// refreshes shieldQuad. Reuse that quad here to preserve native block and
// projectile reflection behavior without creating a second broad collider.
void CustomBodyUpdateShieldCollider(Player* player, PlayState* play) {
    const s16 yaw = player->actor.shape.rot.y;
    SkinMatrix_SetTranslateRotateYXZScale(&player->shieldMf, 1.0f, 1.0f, 1.0f, 0, yaw + 0x8000, 0, 0.0f, 0.0f,
                                          0.0f);

    const f32 sinYaw = Math_SinS(yaw);
    const f32 cosYaw = Math_CosS(yaw);
    const f32 rightX = cosYaw;
    const f32 rightZ = -sinYaw;
    constexpr f32 frontDistance = 12.0f;
    constexpr f32 halfWidth = 25.0f;
    constexpr f32 bottomY = -10.0f;
    constexpr f32 topY = 75.0f;
    const Vec3f& position = player->actor.world.pos;
    const f32 centerX = position.x + sinYaw * frontDistance;
    const f32 centerZ = position.z + cosYaw * frontDistance;

    Vec3f a = { centerX - rightX * halfWidth, position.y + topY, centerZ - rightZ * halfWidth };
    Vec3f b = { centerX + rightX * halfWidth, position.y + topY, centerZ + rightZ * halfWidth };
    Vec3f c = { centerX + rightX * halfWidth, position.y + bottomY, centerZ + rightZ * halfWidth };
    Vec3f d = { centerX - rightX * halfWidth, position.y + bottomY, centerZ - rightZ * halfWidth };

    // The Deku shield is wood; every other selection, including none, is metal.
    player->shieldQuad.base.colType = player->currentShield == PLAYER_SHIELD_DEKU ? COLTYPE_WOOD : COLTYPE_METAL;
    Collider_ResetQuadAC(play, &player->shieldQuad.base);
    Collider_ResetQuadAT(play, &player->shieldQuad.base);
    Collider_SetQuadVertices(&player->shieldQuad, &a, &b, &c, &d);
    CollisionCheck_SetAC(play, &play->colChkCtx, &player->shieldQuad.base);
    CollisionCheck_SetAT(play, &play->colChkCtx, &player->shieldQuad.base);
}

// ---------------------------------------------------------------------------
// Acoes com dono: soco, defesa e instrumento.
//
// Portadas de skijer/Shipwright @ Not-Enough-Items
// (soh/mods/transformation_masks/mm_player_form.cpp), que resolve exatamente o
// mesmo problema — forma de MM rodando dentro do OoT. As tres regras que a
// referencia estabelece e que faltavam aqui:
//
//   1. a entrada e o BOTAO, lido direto do Input, e nao um estado do Player do
//      OoT que a forma nunca produz;
//   2. enquanto a acao vive, PLAYER_STATE3_PAUSE_ACTION_FUNC impede a action
//      func do Player de rodar e reescrever a pose;
//   3. o desenho olha para a FASE da acao, nunca para o nome da animacao.
// ---------------------------------------------------------------------------

const char* CustomBodyPunchAnimationName(uint8_t step) {
    switch (step) {
        case 0:
            return "punch_a";
        case 1:
            return "punch_b";
        case 2:
            return "punch_c";
        default:
            return nullptr;
    }
}

bool CustomBodySpecHasAnim(const char* name) {
    return name != nullptr && gCustomBodySpec.anims.find(name) != gCustomBodySpec.anims.end();
}

// O flag e limpo por Player_UpdateCommon no fim de cada frame
// (z_player.c:12193), entao precisa ser reafirmado sempre — mesmo padrao ja
// usado aqui para PLAYER_STATE2_DISABLE_DRAW.
void CustomBodyHoldPlayerAction(Player* player) {
    player->stateFlags3 |= PLAYER_STATE3_PAUSE_ACTION_FUNC;
}

void CustomBodyEndAction(Player* player) {
    if (gCustomBodyAction == CustomBodyAction::Punch && player != nullptr) {
        player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
        player->meleeWeaponQuads[1].base.atFlags &= ~AT_ON;
    }
    gCustomBodyAction = CustomBodyAction::None;
    gCustomBodyComboStep = 0;
    gCustomBodyPunchQueued = false;
    gCustomBodyPunchRecovering = false;
    gCustomBodyOverrideAnim.reset();
    gCustomBodyOverrideOnce = false;
}

// A forma nao empunha espada, entao `meleeWeaponState` nunca sobe e a versao
// anterior — que dependia dele — simplesmente nunca via um soco. A referencia
// intercepta o B do Input (mm_player_form.cpp:3905).
bool CustomBodyBeginPunch(Player* player, PlayState* play) {
    if (!CustomBodySpecHasAnim("punch_a")) {
        return false;
    }
    gCustomBodyComboStep = 0;
    gCustomBodyPunchQueued = false;
    gCustomBodyPunchRecovering = false;
    // Limpa dano herdado de qualquer collider do Player antes de armar o quad:
    // uma flag deixada por uma bola Goron anterior contaria como segundo ataque.
    player->meleeWeaponQuads[0].base.atFlags &= ~AT_ON;
    player->meleeWeaponQuads[0].info.toucher.dmgFlags = 0;
    player->meleeWeaponQuads[1].base.atFlags &= ~AT_ON;
    player->meleeWeaponQuads[1].info.toucher.dmgFlags = 0;
    player->cylinder.base.atFlags &= ~AT_ON;
    player->cylinder.info.toucher.dmgFlags = 0;
    if (!CustomBodyStartAnimation(play, "punch_a", 1.0f, true)) {
        return false;
    }
    gCustomBodyAction = CustomBodyAction::Punch;
    // Player_StopHorizontalMovement na entrada do golpe (referencia:4410).
    player->linearVelocity = 0.0f;
    // O swing proprio do Goron vive no banco de MM, que ainda nao temos motor
    // para tocar (OOT-AUDIO-001). O equivalente pesado do OoT e o fallback que
    // a propria referencia declara para quando o mm.o2r nao responde.
    Player_PlaySfx(&player->actor, NA_SE_IT_SWORD_SWING_HARD);
    return true;
}

bool CustomBodyBeginShieldVisual() {
    if (gCustomBodyShieldSkelAnime.skeleton == nullptr) {
        return false;
    }
    gCustomBodyShieldActive = true;
    // LOOP, nao ONCE: a postura de defesa e continua enquanto o R estiver
    // mantido. `SkelAnime_InitFlex` ja inicia em Animation_PlayLoop — usar
    // PlayOnce aqui fazia a animacao parar no ultimo frame e a defesa congelar.
    if (gCustomBodyShieldAnimation != nullptr) {
        Animation_PlayLoop(&gCustomBodyShieldSkelAnime, gCustomBodyShieldAnimation);
    }
    return true;
}

void CustomBodyEndShieldVisual(Player* player) {
    gCustomBodyShieldActive = false;
    if (player != nullptr) {
        player->stateFlags1 &= ~PLAYER_STATE1_SHIELDING;
    }
}

void CustomBodyUpdateShieldVisual(Player* player, PlayState* play) {
    CustomBodyUpdateShieldCollider(player, play);
    SkelAnime_Update(&gCustomBodyShieldSkelAnime);
}

// ---------------------------------------------------------------------------
// Ponte para a maquina de acoes portada (soh/soh/mmform/MmGoronForm.cpp).
//
// A maquina pede animacao pelo NOME; e aqui que o nome vira a spec do mod. Ela
// entra como fonte de FALLBACK: nao troca a pose sozinha nem toma o override,
// entao o hook `body_anim_select` e as acoes que ja funcionam seguem intactos.
// ---------------------------------------------------------------------------
bool MmFormStartAnimation(const char* name, float speed, bool once, bool reverse) {
    return name != nullptr && CustomBodyStartAnimation(gCustomBodyPlay, name, speed, once, reverse);
}

bool MmFormHasAnimation(const char* name) {
    return CustomBodySpecHasAnim(name);
}

float MmFormCurrentFrame() {
    return gCustomBodySkelAnime.curFrame;
}

// Ultimo frame de uma animacao declarada. Devolve 0 quando o nome nao esta na
// spec ou o Resource Manager nao entrega — a maquina trata isso como "sem fim
// conhecido" e nao encadeia o combo por frame, em vez de comparar contra lixo.
float MmFormAnimationLastFrame(const char* name) {
    if (name == nullptr) {
        return 0.0f;
    }
    const auto path = gCustomBodySpec.animsPrefixed.find(name);
    if (path == gCustomBodySpec.animsPrefixed.end()) {
        return 0.0f;
    }
    auto* loaded = ResourceMgr_LoadAnimByName(path->second.c_str());
    if (loaded == nullptr) {
        return 0.0f;
    }
    return Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(loaded));
}

// Ponte para o instrumento da ocarina. O MM troca por forma; ver a nota em
// `ApplyGakkiInstrument` (MmGoronForm.cpp) para o limite conhecido de o indice
// dos tambores viver na sequencia do MM, nao na do OoT.
void MmFormSetOcarinaInstrument(std::uint8_t instrumentId) {
    static std::uint8_t lastReported = 0xFF;
    if (lastReported != instrumentId) {
        lastReported = instrumentId;
        SPDLOG_INFO("ShipLua gakki: instrumento da ocarina -> {}", instrumentId);
    }
    AudioOcarina_SetInstrument(instrumentId);
}

void MmFormPlaySample(const char* prefixedSamplePath) {
    if (prefixedSamplePath != nullptr) {
        ShipLua::MmAudio_PlaySampleOneShot(prefixedSamplePath);
    }
}

void MmFormInstallServices() {
    ShipLua::MmForm::Services services{};
    services.startAnimation = MmFormStartAnimation;
    services.hasAnimation = MmFormHasAnimation;
    services.currentFrame = MmFormCurrentFrame;
    services.animationLastFrame = MmFormAnimationLastFrame;
    services.setOcarinaInstrument = MmFormSetOcarinaInstrument;
    services.playMmSample = MmFormPlaySample;
    ShipLua::MmForm::Init(services);
}

bool CustomBodyActivateFromSpec(PlayState* play);

extern "C" void CustomBodyActorUpdate(Actor* actor, PlayState* play) {
    if (!gCustomBodyActive) {
        return;
    }
    gCustomBodyPlay = play;
    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        CustomBodyDeactivate(play, false);
        return;
    }
    // Reafirma todo frame: certas transições de ação do próprio vanilla
    // reescrevem stateFlags2 inteiro (não fazem só |=/&= de bits pontuais),
    // o que apaga a flag se ela só for setada uma vez no toggle — sintoma
    // visto em jogo: braço de manga verde do Link vazando atrás da cabeça
    // custom. Setar aqui garante que ela nunca fica limpa por mais de 1 frame.
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
    actor->world.pos = player->actor.world.pos;
    actor->shape.rot = player->actor.shape.rot;
    // Quem e o dono do rolamento neste corpo. `gChainRoll` vem do
    // `set_roll_mode("chain")` do mod: era "encadeie o roll do OoT para fingir
    // a bola", e passa a significar "use a bola portada do MM". Mesma intencao
    // do mod, implementacao de verdade por baixo.
    ShipLua::MmForm::SetBallEnabled(gChainRoll);
    // O soco vai para a maquina sempre que a spec declarar o primeiro golpe — a
    // mesma condicao que `CustomBodyBeginPunch` ja usava para existir.
    ShipLua::MmForm::SetPunchEnabled(CustomBodySpecHasAnim("punch_a"));
    // A maquina decide a posse de R; o host apenas confirma que o esqueleto
    // dedicado foi carregado e consegue materializar a acao Shield.
    ShipLua::MmForm::SetShieldEnabled(gCustomBodyShieldSkelAnime.skeleton != nullptr);
    const bool ownsRoll = MmFormOwnsRoll();
    const bool ownsPunch = MmFormOwnsPunch();
    const bool ownsShield = ShipLua::MmForm::ShieldEnabled();
    // O mod pode chamar `set_roll_mode("vanilla")` no meio de um rolamento. Sem
    // isto a maquina ficaria com o cylinder armado, o input travado e a
    // gravidade do baque penduradas no Player, e ninguem mais rodaria para
    // devolve-los.
    if (!ownsRoll && ShipLua::MmForm::IsRolling()) {
        ShipLua::MmForm::ReleaseBody(player);
    }

    if (gCustomBodyRollWallBounceTimer > 0) {
        --gCustomBodyRollWallBounceTimer;
    }
    // Todo o caminho antigo fica fora do ar quando a maquina e a dona. Duas
    // bolas no mesmo corpo e o pior estado possivel: cada uma escreveria
    // velocidade e yaw por cima da outra no mesmo frame.
    const bool ootRollActive = player->actionFunc == Player_Action_Roll;
    const bool nativeRolling = !ownsRoll && ootRollActive &&
                               gCustomBodySpec.modelsPrefixed.find("roll") != gCustomBodySpec.modelsPrefixed.end();
    CustomBodyUpdateWaterVoid(player, play);
    const bool waterVoidActive = gCustomBodyWaterVoidPhase != CustomBodyWaterVoidPhase::None;
    // O void da agua tem bola propria (afunda e faz void-out) e usa as mesmas
    // animacoes. Se ele entrar com a bola da maquina rolando, ela larga tudo —
    // collider, input, gravidade, sombra — e volta para idle, deixando o void
    // como unico dono. Sem isto os dois escreveriam `velocity.y` no mesmo frame.
    if (ownsRoll && waterVoidActive && ShipLua::MmForm::IsRolling()) {
        ShipLua::MmForm::ReleaseBody(player);
    }
    if (!ownsRoll) {
        if (!waterVoidActive && nativeRolling && gCustomBodyRollPhase == CustomBodyRollPhase::None) {
            if (CustomBodyStartAnimation(play, "roll_enter", 0.67f, true)) {
                gCustomBodyRollPhase = CustomBodyRollPhase::Enter;
            } else {
                gCustomBodyRollPhase = CustomBodyRollPhase::Rolling;
            }
        }
        if (!waterVoidActive && !nativeRolling && gCustomBodyRollPhase == CustomBodyRollPhase::Rolling &&
            gCustomBodyGroundPoundPhase == CustomBodyGroundPoundPhase::None) {
            if (CustomBodyStartAnimation(play, "roll_exit", 0.67f, true, true)) {
                gCustomBodyRollPhase = CustomBodyRollPhase::Exit;
            } else {
                gCustomBodyRollPhase = CustomBodyRollPhase::None;
            }
        }
    }
    // Enquanto a maquina e a dona, `rolling` vem dela. Aqui ainda e o estado do
    // frame ANTERIOR — ela so roda mais abaixo, depois do dispatcher de acoes —
    // e por isso o valor e recalculado logo apos o `Update()` dela, antes de
    // qualquer uso visual.
    auto rollingNow = [&]() {
        if (gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::Ball) {
            return true;
        }
        if (ownsRoll) {
            return ShipLua::MmForm::IsBallBody();
        }
        return (!waterVoidActive && nativeRolling && gCustomBodyRollPhase == CustomBodyRollPhase::Rolling) ||
               gCustomBodyGroundPoundPhase != CustomBodyGroundPoundPhase::None;
    };
    bool rolling = rollingNow();
    if (!ownsRoll && !waterVoidActive) {
        CustomBodyUpdateGroundPound(player, play, rolling);
        rolling = rollingNow();
    }
    if (!ownsRoll) {
        CustomBodyUpdateRollSpikes(play, rolling && !waterVoidActive);
    }
    // -----------------------------------------------------------------------
    // Dispatcher das acoes com dono.
    //
    // Defesa e instrumento agora entram na maquina. O host conserva apenas o
    // fallback de soco para specs que nao habilitam o combo portado.
    // -----------------------------------------------------------------------
    Input* input = &play->state.input[0];
    // `rolling` cobre so a esfera; o curl e o desenrolar tambem ocupam o corpo e
    // precisam bloquear as outras acoes do mesmo jeito. Dai `IsRolling()` (as
    // cinco fases) e nao `IsBallBody()` (as tres do meio) aqui.
    const bool bodyBusy = rolling || waterVoidActive ||
                           (ownsRoll && ShipLua::MmForm::IsRolling()) ||
                           (ownsPunch && ShipLua::MmForm::IsPunching()) ||
                           (ownsShield && ShipLua::MmForm::IsShielding()) ||
                           gCustomBodyGroundPoundPhase != CustomBodyGroundPoundPhase::None ||
                           gMaskTransitionPhase != MaskTransitionPhase::None;
    const bool ocarinaHeld = (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) != 0;
    // Estados do OoT em que a forma nao pode tomar o corpo. A ocarina e tratada
    // a parte: ela roda DENTRO de PLAYER_STATE1_IN_ITEM_CS e o instrumento
    // precisa conviver com ela, nunca pausa-la.
    const bool playerLocked =
        (player->stateFlags1 & (PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM |
                                PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_DEAD | PLAYER_STATE1_INPUT_DISABLED)) != 0;
    const bool onGround = (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) != 0;

    // Saidas: qualquer condicao que invalide a acao a encerra antes da entrada.
    if (gCustomBodyAction == CustomBodyAction::Punch && (bodyBusy || playerLocked)) {
        CustomBodyEndAction(player);
    }
    if (gCustomBodyAction == CustomBodyAction::None && !bodyBusy && !playerLocked && onGround) {
        if (!ownsPunch && CHECK_BTN_ALL(input->press.button, BTN_B)) {
            // Com a maquina no comando o B e dela: quem inicia o combo e
            // `TryStartPunch`. Dois combos armando o mesmo `meleeWeaponQuads[0]`
            // no mesmo frame e o estado que este `!ownsPunch` existe para evitar.
            CustomBodyBeginPunch(player, play);
        }
    }

    // Agua/void, transicao de mascara e o fallback legado ainda vivem no host.
    // Um unico gate externo bloqueia novas entradas sem duplicar a posse de
    // bola, soco, defesa ou instrumento, que a maquina ja enxerga sozinha.
    ShipLua::MmForm::SetEntryBlocked(gCustomBodyAction != CustomBodyAction::None || waterVoidActive ||
                                     gMaskTransitionPhase != MaskTransitionPhase::None);
    // O roll do OoT e o gatilho autoritativo do curl quando a maquina e dona.
    // Ela nao referencia o simbolo do overlay de z_player; recebe so o fato
    // deste frame e o converte antes de ceder a pose ao OoT.
    ShipLua::MmForm::SetOotRollActive(ownsRoll && ootRollActive);
    // A maquina portada roda TODO frame, mas so opina sobre a pose quando
    // nenhuma acao nossa e dona do corpo (ver o fallback mais abaixo). Rodar
    // sempre mantem a cessao ao OoT e as transicoes de ar coerentes.
    //
    // `gCustomBodyLastAnimFinished` e o fim de one-shot do frame ANTERIOR: o
    // `LinkAnimation_Update` deste frame so acontece bem mais abaixo, e a
    // maquina precisa do sinal agora para o curl virar bola.
    const bool machineWasRolling = ownsRoll && ShipLua::MmForm::IsRolling();
    const bool machineWasShielding = ownsShield && ShipLua::MmForm::IsShielding();
    const bool machineWasGakki = ShipLua::MmForm::GakkiPhase() != 0;
    ShipLua::MmForm::Update(player, play, gCustomBodyLastAnimFinished);
    const bool machineIsRolling = ownsRoll && ShipLua::MmForm::IsRolling();
    bool machineIsShielding = ownsShield && ShipLua::MmForm::IsShielding();
    const bool machineIsGakki = ShipLua::MmForm::GakkiPhase() != 0;
    const bool machineIsDamage = ShipLua::MmForm::CurrentAction() == ShipLua::MmForm::GoronAction::Damage;

    if (!machineWasShielding && machineIsShielding) {
        if (!CustomBodyBeginShieldVisual()) {
            SPDLOG_ERROR("ShipLua corpo: maquina entrou em Shield sem esqueleto visual disponivel");
            ShipLua::MmForm::ReleaseBody(player);
            machineIsShielding = false;
        }
    } else if (machineWasShielding && !machineIsShielding) {
        CustomBodyEndShieldVisual(player);
    }
    if (machineWasGakki && !machineIsGakki && !machineIsDamage) {
        // A decisao de terminar e da maquina; o override e armazenamento do
        // SkelAnime do host e precisa ser liberado na mesma borda.
        gCustomBodyOverrideAnim.reset();
        gCustomBodyOverrideOnce = false;
    }

    // A referencia troca `Player_Action_Roll` pela acao de curl. Aqui a
    // maquina vive fora do overlay de z_player, entao o equivalente e retirar
    // a action func vanilla assim que o curl assumir o corpo. Fazemos o mesmo
    // na borda de saida: enquanto a bola estava ativa a action func ficou
    // pausada, e deixa-la como Roll faria a esquiva antiga reaparecer depois do
    // desenrolar.
    if (ownsRoll &&
        ((machineIsRolling && ootRollActive) ||
         (machineWasRolling && !machineIsRolling && (player->stateFlags1 & PLAYER_STATE1_DAMAGED) == 0))) {
        player->actionFunc = Player_Action_Idle;
    }

    // A partir daqui `rolling` e o estado DESTE frame — o que escala o ator,
    // escolhe o offset e decide o desenho tem de ver a maquina ja atualizada.
    rolling = rollingNow();

    // A bola escreve `linearVelocity`, `yaw`, `velocity.y` e `gravity` por conta
    // propria. Sem pausar a action func do Player, ela desfaria tudo no frame
    // seguinte. O flag e limpo por `Player_UpdateCommon` todo frame
    // (z_player.c:12193), entao tem de ser reafirmado sempre.
    // Vale para a bola E para o combo: os dois escrevem movimento por conta
    // propria (o soco avanca pela translacao de raiz e desacelera o resto), e a
    // action func do Player desfaria os dois no frame seguinte.
    if (ShipLua::MmForm::WantsPlayerActionPaused()) {
        CustomBodyHoldPlayerAction(player);
    }

    if (machineIsShielding) {
        CustomBodyUpdateShieldVisual(player, play);
    } else if (gCustomBodyAction == CustomBodyAction::Punch) {
        CustomBodyHoldPlayerAction(player);
        if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
            if (gCustomBodyPunchRecovering) {
                // Durante a RECUPERACAO um B novo encadeia na hora, e o passo
                // volta ciclicamente para A depois de C (referencia:4740). Sem
                // isto o combo morria no terceiro golpe e so voltava a existir
                // depois de a recuperacao inteira terminar.
                const uint8_t nextStep = static_cast<uint8_t>((gCustomBodyComboStep + 1) % 3);
                const char* next = CustomBodyPunchAnimationName(nextStep);
                if (next != nullptr && CustomBodySpecHasAnim(next) &&
                    CustomBodyStartAnimation(play, next, 1.0f, true)) {
                    gCustomBodyComboStep = nextStep;
                    gCustomBodyPunchRecovering = false;
                    gCustomBodyPunchQueued = false;
                    player->linearVelocity = 0.0f;
                    Player_PlaySfx(&player->actor, NA_SE_IT_SWORD_SWING_HARD);
                }
            } else if (gCustomBodySkelAnime.curFrame >= 1.0f) {
                // MM guarda o proximo B enquanto A/B ainda corre; o frame 0 e o
                // mesmo B que iniciou o golpe e nao pode virar combo automatico.
                gCustomBodyPunchQueued = true;
            }
        }
        // MM desacelera durante o golpe; o avanco real vem do root motion.
        Math_StepToF(&player->linearVelocity, 0.0f, 5.0f);
    }
    // O draw nativo do MM posiciona o centro da esfera 12 unidades acima do
    // chão e a amplia 15%. Actor_Draw aplica yOffset * scale, portanto o
    // offset abaixo preserva exatamente esses 12 com a escala 0.0115.
    Actor_SetScale(actor, rolling ? 0.0115f : 0.01f);
    // O ator Player do OoT tem sua origem de colisão abaixo do ponto onde o
    // root do skeleton Goron de MM pousa. Sem compensação, o corpo ereto fica
    // visivelmente ~30 unidades acima do solo, embora a física do Player já
    // esteja em contato com ele. Actor_Draw multiplica yOffset pela escala,
    // portanto -3000.0f em 0.01 baixa o visual exatamente 30 unidades. O
    // modelo enrolado é uma DL separada e preserva o offset positivo do draw
    // nativo de MM (centro da esfera 12 unidades acima do chão).
    const f32 worldOffset = rolling ? gCustomBodySpec.rollOffset : gCustomBodySpec.groundOffset;
    actor->shape.yOffset = worldOffset / actor->scale.y;
    // Actor_Draw já aplica shape.rot antes de chamar este draw. Durante o
    // rolamento usamos esse transform para fazer a display list enrolada girar,
    // sem depender das macros de matriz que só existem no caminho C do engine.
    if (ownsRoll && ShipLua::MmForm::IsRolling() &&
        gCustomBodyWaterVoidPhase != CustomBodyWaterVoidPhase::Ball) {
        // A maquina ja escreveu pitch, tombamento e yaw em `player->actor.shape.rot`
        // com o giro real da esfera (`rollSpinRate`) — nao ha acumulador a manter
        // aqui. A copia do inicio deste update e anterior ao `Update()` dela, por
        // isso o rot e recopiado agora.
        gCustomBodyRollSpin = 0;
        actor->shape.rot = player->actor.shape.rot;
    } else if (rolling && gCustomBodyWaterVoidPhase != CustomBodyWaterVoidPhase::Ball) {
        // Caminho antigo: a rotacao acompanha a DISTANCIA PERCORRIDA, nao o
        // contador global de frames. A versao anterior usava
        // `gameplayFrames * -0x1200`: taxa fixa, sentido fixo, girando mesmo com
        // o Link parado e no sentido contrario ao do movimento — era o
        // "rolamento invertido" relatado.
        //
        // Uma bola de raio r que percorre d gira d/r radianos. Com a esfera do
        // Goron em ~13 unidades, 470 por unidade de avanco da a mesma cadencia
        // visual da taxa antiga em velocidade normal de rolamento, mas agora
        // proporcional e no sentido certo.
        gCustomBodyRollSpin += static_cast<s16>(player->linearVelocity * 470.0f);
        actor->shape.rot.x = player->actor.shape.rot.x + gCustomBodyRollSpin;
    } else if (gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::Ball) {
        actor->shape.rot.x = 0;
    } else {
        // Fora do rolamento o acumulador zera, para a proxima bola comecar
        // alinhada em vez de herdar o angulo da anterior.
        gCustomBodyRollSpin = 0;
        // E o pitch VOLTA a acompanhar o jogador. Sem esta linha o boneco
        // conservava para sempre o ultimo angulo da bola: so o ramo do
        // rolamento escrevia em shape.rot.x, entao apos rolar uma vez o corpo
        // ficava tombado em todas as poses seguintes. Na defesa com R isso
        // enterrava a cabeca dentro do casco — era o "Goron sem cabeca".
        actor->shape.rot.x = player->actor.shape.rot.x;
    }

    std::string wanted;
    const f32 speed = fabsf(player->linearVelocity);
    const bool climbing = (player->stateFlags1 & PLAYER_STATE1_CLIMBING_LADDER) != 0;
    const s16 climbInput = play->state.input[0].rel.stick_y;
    const char* climbDirection = !climbing ? "none" : climbInput > 0 ? "up" : climbInput < 0 ? "down" : "idle";
    // Porta com macaneta: este actionFunc e instalado somente depois de A ter
    // confirmado a abertura. Portas de correr/falsas continuam inteiramente no
    // caminho nativo, pois nao usam a animacao de empurrar/puxar do Goron.
    const bool handleDoorOpening = player->actionFunc == Player_Action_80845EF8 && player->doorActor != nullptr &&
                                   player->doorType == PLAYER_DOORTYPE_HANDLE;
    // O fluxo de baú marca GETTING_ITEM antes de disparar a animacao da caixa.
    // O actor-alvo diferencia esse caso de qualquer outro get-item/cutscene.
    const bool chestOpening = (player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) != 0 &&
                              player->interactRangeActor != nullptr && player->interactRangeActor->id == ACTOR_EN_BOX;
    // Ao ceder a escada ao OoT, a maquina conserva o ultimo override (em geral
    // `idle`) enquanto permanece em OotAction. A pose de escalada precisa ser
    // escolhida antes desse override. Os nomes continuam opcionais: corpos que
    // nao declaram a tabela convencional caem no hook Lua normalmente.
    const char* nativeClimbAnim = nullptr;
    if (climbing) {
        const bool rightStep = (player->av2.actionVar2 & 1) != 0;
        if (player->av2.actionVar2 < 0) {
            nativeClimbAnim = player->av2.actionVar2 == -4 ? "climb_start_b" : "climb_start_a";
        } else if (climbInput > 0) {
            nativeClimbAnim = rightStep ? "climb_up_r" : "climb_up_l";
        } else if (climbInput < 0) {
            nativeClimbAnim = rightStep ? "climb_down_r" : "climb_down_l";
        } else {
            nativeClimbAnim = "climb_wait";
        }
    }
    const bool nativeClimbOwnsPose = nativeClimbAnim != nullptr && CustomBodySpecHasAnim(nativeClimbAnim);
    if (nativeClimbOwnsPose) {
        wanted = nativeClimbAnim;
    } else if (gCustomBodyOverrideAnim.has_value()) {
        wanted = *gCustomBodyOverrideAnim;
    } else {
        const auto result = DispatchHookTransform(
            "hook.oot.player.body_anim_select",
            ShipLua::EventPayload{
                {"speed", static_cast<double>(speed)},
                {"on_ground", (player->actor.bgCheckFlags & 1) != 0},
                {"rolling", rolling},
                {"roll_charge", static_cast<std::int64_t>(CustomBodyRollChargeForDisplay())},
                {"roll_phase", std::string(CustomBodyCurrentRollPhaseName())},
                {"falling", (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) == 0 && player->actor.velocity.y < 0.0f},
                {"landing", (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND_TOUCH) != 0},
                {"climbing", climbing},
                {"climb_direction", std::string(climbDirection)},
                // O Player alterna av2.actionVar2 ao trocar o pé da escada.
                // O bit baixo se mantém estável inclusive nos valores negativos
                // de entrada (-2/-4), sem expor ponteiros ou ação interna ao Lua.
                {"climb_step", static_cast<std::int64_t>(player->av2.actionVar2 & 1)},
                {"climb_starting", climbing && player->av2.actionVar2 < 0},
                {"door_opening", handleDoorOpening},
                {"door_direction", std::string(!handleDoorOpening ? "none"
                                                                     : player->doorDirection < 0 ? "left" : "right")},
                {"chest_opening", chestOpening},
                // O discriminador e PLAYER_STATE2_OCARINA_PLAYING, ligada em
                // z_player.c:6122 no instante em que a ocarina sai (junto com
                // Player_Action_8084E3C4) e limpa em z_player.c:3345 quando ela
                // e guardada. Cobre a posse inteira do instrumento, nao so o
                // frame da nota.
                //
                // As duas tentativas anteriores falharam por motivos distintos:
                // PLAYER_STATE1_IN_ITEM_CS marca cutscene de ITEM em geral, e
                // msgCtx.ocarinaMode so deixa OCARINA_MODE_00 em estados
                // especificos de cancao — nunca enquanto o Link apenas segura a
                // ocarina, que e justamente quando o tambor deve aparecer.
                {"instrument", ocarinaHeld},
                // A nota vem do staff de execucao, a mesma fonte que o Player
                // de MM usa via ocarinaButtonIndex. A leitura anterior era o
                // botao cru, que durante a ocarina o OoT ja consumiu — por isso
                // quase nunca via nota alguma. Somente informativo: quem toca a
                // animacao do instrumento agora e a acao Gakki da maquina.
                {"instrument_note",
                 std::string([]() -> const char* {
                     OcarinaStaff* staff = AudioOcarina_GetPlayingStaff();
                     if (staff == nullptr || staff->state == 0) {
                         return "none";
                     }
                     switch (staff->buttonIndex) {
                         case OCARINA_BTN_A:
                             return "a";
                         case OCARINA_BTN_C_DOWN:
                             return "d";
                         case OCARINA_BTN_C_RIGHT:
                             return "r";
                         case OCARINA_BTN_C_LEFT:
                             return "l";
                         case OCARINA_BTN_C_UP:
                             return "u";
                         default:
                             return "none";
                     }
                 }())},
                // `attacking` reflete a acao de soco do corpo, nao mais
                // meleeWeaponState: a forma nao empunha espada, entao aquele
                // campo nunca subia e o soco nunca era anunciado ao mod.
                {"attacking", ownsPunch ? ShipLua::MmForm::IsPunching()
                                        : gCustomBodyAction == CustomBodyAction::Punch},
                {"punch_step", static_cast<std::int64_t>(ownsPunch ? ShipLua::MmForm::ComboStep()
                                                                   : gCustomBodyComboStep)},
                {"shielding", ownsShield && ShipLua::MmForm::IsShielding()},
                {"attack_animation", static_cast<std::int64_t>(player->meleeWeaponAnimation)},
            });
        if (result.has_value() && std::holds_alternative<std::string>(result->value)) {
            const std::string& requested = std::get<std::string>(result->value);
            if (gCustomBodySpec.anims.find(requested) != gCustomBodySpec.anims.end()) {
                wanted = requested;
            }
        }
    }
    if (wanted.empty() && !gCustomBodyOverrideAnim.has_value()) {
        // Fallback: a maquina de acoes portada escolhe. Ela substitui o limiar
        // de velocidade solto que existia aqui — mesma regra de idle/walk/run,
        // agora dentro de uma acao com dono, com cessao explicita ao OoT e com
        // as transicoes de ar tratadas antes do dispatch.
        const char* fromForm = ShipLua::MmForm::DesiredAnimation();
        if (fromForm != nullptr && CustomBodySpecHasAnim(fromForm)) {
            wanted = fromForm;
        } else {
            wanted = gCustomBodySpec.defaultAnim;
        }
    }

    if (wanted != gCustomBodyCurrentAnim) {
        // O ponteiro vem do membro persistente da spec, NÃO de uma temporária:
        // o engine guarda este endereço e o usa nos frames seguintes.
        const auto animPath = gCustomBodySpec.animsPrefixed.find(wanted);
        if (animPath != gCustomBodySpec.animsPrefixed.end()) {
            const bool reverse = gCustomBodySpec.reverseAnims.find(wanted) != gCustomBodySpec.reverseAnims.end();
            auto* loaded = reverse ? ResourceMgr_LoadAnimByName(animPath->second.c_str()) : nullptr;
            if (reverse && loaded == nullptr) {
                SPDLOG_WARN("ShipLua body animation: nao carreguei animacao reversa '{}'", wanted);
                return;
            }
            gCustomBodyCurrentAnim = wanted;
            const f32 lastFrame = reverse ? Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(loaded)) : 0.0f;
            LinkAnimation_Change(play, &gCustomBodySkelAnime, (LinkAnimationHeader*)animPath->second.c_str(),
                                 reverse ? -1.0f : 1.0f, reverse ? lastFrame : 0.0f, 0.0f, ANIMMODE_LOOP, 0.0f);
            // Mesmo motivo do caminho one-shot: nenhuma troca pode herdar o
            // `prevTransl` da animacao anterior.
            gCustomBodySkelAnime.movementFlags |= ANIM_FLAG_NOMOVE;
        }
    }
    // A escada do OoT NAO tem cadencia fixa: Player_Action_8084BF1C reescreve
    // `skelAnime.playSpeed` a cada frame a partir da magnitude do analogico
    // (z_player.c:13218) e usa SINAL NEGATIVO para descer. Enquanto o corpo
    // externo tocava os passos em velocidade fixa 1.0, a subida nao acompanhava
    // o Player e a descida — que so existe pelo sinal — nao acontecia.
    // Espelhar a playSpeed do Player resolve os dois casos com a mesma
    // animacao, exatamente como a tabela de escalada de MM faz.
    if (climbing) {
        gCustomBodySkelAnime.playSpeed = player->skelAnime.playSpeed;
    }
    const bool animationFinished = LinkAnimation_Update(play, &gCustomBodySkelAnime) != 0;
    // Guarda para o PROXIMO frame. A atribuicao fica aqui, e nao no fim da
    // funcao, porque o fallback de soco abaixo sai por `return`; no fim, o sinal
    // se perderia justamente nos frames em que uma acao termina.
    gCustomBodyLastAnimFinished = animationFinished;
    CustomBodyQueueRootMotion(play, player);
    // O quad do soco tambem passa a ser da maquina: `EnablePunchQuad` e
    // `DisablePunchQuad` rodam dentro de `ActionPunch`, com a mesma tabela de
    // janelas e o mesmo `earlyStart = 5.0f`.
    if (!ownsPunch) {
        CustomBodyUpdatePunchHitbox(player, play);
    }
    // Com a maquina no comando o collider da bola e dela: `ArmRollAttack` e
    // `ClearRollAttack` rodam dentro de `ActionGoronRoll`, com o raio por faixa
    // (25 rolando, 60 no baque) que o caminho antigo aproximava.
    if (!ownsRoll) {
        CustomBodyUpdateRollHitbox(player, play, rolling);
    }

    if (gCustomBodyAction == CustomBodyAction::Punch) {
        if (!animationFinished) {
            return;
        }
        if (gCustomBodyPunchRecovering) {
            CustomBodyEndAction(player);
            return;
        }
        // Um B guardado encadeia o proximo golpe sem passar pela recuperacao;
        // sem ele, a recuperacao fecha a acao e devolve o corpo a locomocao.
        if (gCustomBodyPunchQueued) {
            gCustomBodyPunchQueued = false;
            const char* next = CustomBodyPunchAnimationName(static_cast<uint8_t>(gCustomBodyComboStep + 1));
            if (next != nullptr && CustomBodySpecHasAnim(next) && CustomBodyStartAnimation(play, next, 1.0f, true)) {
                ++gCustomBodyComboStep;
                Player_PlaySfx(&player->actor, NA_SE_IT_SWORD_SWING_HARD);
                return;
            }
        }
        const char* punchName = CustomBodyPunchAnimationName(gCustomBodyComboStep);
        if (punchName != nullptr && CustomBodyStartPunchRecovery(play, punchName, player)) {
            gCustomBodyPunchRecovering = true;
            return;
        }
        CustomBodyEndAction(player);
        return;
    }

    if (animationFinished && gCustomBodyOverrideOnce) {
        if (gCustomBodyRollPhase == CustomBodyRollPhase::Enter) {
            gCustomBodyRollPhase = nativeRolling ? CustomBodyRollPhase::Rolling : CustomBodyRollPhase::None;
        } else if (gCustomBodyRollPhase == CustomBodyRollPhase::Exit) {
            gCustomBodyRollPhase = CustomBodyRollPhase::None;
        }
        gCustomBodyOverrideAnim.reset();
        gCustomBodyOverrideOnce = false;
    }
}

// A bola, a malha de espinhos e as duas camadas de energia são display lists
// distintas no MM. As energias usam o segmento 0x08 para TwoTexScroll, por
// isso precisam de um passe Xlu separado e nunca podem ser tratadas como uma
// textura comum do skeleton.
extern "C" void CustomBodyDrawRoll(PlayState* play, const char* path, const char* spikesPath,
                                   const char* energy1Path, const char* energy2Path, int charge,
                                   bool machineVisuals) {
    Gfx* dList = reinterpret_cast<Gfx*>(const_cast<char*>(path));
    // NAO ha "kick visual" de parede no MM. Conferido na fonte primaria: em
    // `Player_Action_96` (2S2H z_player.c:19932-19944) o quique muda o yaw,
    // soma `unk_B0C` e toca NA_SE_IT_GORON_ROLLING_REFLECTION — nada mais. A
    // deformacao da esfera vem de `func_808577E0`, que e continua e ligada ao
    // giro, nunca ao impacto.
    //
    // O pulso que existia aqui era invencao nossa e deformava a bola a cada
    // batida. Removido: o pedido e ficar como o MM original do 2S2H.
    const f32 rollSquash = machineVisuals ? ShipLua::MmForm::RollSquash() : 0.0f;
    const f32 squash = std::clamp(rollSquash, -0.7f, 0.3f);
    const f32 scaleY = 1.0f + squash;
    const f32 scaleX = 1.0f - (squash * 0.5f);
    const f32 scaleZ = std::max(scaleX, scaleY);
    const f32 binangToRad = static_cast<f32>(M_PI) / 32768.0f;
    const s16 rollDriftYaw = machineVisuals ? ShipLua::MmForm::RollDriftYaw() : 0;
    const s16 rollSfxCounter = machineVisuals ? ShipLua::MmForm::RollSfxCounter() : 0;
    const f32 driftYaw = rollDriftYaw * binangToRad;
    const f32 driftTilt = rollSfxCounter * binangToRad;

    // Tudo abaixo pertence somente a matriz de desenho: nada aqui escreve de
    // volta no Player, no collider ou na action func.
    Matrix_Push();
    if (rollSfxCounter != 0 && rollDriftYaw != 0) {
        Matrix_RotateY(driftYaw, MTXMODE_APPLY);
        Matrix_RotateX(driftTilt, MTXMODE_APPLY);
        Matrix_RotateY(-driftYaw, MTXMODE_APPLY);
    }
    Matrix_Scale(scaleX, scaleY, scaleZ, MTXMODE_APPLY);

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    Gfx* twoTexScroll = Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x40, 0x40, 1,
                                          play->gameplayFrames * 2, play->gameplayFrames * 2, 0x40, 0x40);
    gSPSegment(POLY_OPA_DISP++, 0x08, reinterpret_cast<uintptr_t>(twoTexScroll));
    const f32 colorLerp =
        machineVisuals ? std::clamp(ShipLua::MmForm::RollColorLerp(), 0.0f, 1.0f) : 0.0f;
    const u8 envR = static_cast<u8>(255.0f - (175.0f * colorLerp));
    const u8 envG = static_cast<u8>(255.0f - (175.0f * colorLerp));
    const u8 envB = static_cast<u8>(255.0f - (55.0f * colorLerp));
    gDPSetEnvColor(POLY_OPA_DISP++, envR, envG, envB, 255);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(dList)) == 1) {
        gsSPPushCD(POLY_OPA_DISP++, dList);
    }
    gSPDisplayList(POLY_OPA_DISP++, dList);
    if (spikesPath != nullptr) {
        Gfx* spikes = reinterpret_cast<Gfx*>(const_cast<char*>(spikesPath));
        if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(spikes)) == 1) {
            gsSPPushCD(POLY_OPA_DISP++, spikes);
        }
        gSPDisplayList(POLY_OPA_DISP++, spikes);
    }

    // `grt_01_model` e `grt_02_model`: começam a aparecer no nível 5 de
    // carga, como no Player_Draw de MM. Após os espinhos, a energia fica
    // compacta e opaca em vez de continuar crescendo indefinidamente.
    if (charge >= 5 && energy1Path != nullptr && energy2Path != nullptr) {
        const bool spikesActive = CustomBodyRollSpikesForDisplay();
        const f32 energyScale = spikesActive ? 0.65f : std::min((charge - 4) * 0.02f, 1.0f);
        const u8 alpha = spikesActive ? 255 : static_cast<u8>(std::min(200.0f, energyScale * 200.0f));
        Gfx* energy1 = reinterpret_cast<Gfx*>(const_cast<char*>(energy1Path));
        Gfx* energy2 = reinterpret_cast<Gfx*>(const_cast<char*>(energy2Path));
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gSPSegment(POLY_XLU_DISP++, 0x08, reinterpret_cast<uintptr_t>(twoTexScroll));
        Matrix_Push();
        Matrix_Scale(1.0f, energyScale, energyScale, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        gDPSetEnvColor(POLY_XLU_DISP++, 155, 0, 0, alpha);
        if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(energy1)) == 1) {
            gsSPPushCD(POLY_XLU_DISP++, energy1);
        }
        gSPDisplayList(POLY_XLU_DISP++, energy1);
        gDPSetEnvColor(POLY_XLU_DISP++, (play->gameplayFrames & 1) == 0 ? 100 : 200, 0, 0, 255);
        if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(energy2)) == 1) {
            gsSPPushCD(POLY_XLU_DISP++, energy2);
        }
        gSPDisplayList(POLY_XLU_DISP++, energy2);
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
    Matrix_Pop();
}

// Os tambores sao desenhados enquanto a FASE do instrumento esta ativa, nao
// enquanto um NOME de animacao esta em curso. Era essa a causa direta de eles
// nunca aparecerem: bastava a selecao cair no fallback de velocidade — que e o
// que acontecia — para o nome deixar de bater e o callback de limb desistir.
bool CustomBodyIsGakkiAnimation() {
    return ShipLua::MmForm::GakkiPhase() != 0;
}

struct CustomBodyGakkiKey {
    f32 frame;
    Vec3f scale;
};

// Tabelas de escala do instrumento, do z_player_lib.c de MM. O formato
// original e struct_80124618 { s16 frame; Vec3s scale; } com escala x0.01;
// aqui ja convertida. Sao as mesmas tres que a referencia usa em
// sGakkiGoronStart/Play/Wait — antes so a primeira existia, e por isso o
// conjunto ficava estatico depois de aberto.

// D_801C0428 — gakkistart: os tambores surgem. Reutilizada na saida, com a
// animacao correndo ao contrario.
static const std::array<CustomBodyGakkiKey, 7> kGakkiGoronStart = {
    CustomBodyGakkiKey{ 0.0f, { 0.0f, 0.0f, 0.0f } },  CustomBodyGakkiKey{ 6.0f, { 0.0f, 0.0f, 0.0f } },
    CustomBodyGakkiKey{ 7.0f, { 0.6f, 0.6f, 0.5f } },  CustomBodyGakkiKey{ 8.0f, { 1.2f, 1.3f, 1.0f } },
    CustomBodyGakkiKey{ 9.0f, { 1.0f, 1.2f, 0.8f } },  CustomBodyGakkiKey{ 11.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 13.0f, { 1.0f, 1.0f, 1.0f } },
};
// D_801C0490 — gakkiplay: a pulsacao ritmica enquanto a nota soa.
static const std::array<CustomBodyGakkiKey, 16> kGakkiGoronPlay = {
    CustomBodyGakkiKey{ 0.0f, { 1.0f, 1.0f, 1.0f } },   CustomBodyGakkiKey{ 1.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 2.0f, { 0.9f, 1.0f, 1.05f } },  CustomBodyGakkiKey{ 4.0f, { 1.1f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 5.0f, { 0.9f, 1.0f, 1.05f } },  CustomBodyGakkiKey{ 6.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 7.0f, { 0.9f, 1.0f, 1.05f } },  CustomBodyGakkiKey{ 8.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 9.0f, { 0.9f, 1.0f, 1.05f } },  CustomBodyGakkiKey{ 10.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 11.0f, { 0.9f, 1.0f, 1.05f } }, CustomBodyGakkiKey{ 12.0f, { 1.1f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 13.0f, { 1.0f, 1.0f, 1.0f } },  CustomBodyGakkiKey{ 14.0f, { 0.9f, 1.0f, 1.05f } },
    CustomBodyGakkiKey{ 15.0f, { 0.9f, 1.0f, 1.05f } }, CustomBodyGakkiKey{ 17.0f, { 1.0f, 1.0f, 1.0f } },
};
// D_801C0510 — repouso entre notas.
static const std::array<CustomBodyGakkiKey, 5> kGakkiGoronWait = {
    CustomBodyGakkiKey{ 0.0f, { 1.0f, 1.0f, 1.0f } },  CustomBodyGakkiKey{ 4.0f, { 1.0f, 1.0f, 1.0f } },
    CustomBodyGakkiKey{ 5.0f, { 0.9f, 1.1f, 1.0f } },  CustomBodyGakkiKey{ 6.0f, { 1.1f, 1.05f, 1.0f } },
    CustomBodyGakkiKey{ 8.0f, { 1.0f, 1.0f, 1.0f } },
};

Vec3f CustomBodyGakkiInterp(const CustomBodyGakkiKey* keys, size_t count, f32 frame) {
    if (keys == nullptr || count == 0) {
        return { 1.0f, 1.0f, 1.0f };
    }
    for (size_t i = 1; i < count; ++i) {
        if (frame <= keys[i].frame) {
            const auto& previous = keys[i - 1];
            const auto& next = keys[i];
            const f32 span = next.frame - previous.frame;
            const f32 t = span <= 0.0f ? 0.0f : std::clamp((frame - previous.frame) / span, 0.0f, 1.0f);
            return {
                previous.scale.x + (next.scale.x - previous.scale.x) * t,
                previous.scale.y + (next.scale.y - previous.scale.y) * t,
                previous.scale.z + (next.scale.z - previous.scale.z) * t,
            };
        }
    }
    return keys[count - 1].scale;
}

// Manter os tres eixos separados preserva o "pop" assimetrico da abertura e faz
// a saida recolher os tambores no ritmo certo.
Vec3f CustomBodyGakkiScale() {
    const f32 frame = gCustomBodySkelAnime.curFrame;
    switch (ShipLua::MmForm::GakkiPhase()) {
        case 1:
        case 3:
            return CustomBodyGakkiInterp(kGakkiGoronStart.data(), kGakkiGoronStart.size(), frame);
        case 2:
            // Tocando uma nota usa a tabela de pulsacao; entre notas, a de repouso.
            return ShipLua::MmForm::GakkiLastNote() == 0xFF
                       ? CustomBodyGakkiInterp(kGakkiGoronWait.data(), kGakkiGoronWait.size(), frame)
                       : CustomBodyGakkiInterp(kGakkiGoronPlay.data(), kGakkiGoronPlay.size(), frame);
        default:
            return { 1.0f, 1.0f, 1.0f };
    }
}

// O efeito de soco é uma DL translúcida própria de MM. A mesma tabela de
// impacto já usada pelo collider decide quando desenhá-la; A usa mão esquerda,
// B mão direita e C a cintura, como o Player_PostLimbDrawGameplay original.
extern "C" void CustomBodyDrawPunchEffect(PlayState* play, s32 limbIndex) {
    if (!gCustomBodyPunchHitActive) {
        return;
    }
    const bool atTargetLimb = (gCustomBodyCurrentAnim == "punch_a" && limbIndex == PLAYER_LIMB_L_HAND) ||
                              (gCustomBodyCurrentAnim == "punch_b" && limbIndex == PLAYER_LIMB_R_HAND) ||
                              (gCustomBodyCurrentAnim == "punch_c" && limbIndex == PLAYER_LIMB_WAIST);
    const auto effect = gCustomBodySpec.modelsPrefixed.find("punch_effect");
    if (!atTargetLimb || effect == gCustomBodySpec.modelsPrefixed.end()) {
        return;
    }

    Gfx* dList = reinterpret_cast<Gfx*>(const_cast<char*>(effect->second.c_str()));
    // Mesma regra dos tambores: recurso que nao resolve nao vai para o
    // renderizador. Emitir a display list com a assinatura reprovada entrega
    // lixo ao pipeline.
    if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(dList)) != 1) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            SPDLOG_WARN("ShipLua corpo: 'punch_effect' nao resolve como recurso; pulando o desenho");
        }
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, 180);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gsSPPushCD(POLY_XLU_DISP++, dList);
    gSPDisplayList(POLY_XLU_DISP++, dList);
    CLOSE_DISPS(play->state.gfxCtx);
}

// O Player de MM anexa o conjunto de tambores no torso do Goron durante as
// animacoes gakki. O custom body usa o mesmo ponto de esqueleto (limb 0x15)
// e paths declarados pela spec; outros corpos nao ativam este callback.
extern "C" void CustomBodyPostLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg) {
    constexpr s32 kGoronTorsoLimb = 0x15;
    CustomBodyDrawPunchEffect(play, limbIndex);
    if (limbIndex != kGoronTorsoLimb || !CustomBodyIsGakkiAnimation()) {
        return;
    }

    static constexpr std::array<const char*, 6> kGakkiModels = {
        "gakki_container", "gakki_piece_1", "gakki_piece_2", "gakki_piece_3", "gakki_piece_4", "gakki_piece_5",
    };
    const Vec3f scale = CustomBodyGakkiScale();
    if (scale.x <= 0.0f && scale.y <= 0.0f && scale.z <= 0.0f) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    for (const char* name : kGakkiModels) {
        const auto model = gCustomBodySpec.modelsPrefixed.find(name);
        if (model == gCustomBodySpec.modelsPrefixed.end()) {
            continue;
        }
        Gfx* dList = reinterpret_cast<Gfx*>(const_cast<char*>(model->second.c_str()));
        // O `gSPDisplayList` era emitido MESMO com a assinatura OTR reprovada —
        // ou seja, com um ponteiro que nao e um recurso valido. Isso entrega
        // lixo ao renderizador; foi o que travou a tela quando a fase do
        // instrumento passou a existir de verdade e estes DLs comecaram a ser
        // desenhados pela primeira vez. Recurso que nao resolve agora e pulado.
        if (ResourceMgr_OTRSigCheck(reinterpret_cast<char*>(dList)) != 1) {
            static std::set<std::string> reported;
            if (reported.insert(model->second).second) {
                SPDLOG_WARN("ShipLua corpo: modelo '{}' nao resolve como recurso; pulando o desenho", name);
            }
            continue;
        }
        Matrix_Push();
        Matrix_Scale(scale.x, scale.y, scale.z, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        gsSPPushCD(POLY_OPA_DISP++, dList);
        gSPDisplayList(POLY_OPA_DISP++, dList);
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void CustomBodyActorDraw(Actor* actor, PlayState* play) {
    if (!gCustomBodyActive) {
        return;
    }
    // A display list enrolada do Goron é geometria direta, não um skeleton.
    // Ela precisa do seu próprio pipeline Xlu, cor ambiente e ModelView.
    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        return;
    }
    // MOVEACTOR roda entre Actor_UpdateAll e Actor_DrawAll. O ator hospedeiro
    // ja recebeu a matriz antiga no inicio de Actor_Draw; refazemos a matriz a
    // partir da posicao atualizada do Player para corpo e colisao nao ficarem um
    // frame defasados durante o root motion.
    actor->world.pos = player->actor.world.pos;
    actor->shape.rot = player->actor.shape.rot;
    Matrix_SetTranslateRotateYXZ(actor->world.pos.x, actor->world.pos.y + (actor->shape.yOffset * actor->scale.y),
                                 actor->world.pos.z, &actor->shape.rot);
    Matrix_Scale(actor->scale.x, actor->scale.y, actor->scale.z, MTXMODE_APPLY);
    // Diagnostico de estado: registra so na MUDANCA, para nao virar 20 linhas
    // por segundo. Distingue de uma vez as tres duvidas em aberto — se a bola
    // enrolada esta sendo desenhada no lugar do esqueleto, se a defesa chegou a
    // ativar, e se a ocarina esta sendo detectada.
    {
        static std::string lastSnapshot;
        char buf[384];
        snprintf(buf, sizeof(buf),
                 "roll=%s mmAction=%s gpound=%d water=%d actRoll=%d action=%d combo=%d rec=%d gakki=%d climb=%d "
                 "ovr=%d ocarina=%d hp=%d damaged=%d vy=%.2f linear=%.2f anim=%s",
                 CustomBodyCurrentRollPhaseName(), ShipLua::MmForm::CurrentActionName(),
                 static_cast<int>(gCustomBodyGroundPoundPhase), static_cast<int>(gCustomBodyWaterVoidPhase),
                 (player != nullptr && player->actionFunc == Player_Action_Roll) ? 1 : 0,
                 static_cast<int>(gCustomBodyAction), static_cast<int>(gCustomBodyComboStep),
                 gCustomBodyPunchRecovering ? 1 : 0, static_cast<int>(ShipLua::MmForm::GakkiPhase()),
                 (player != nullptr && (player->stateFlags1 & PLAYER_STATE1_CLIMBING_LADDER)) ? 1 : 0,
                 gCustomBodyOverrideAnim.has_value() ? 1 : 0,
                 (player != nullptr && (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING)) ? 1 : 0,
                 gSaveContext.health,
                 (player != nullptr && (player->stateFlags1 & PLAYER_STATE1_DAMAGED)) ? 1 : 0,
                 player != nullptr ? player->actor.velocity.y : 0.0f,
                 player != nullptr ? player->linearVelocity : 0.0f,
                 gCustomBodyCurrentAnim.c_str());
        if (lastSnapshot != buf) {
            lastSnapshot = buf;
            SPDLOG_INFO("ShipLua corpo: {}", buf);
            SPDLOG_INFO("ShipLua raiz: joint0=({},{},{}) prev=({},{},{}) base=({},{},{}) playerY={} hostY={}",
                        gCustomBodySkelAnime.jointTable[0].x, gCustomBodySkelAnime.jointTable[0].y,
                        gCustomBodySkelAnime.jointTable[0].z, gCustomBodySkelAnime.prevTransl.x,
                        gCustomBodySkelAnime.prevTransl.y, gCustomBodySkelAnime.prevTransl.z,
                        gCustomBodySkelAnime.baseTransl.x, gCustomBodySkelAnime.baseTransl.y,
                        gCustomBodySkelAnime.baseTransl.z, player->actor.world.pos.y, actor->world.pos.y);
        }
    }
    const auto rollModel = gCustomBodySpec.modelsPrefixed.find("roll");
    const bool waterBall = gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::Ball;
    // Quem responde "a esfera e o corpo agora" e `IsBallBody()` — as tres fases
    // do meio. Nas duas pontas (curl e desenrolar) o esqueleto ainda toca a
    // animacao, e desenhar a bola ali cortaria a transicao.
    const bool ballIsBody =
        MmFormOwnsRoll()
            ? ShipLua::MmForm::IsBallBody()
            : (gCustomBodyRollPhase == CustomBodyRollPhase::Rolling &&
               (player != nullptr && (player->actionFunc == Player_Action_Roll ||
                                      gCustomBodyGroundPoundPhase != CustomBodyGroundPoundPhase::None)));
    if (player != nullptr && (ballIsBody || waterBall) &&
        rollModel != gCustomBodySpec.modelsPrefixed.end()) {
        const auto spikes = gCustomBodySpec.modelsPrefixed.find("roll_spikes");
        const auto energy1 = gCustomBodySpec.modelsPrefixed.find("roll_energy_1");
        const auto energy2 = gCustomBodySpec.modelsPrefixed.find("roll_energy_2");
        const char* spikesPath =
            (!waterBall && CustomBodyRollSpikesForDisplay() && spikes != gCustomBodySpec.modelsPrefixed.end())
                ? spikes->second.c_str()
                : nullptr;
        const char* energy1Path = !waterBall && energy1 != gCustomBodySpec.modelsPrefixed.end()
                                      ? energy1->second.c_str()
                                      : nullptr;
        const char* energy2Path = !waterBall && energy2 != gCustomBodySpec.modelsPrefixed.end()
                                      ? energy2->second.c_str()
                                      : nullptr;
        CustomBodyDrawRoll(play, rollModel->second.c_str(), spikesPath, energy1Path, energy2Path,
                           waterBall ? 0 : CustomBodyRollChargeForDisplay(), MmFormOwnsRoll() && !waterBall);
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    // Setup de render opaco padrão (SETUPDL_25: combiner/render mode/geometry
    // mode). SEM isto, SkelAnime_DrawFlexOpa emite a geometria com o estado de
    // pipeline herdado do ator desenhado logo antes — resultado em jogo:
    // corpo totalmente INVISÍVEL (não é bind pose, não é tamanho errado — não
    // desenha nada). Todo ator vanilla que desenha esqueleto chama isto após
    // OPEN_DISPS (ver z_en_go2.c:2032 e func_80093C80 usado pelo puppet). Foi
    // a divergência exata deste draw em relação aos exemplos que funcionam.
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    // Configure os segmentos antes de decidir entre skeleton e modelo de
    // estado: uma display list alternativa também pode referenciar texturas
    // segmentadas do objeto de Player.
    for (const auto& [segment, path] : gCustomBodySpec.segmentsPrefixed) {
        char* data = ResourceMgr_GetResourceDataByNameHandlingMQ(path.c_str());
        if (data != nullptr) {
            gSPSegment(POLY_OPA_DISP++, segment, reinterpret_cast<uintptr_t>(data));
        }
    }
    if (gCustomBodyShieldActive && gCustomBodyShieldSkelAnime.skeleton != nullptr) {
        SkelAnime_DrawFlexOpa(play, gCustomBodyShieldSkelAnime.skeleton, gCustomBodyShieldSkelAnime.jointTable,
                              gCustomBodyShieldSkelAnime.dListCount, nullptr, nullptr, actor);
    } else {
        SkelAnime_DrawFlexOpa(play, gCustomBodySkelAnime.skeleton, gCustomBodySkelAnime.jointTable,
                              gCustomBodySkelAnime.dListCount, nullptr, CustomBodyPostLimbDraw, actor);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

bool CustomBodyActivateFromSpec(PlayState* play) {
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr) {
        return false;
    }
    // Todos os ponteiros abaixo saem de membros persistentes da spec global —
    // ver o comentário em CustomBodySpec: o engine guarda estes endereços.
    const auto defaultAnimPath = gCustomBodySpec.animsPrefixed.find(gCustomBodySpec.defaultAnim);
    if (gCustomBodySpec.skeletonPathPrefixed.empty() || defaultAnimPath == gCustomBodySpec.animsPrefixed.end()) {
        SPDLOG_WARN("ShipLua set_body: spec incompleta");
        return false;
    }
    if (!ResourceMgr_FileExists(gCustomBodySpec.skeletonPathPrefixed.c_str()) ||
        !ResourceMgr_FileExists(defaultAnimPath->second.c_str())) {
        SPDLOG_WARN("ShipLua set_body: assets indispon\xC3\xADveis para a spec atual");
        return false;
    }
    // Uma spec de corpo e atomica: nao basta o idle existir. Precarregamos e
    // validamos todos os headers declarados para evitar que uma habilidade
    // (por exemplo roll, tambor ou porta) revele uma animacao ausente somente
    // depois de a transformacao ja ter ocultado o Player vanilla.
    for (const auto& [name, path] : gCustomBodySpec.animsPrefixed) {
        if (!ResourceMgr_FileExists(path.c_str()) || ResourceMgr_LoadAnimByName(path.c_str()) == nullptr) {
            SPDLOG_WARN("ShipLua set_body: animacao '{}' indisponivel ('{}')", name, path);
            return false;
        }
    }
    const bool hasShieldSkeleton = !gCustomBodySpec.shieldSkeletonPathPrefixed.empty();
    const bool hasShieldAnimation = !gCustomBodySpec.shieldAnimationPathPrefixed.empty();
    if (hasShieldSkeleton != hasShieldAnimation) {
        SPDLOG_WARN("ShipLua set_body: 'shield' precisa declarar skeleton e animation juntos");
        return false;
    }
    if (hasShieldSkeleton &&
        (!ResourceMgr_FileExists(gCustomBodySpec.shieldSkeletonPathPrefixed.c_str()) ||
         !ResourceMgr_FileExists(gCustomBodySpec.shieldAnimationPathPrefixed.c_str()))) {
        SPDLOG_WARN("ShipLua set_body: assets de 'shield' indispon\xC3\xADveis");
        return false;
    }
    for (const auto& [segment, path] : gCustomBodySpec.segmentsPrefixed) {
        if (!ResourceMgr_FileExists(path.c_str())) {
            SPDLOG_WARN("ShipLua set_body: segmento 0x{:02X} indispon\xC3\xADvel ('{}')", segment, path);
            return false;
        }
    }
    for (const auto& [state, path] : gCustomBodySpec.modelsPrefixed) {
        if (!ResourceMgr_FileExists(path.c_str())) {
            SPDLOG_WARN("ShipLua set_body: modelo do estado '{}' indispon\xC3\xADvel ('{}')", state, path);
            return false;
        }
    }

    CustomBodyFreeSkelBuffers();
    // flags=9: mesmo modo usado pelo próprio Player e pelo puppet do Kafei —
    // buffers do chamador quando não-NULL, mas aqui passamos NULL para os
    // dois: aloca do tamanho real do header do skeleton fornecido pelo mod,
    // não de PLAYER_LIMB_MAX.
    SkelAnime_InitLink(play, &gCustomBodySkelAnime,
                       (FlexSkeletonHeader*)gCustomBodySpec.skeletonPathPrefixed.c_str(),
                       (LinkAnimationHeader*)defaultAnimPath->second.c_str(), 9, nullptr, nullptr, 0);
    // SkelAnime_InitLink nao inicializa baseTransl. O skeleton de MM pertence a
    // mesma familia do Player e usa a base canonica definida em z_player.c.
    // Sem ela, a neutralizacao enfileirada da raiz colocaria a malha em Y=0.
    gCustomBodySkelAnime.baseTransl = { -57, 3377, 0 };
    gCustomBodySkelAnime.prevTransl = gCustomBodySkelAnime.baseTransl;
    gCustomBodyCurrentAnim = gCustomBodySpec.defaultAnim;

    if (hasShieldSkeleton) {
        // O escudo/curl do Goron é uma animação normal de quatro membros,
        // não LinkAnimation. Carregamos o header antes do InitFlex porque
        // Animation_PlayLoop opera sobre os dados, e não sobre o caminho OTR.
        auto* shieldAnimation = ResourceMgr_LoadAnimByName(gCustomBodySpec.shieldAnimationPathPrefixed.c_str());
        if (shieldAnimation == nullptr) {
            SPDLOG_WARN("ShipLua set_body: n\xC3\xA3o carreguei a anima\xC3\xA7\xC3\xA3o de 'shield'");
            CustomBodyFreeSkelBuffers();
            return false;
        }
        SkelAnime_InitFlex(play, &gCustomBodyShieldSkelAnime,
                           (FlexSkeletonHeader*)gCustomBodySpec.shieldSkeletonPathPrefixed.c_str(),
                           reinterpret_cast<AnimationHeader*>(shieldAnimation), nullptr, nullptr, 0);
        // Guardado para reiniciar o agachamento a cada entrada na defesa; sem
        // isso a postura ficava congelada no ultimo frame da defesa anterior.
        gCustomBodyShieldAnimation = reinterpret_cast<AnimationHeader*>(shieldAnimation);
        if (gCustomBodyShieldSkelAnime.jointTable == nullptr || gCustomBodyShieldSkelAnime.morphTable == nullptr) {
            SPDLOG_WARN("ShipLua set_body: sem mem\xC3\xB3ria para a postura de 'shield'");
            CustomBodyFreeSkelBuffers();
            return false;
        }
        // Diagnóstico da postura de defesa: o esqueleto do MM tem 5 membros
        // (LINK_GORON_SHIELDING_LIMB_MAX), a cabeça é o 0x03. Se limbCount vier
        // diferente de 5, ou dListCount menor, faltam membros na geometria; se
        // o jointTable ficar zerado, a animação não chegou a ser aplicada. São
        // causas distintas para o mesmo sintoma visual, e só o valor separa.
        SPDLOG_INFO("ShipLua set_body/shield: limbCount={} dListCount={} animLength={} curFrame={} "
                    "joint0=({},{},{}) joint3=({},{},{})",
                    gCustomBodyShieldSkelAnime.limbCount, gCustomBodyShieldSkelAnime.dListCount,
                    gCustomBodyShieldSkelAnime.animLength, gCustomBodyShieldSkelAnime.curFrame,
                    gCustomBodyShieldSkelAnime.jointTable[0].x, gCustomBodyShieldSkelAnime.jointTable[0].y,
                    gCustomBodyShieldSkelAnime.jointTable[0].z,
                    gCustomBodyShieldSkelAnime.limbCount > 3 ? gCustomBodyShieldSkelAnime.jointTable[3].x : -1,
                    gCustomBodyShieldSkelAnime.limbCount > 3 ? gCustomBodyShieldSkelAnime.jointTable[3].y : -1,
                    gCustomBodyShieldSkelAnime.limbCount > 3 ? gCustomBodyShieldSkelAnime.jointTable[3].z : -1);
    }

    Actor* actor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ITEM00, player->actor.world.pos.x,
                               player->actor.world.pos.y, player->actor.world.pos.z, 0, 0, 0, 0);
    if (actor == nullptr) {
        SPDLOG_WARN("ShipLua set_body: n\xC3\xA3o consegui spawnar o ator hospedeiro");
        CustomBodyFreeSkelBuffers();
        return false;
    }
    actor->update = CustomBodyActorUpdate;
    actor->draw = CustomBodyActorDraw;
    // O ator hospedeiro é um En_Item00: o Init dele (params 0 = rupia verde)
    // já aplicou Actor_SetScale(0.015). Reafirma a escala do Player real
    // (z_player.c:12254, scale = 0.01) — o esqueleto do Goron é da família
    // Player e é desenhado nessa mesma escala de mundo. Sem isto, o corpo
    // sairia em tamanho de rupia.
    Actor_SetScale(actor, 0.01f);
    // Enquanto a ocarina esta na mao, `Actor_UpdateAll` so atualiza atores que
    // tenham ACTOR_FLAG_UPDATE_DURING_OCARINA (z_actor.c:2624). Sem esta flag o
    // ator hospedeiro PARAVA de atualizar assim que o instrumento aparecia: o
    // draw continuava — e por isso o log ainda registrava `ocarina=1` — mas o
    // update nao rodava, entao a acao do instrumento nunca comecava e o corpo
    // congelava na pose em que estivesse. Era a causa real de "o Goron trava na
    // animacao que estiver" e de os tambores nunca surgirem; nem o DL nem o
    // audio nem a deteccao da flag tinham a ver.
    actor->flags |= ACTOR_FLAG_UPDATE_DURING_OCARINA;
    gCustomBodyPlay = play;
    MmFormInstallServices();
    gCustomBodyActor = actor;
    gCustomBodyActive = true;
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
    MaskTransitionBeginReveal(player);
    return true;
}

// ship.oot.player.set_body(spec): spec = { skeleton = "caminho/sem/prefixo",
// anims = { nome = "caminho", ... }, reverse_anims = { nome = true, ... },
// segments = { [8] = "textura", ... },
// models = { roll = "display/list", ... }, shield = { skeleton = "...",
// animation = "..." } (opcional), default_anim = "nome" (opcional, usa
// "idle" ou a 1a chave se omitido), water_void = true (opcional) }. spec =
// nil desliga e esquece a forma.
int LuaSetPlayerBody(lua_State* state) {
    PlayState* play = gPlayState;
    if (lua_isnil(state, 1)) {
        Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
        CustomBodyDeactivate(play, true);
        // A saida mask_off chega aqui no pico do flash; o desenho nativo de
        // Link volta antes do fade, portanto a troca tambem fica invisivel.
        MaskTransitionBeginReveal(player);
        SPDLOG_INFO("ShipLua set_body: corpo customizado desligado");
        lua_pushboolean(state, 1);
        return 1;
    }
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr) {
        SPDLOG_WARN("ShipLua set_body: fora de gameplay");
        lua_pushboolean(state, 0);
        return 1;
    }
    luaL_checktype(state, 1, LUA_TTABLE);

    lua_getfield(state, 1, "skeleton");
    const char* skeleton = luaL_optstring(state, -1, nullptr);
    if (skeleton == nullptr || *skeleton == '\0') {
        SPDLOG_WARN("ShipLua set_body: 'skeleton' \xC3\xA9 obrigat\xC3\xB3rio");
        lua_pop(state, 1);
        lua_pushboolean(state, 0);
        return 1;
    }
    CustomBodySpec spec;
    spec.skeletonPath = skeleton;
    lua_pop(state, 1);

    lua_getfield(state, 1, "anims");
    if (!lua_istable(state, -1)) {
        SPDLOG_WARN("ShipLua set_body: 'anims' \xC3\xA9 obrigat\xC3\xB3ria (tabela nome->caminho)");
        lua_pop(state, 1);
        lua_pushboolean(state, 0);
        return 1;
    }
    lua_pushnil(state);
    while (lua_next(state, -2) != 0) {
        if (lua_type(state, -2) == LUA_TSTRING && lua_type(state, -1) == LUA_TSTRING) {
            spec.anims[lua_tostring(state, -2)] = lua_tostring(state, -1);
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 1); // anims

    if (spec.anims.empty()) {
        SPDLOG_WARN("ShipLua set_body: 'anims' n\xC3\xA3o tem nenhuma entrada v\xC3\xA1lida");
        lua_pushboolean(state, 0);
        return 1;
    }

    lua_getfield(state, 1, "reverse_anims");
    if (!lua_isnil(state, -1) && !lua_istable(state, -1)) {
        SPDLOG_WARN("ShipLua set_body: 'reverse_anims' precisa ser tabela nome->boolean");
        lua_pop(state, 1);
        lua_pushboolean(state, 0);
        return 1;
    }
    if (lua_istable(state, -1)) {
        lua_pushnil(state);
        while (lua_next(state, -2) != 0) {
            const bool valid = lua_type(state, -2) == LUA_TSTRING && lua_isboolean(state, -1);
            const char* name = valid ? lua_tostring(state, -2) : nullptr;
            const bool enabled = valid && lua_toboolean(state, -1) != 0;
            if (!valid || (enabled && spec.anims.find(name) == spec.anims.end())) {
                SPDLOG_WARN("ShipLua set_body: 'reverse_anims' aceita apenas chaves de 'anims' com valor boolean");
                lua_pop(state, 2);
                lua_pushboolean(state, 0);
                return 1;
            }
            if (enabled) {
                spec.reverseAnims.insert(name);
            }
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1); // reverse_anims

    lua_getfield(state, 1, "segments");
    if (!lua_isnil(state, -1) && !lua_istable(state, -1)) {
        SPDLOG_WARN("ShipLua set_body: 'segments' precisa ser tabela índice->caminho");
        lua_pop(state, 1);
        lua_pushboolean(state, 0);
        return 1;
    }
    if (lua_istable(state, -1)) {
        lua_pushnil(state);
        while (lua_next(state, -2) != 0) {
            const bool valid = lua_isinteger(state, -2) && lua_type(state, -1) == LUA_TSTRING;
            const int segment = valid ? static_cast<int>(lua_tointeger(state, -2)) : 0;
            if (!valid || segment < 1 || segment > 15) {
                SPDLOG_WARN("ShipLua set_body: 'segments' aceita apenas índices 1..15 e caminhos string");
                lua_pop(state, 2);
                lua_pushboolean(state, 0);
                return 1;
            }
            spec.segments[segment] = lua_tostring(state, -1);
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1); // segments

    lua_getfield(state, 1, "models");
    if (!lua_isnil(state, -1) && !lua_istable(state, -1)) {
        SPDLOG_WARN("ShipLua set_body: 'models' precisa ser tabela estado->caminho");
        lua_pop(state, 1);
        lua_pushboolean(state, 0);
        return 1;
    }
    if (lua_istable(state, -1)) {
        lua_pushnil(state);
        while (lua_next(state, -2) != 0) {
            if (lua_type(state, -2) != LUA_TSTRING || lua_type(state, -1) != LUA_TSTRING) {
                SPDLOG_WARN("ShipLua set_body: 'models' aceita apenas estado string -> caminho string");
                lua_pop(state, 2);
                lua_pushboolean(state, 0);
                return 1;
            }
            spec.models[lua_tostring(state, -2)] = lua_tostring(state, -1);
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1); // models

    lua_getfield(state, 1, "shield");
    if (!lua_isnil(state, -1) && !lua_istable(state, -1)) {
        SPDLOG_WARN("ShipLua set_body: 'shield' precisa ser tabela { skeleton, animation }");
        lua_pop(state, 1);
        lua_pushboolean(state, 0);
        return 1;
    }
    if (lua_istable(state, -1)) {
        lua_getfield(state, -1, "skeleton");
        const char* shieldSkeleton = luaL_optstring(state, -1, nullptr);
        if (shieldSkeleton == nullptr || *shieldSkeleton == '\0') {
            SPDLOG_WARN("ShipLua set_body: 'shield.skeleton' \xC3\xA9 obrigat\xC3\xB3rio");
            lua_pop(state, 2);
            lua_pushboolean(state, 0);
            return 1;
        }
        spec.shieldSkeletonPath = shieldSkeleton;
        lua_pop(state, 1);

        lua_getfield(state, -1, "animation");
        const char* shieldAnimation = luaL_optstring(state, -1, nullptr);
        if (shieldAnimation == nullptr || *shieldAnimation == '\0') {
            SPDLOG_WARN("ShipLua set_body: 'shield.animation' \xC3\xA9 obrigat\xC3\xB3rio");
            lua_pop(state, 2);
            lua_pushboolean(state, 0);
            return 1;
        }
        spec.shieldAnimationPath = shieldAnimation;
        lua_pop(state, 1);
    }
    lua_pop(state, 1); // shield

    lua_getfield(state, 1, "ground_offset");
    if (!lua_isnil(state, -1)) {
        if (!lua_isnumber(state, -1)) {
            SPDLOG_WARN("ShipLua set_body: 'ground_offset' precisa ser n\xC3\xBAmero");
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
        spec.groundOffset = static_cast<f32>(lua_tonumber(state, -1));
        if (!std::isfinite(spec.groundOffset) || fabsf(spec.groundOffset) > 10000.0f) {
            SPDLOG_WARN("ShipLua set_body: 'ground_offset' fora da faixa segura");
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, 1, "roll_offset");
    if (!lua_isnil(state, -1)) {
        if (!lua_isnumber(state, -1)) {
            SPDLOG_WARN("ShipLua set_body: 'roll_offset' precisa ser n\xC3\xBAmero");
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
        spec.rollOffset = static_cast<f32>(lua_tonumber(state, -1));
        if (!std::isfinite(spec.rollOffset) || fabsf(spec.rollOffset) > 10000.0f) {
            SPDLOG_WARN("ShipLua set_body: 'roll_offset' fora da faixa segura");
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, 1, "water_void");
    if (!lua_isnil(state, -1)) {
        if (!lua_isboolean(state, -1)) {
            SPDLOG_WARN("ShipLua set_body: 'water_void' precisa ser booleano");
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
        spec.waterVoid = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);

    lua_getfield(state, 1, "block_ledge_grab");
    if (!lua_isnil(state, -1)) {
        if (!lua_isboolean(state, -1)) {
            SPDLOG_WARN("ShipLua set_body: 'block_ledge_grab' precisa ser booleano");
            lua_pop(state, 1);
            lua_pushboolean(state, 0);
            return 1;
        }
        spec.blockLedgeGrab = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);

    lua_getfield(state, 1, "default_anim");
    const char* defaultAnim = luaL_optstring(state, -1, nullptr);
    if (defaultAnim != nullptr && spec.anims.find(defaultAnim) != spec.anims.end()) {
        spec.defaultAnim = defaultAnim;
    } else if (spec.anims.find("idle") != spec.anims.end()) {
        spec.defaultAnim = "idle";
    } else {
        spec.defaultAnim = spec.anims.begin()->first;
    }
    lua_pop(state, 1);

    // Monta os espelhos com "__OTR__" UMA vez, aqui: são estes que fornecem os
    // ponteiros ao engine, e precisam viver enquanto o corpo estiver ativo.
    spec.skeletonPathPrefixed = std::string("__OTR__") + spec.skeletonPath;
    for (const auto& [animName, animPath] : spec.anims) {
        spec.animsPrefixed[animName] = std::string("__OTR__") + animPath;
    }
    for (const auto& [segment, path] : spec.segments) {
        spec.segmentsPrefixed[segment] = std::string("__OTR__") + path;
    }
    for (const auto& [stateName, path] : spec.models) {
        spec.modelsPrefixed[stateName] = std::string("__OTR__") + path;
    }
    if (!spec.shieldSkeletonPath.empty()) {
        spec.shieldSkeletonPathPrefixed = std::string("__OTR__") + spec.shieldSkeletonPath;
        spec.shieldAnimationPathPrefixed = std::string("__OTR__") + spec.shieldAnimationPath;
    }

    CustomBodyDeactivate(play, false); // desliga o corpo antigo, mas a spec só troca depois
    gCustomBodySpec = spec;
    if (!CustomBodyActivateFromSpec(play)) {
        gCustomBodySpec = CustomBodySpec{};
        lua_pushboolean(state, 0);
        return 1;
    }
    SPDLOG_INFO("ShipLua set_body: corpo customizado ligado ('{}')", spec.skeletonPath);
    lua_pushboolean(state, 1);
    return 1;
}

// ship.oot.player.get_body(): retorna a spec atual como tabela, ou nil se
// nenhum corpo customizado está ativo. Permite a um mod checar/coexistir com
// outro que já tenha trocado o corpo.
int LuaGetPlayerBody(lua_State* state) {
    if (!gCustomBodyActive) {
        lua_pushnil(state);
        return 1;
    }
    lua_newtable(state);
    lua_pushstring(state, gCustomBodySpec.skeletonPath.c_str());
    lua_setfield(state, -2, "skeleton");
    lua_pushstring(state, gCustomBodySpec.defaultAnim.c_str());
    lua_setfield(state, -2, "default_anim");
    lua_pushstring(state, gCustomBodyCurrentAnim.c_str());
    lua_setfield(state, -2, "current_anim");
    lua_newtable(state);
    for (const auto& [segment, path] : gCustomBodySpec.segments) {
        lua_pushstring(state, path.c_str());
        lua_rawseti(state, -2, segment);
    }
    lua_setfield(state, -2, "segments");
    lua_newtable(state);
    for (const auto& [name, path] : gCustomBodySpec.models) {
        lua_pushstring(state, path.c_str());
        lua_setfield(state, -2, name.c_str());
    }
    lua_setfield(state, -2, "models");
    lua_newtable(state);
    for (const auto& name : gCustomBodySpec.reverseAnims) {
        lua_pushboolean(state, 1);
        lua_setfield(state, -2, name.c_str());
    }
    lua_setfield(state, -2, "reverse_anims");
    lua_pushnumber(state, gCustomBodySpec.groundOffset);
    lua_setfield(state, -2, "ground_offset");
    lua_pushnumber(state, gCustomBodySpec.rollOffset);
    lua_setfield(state, -2, "roll_offset");
    lua_pushboolean(state, gCustomBodySpec.waterVoid);
    lua_setfield(state, -2, "water_void");
    lua_pushboolean(state, gCustomBodySpec.blockLedgeGrab);
    lua_setfield(state, -2, "block_ledge_grab");
    lua_pushboolean(state, gCustomBodyWaterVoidPhase != CustomBodyWaterVoidPhase::None);
    lua_setfield(state, -2, "water_void_active");
    lua_pushstring(state, CustomBodyWaterVoidPhaseName());
    lua_setfield(state, -2, "water_void_phase");
    lua_pushinteger(state, gCustomBodyWaterVoidTimer);
    lua_setfield(state, -2, "water_void_frames");
    // Todo o bloco de rolamento le pela fonte que estiver no comando — o mod ve
    // o mesmo vocabulario com a maquina portada ou com o caminho antigo.
    const bool ownsRollForState = MmFormOwnsRoll();
    lua_pushinteger(state, CustomBodyRollChargeForDisplay());
    lua_setfield(state, -2, "roll_charge");
    lua_pushstring(state, CustomBodyCurrentRollPhaseName());
    lua_setfield(state, -2, "roll_phase");
    const bool ownsPunchForState = MmFormOwnsPunch();
    lua_pushboolean(state, ownsPunchForState ? ShipLua::MmForm::PunchHitActive() : gCustomBodyPunchHitActive);
    lua_setfield(state, -2, "punch_hit_active");
    // A maquina nao expoe o B agendado: ela consome a borda de pressao dentro do
    // proprio frame do golpe, sem janela observavel de fora. Com ela no comando
    // o campo fica false em vez de mentir um valor do caminho antigo.
    lua_pushboolean(state, !ownsPunchForState && gCustomBodyPunchQueued);
    lua_setfield(state, -2, "punch_combo_queued");
    lua_pushboolean(state,
                    ownsRollForState ? ShipLua::MmForm::RollAttackActive() : gCustomBodyRollColliderArmed);
    lua_setfield(state, -2, "roll_attack_active");
    lua_pushboolean(state, ownsRollForState
                               ? (ShipLua::MmForm::IsBallBody() && ShipLua::MmForm::RollChargeLevel() >= 5)
                               : (gCustomBodyRollPhase == CustomBodyRollPhase::Rolling &&
                                  gCustomBodyRollCharge >= 5));
    lua_setfield(state, -2, "roll_energy_active");
    lua_pushboolean(state, CustomBodyRollSpikesForDisplay());
    lua_setfield(state, -2, "roll_spikes_active");
    // A maquina nao expoe o contador de quique: ela quica dentro da propria
    // fisica, sem janela observavel de fora. Com ela no comando os dois campos
    // ficam zerados em vez de mentir um valor do caminho antigo.
    lua_pushboolean(state, !ownsRollForState && gCustomBodyRollWallBounceTimer > 0);
    lua_setfield(state, -2, "roll_bounce_active");
    lua_pushinteger(state, ownsRollForState ? 0 : gCustomBodyRollWallBounceTimer);
    lua_setfield(state, -2, "roll_bounce_frames");
    lua_pushboolean(state, ownsRollForState ? ShipLua::MmForm::IsGroundPound()
                                            : (gCustomBodyGroundPoundPhase != CustomBodyGroundPoundPhase::None));
    lua_setfield(state, -2, "ground_pound_active");
    lua_pushboolean(state, gCustomBodyShieldActive);
    lua_setfield(state, -2, "shield_active");
    if (!gCustomBodySpec.shieldSkeletonPath.empty()) {
        lua_newtable(state);
        lua_pushstring(state, gCustomBodySpec.shieldSkeletonPath.c_str());
        lua_setfield(state, -2, "skeleton");
        lua_pushstring(state, gCustomBodySpec.shieldAnimationPath.c_str());
        lua_setfield(state, -2, "animation");
        lua_setfield(state, -2, "shield");
    }
    lua_newtable(state);
    for (const auto& [name, path] : gCustomBodySpec.anims) {
        lua_pushstring(state, path.c_str());
        lua_setfield(state, -2, name.c_str());
    }
    lua_setfield(state, -2, "anims");
    return 1;
}

// Toca uma animação nomeada declarada na spec atual. Em modo "once", a
// seleção automática volta a assumir no frame seguinte ao término.
int LuaPlayPlayerBodyAnimation(lua_State* state) {
    PlayState* play = gPlayState;
    if (!gCustomBodyActive || play == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* name = luaL_checkstring(state, 1);
    const auto path = gCustomBodySpec.animsPrefixed.find(name);
    if (path == gCustomBodySpec.animsPrefixed.end()) {
        SPDLOG_WARN("ShipLua play_body_animation: anima\xC3\xA7\xC3\xA3o desconhecida '{}'", name);
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* mode = luaL_optstring(state, 2, "once");
    const bool once = strcmp(mode, "once") == 0;
    const bool loop = strcmp(mode, "loop") == 0;
    const bool reverseOnce = strcmp(mode, "reverse_once") == 0;
    if (!once && !loop && !reverseOnce) {
        SPDLOG_WARN("ShipLua play_body_animation: modo precisa ser 'once', 'loop' ou 'reverse_once'");
        lua_pushboolean(state, 0);
        return 1;
    }
    const double requestedSpeed = luaL_optnumber(state, 3, 1.0);
    if (!std::isfinite(requestedSpeed) || requestedSpeed <= 0.0 || requestedSpeed > 4.0) {
        SPDLOG_WARN("ShipLua play_body_animation: velocidade fora da faixa segura");
        lua_pushboolean(state, 0);
        return 1;
    }
    auto* loaded = ResourceMgr_LoadAnimByName(path->second.c_str());
    if (loaded == nullptr) {
        SPDLOG_WARN("ShipLua play_body_animation: n\xC3\xA3o carreguei '{}'", name);
        lua_pushboolean(state, 0);
        return 1;
    }
    const f32 lastFrame = Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(loaded));
    LinkAnimation_Change(play, &gCustomBodySkelAnime,
                          reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(path->second.c_str())),
                          reverseOnce ? -static_cast<f32>(requestedSpeed) : static_cast<f32>(requestedSpeed),
                          reverseOnce ? lastFrame : 0.0f, reverseOnce ? 0.0f : lastFrame,
                          loop ? ANIMMODE_LOOP : ANIMMODE_ONCE, 0.0f);
    if (once && std::strcmp(name, "mask_off") == 0) {
        Player* player = GET_PLAYER(play);
        if (player != nullptr) {
            MaskTransitionStartRemoval(player, static_cast<int>(ceilf(lastFrame)) + 1);
        }
    }
    if (std::strcmp(name, "punch_a") == 0 || std::strcmp(name, "punch_b") == 0 || std::strcmp(name, "punch_c") == 0) {
        gCustomBodySkelAnime.movementFlags |= ANIM_FLAG_NOMOVE;
    }
    gCustomBodyCurrentAnim = name;
    gCustomBodyOverrideAnim = name;
    gCustomBodyOverrideOnce = once || reverseOnce;
    lua_pushboolean(state, 1);
    return 1;
}

// Troca, em tempo de execução, um segmento que a display list lê (por exemplo
// 0x08 dos olhos do Goron). O caminho fica armazenado na spec persistente.
int LuaSetPlayerBodySegment(lua_State* state) {
    if (!gCustomBodyActive) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const int segment = static_cast<int>(luaL_checkinteger(state, 1));
    const char* path = luaL_checkstring(state, 2);
    if (segment < 1 || segment > 15 || *path == '\0') {
        SPDLOG_WARN("ShipLua set_body_segment: segmento precisa estar entre 1 e 15");
        lua_pushboolean(state, 0);
        return 1;
    }
    const std::string prefixed = std::string("__OTR__") + path;
    if (!ResourceMgr_FileExists(prefixed.c_str())) {
        SPDLOG_WARN("ShipLua set_body_segment: resource indispon\xC3\xADvel '{}'", path);
        lua_pushboolean(state, 0);
        return 1;
    }
    gCustomBodySpec.segments[segment] = path;
    gCustomBodySpec.segmentsPrefixed[segment] = prefixed;
    lua_pushboolean(state, 1);
    return 1;
}

// ship.oot.player.set_mask(name): veste qualquer máscara do OoT pelo nome
// lógico, mantendo-a equipada sem ocupar um botão C. "none" remove.
struct MaskEntry {
    const char* name;
    std::uint8_t mask;
};

constexpr std::array<MaskEntry, 8> kOotMasks = { {
    { "keaton", PLAYER_MASK_KEATON },
    { "skull", PLAYER_MASK_SKULL },
    { "spooky", PLAYER_MASK_SPOOKY },
    { "bunny_hood", PLAYER_MASK_BUNNY },
    { "goron", PLAYER_MASK_GORON },
    { "zora", PLAYER_MASK_ZORA },
    { "gerudo", PLAYER_MASK_GERUDO },
    { "truth", PLAYER_MASK_TRUTH },
} };

bool gMaskForced = false;
int gMaskPreviousPersistent = 0;

void MaskTransitionClearForcedMask(Player* player) {
    if (player != nullptr) {
        player->currentMask = PLAYER_MASK_NONE;
    }
    gSaveContext.ship.maskMemory = PLAYER_MASK_NONE;
    if (gMaskForced) {
        CVarSetInteger(CVAR_ENHANCEMENT("PersistentMasks"), gMaskPreviousPersistent);
        gMaskForced = false;
    }
}

int LuaSetMask(lua_State* state) {
    const char* requested = luaL_optstring(state, 1, "none");
    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr || (player->stateFlags1 & PLAYER_STATE1_DEAD) != 0) {
        SPDLOG_WARN("ShipLua set_mask: fora de gameplay");
        lua_pushboolean(state, 0);
        return 1;
    }

    if (std::strcmp(requested, "none") == 0) {
        // Durante a entrada, remover a máscara é um cancelamento explícito.
        // Durante a saída, set_body(nil) já promoveu Conceal -> Reveal e o
        // mod limpa a máscara no mesmo callback; preservar o fade evita que
        // Link apareça antes do último frame branco.
        if (gMaskTransitionPhase == MaskTransitionPhase::Conceal) {
            MaskTransitionReset(player);
        }
        player->currentMask = PLAYER_MASK_NONE;
        gSaveContext.ship.maskMemory = PLAYER_MASK_NONE;
        if (gMaskForced) {
            CVarSetInteger(CVAR_ENHANCEMENT("PersistentMasks"), gMaskPreviousPersistent);
            gMaskForced = false;
        }
        SPDLOG_INFO("ShipLua set_mask: máscara removida");
        lua_pushboolean(state, 1);
        return 1;
    }

    const auto found = std::find_if(kOotMasks.begin(), kOotMasks.end(), [requested](const MaskEntry& entry) {
        return std::strcmp(entry.name, requested) == 0;
    });
    if (found == kOotMasks.end()) {
        SPDLOG_WARN("ShipLua set_mask: máscara desconhecida '{}'", requested);
        lua_pushboolean(state, 0);
        return 1;
    }

    if (!gMaskForced) {
        gMaskPreviousPersistent = CVarGetInteger(CVAR_ENHANCEMENT("PersistentMasks"), 0);
        gMaskForced = true;
    }
    // Sem PersistentMasks o jogo zera currentMask no frame seguinte quando a
    // máscara não está num botão C.
    CVarSetInteger(CVAR_ENHANCEMENT("PersistentMasks"), 1);
    player->currentMask = found->mask;
    gSaveContext.ship.maskMemory = found->mask;
    SPDLOG_INFO("ShipLua set_mask: máscara '{}' equipada", requested);
    lua_pushboolean(state, 1);
    return 1;
}

// A entrada de uma transformação de MM começa no Player humano com
// gPlayerAnim_cl_setmask. Ao contrário do corte completo de MM, esta
// primitive não toma câmera, cutscene ou física: ela só toca a animação
// segura, em repouso, e devolve a duração para o mod sincronizar a troca de
// corpo. O caminho é estático porque LinkAnimation_Change o consulta em
// frames posteriores.
int LuaPlayMaskOnAnimation(lua_State* state) {
    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    const u32 blockedFlags =
        PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM |
        PLAYER_STATE1_TALKING | PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_IN_WATER;
    const bool ocarinaActive = player != nullptr && (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) != 0;
    const bool ownedAction = gCustomBodyAction != CustomBodyAction::None ||
                             gCustomBodyWaterVoidPhase != CustomBodyWaterVoidPhase::None ||
                             gCustomBodyGroundPoundPhase != CustomBodyGroundPoundPhase::None ||
                             ShipLua::MmForm::WantsPlayerActionPaused();
    if (player == nullptr || gCustomBodyActive || gCutsceneActive || ownedAction || ocarinaActive ||
        (player->stateFlags1 & blockedFlags) != 0 || player->csAction != 0 || play->msgCtx.msgMode != MSGMODE_NONE ||
        player->actionFunc != Player_Action_Idle || (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) == 0 ||
        fabsf(player->linearVelocity) >= 0.5f) {
        SPDLOG_WARN("ShipLua play_mask_on_animation: entrada recusada (repouso/camera/dialogo/agua/acao)");
        lua_pushinteger(state, 0);
        return 1;
    }
    auto* loaded = ResourceMgr_LoadAnimByName(kMaskOnAnimationPath);
    auto* endLoaded = ResourceMgr_LoadAnimByName(kMaskOnEndAnimationPath);
    // A contorcao entra na cena no frame 66, muito depois deste ponto. Validar
    // aqui evita descobrir a falta dela no meio da cutscene, quando ja nao ha o
    // que fazer alem de degradar em silencio.
    auto* hensinLoaded = ResourceMgr_LoadAnimByName(kMaskHensinAnimationPath);
    if (loaded == nullptr || endLoaded == nullptr ||
        !ResourceMgr_FileExists("mm/objects/gameplay_keep/gGoronMaskDL")) {
        SPDLOG_WARN("ShipLua play_mask_on_animation: animacoes ou mascara expressiva do MM indisponiveis");
        lua_pushinteger(state, 0);
        return 1;
    }
    if (hensinLoaded == nullptr) {
        // Nao recusa a entrada: a cena ainda funciona sem a contorcao, so fica
        // com a pose curta. Mas o motivo tem de aparecer no log.
        SPDLOG_WARN("ShipLua play_mask_on_animation: al_hensin ausente do mm.o2r; a transformacao vai usar a pose "
                    "curta de cl_setmaskend");
    }
    const f32 lastFrame = Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(loaded));
    LinkAnimation_Change(play, &player->skelAnime,
                          reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(kMaskOnAnimationPath)), 1.0f, 0.0f,
                          lastFrame, ANIMMODE_ONCE, 0.0f);
    const int transitionFrames = MaskTransitionStart(player, play);
    lua_pushinteger(state, static_cast<lua_Integer>(transitionFrames));
    return 1;
}

// PASSO 2 — ship.oot.player.attach_model(slot, path).
// Desenha uma display list ARBITRÁRIA no player, endereçada por caminho de
// resource — inclusive assets de mod ("mod/<id>/...") e do jogo vizinho
// ("mm/..."). É o que permite conteúdo novo (uma máscara que não existe em
// nenhum dos dois jogos) sem precisar de um slot no enum do engine.
//
// Hoje o slot suportado é "head": o host força uma máscara-veículo para que o
// bloco de desenho de máscara execute e substitui a DL pela do mod.
std::string gAttachedHeadModel;
HOOK_ID gMaskDrawHook = 0;
HOOK_ID gFireImmunityHook = 0;
HOOK_ID gRollStartHook = 0;
HOOK_ID gRollChainHook = 0;
HOOK_ID gRollSteerHook = 0;

// ---------------------------------------------------------------------------
// Ponte de hooks — v1 curada. Os ~476 pontos VB_*/On* do GameInteractor já
// existiam; nenhum mod alcançava nenhum deles. Em vez de escrever uma função
// nativa por habilidade (o padrão de toda a noite), cada hook curado aqui só
// republica seus argumentos como ship.events.on("hook.oot....", fn) — a
// escolha de comportamento fica inteira no Lua. Ver ship.hooks.result() na
// lib compartilhada para o caminho de volta (gate/modify).
HOOK_ID gHookRunSpeedHook = 0;
HOOK_ID gHookFallDamageHook = 0;
HOOK_ID gHookItemReceiveHook = 0;
HOOK_ID gHookHealthChangeHook = 0;
HOOK_ID gHookBonkHook = 0;
HOOK_ID gHeldItemDrawHook = 0;
HOOK_ID gMaskTransitionUpdateHook = 0;
HOOK_ID gHookFirstPersonHook = 0;
HOOK_ID gHookArrowTypeSelectHook = 0;
HOOK_ID gHookEnemyDefeatHook = 0;

// Dispara um evento "should"/"modify": devolve o EventValue que um callback
// Lua gravou via ship.hooks.result(), se algum gravou.
std::optional<ShipLua::EventValue> DispatchHookTransform(const char* name, ShipLua::EventPayload payload) {
    if (gModHost == nullptr) {
        return std::nullopt;
    }
    const auto outcome = gModHost->DispatchEvent(name, payload);
    if (!outcome.isOk()) {
        return std::nullopt;
    }
    const auto found = payload.find("__hook_result");
    if (found == payload.end()) {
        return std::nullopt;
    }
    return found->second;
}

void DispatchDirectionalInput(PlayState* play) {
    if (play == nullptr || std::strcmp(CurrentGameStateMode(), "gameplay") != 0) {
        return;
    }
    struct Direction {
        u16 mask;
        const char* action;
    };
    constexpr std::array<Direction, 8> kDirections = {{
        { BTN_DUP, "dpad_up" },
        { BTN_DDOWN, "dpad_down" },
        { BTN_DLEFT, "dpad_left" },
        { BTN_DRIGHT, "dpad_right" },
        { BTN_CUP, "c_up" },
        { BTN_CDOWN, "c_down" },
        { BTN_CLEFT, "c_left" },
        { BTN_CRIGHT, "c_right" },
    }};

    Input& input = play->state.input[0];
    for (const auto& direction : kDirections) {
        if (!CHECK_BTN_ALL(input.press.button, direction.mask)) {
            continue;
        }
        const auto accepted = DispatchHookTransform(
            "input.action",
            ShipLua::EventPayload{
                { "action", direction.action },
                { "pressed", true },
                { "source", "controller" },
            });
        if (accepted.has_value() && std::holds_alternative<bool>(accepted->value) &&
            std::get<bool>(accepted->value)) {
            input.press.button &= static_cast<u16>(~direction.mask);
        }
    }
}

// Dispara um evento puro de notificação — nenhum retorno é lido.
void DispatchHookEvent(const char* name, ShipLua::EventPayload payload) {
    if (gModHost == nullptr) {
        return;
    }
    gModHost->DispatchEvent(name, payload);
}

int LuaAttachModel(lua_State* state) {
    const char* slot = luaL_checkstring(state, 1);
    const char* path = luaL_optstring(state, 2, nullptr);
    if (std::strcmp(slot, "head") != 0) {
        SPDLOG_WARN("ShipLua attach_model: slot '{}' n\xC3\xA3o suportado (use 'head')", slot);
        lua_pushboolean(state, 0);
        return 1;
    }

    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr) {
        SPDLOG_WARN("ShipLua attach_model: fora de gameplay");
        lua_pushboolean(state, 0);
        return 1;
    }

    if (path == nullptr || *path == '\0') {
        gAttachedHeadModel.clear();
        player->currentMask = PLAYER_MASK_NONE;
        gSaveContext.ship.maskMemory = PLAYER_MASK_NONE;
        SPDLOG_INFO("ShipLua attach_model: modelo removido do slot 'head'");
        lua_pushboolean(state, 1);
        return 1;
    }

    if (!ResourceMgr_FileExists(path)) {
        SPDLOG_WARN("ShipLua attach_model: asset '{}' n\xC3\xA3o encontrado", path);
        lua_pushboolean(state, 0);
        return 1;
    }

    // O interpretador gráfico só trata um ponteiro como caminho de recurso
    // quando ele começa com "__OTR__". Sem o prefixo, ele executa os bytes da
    // string como comandos de display list — e o jogo morre em opcodes ASCII.
    gAttachedHeadModel = std::string("__OTR__") + path;
    // Máscara-veículo: o engine só entra no bloco de desenho quando há uma
    // máscara equipada. A DL dela é substituída pela do mod no hook.
    CVarSetInteger(CVAR_ENHANCEMENT("PersistentMasks"), 1);
    player->currentMask = PLAYER_MASK_KEATON;
    gSaveContext.ship.maskMemory = PLAYER_MASK_KEATON;
    SPDLOG_INFO("ShipLua attach_model: '{}' anexado ao slot 'head'", path);
    lua_pushboolean(state, 1);
    return 1;
}

// ship.oot.player.set_held_item_model(slot, path): desenha um DL arbitrário
// ancorado no osso da mão do player — o mecanismo que praticamente todo item
// customizado com modelo na mão precisa (achado minerando dois forks de
// sistemas de item independentes: Ball and Chain, Gust Jar, Whip, Dominion
// Rod, Cane of Somaria, os cajados... 10+ itens no relatório de um deles
// dependiam exatamente disto). bodyPartsPos já vem resolvido pelo próprio
// desenho do esqueleto do player no mesmo frame — só ler e desenhar.
// ---------------------------------------------------------------------------
// HUD: primitiva genérica de desenho em overlay.
//
// Um mod que precise mostrar qualquer medidor próprio (fome/sede/stamina de um
// sistema de sobrevivência, barra de XP de um RPG, cronômetro) não tinha como
// desenhar nada — a única saída era sequestrar os medidores do jogo. Aqui o
// host só oferece "desenhe um retângulo colorido" e "escreva um texto"; o que
// isso significa é decisão inteira do mod.
//
// Ancorado no evento hook.oot.hud.draw, disparado de OnPlayDrawEnd, antes de
// Play_DrawOverlayElements (pausa, Interface_Draw e textbox). O overlay e o HUD
// nativo vão para o mesmo OVERLAY_DISP na ordem em que são emitidos, então onde
// os dois se cruzam o HUD nativo sai por cima. Por isso o fundo do D-pad tomado
// por provider é desenhado aqui, antes dos callbacks.
//
// gMagicMeterFillTex é a textura de preenchimento sólido que o próprio medidor
// de magia usa (z_parameter.c, Interface_DrawMagicBar) — reaproveitá-la evita
// trocar o cycle type para G_CYC_FILL, que exigiria restaurar estado e poderia
// corromper o desenho seguinte.
// OPEN_DISPS/CLOSE_DISPS expandem para estas funções. frame_interpolation.h as
// declara FORA do seu bloco extern "C" (que está vazio), então em C++ elas
// ganhariam linkage C++ — mas a definição, em frame_interpolation.cpp, está
// dentro de extern "C". O resto deste arquivo só usa as macros dentro de
// funções extern "C" e por isso nunca esbarrou nisso; as funções de HUD abaixo
// são C++ normais, então precisam da declaração com o linkage certo.
extern "C" {
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

bool gHudDrawActive = false; // true só durante o dispatch do evento

// Orçamento de desenho por frame. Cada retângulo custa comandos na display
// list, e o pool gráfico do jogo é fixo: um mod que desenhe demais estoura o
// buffer e derruba o host inteiro (Fault "região dinâmica destruída" em
// graph.c). O host não pode depender do bom senso do mod — corta no teto e
// avisa uma vez, em vez de crashar.
constexpr int kHudMaxRectsPerFrame = 400;
int gHudRectsThisFrame = 0;
bool gHudBudgetWarned = false;
// A textura de preenchimento é carregada UMA vez por frame, não por retângulo:
// gDPLoadMultiBlock_4b sozinho expande para vários comandos GBI, e recarregá-la
// a cada chamada foi o que estourou o pool. draw_text usa o caminho nativo de
// texto (POLY_OPA) e mexe no tile, então marca para recarregar.
bool gHudTileDirty = true;

// Carrega a textura de preenchimento no tile, se necessário.
void HudEnsureFillTexture(PlayState* play) {
    if (!gHudTileDirty) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gDPLoadMultiBlock_4b(OVERLAY_DISP++, gMagicMeterFillTex, 0, G_TX_RENDERTILE, G_IM_FMT_I, 16, 16, 0,
                         G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                         G_TX_NOLOD);
    CLOSE_DISPS(play->state.gfxCtx);
    gHudTileDirty = false;
}

// Reserva espaço no orçamento; false quando o teto já foi atingido.
bool HudTakeBudget(int count) {
    if (gHudRectsThisFrame + count > kHudMaxRectsPerFrame) {
        if (!gHudBudgetWarned) {
            gHudBudgetWarned = true;
            SPDLOG_WARN("ShipLua hud: teto de {} retângulos por frame atingido — o excedente deste frame não "
                        "será desenhado (evita estourar a display list)",
                        kHudMaxRectsPerFrame);
        }
        return false;
    }
    gHudRectsThisFrame += count;
    return true;
}

// Modelos anexados às mãos (ship.oot.player.set_held_item_model). Declarados
// aqui porque DrawHeldItemModels, logo abaixo, os consome.
std::string gHeldItemModelLeft;
std::string gHeldItemModelRight;

void HudBeginOverlay(PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    // Setup de overlay padrão — sem isto a geometria sai com o estado de render
    // herdado do que foi desenhado antes (mesma classe de bug que deixou o
    // corpo do Goron invisível).
    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE);
    gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 255);
    CLOSE_DISPS(play->state.gfxCtx);
}

// ship.hud.draw_rect(x, y, w, h, r, g, b, a)
int LuaHudDrawRect(lua_State* state) {
    PlayState* play = gPlayState;
    if (!gHudDrawActive || play == nullptr) {
        SPDLOG_WARN("ShipLua hud.draw_rect: s\xC3\xB3 pode ser chamado de hook.oot.hud.draw");
        lua_pushboolean(state, 0);
        return 1;
    }
    const int x = static_cast<int>(luaL_checkinteger(state, 1));
    const int y = static_cast<int>(luaL_checkinteger(state, 2));
    const int w = static_cast<int>(luaL_checkinteger(state, 3));
    const int h = static_cast<int>(luaL_checkinteger(state, 4));
    const int r = static_cast<int>(luaL_optinteger(state, 5, 255));
    const int g = static_cast<int>(luaL_optinteger(state, 6, 255));
    const int b = static_cast<int>(luaL_optinteger(state, 7, 255));
    const int a = static_cast<int>(luaL_optinteger(state, 8, 255));
    if (w <= 0 || h <= 0) {
        lua_pushboolean(state, 0);
        return 1;
    }
    if (!HudTakeBudget(1)) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const auto clamp8 = [](int v) { return static_cast<u8>(std::clamp(v, 0, 255)); };

    HudEnsureFillTexture(play);
    OPEN_DISPS(play->state.gfxCtx);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, clamp8(r), clamp8(g), clamp8(b), clamp8(a));
    gSPWideTextureRectangle(OVERLAY_DISP++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10,
                            1 << 10);
    CLOSE_DISPS(play->state.gfxCtx);
    lua_pushboolean(state, 1);
    return 1;
}

// ship.hud.draw_ring(cx, cy, raio, espessura, fração, r, g, b, a)
//
// Desenha um arco circular preenchido de `fração` (0..1) a partir do topo, no
// sentido horário — a roda de stamina de BotW/Skyward Sword. Combinada com
// ship.player.get("screen_x"/"screen_y"), o medidor acompanha o personagem.
//
// Implementação: o arco é composto por quadrados pequenos ao longo da
// circunferência. gSPTextureRectangle só desenha retângulos alinhados aos
// eixos — não há como rotacionar um quad por aqui sem montar vértices e
// matriz à mão. Com raio típico (12-20px) e ~48 segmentos o resultado lê como
// um anel contínuo. É uma aproximação assumida, não um arco analítico.
int LuaHudDrawRing(lua_State* state) {
    PlayState* play = gPlayState;
    if (!gHudDrawActive || play == nullptr) {
        SPDLOG_WARN("ShipLua hud.draw_ring: s\xC3\xB3 pode ser chamado de hook.oot.hud.draw");
        lua_pushboolean(state, 0);
        return 1;
    }
    const double cx = luaL_checknumber(state, 1);
    const double cy = luaL_checknumber(state, 2);
    const double radius = luaL_checknumber(state, 3);
    const double thickness = luaL_optnumber(state, 4, 3.0);
    const double fraction = std::clamp(luaL_optnumber(state, 5, 1.0), 0.0, 1.0);
    const int r = static_cast<int>(luaL_optinteger(state, 6, 255));
    const int g = static_cast<int>(luaL_optinteger(state, 7, 255));
    const int b = static_cast<int>(luaL_optinteger(state, 8, 255));
    const int a = static_cast<int>(luaL_optinteger(state, 9, 255));
    if (radius <= 0.0 || fraction <= 0.0) {
        lua_pushboolean(state, 1); // nada a desenhar não é erro
        return 1;
    }
    const auto clamp8 = [](int v) { return static_cast<u8>(std::clamp(v, 0, 255)); };
    // Densidade de segmentos: 1.5x o raio dá um anel visualmente contínuo sem
    // torrar a display list (era 3x, e um anel grande sozinho consumia quase
    // 100 retângulos por frame).
    const int seg = static_cast<int>(std::clamp(radius * 1.5, 12.0, 48.0));
    const int drawn = static_cast<int>(seg * fraction + 0.5);
    const int dot = std::max(1, static_cast<int>(thickness + 0.5));

    if (drawn <= 0) {
        lua_pushboolean(state, 1);
        return 1;
    }
    if (!HudTakeBudget(drawn)) {
        lua_pushboolean(state, 0);
        return 1;
    }

    HudEnsureFillTexture(play);
    OPEN_DISPS(play->state.gfxCtx);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, clamp8(r), clamp8(g), clamp8(b), clamp8(a));
    // Constante local: M_PI não é garantido no MSVC sem _USE_MATH_DEFINES
    // antes dos headers de math, e mexer na ordem de includes deste arquivo
    // por causa disso não vale a pena.
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < drawn; ++i) {
        // Começa no topo (-PI/2) e avança no sentido horário.
        const double ang = -kPi / 2.0 + (2.0 * kPi * i) / seg;
        const int px = static_cast<int>(cx + std::cos(ang) * radius - dot / 2.0 + 0.5);
        const int py = static_cast<int>(cy + std::sin(ang) * radius - dot / 2.0 + 0.5);
        gSPWideTextureRectangle(OVERLAY_DISP++, px << 2, py << 2, (px + dot) << 2, (py + dot) << 2, G_TX_RENDERTILE, 0,
                                0, 1 << 10, 1 << 10);
    }
    CLOSE_DISPS(play->state.gfxCtx);
    lua_pushboolean(state, 1);
    return 1;
}

// ship.hud.draw_icon(path, x, y, w, h, { alpha = 255 })
// A Fase 0 aceita texturas RGBA32 registradas no resource manager. Os ícones
// vanilla em textures/icon_item_static têm 32x32 e cabem exatamente na TMEM.
int LuaHudDrawIcon(lua_State* state) {
    PlayState* play = gPlayState;
    if (!gHudDrawActive || play == nullptr) {
        SPDLOG_WARN("ShipLua hud.draw_icon: só pode ser chamado de hook.oot.hud.draw");
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* path = luaL_checkstring(state, 1);
    const int x = static_cast<int>(luaL_checkinteger(state, 2));
    const int y = static_cast<int>(luaL_checkinteger(state, 3));
    const int w = static_cast<int>(luaL_checkinteger(state, 4));
    const int h = static_cast<int>(luaL_checkinteger(state, 5));
    int alpha = 255;
    if (lua_istable(state, 6)) {
        lua_getfield(state, 6, "alpha");
        if (!lua_isnil(state, -1)) {
            alpha = static_cast<int>(luaL_checkinteger(state, -1));
        }
        lua_pop(state, 1);
    }
    if (path == nullptr || *path == '\0' || w < 1 || w > 64 || h < 1 || h > 64 ||
        !ResourceMgr_FileExists(path)) {
        lua_pushboolean(state, 0);
        return 1;
    }
    // icon_item_static é um atlas lógico de entradas RGBA32 32x32. As funções
    // ResourceMgr_LoadTexWidth/HeightByName constam do header deste fork, mas
    // não possuem definição ligada; usar o contrato fixo evita um LNK2001.
    constexpr uint16_t texW = 32;
    constexpr uint16_t texH = 32;
    char* texture = ResourceMgr_LoadTexOrDListByName(path);
    if (texture == nullptr) {
        SPDLOG_WARN("ShipLua hud.draw_icon: textura '{}' ausente", path);
        lua_pushboolean(state, 0);
        return 1;
    }
    if (!HudTakeBudget(1)) {
        lua_pushboolean(state, 0);
        return 1;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, static_cast<u8>(std::clamp(alpha, 0, 255)));
    gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
    gDPLoadTextureBlock(OVERLAY_DISP++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, texW, texH, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    const int dsdx = std::max(1, (static_cast<int>(texW) << 10) / w);
    const int dtdy = std::max(1, (static_cast<int>(texH) << 10) / h);
    gSPWideTextureRectangle(OVERLAY_DISP++, x << 2, y << 2, (x + w) << 2, (y + h) << 2,
                            G_TX_RENDERTILE, 0, 0, dsdx, dtdy);
    CLOSE_DISPS(play->state.gfxCtx);
    gHudTileDirty = true;
    lua_pushboolean(state, 1);
    return 1;
}

// ship.hud.draw_text(text, x, y, r, g, b, a, scale)
//
// Interface_DrawTextLine escreve em POLY_OPA. Os demais primitivos ShipLua
// vivem em OVERLAY e, portanto, eram compostos depois do texto, escurecendo ou
// cobrindo labels e quantidades. Este caminho equivalente escreve a fonte no
// mesmo buffer OVERLAY para preservar a ordem declarada pelo mod.
int LuaHudDrawText(lua_State* state) {
    PlayState* play = gPlayState;
    if (!gHudDrawActive || play == nullptr) {
        SPDLOG_WARN("ShipLua hud.draw_text: s\xC3\xB3 pode ser chamado de hook.oot.hud.draw");
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* text = luaL_checkstring(state, 1);
    const int x = static_cast<int>(luaL_checkinteger(state, 2));
    const int y = static_cast<int>(luaL_checkinteger(state, 3));
    const int r = static_cast<int>(luaL_optinteger(state, 4, 255));
    const int g = static_cast<int>(luaL_optinteger(state, 5, 255));
    const int b = static_cast<int>(luaL_optinteger(state, 6, 255));
    const int a = static_cast<int>(luaL_optinteger(state, 7, 255));
    const double scale = luaL_optnumber(state, 8, 1.0);
    if (text == nullptr) {
        lua_pushboolean(state, 0);
        return 1;
    }
    // Limite defensivo: o helper nativo desenha caractere a caractere e uma
    // string enorme por frame encheria a display list.
    std::string line(text);
    if (line.size() > 128) {
        line.resize(128);
    }
    const auto clamp8 = [](int v) { return static_cast<uint16_t>(std::clamp(v, 0, 255)); };
    // Cada caractere carrega sua textura I4 e consome display list.
    if (!HudTakeBudget(static_cast<int>(line.size()))) {
        lua_pushboolean(state, 0);
        return 1;
    }

    const char* processed = Interface_ReplaceSpecialCharacters(line.data());
    const float textScale = static_cast<float>(std::clamp(scale, 0.1, 4.0));
    const int charSize = std::max(1, static_cast<int>(16.0f * textScale));
    const int texScale = std::max(1, static_cast<int>(1024.0f / textScale));
    int kerning = 0;
    int lineOffset = 0;

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    for (const unsigned char ch : std::string(processed)) {
        if (ch == '\n') {
            lineOffset += static_cast<int>(15.0f * textScale);
            kerning = 0;
            continue;
        }
        if (ch != ' ') {
            void* texture = Ship_GetCharFontTexture(ch);
            if (texture != nullptr) {
                gDPPipeSync(OVERLAY_DISP++);
                gDPLoadTextureBlock_4b(OVERLAY_DISP++, texture, G_IM_FMT_I, FONT_CHAR_TEX_WIDTH,
                                       FONT_CHAR_TEX_HEIGHT, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                       G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, clamp8(a));
                gSPTextureRectangle(OVERLAY_DISP++, (x + kerning + 1) << 2, (y + lineOffset + 1) << 2,
                                    (x + kerning + 1 + charSize) << 2, (y + lineOffset + 1 + charSize) << 2,
                                    G_TX_RENDERTILE, 0, 0, texScale, texScale);
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, clamp8(r), clamp8(g), clamp8(b), clamp8(a));
                gSPTextureRectangle(OVERLAY_DISP++, (x + kerning) << 2, (y + lineOffset) << 2,
                                    (x + kerning + charSize) << 2, (y + lineOffset + charSize) << 2,
                                    G_TX_RENDERTILE, 0, 0, texScale, texScale);
            }
        }
        kerning += static_cast<int>(Ship_GetCharFontWidth(ch) * textScale);
    }
    CLOSE_DISPS(play->state.gfxCtx);
    gHudTileDirty = true;
    lua_pushboolean(state, 1);
    return 1;
}

// D-pad do HUD tomado por provider nativo (OOT-MOVE-006): capturado em OotNativeEngine.cpp e desenhado por
// z_parameter.c.
extern "C" s32 LinkSpan_OwnedDpadBackground(PlayState* play, s16* x, s16* y, u8* r, u8* g, u8* b, u8* alpha);
extern "C" void LinkSpan_DrawDpadBackground(PlayState* play, s16 x, s16 y, u8 r, u8 g, u8 b, u8 alpha);

extern "C" void DrawHudOverlay(PlayState* play) {
    if (gModHost == nullptr) {
        return;
    }
    // Só durante jogo de verdade. Checar apenas "existe Player" NÃO basta: a
    // tela de título carrega uma cena real, com Link nela, então o HUD do mod
    // aparecia por cima do logo. gameMode é o discriminador que o próprio
    // engine usa (z_play.c) e cobre título, file select e créditos.
    if (play == nullptr || GET_PLAYER(play) == nullptr || gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return;
    }
    // Orçamento e estado de tile são por frame.
    gHudRectsThisFrame = 0;
    gHudTileDirty = true;
    HudBeginOverlay(play);
    // O Interface_Draw vem depois deste hook. Com o D-pad tomado por provider, o fundo sai aqui, antes dos
    // callbacks, para os ícones do mod ficarem por cima dele.
    s16 dpadX = 0;
    s16 dpadY = 0;
    u8 dpadR = 0;
    u8 dpadG = 0;
    u8 dpadB = 0;
    u8 dpadAlpha = 0;
    if (LinkSpan_OwnedDpadBackground(play, &dpadX, &dpadY, &dpadR, &dpadG, &dpadB, &dpadAlpha) && dpadAlpha) {
        LinkSpan_DrawDpadBackground(play, dpadX, dpadY, dpadR, dpadG, dpadB, dpadAlpha);
        HudBeginOverlay(play);
    }
    gHudDrawActive = true;
    DispatchHookEvent("hook.oot.hud.draw", ShipLua::EventPayload{});
    gHudDrawActive = false;
}

extern "C" void DrawHeldItemModels(PlayState* play) {
    if (gHeldItemModelLeft.empty() && gHeldItemModelRight.empty()) {
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    if (!gHeldItemModelLeft.empty()) {
        Matrix_Translate(player->bodyPartsPos[PLAYER_BODYPART_L_HAND].x,
                         player->bodyPartsPos[PLAYER_BODYPART_L_HAND].y,
                         player->bodyPartsPos[PLAYER_BODYPART_L_HAND].z, MTXMODE_NEW);
        gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                 G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gHeldItemModelLeft.c_str());
    }
    if (!gHeldItemModelRight.empty()) {
        Matrix_Translate(player->bodyPartsPos[PLAYER_BODYPART_R_HAND].x,
                         player->bodyPartsPos[PLAYER_BODYPART_R_HAND].y,
                         player->bodyPartsPos[PLAYER_BODYPART_R_HAND].z, MTXMODE_NEW);
        gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                 G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gHeldItemModelRight.c_str());
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Mesmo passe Xlu de tela inteira usado pela maquina de transformacao de
// OoTMM. Roda antes do HUD, para que os botoes/hearts nunca sejam apagados.
extern "C" void DrawMaskTransitionFlash(PlayState* play) {
    if (play == nullptr || gMaskTransitionFlashAlpha <= 0) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_44Xlu(play->state.gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetCombineLERP(POLY_XLU_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                      PRIMITIVE);
    const u8 alpha = static_cast<u8>(gMaskTransitionFlashAlpha);
    const u8 red = static_cast<u8>(180 + (75 * alpha / 255));
    const u8 green = static_cast<u8>(215 + (40 * alpha / 255));
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, red, green, 255, alpha);
    gDPFillRectangle(POLY_XLU_DISP++, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    gDPPipeSync(POLY_XLU_DISP++);
    CLOSE_DISPS(play->state.gfxCtx);
}

int LuaSetHeldItemModel(lua_State* state) {
    const char* slot = luaL_checkstring(state, 1);
    const char* path = luaL_optstring(state, 2, nullptr);
    std::string* target = nullptr;
    if (std::strcmp(slot, "left_hand") == 0) {
        target = &gHeldItemModelLeft;
    } else if (std::strcmp(slot, "right_hand") == 0) {
        target = &gHeldItemModelRight;
    } else {
        SPDLOG_WARN("ShipLua set_held_item_model: slot '{}' n\xC3\xA3o suportado (use 'left_hand'/'right_hand')",
                    slot);
        lua_pushboolean(state, 0);
        return 1;
    }

    if (path == nullptr || *path == '\0') {
        target->clear();
        lua_pushboolean(state, 1);
        return 1;
    }
    if (!ResourceMgr_FileExists(path)) {
        SPDLOG_WARN("ShipLua set_held_item_model: asset '{}' n\xC3\xA3o encontrado", path);
        lua_pushboolean(state, 0);
        return 1;
    }
    *target = std::string("__OTR__") + path;
    SPDLOG_INFO("ShipLua set_held_item_model: '{}' anexado ao slot '{}'", path, slot);
    lua_pushboolean(state, 1);
    return 1;
}

// PASSO 3 — ship.player.get/set: acesso a campos do player por nome, com
// validação, em vez de uma função nativa dedicada por ideia.
enum class FieldKind {
    Health, HealthCapacity, Magic, Rupees, PosX, PosY, PosZ, RotY, Speed, VelX, VelY, VelZ, OnGround,
    // Estados de ação, somente leitura. Semânticos de propósito: expor
    // stateFlags cru amarraria os mods a bits internos do engine.
    Rolling, Climbing, Swimming,
    // Posição do jogador projetada na tela — permite ancorar um medidor ao
    // personagem (roda de stamina ao estilo BotW) em vez de fixá-lo num canto.
    ScreenX, ScreenY
};

struct PlayerField {
    const char* name;
    FieldKind kind;
    double min;
    double max;
    bool writable;
};

// Array C cru de propósito: com std::array<N> era preciso manter o N à mão, e
// esquecer disso ao acrescentar um campo dá um erro de "muitos inicializadores"
// que não aponta para a linha do campo novo. std::begin/std::end abaixo tiram
// o tamanho do próprio literal.
constexpr PlayerField kPlayerFields[] = {
    { "health", FieldKind::Health, 0, 20 * 16, true },
    { "health_capacity", FieldKind::HealthCapacity, 16, 20 * 16, true },
    { "magic", FieldKind::Magic, 0, 96, true },
    { "rupees", FieldKind::Rupees, 0, 999, true },
    { "pos_x", FieldKind::PosX, -100000, 100000, true },
    { "pos_y", FieldKind::PosY, -100000, 100000, true },
    { "pos_z", FieldKind::PosZ, -100000, 100000, true },
    { "rot_y", FieldKind::RotY, -32768, 32767, true },
    { "speed", FieldKind::Speed, -50, 50, true },
    // vel_x/y/z + on_ground: física real do actor (Vec3f velocity, bgCheckFlags
    // bit 0). Faltava a componente vertical — "speed"/linearVelocity é só o
    // escalar planar. Com isto + game.frame (já existente), um mod implementa
    // pulo duplo/planador/air-dash inteiro em Lua, sem hook nativo dedicado.
    { "vel_x", FieldKind::VelX, -100, 100, true },
    { "vel_y", FieldKind::VelY, -100, 100, true },
    { "vel_z", FieldKind::VelZ, -100, 100, true },
    { "on_ground", FieldKind::OnGround, 0, 1, false },
    // Estados de ação (0/1, somente leitura). Permitem a um mod cobrar custo
    // por esforço — rolar, escalar, nadar — sem inferir da velocidade.
    { "rolling", FieldKind::Rolling, 0, 1, false },
    // climbing é gravável: escrever 0 solta o jogador da escada e o faz cair
    // (um mod de stamina precisa disso para "ficou sem força no meio da subida").
    { "climbing", FieldKind::Climbing, 0, 1, true },
    { "swimming", FieldKind::Swimming, 0, 1, false },
    // Coordenadas de tela do jogador (só leitura). Podem sair da tela quando a
    // câmera não o enquadra — o mod decide se desenha mesmo assim.
    { "screen_x", FieldKind::ScreenX, -10000, 10000, false },
    { "screen_y", FieldKind::ScreenY, -10000, 10000, false },
};

const PlayerField* FindPlayerField(const char* name) {
    const auto found = std::find_if(std::begin(kPlayerFields), std::end(kPlayerFields),
                                    [name](const PlayerField& f) { return std::strcmp(f.name, name) == 0; });
    return found == std::end(kPlayerFields) ? nullptr : &*found;
}

double ReadPlayerField(const PlayerField& field, Player* player) {
    switch (field.kind) {
        case FieldKind::Health:
            return gSaveContext.health;
        case FieldKind::HealthCapacity:
            return gSaveContext.healthCapacity;
        case FieldKind::Magic:
            return gSaveContext.magic;
        case FieldKind::Rupees:
            return gSaveContext.rupees;
        case FieldKind::PosX:
            return player->actor.world.pos.x;
        case FieldKind::PosY:
            return player->actor.world.pos.y;
        case FieldKind::PosZ:
            return player->actor.world.pos.z;
        case FieldKind::RotY:
            return player->actor.shape.rot.y;
        case FieldKind::Speed:
            return player->linearVelocity;
        case FieldKind::VelX:
            return player->actor.velocity.x;
        case FieldKind::VelY:
            return player->actor.velocity.y;
        case FieldKind::VelZ:
            return player->actor.velocity.z;
        case FieldKind::OnGround:
            return (player->actor.bgCheckFlags & 1) ? 1.0 : 0.0;
        case FieldKind::Rolling:
            // Comparar a actionFunc é o teste confiável — não há flag de estado
            // para rolamento. Mesmo critério que o corpo customizado usa.
            return player->actionFunc == Player_Action_Roll ? 1.0 : 0.0;
        case FieldKind::Climbing:
            return (player->stateFlags1 & (PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_CLIMBING_LEDGE)) ? 1.0 : 0.0;
        case FieldKind::Swimming:
            return (player->stateFlags1 & PLAYER_STATE1_IN_WATER) ? 1.0 : 0.0;
        case FieldKind::ScreenX:
        case FieldKind::ScreenY: {
            if (gPlayState == nullptr) {
                return 0.0;
            }
            s16 sx = 0;
            s16 sy = 0;
            // Projeta focus.pos (a cabeça), não world.pos — é o ponto que o
            // próprio engine usa para ancorar UI ao ator.
            Actor_GetScreenPos(gPlayState, &player->actor, &sx, &sy);
            return field.kind == FieldKind::ScreenX ? static_cast<double>(sx) : static_cast<double>(sy);
        }
    }
    return 0.0;
}

void WritePlayerField(const PlayerField& field, Player* player, double value) {
    switch (field.kind) {
        case FieldKind::Health:
            gSaveContext.health = static_cast<int16_t>(value);
            break;
        case FieldKind::HealthCapacity:
            gSaveContext.healthCapacity = static_cast<int16_t>(value);
            break;
        case FieldKind::Magic:
            gSaveContext.magic = static_cast<int8_t>(value);
            break;
        case FieldKind::Rupees:
            gSaveContext.rupees = static_cast<int16_t>(value);
            break;
        case FieldKind::PosX:
            player->actor.world.pos.x = static_cast<float>(value);
            break;
        case FieldKind::PosY:
            player->actor.world.pos.y = static_cast<float>(value);
            break;
        case FieldKind::PosZ:
            player->actor.world.pos.z = static_cast<float>(value);
            break;
        case FieldKind::RotY:
            player->actor.shape.rot.y = static_cast<int16_t>(value);
            break;
        case FieldKind::Speed:
            player->linearVelocity = static_cast<float>(value);
            break;
        case FieldKind::VelX:
            player->actor.velocity.x = static_cast<float>(value);
            break;
        case FieldKind::VelY:
            player->actor.velocity.y = static_cast<float>(value);
            break;
        case FieldKind::VelZ:
            player->actor.velocity.z = static_cast<float>(value);
            break;
        case FieldKind::Climbing:
            // Só faz sentido soltar (0); escrever 1 não gruda o jogador numa
            // escada do nada, então é ignorado.
            if (value < 0.5 && (player->stateFlags1 & PLAYER_STATE1_CLIMBING_LADDER) && gPlayState != nullptr) {
                func_8083FB7C(player, gPlayState);
            }
            break;
        case FieldKind::OnGround:
        case FieldKind::Rolling:
        case FieldKind::Swimming:
        case FieldKind::ScreenX:
        case FieldKind::ScreenY:
            break; // somente leitura
    }
}

// Slot genérico para qualquer estado que um item customizado precise (timer,
// contador, flag) sem exigir um campo novo na struct nativa do Player a cada
// mod novo — achado como necessidade real cruzando dois forks independentes
// de sistemas de item customizado (cada um "colou" ~10 campos ad-hoc na
// struct Player nativa; aqui isso vira uma chave string qualquer). Não é
// salvo — vive só durante a sessão, mesma ressalva do storage mock.
std::map<std::string, double> gPlayerScratch;

int LuaPlayerGet(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    const PlayerField* field = FindPlayerField(name);
    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (field != nullptr) {
        if (player == nullptr) {
            lua_pushnil(state);
            return 1;
        }
        lua_pushnumber(state, ReadPlayerField(*field, player));
        return 1;
    }
    const auto scratch = gPlayerScratch.find(name);
    if (scratch != gPlayerScratch.end()) {
        lua_pushnumber(state, scratch->second);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

int LuaPlayerSet(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    const double value = luaL_checknumber(state, 2);
    if (!std::isfinite(value)) {
        SPDLOG_WARN("ShipLua player.set: valor inv\xC3\xA1lido para '{}'", name);
        lua_pushboolean(state, 0);
        return 1;
    }
    const PlayerField* field = FindPlayerField(name);
    if (field != nullptr) {
        if (!field->writable || value < field->min || value > field->max) {
            SPDLOG_WARN("ShipLua player.set: valor fora da faixa para '{}'", name);
            lua_pushboolean(state, 0);
            return 1;
        }
        PlayState* play = gPlayState;
        Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
        if (player == nullptr) {
            lua_pushboolean(state, 0);
            return 1;
        }
        WritePlayerField(*field, player, value);
        lua_pushboolean(state, 1);
        return 1;
    }
    // Chave desconhecida na tabela curada: cai no slot genérico — qualquer
    // string serve, nenhum campo nativo precisa existir para isso.
    gPlayerScratch[name] = value;
    lua_pushboolean(state, 1);
    return 1;
}

// ship.oot.player.set_bunny_hood(equipped): veste a Bunny Hood do OoT e liga
// o comportamento de Majora's Mask (corrida mais rápida e pulo maior), que o
// próprio host já implementa atrás do enhancement MMBunnyHood. Ao desequipar,
// o valor anterior do enhancement é restaurado.
bool gBunnyHoodForced = false;
int gBunnyHoodPreviousMode = BUNNY_HOOD_VANILLA;
int gBunnyHoodPreviousPersistent = 0;

int LuaSetBunnyHood(lua_State* state) {
    const bool equip = lua_toboolean(state, 1) != 0;
    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr || (player->stateFlags1 & PLAYER_STATE1_DEAD) != 0) {
        SPDLOG_WARN("ShipLua set_bunny_hood: fora de gameplay");
        lua_pushboolean(state, 0);
        return 1;
    }

    if (equip) {
        if (!gBunnyHoodForced) {
            gBunnyHoodPreviousMode = CVarGetInteger(CVAR_ENHANCEMENT("MMBunnyHood"), BUNNY_HOOD_VANILLA);
            gBunnyHoodPreviousPersistent = CVarGetInteger(CVAR_ENHANCEMENT("PersistentMasks"), 0);
            gBunnyHoodForced = true;
        }
        CVarSetInteger(CVAR_ENHANCEMENT("MMBunnyHood"), BUNNY_HOOD_FAST_AND_JUMP);
        // Sem PersistentMasks, Player_ProcessItemButtons zera currentMask no
        // frame seguinte quando a máscara não está num botão C — e a Bunny
        // Hood some antes de ser desenhada.
        CVarSetInteger(CVAR_ENHANCEMENT("PersistentMasks"), 1);
        player->currentMask = PLAYER_MASK_BUNNY;
        gSaveContext.ship.maskMemory = PLAYER_MASK_BUNNY;
        SPDLOG_INFO("ShipLua set_bunny_hood: Bunny Hood equipada com o comportamento de MM");
    } else {
        if (player->currentMask == PLAYER_MASK_BUNNY) {
            player->currentMask = PLAYER_MASK_NONE;
        }
        gSaveContext.ship.maskMemory = PLAYER_MASK_NONE;
        if (gBunnyHoodForced) {
            CVarSetInteger(CVAR_ENHANCEMENT("MMBunnyHood"), gBunnyHoodPreviousMode);
            CVarSetInteger(CVAR_ENHANCEMENT("PersistentMasks"), gBunnyHoodPreviousPersistent);
            gBunnyHoodForced = false;
        }
        SPDLOG_INFO("ShipLua set_bunny_hood: Bunny Hood removida");
    }
    // Mesmo motivo do set_speed_multiplier: sem re-registrar, o hook de
    // velocidade da Bunny Hood não é instalado e a máscara fica só visual.
    ShipInit::Init(CVAR_ENHANCEMENT("MMBunnyHood"));

    lua_pushboolean(state, 1);
    return 1;
}

int LuaSpawnDog(lua_State* state) {
    PlayState* play = gPlayState;
    Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
    if (player == nullptr || (play->sceneNum != SCENE_MARKET_DAY && play->sceneNum != SCENE_MARKET_NIGHT) ||
        Object_GetIndex(&play->objectCtx, OBJECT_DOG) < 0) {
        lua_pushboolean(state, 0);
        return 1;
    }
    Actor* dog =
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_DOG, player->actor.world.pos.x, player->actor.world.pos.y,
                    player->actor.world.pos.z, 0, player->actor.shape.rot.y, 0, static_cast<s16>(0x8000));
    lua_pushboolean(state, dog != nullptr);
    return 1;
}

void InstallOotApi(lua_State* state) {
    if (state == nullptr) {
        return;
    }
    lua_getglobal(state, "require");
    lua_pushstring(state, "ship");
    if (lua_pcall(state, 1, 1, 0) != LUA_OK || !lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    const int shipTable = lua_gettop(state);
    lua_getfield(state, shipTable, "game");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
    }
    lua_pushcfunction(state, LuaGameState);
    lua_setfield(state, -2, "state");
    lua_setfield(state, shipTable, "game");

    lua_getfield(state, shipTable, "oot");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
    }
    const int ootTable = lua_gettop(state);
    lua_pushcfunction(state, LuaSpawnDog);
    lua_setfield(state, ootTable, "spawn_dog");
    lua_getfield(state, ootTable, "player");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
    }
    lua_pushcfunction(state, LuaPlayerJump);
    lua_setfield(state, -2, "jump");
    lua_pushcfunction(state, LuaSetBunnyHood);
    lua_setfield(state, -2, "set_bunny_hood");
    lua_pushcfunction(state, LuaSetMask);
    lua_setfield(state, -2, "set_mask");
    lua_pushcfunction(state, LuaPlayMaskOnAnimation);
    lua_setfield(state, -2, "play_mask_on_animation");
    lua_pushcfunction(state, LuaSetPlayerBody);
    lua_setfield(state, -2, "set_body");
    lua_pushcfunction(state, LuaGetPlayerBody);
    lua_setfield(state, -2, "get_body");
    lua_pushcfunction(state, LuaPlayPlayerBodyAnimation);
    lua_setfield(state, -2, "play_body_animation");
    lua_pushcfunction(state, LuaSetPlayerBodySegment);
    lua_setfield(state, -2, "set_body_segment");
    lua_pushcfunction(state, LuaAttachModel);
    lua_setfield(state, -2, "attach_model");
    lua_pushcfunction(state, LuaSetHeldItemModel);
    lua_setfield(state, -2, "set_held_item_model");
    lua_pushcfunction(state, LuaSetDamageImmunity);
    lua_setfield(state, -2, "set_damage_immunity");
    lua_pushcfunction(state, LuaSetWeight);
    lua_setfield(state, -2, "set_weight");
    lua_pushcfunction(state, LuaSetRollMode);
    lua_setfield(state, -2, "set_roll_mode");
    lua_pushcfunction(state, LuaSetRollBlocked);
    lua_setfield(state, -2, "set_roll_blocked");
    lua_pushcfunction(state, LuaPlayerSetPointLight);
    lua_setfield(state, -2, "set_point_light");
    lua_setfield(state, ootTable, "player");

    // ship.oot.env: estado do ambiente (hora do dia, cena). Separado de
    // ship.oot.player porque não é propriedade do jogador.
    lua_newtable(state);
    lua_pushcfunction(state, LuaEnvGet);
    lua_setfield(state, -2, "get");
    lua_pushcfunction(state, LuaEnvSetLightOverride);
    lua_setfield(state, -2, "set_light_override");
    lua_pushcfunction(state, LuaEnvClearLightOverride);
    lua_setfield(state, -2, "clear_light_override");
    lua_setfield(state, ootTable, "env");

    // ship.oot.audio: hoje só o mapa de voz. play_sfx entra quando a Fase 4
    // fechar o contrato no schema.
    lua_newtable(state);
    lua_pushcfunction(state, LuaSetVoiceMap);
    lua_setfield(state, -2, "set_voice_map");
    lua_pushcfunction(state, LuaPlayFontSfx);
    lua_setfield(state, -2, "play_sfx");
    lua_pushcfunction(state, LuaDumpSfxTable);
    lua_setfield(state, -2, "dump_sfx_table");
    lua_setfield(state, ootTable, "audio");

    // ship.oot.cutscene: assume a câmera por N frames. Genérica — serve para
    // transformação, item dramático, revelação de porta, o que o mod quiser.
    lua_newtable(state);
    lua_pushcfunction(state, LuaCutsceneStart);
    lua_setfield(state, -2, "start");
    lua_pushcfunction(state, LuaCutsceneStop);
    lua_setfield(state, -2, "stop");
    lua_pushcfunction(state, LuaCutsceneActive);
    lua_setfield(state, -2, "is_active");
    lua_setfield(state, ootTable, "cutscene");

    lua_setfield(state, shipTable, "oot");

    // A API comum registra set_speed_multiplier no núcleo antes do entrypoint.
    // O host acrescenta somente as operações específicas de campos do OoT.
    lua_getfield(state, shipTable, "player");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
    }
    lua_pushcfunction(state, LuaPlayerGet);
    lua_setfield(state, -2, "get");
    lua_pushcfunction(state, LuaPlayerSet);
    lua_setfield(state, -2, "set");
    lua_setfield(state, shipTable, "player");

    // ship.hud: desenho genérico em overlay, válido só dentro de
    // hook.oot.hud.draw. Comum aos dois jogos por design (o MM ganha o mesmo).
    lua_newtable(state);
    lua_pushcfunction(state, LuaHudDrawRect);
    lua_setfield(state, -2, "draw_rect");
    lua_pushcfunction(state, LuaHudDrawText);
    lua_setfield(state, -2, "draw_text");
    lua_pushcfunction(state, LuaHudDrawRing);
    lua_setfield(state, -2, "draw_ring");
    lua_pushcfunction(state, LuaHudDrawIcon);
    lua_setfield(state, -2, "draw_icon");
    lua_setfield(state, shipTable, "hud");

    lua_pop(state, 1);
}

// Prefixo de namespace dos assets do MM dentro do OOT: todo o mm.o2r fica
// endereçável como "mm/<caminho original>".
constexpr const char* kMmNamespace = "mm/";

// Archive que lê DIRETO do mm.o2r da instalação do MM, sem copiar nada:
// - toda entrada é exposta com o prefixo "mm/" (100% do arquivo, sem colisão);
// - entradas cujo caminho original NÃO existe no OOT também ganham alias no
//   hash original, para as referências internas dos resources (DL→textura
//   etc., gravadas por hash do caminho sem prefixo) resolverem sozinhas.
// Só as ~5% de entradas que colidem com caminhos do OOT ficam sem alias.
class MmCrossWorldArchive final : public Ship::Archive {
  public:
    // O prefixo é parametrizável: "mm/" para o archive do jogo vizinho e
    // "mod/<id>/" para os assets próprios de um mod.
    MmCrossWorldArchive(const std::string& path, Ship::ArchiveManager* manager, std::string prefix = kMmNamespace)
        : Ship::Archive(path), mInner(std::make_shared<Ship::O2rArchive>(path)), mManager(manager),
          mPrefix(std::move(prefix)) {
    }

    bool Open() override {
        if (!mInner->Open()) {
            SPDLOG_WARN("ShipLua n\xC3\xA3o abriu o archive interno '{}'", GetPath());
            return false;
        }
        std::size_t aliased = 0;
        std::size_t blocked = 0;
        const auto files = mInner->ListFiles();
        for (const auto& [hash, filePath] : *files) {
            IndexFile(mPrefix + filePath);
            // Alias no hash original apenas para dados de render: sistemas como
            // o de áudio do SoH ENUMERAM o índice global (audio/*) e quebram ao
            // encontrar entradas do MM em formato próprio. objects/ e textures/
            // só são resolvidos por referência direta (hash/caminho), nunca por
            // varredura — seguros de aliasar.
            const bool renderData = filePath.rfind("objects/", 0) == 0 || filePath.rfind("textures/", 0) == 0;
            if (renderData && mManager != nullptr && !mManager->HasFile(filePath)) {
                IndexFile(filePath);
                ++aliased;
            } else {
                ++blocked;
            }
        }
        mOwnIndex = ListFiles();
        SPDLOG_INFO("ShipLua exp\xC3\xB4s {} assets sob '{}' ({} com alias direto, {} bloqueados por "
                    "colis\xC3\xA3o de caminho)",
                    files->size(), mPrefix, aliased, blocked);
        return !files->empty();
    }

    bool Close() override {
        mOwnIndex.reset();
        return mInner->Close();
    }

    std::shared_ptr<Ship::File> LoadFile(const std::string& filePath) override {
        if (!HasFile(filePath)) {
            return nullptr;
        }
        std::shared_ptr<Ship::File> file;
        if (filePath.rfind(mPrefix, 0) == 0) {
            file = mInner->LoadFile(filePath.substr(mPrefix.size()));
        } else {
            file = mInner->LoadFile(filePath);
        }
        SanitizeMmDisplayList(file);
        return file;
    }

    // Display lists do 2ship contêm comandos fora do dialeto do SoH:
    // G_DL_INDEX (0x3D) salta para a tabela de setup-DLs pela convenção do
    // 2ship (segmento inexistente aqui — o salto cairia em lixo e derruba o
    // interpretador), e 0x43+ têm numeração divergente entre os dois LUS.
    // Neutraliza/remapeia no load; os comandos de material inline da própria
    // DL permanecem, e o host já aplica o setup do OOT antes do draw.
    static void SanitizeMmDisplayList(const std::shared_ptr<Ship::File>& file) {
        if (file == nullptr || file->Buffer == nullptr || file->Buffer->size() < 0x48) {
            return;
        }
        std::vector<char>& bytes = *file->Buffer;
        if (std::memcmp(bytes.data() + 4, "TLDO", 4) != 0) {
            return;
        }
        for (std::size_t i = 0x40; i + 8 <= bytes.size();) {
            const uint8_t opcode = static_cast<uint8_t>(bytes[i + 3]);
            std::size_t advance = 8;
            switch (opcode) {
                case 0x20: // SETTIMG_OTR_HASH
                case 0x24: // VTX_OTR_FILEPATH
                case 0x25: // SETTIMG_OTR_FILEPATH
                case 0x27: // DL_OTR_FILEPATH
                case 0x29: // MTX_OTR_FILEPATH
                case 0x31: // DL_OTR_HASH
                case 0x32: // VTX_OTR_HASH
                case 0x33: // MARKER
                case 0x35: // BRANCH_Z_OTR
                case 0x36: // MTX_OTR
                case 0x42: // MOVEMEM_HASH
                    advance = 16;
                    break;
                case 0x3D: // G_DL_INDEX (convenção 2ship) — neutraliza
                case 0x43: // G_LOAD_SHADER do MM (inexistente no SoH) — neutraliza
                    std::memset(bytes.data() + i, 0, 8);
                    break;
                case 0x44: // SETTILESIZE_INTERP: MM 0x44 → SoH 0x45
                    bytes[i + 3] = 0x45;
                    break;
                case 0x45: // SETTARGETINTERPINDEX: MM 0x45 → SoH 0x46
                    bytes[i + 3] = 0x46;
                    break;
                default:
                    break;
            }
            i += advance;
            if (opcode == 0xDF) { // G_ENDDL
                break;
            }
        }
    }

    std::shared_ptr<Ship::File> LoadFile(uint64_t hash) override {
        if (mOwnIndex == nullptr) {
            return nullptr;
        }
        const auto it = mOwnIndex->find(hash);
        if (it == mOwnIndex->end()) {
            return nullptr;
        }
        return LoadFile(it->second);
    }

    bool WriteFile(const std::string&, const std::vector<uint8_t>&) override {
        return false;
    }

  private:
    std::shared_ptr<Ship::O2rArchive> mInner;
    Ship::ArchiveManager* mManager = nullptr;
    std::string mPrefix;
    std::shared_ptr<std::unordered_map<uint64_t, std::string>> mOwnIndex;
};

// Monta em runtime o mm.o2r da instalação irmã do MM (../MM por convenção,
// override via SHIPLUA_MM_ROOT). Vice-versa equivalente vive no host MM.
void MountCrossWorldArchives() {
    Ship::Context* shipContext = Ship::Context::GetRawInstance();
    if (shipContext == nullptr || shipContext->GetResourceManager() == nullptr) {
        return;
    }
    const auto archiveManager = shipContext->GetResourceManager()->GetArchiveManager();
    if (archiveManager == nullptr) {
        return;
    }

    // Sonda os layouts conhecidos: override explícito, pacote do launcher
    // (raiz/hosts/mm), host irmão ao lado do exe e, por último, o CWD.
    std::error_code ec;
    std::vector<std::filesystem::path> candidates;
    // O override é exclusivo: quem aponta SHIPLUA_MM_ROOT espera aquele
    // caminho, não uma busca automática em volta dele.
    if (const char* configured = std::getenv("SHIPLUA_MM_ROOT"); configured != nullptr && *configured != '\0') {
        candidates.emplace_back(configured);
    } else {
    // O layout do launcher mantém os dois archives na raiz do pacote (é onde
    // o próprio oot.o2r é encontrado), então a pasta do app vem primeiro.
    const std::filesystem::path cwd = std::filesystem::absolute(Ship::Context::GetAppDirectoryPath(""), ec);
    candidates.push_back(cwd);
    candidates.push_back(cwd / "hosts" / "mm");
    candidates.push_back(cwd.parent_path());
    candidates.push_back(cwd.parent_path() / "mm");
    candidates.push_back(cwd.parent_path() / "MM");
    candidates.push_back(cwd.parent_path().parent_path() / "hosts" / "mm");
#ifdef _WIN32
    const std::filesystem::path runtimeRoot = RuntimeRoot();
    candidates.push_back(runtimeRoot);
    candidates.push_back(runtimeRoot / "hosts" / "mm");
    candidates.push_back(runtimeRoot.parent_path());
    candidates.push_back(runtimeRoot.parent_path() / "mm");
    candidates.push_back(runtimeRoot.parent_path().parent_path());
    candidates.push_back(runtimeRoot.parent_path().parent_path() / "hosts" / "mm");
#endif
    }

    std::filesystem::path mmArchive;
    for (const std::filesystem::path& candidate : candidates) {
        const std::filesystem::path probe = candidate / "mm.o2r";
        if (std::filesystem::is_regular_file(probe, ec)) {
            mmArchive = probe;
            break;
        }
    }
    if (mmArchive.empty()) {
        std::string tried;
        for (const auto& candidate : candidates) {
            tried += (tried.empty() ? "" : "; ") + candidate.string();
        }
        SPDLOG_INFO("ShipLua: mm.o2r n\xC3\xA3o encontrado (procurado em: {}) — assets do MM indispon\xC3\xADveis "
                    "no OOT",
                    tried);
        return;
    }

    const auto crossWorld = std::make_shared<MmCrossWorldArchive>(mmArchive.string(), archiveManager.get());
    crossWorld->Load();
    if (crossWorld->IsLoaded() && archiveManager->AddArchive(crossWorld) != nullptr) {
        SPDLOG_INFO("ShipLua montou o mm.o2r do MM em modo cross-world: {}", mmArchive.string());
    } else {
        SPDLOG_WARN("ShipLua n\xC3\xA3o conseguiu montar '{}'", mmArchive.string());
    }
}

// PASSO 1 — assets próprios de mod.
// Camada Unbound e a pasta do mod que a contém (vazia para .o2r solto na raiz de mods/).
struct PendingUnboundArchive {
    std::filesystem::path archive;
    std::filesystem::path modDirectory;
};
std::vector<PendingUnboundArchive> gPendingUnboundArchives;
// Leitor das camadas Unbound v2. Sem ele, nada lê os documentos da camada: montá-la seria carga parcial
// (plano §10.4, recusa com diagnóstico no host sem o framework).
constexpr const char* kUnboundFrameworkId = "linkspan.unbound.framework";

bool IsUnboundLayerArchive(const std::filesystem::path& path) {
    const auto archive = std::make_shared<Ship::O2rArchive>(path.generic_string());
    archive->Load();
    if (!archive->IsLoaded() || !archive->HasFile("unbound.json")) {
        return false;
    }
    const auto file = archive->LoadFile("unbound.json");
    if (!file || !file->IsLoaded || !file->Buffer || file->Buffer->size() > 64 * 1024) {
        return false;
    }
    const auto& bytes = *file->Buffer;
    const auto manifest = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
    return manifest.is_object() && manifest.contains("format") && manifest["format"] == "unbound" &&
           manifest.contains("formatVersion") && manifest["formatVersion"] == 2;
}

// Archives de assets comuns ficam sob "mod/<nome>/", sem colidir com o jogo.
// Camadas Unbound v2 precisam preservar os caminhos originais para que cenas,
// colisões e entradas sejam mescladas; elas são montadas após o provider.
void MountModAssetArchives() {
    gPendingUnboundArchives.clear();
    Ship::Context* shipContext = Ship::Context::GetRawInstance();
    if (shipContext == nullptr || shipContext->GetResourceManager() == nullptr) {
        return;
    }
    const auto archiveManager = shipContext->GetResourceManager()->GetArchiveManager();
    if (archiveManager == nullptr) {
        return;
    }

    std::error_code ec;
    const std::filesystem::path modsRoot =
        Ship::Context::GetPathRelativeToAppDirectory("mods", Ship::Context::GetRawInstance()->GetShortName());
    if (!std::filesystem::is_directory(modsRoot, ec)) {
        return;
    }

    std::vector<std::filesystem::path> archives;
    std::map<std::filesystem::path, std::filesystem::path> archiveModDirectory;
    for (const auto& entry : std::filesystem::directory_iterator(modsRoot, ec)) {
        if (entry.is_regular_file()) {
            const std::string extension = entry.path().extension().string();
            if (extension == ".o2r" || extension == ".otr") {
                archives.push_back(entry.path());
            }
        } else if (entry.is_directory()) {
            const std::filesystem::path assetsDir = entry.path() / "assets";
            if (!std::filesystem::is_directory(assetsDir, ec)) {
                continue;
            }
            for (const auto& asset : std::filesystem::directory_iterator(assetsDir, ec)) {
                const std::string extension = asset.path().extension().string();
                if (asset.is_regular_file() && (extension == ".o2r" || extension == ".otr")) {
                    archives.push_back(asset.path());
                    archiveModDirectory[asset.path()] = entry.path();
                }
            }
        }
    }

    std::size_t mounted = 0;
    std::sort(archives.begin(), archives.end());
    for (const std::filesystem::path& archivePath : archives) {
        // A base Unbound é convertida no Init do provider. Só depois dele uma
        // camada Unbound pode entrar na raiz: ela precisa mesclar cenas e exits
        // por caminho original antes do game.ready, sem contaminar a conversão.
        if (archivePath.extension() == ".o2r" && IsUnboundLayerArchive(archivePath)) {
            const auto owner = archiveModDirectory.find(archivePath);
            gPendingUnboundArchives.push_back(
                { archivePath, owner == archiveModDirectory.end() ? std::filesystem::path{} : owner->second });
            continue;
        }
        const std::string id = archivePath.stem().string();
        const auto modArchive =
            std::make_shared<MmCrossWorldArchive>(archivePath.string(), archiveManager.get(), "mod/" + id + "/");
        modArchive->Load();
        if (modArchive->IsLoaded() && archiveManager->AddArchive(modArchive) != nullptr) {
            ++mounted;
            SPDLOG_INFO("ShipLua montou assets do mod '{}' sob 'mod/{}/': {}", id, id, archivePath.string());
        } else {
            SPDLOG_WARN("ShipLua n\xC3\xA3o conseguiu montar os assets de mod '{}'", archivePath.string());
        }
    }
    if (mounted > 0) {
        SPDLOG_INFO("ShipLua: {} pacote(s) de assets de mod montado(s)", mounted);
    }
}

// SOH [Link-Span] OOT-VANILLA-001: sem mod no boot, o jogo volta ao comportamento do upstream nos pontos que o
// substrato Unbound alargou. Travada no Initialize, antes do Heaps_Alloc (main.c chama InitOTR primeiro), e nunca
// desliga na sessão: o que foi alocado no tamanho alargado continua assim.
bool HasModArchive() {
    const auto manager = NativeResourceManager();
    const auto archiveManager = manager ? manager->GetArchiveManager() : nullptr;
    const auto archives = archiveManager ? archiveManager->GetArchives() : nullptr;
    if (!archives) return false;
    for (const auto& archive : *archives) {
        if (!archive) continue;
        for (const auto& part : std::filesystem::path(archive->GetPath())) {
            if (part == "mods") return true;
        }
    }
    return false;
}

void SetEngineExtended(bool extended) {
    gLinkSpanEngineExtended = extended ? 1 : 0;
    // A matriz de interpolação do renderizador segue a mesma chave que o guMtxF2L (gu_pc.c).
    Fast_SetQuantizeFloatMtx(!extended);
}

void LoadModsAndDispatchReady(const ShipLua::LuaApiHostContext& context) {
    Ship::Context* shipContext = Ship::Context::GetRawInstance();
    if (shipContext == nullptr) {
        SPDLOG_ERROR("ShipLua n\xC3\xA3o encontrou o contexto do aplicativo");
        return;
    }

    const std::string appName = shipContext->GetShortName();
    const std::filesystem::path modsRoot = Ship::Context::GetPathRelativeToAppDirectory("mods", appName);
    const std::filesystem::path cacheRoot = modsRoot / ".shiplua-cache";
    std::error_code error;
    std::filesystem::create_directories(modsRoot, error);
    if (error) {
        SPDLOG_ERROR("ShipLua n\xC3\xA3o conseguiu criar a pasta de mods '{}': {}", modsRoot.string(), error.message());
        return;
    }

    // Mod nativo que derrubar o boot fica em mods/.shiplua-disabled no próximo início.
    gModHost->EnableBootGuard(modsRoot);
    auto loaded = gModHost->LoadModsFromRoot(modsRoot, cacheRoot);
    if (!loaded.isOk()) {
        SPDLOG_ERROR("ShipLua n\xC3\xA3o conseguiu carregar a pasta de mods '{}': {}", modsRoot.string(),
                     loaded.message);
        return;
    }
    if (!loaded.value->loadedIds.empty()) SetEngineExtended(true);
    for (const auto& [modId, reason] : loaded.value->rejected) {
        SPDLOG_WARN("ShipLua rejeitou o mod '{}': {}", modId, reason);
    }
    const bool unboundReader = std::find(loaded.value->loadedIds.begin(), loaded.value->loadedIds.end(),
                                         kUnboundFrameworkId) != loaded.value->loadedIds.end();
    for (const auto& [archivePath, modDirectory] : gPendingUnboundArchives) {
        // Camada dentro da pasta de um mod só entra na raiz do VFS se esse mod carregou: um mod rejeitado não
        // pode sombrear cenas do jogo. O .o2r solto em mods/ é data mod e não tem manifesto.
        if (!modDirectory.empty()) {
            const auto manifest = ShipLua::ParseManifestFile((modDirectory / "manifest.toml").string());
            const bool accepted =
                manifest.isOk() && std::find(loaded.value->loadedIds.begin(), loaded.value->loadedIds.end(),
                                             manifest.value->id) != loaded.value->loadedIds.end();
            if (!accepted) {
                SPDLOG_WARN("ShipLua ignorou a camada Unbound '{}': o mod da pasta '{}' n\xC3\xA3o carregou",
                            archivePath.string(), modDirectory.string());
                continue;
            }
        }
        if (!unboundReader) {
            SPDLOG_WARN("ShipLua ignorou a camada Unbound '{}': o framework Unbound ('{}') n\xC3\xA3o carregou",
                        archivePath.string(), kUnboundFrameworkId);
            continue;
        }
        uint64_t handle = 0;
        const auto status = NativeMountResourceArchive(archivePath.string().c_str(), &handle);
        if (status == SHIP_NATIVE_OK) {
            SPDLOG_INFO("ShipLua montou camada Unbound '{}' na raiz do VFS", archivePath.string());
        } else {
            SPDLOG_WARN("ShipLua não conseguiu montar camada Unbound '{}' (status {})", archivePath.string(),
                        static_cast<int>(status));
        }
    }
    gPendingUnboundArchives.clear();
    SPDLOG_INFO("ShipLua carregou {} mod(s) de '{}'", loaded.value->loadedIds.size(), modsRoot.string());
    if (gMenuRegistry != nullptr) gMenuRegistry->SetLoadedMods(loaded.value->loadedIds);

    for (const std::string& modId : loaded.value->loadedIds) {
        ShipLua::LuaRuntime* runtime = gModHost->GetRuntime(modId);
        if (runtime != nullptr) {
            InstallOotApi(runtime->State());
        }
    }

    ShipLua::EventPayload payload{
        { "game_id", context.gameId },
        { "host_version", context.hostVersion },
        { "runtime_version", context.runtimeVersion },
        { "api_version", std::string(ShipLua::Generated::kApiVersion) },
    };
    auto ready = gModHost->DispatchEvent("game.ready", payload);
    if (!ready.isOk()) {
        SPDLOG_ERROR("ShipLua n\xC3\xA3o conseguiu publicar game.ready: {}", ready.message);
        return;
    }
    for (const ShipLua::CallbackFailure& failure : ready.value->failures) {
        SPDLOG_ERROR("ShipLua [{}] falhou em game.ready: {}", failure.modId, failure.message);
    }
}

} // namespace

// z_player_lib.c chama estes pontos enquanto as matrizes da mão e da cabeça
// ainda estão ativas. Os wrappers ficam fora do namespace anônimo para expor
// símbolos C reais ao translation unit do Player.
extern "C" void ShipLua_DrawMaskTransitionHand(PlayState* play, Player* player) {
    DrawMaskTransitionHandImpl(play, player);
}

extern "C" void ShipLua_DrawMaskTransitionHead(PlayState* play, Player* player) {
    DrawMaskTransitionHeadImpl(play, player);
}

void Initialize() {
    if (gModHost != nullptr) {
        SPDLOG_WARN("ShipLua j\xC3\xA1 foi inicializado");
        return;
    }
    // Pacote .o2r/.otr do SoH em mods/ também conta como mod.
    SetEngineExtended(HasModArchive());

    gHotkeys = std::make_shared<OotHotkeyRegistry>();
    gMenuRegistry = std::make_shared<SohGui::SohMenuModRegistry>();
    // Comandos de diagnóstico determinísticos. A automação de janela envia
    // eventos Win32 que o ImGui recebe, mas que podem escapar da amostragem do
    // SDL/N64; estes comandos exercitam o mesmo carregamento de arquivo e o
    // mesmo callback registrado pela hotkey, sem criar um segundo caminho de
    // transformação.
    auto console = Ship::Context::GetRawInstance()->GetConsole();
    console->AddCommand(
        "shiplua_goto_file_select",
        { [](std::shared_ptr<Ship::Console>, std::vector<std::string>, std::string* output) -> int32_t {
             if (gGameState == nullptr || gSaveContext.gameMode != GAMEMODE_TITLE_SCREEN) {
                 if (output != nullptr) {
                     *output = "use este comando somente na tela de titulo";
                 }
                 return 1;
             }
             gSaveContext.gameMode = GAMEMODE_FILE_SELECT;
             gGameState->running = false;
             SET_NEXT_GAMESTATE(gGameState, FileChoose_Init, FileChooseContext);
             if (output != nullptr) {
                 *output = "selecao de arquivo solicitada";
             }
             return 0;
         },
          "Vai para a tela de selecao usando a transicao nativa de GameState.", {} });
    console->AddCommand(
        "shiplua_open_file",
        { [](std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) -> int32_t {
             if (gGameState == nullptr || gSaveContext.gameMode != GAMEMODE_FILE_SELECT) {
                 if (output != nullptr) {
                     *output = "use este comando somente na tela de seleção de arquivo";
                 }
                 return 1;
             }
             int slot = 0;
             try {
                 if (args.size() >= 2) {
                     slot = std::stoi(args[1]);
                 }
             } catch (const std::exception&) {
                 if (output != nullptr) {
                     *output = "slot deve ser 0, 1 ou 2";
                 }
                 return 1;
             }
             if (slot < 0 || slot > 2) {
                 if (output != nullptr) {
                     *output = "slot deve ser 0, 1 ou 2";
                 }
                 return 1;
             }
             if (Save_Exist(slot) == 0) {
                 if (output != nullptr) {
                     *output = "slot vazio; nenhum arquivo foi aberto";
                 }
                 return 1;
             }
             auto* fileChoose = reinterpret_cast<FileChooseContext*>(gGameState);
             fileChoose->buttonIndex = slot;
             FileChoose_LoadGame(gGameState);
             if (output != nullptr) {
                 *output = "arquivo solicitado";
             }
             return 0;
         },
          "Abre um arquivo pelo mesmo caminho da tela de seleção.",
          { { "slot (0..2)", Ship::ArgumentType::NUMBER, true } } });
    console->AddCommand(
        "shiplua_fire_hotkey",
        { [](std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) -> int32_t {
             if (gHotkeys == nullptr || args.size() < 3) {
                 if (output != nullptr) {
                     *output = "uso: shiplua_fire_hotkey <mod_id> <hotkey_id>";
                 }
                 return 1;
             }
             gHotkeys->Fire(args[1], args[2]);
             if (output != nullptr) {
                 *output = "callback solicitado";
             }
             return 0;
         },
          "Dispara o callback já registrado por uma hotkey ShipLua.",
          { { "mod_id", Ship::ArgumentType::TEXT, false }, { "hotkey_id", Ship::ArgumentType::TEXT, false } } });
    // Viagem por nome de entrada (UNBOUND-007): vanilla (ENTR_*) ou de mod ("<cena>/<entrada>"), ou índice.
    console->AddCommand(
        "linkspan_travel",
        { [](std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) -> int32_t {
             const auto reply = [output](std::string text) {
                 if (output != nullptr) {
                     *output = std::move(text);
                 }
             };
             if (args.size() < 2) {
                 reply("uso: linkspan_travel <ENTR_*|cena/entrada|indice>");
                 return 1;
             }
             const auto& scenes = ShipLuaHost::GetOotNativeScenesService();
             int32_t index = -1;
             if (scenes.find_entrance(args[1].c_str(), &index) != SHIP_NATIVE_OK) {
                 index = -1;
                 try {
                     size_t used = 0;
                     const int value = std::stoi(args[1], &used, 0);
                     if (used == args[1].size()) {
                         index = value;
                     }
                 } catch (const std::exception&) {
                 }
             }
             if (index < 0 || index >= ShipLuaHost::OotEntranceCount()) {
                 reply("entrada desconhecida: " + args[1]);
                 return 1;
             }
             if (scenes.travel_to_entrance(index) != SHIP_NATIVE_OK) {
                 reply("viagem recusada: abra um save e espere a transicao terminar");
                 return 1;
             }
             reply("viajando para " + args[1] + " (" + std::to_string(index) + ")");
             return 0;
         },
          "Viaja para uma entrada pelo nome (ENTR_*, cena/entrada de mod) ou pelo indice.",
          { { "entrada", Ship::ArgumentType::TEXT, false } } });
    gCapabilityRegistry = std::make_shared<ShipLua::CapabilityRegistry>();
    gTimers = std::make_shared<ShipLua::FrameTimerScheduler>();
    gActorProvider = CreateActorProvider();
    const auto actorCapabilities = gActorProvider->RegisterCapabilities(*gCapabilityRegistry);
    if (!actorCapabilities.isOk()) {
        SPDLOG_ERROR("ShipLua failed to register the OoT actor provider: {}", actorCapabilities.message);
        gActorProvider.reset();
        gCapabilityRegistry.reset();
        gHotkeys.reset();
        return;
    }
    auto catalog = ShipLua::PortableItemCatalog::CreateDefault();
    if (!catalog.isOk()) {
        SPDLOG_ERROR("ShipLua não conseguiu criar o catálogo portátil OoT: {}", catalog.message);
        gActorProvider.reset();
        gCapabilityRegistry.reset();
        gHotkeys.reset();
        return;
    }
    gWorldAdapter = std::make_shared<OotWorldAdapter>(std::move(*catalog.value));
    gLoadGameHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t fileNum) {
        TryConsumeWorldHandoff();
        // OnLoadGame ainda roda na tela de arquivos, antes de Play_Init criar o
        // Player. Adie o evento público para o primeiro frame em que o payload
        // pode reagir ao save sem encontrar a API de gameplay indisponível.
        gPendingSaveLoadedSlot = fileNum;
    });
    gImportTickHook =
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
            // Boot confirmado depois de BOOT_CONFIRM_FRAMES frames de jogo: crashes posteriores
            // aparecem no log, mas não desativam mods.
            static constexpr uint32_t BOOT_CONFIRM_FRAMES = 300;
            static uint32_t bootFrames = 0;
            if (gModHost && bootFrames < BOOT_CONFIRM_FRAMES && ++bootFrames == BOOT_CONFIRM_FRAMES) {
                gModHost->ConfirmBoot();
                SPDLOG_INFO("Link-Span: boot confirmado; proteção de boot encerrada");
            }
            TickWorldImport();
            if (gPendingSaveLoadedSlot.has_value() && gPlayState != nullptr && GET_PLAYER(gPlayState) != nullptr) {
                const int32_t fileNum = *gPendingSaveLoadedSlot;
                gPendingSaveLoadedSlot.reset();
                DispatchHookEvent(
                    "save.loaded",
                    ShipLua::EventPayload{{ "slot", static_cast<std::int64_t>(fileNum) }});
            }
            DispatchDirectionalInput(gPlayState);
            // Avança a cutscene AQUI, não no update do Player: com atores
            // congelados (freeze_player) o hook do Player não dispara, e a
            // câmera ficaria parada e sem nunca ser devolvida.
            if (gPlayState != nullptr) {
                CutsceneUpdate(gPlayState);
                // A luz pontual precisa seguir o jogador; sem isto ficaria
                // parada onde foi criada.
                PointLightUpdate(gPlayState);
            }
            // Avança os timers de mod uma vez por frame. Sem isto, ship.timer
            // nunca dispara e qualquer mod que sequencie ações (animação e
            // depois efeito) trava no primeiro passo.
            // ImGui apenas enfileira input; persistencia e callbacks Lua rodam
            // aqui, na thread proprietaria do ModHost.
            if (gModHost != nullptr) {
                gModHost->PumpMenu();
            }
            if (gTimers != nullptr) {
                const auto ticked = gTimers->Tick();
                if (ticked.isOk()) {
                    for (const auto& failure : ticked.value->failures) {
                        SPDLOG_WARN("ShipLua [{}] timer falhou: {}", failure.modId, failure.message);
                    }
                    // O contrato comum publica game.frame com o mesmo contador
                    // que acabou de avançar no scheduler (mesma ordem do
                    // MockRuntime). Sem este dispatch, callbacks Lua por frame
                    // nunca rodam apesar de ship.timer continuar funcionando.
                    DispatchHookEvent(
                        "game.frame",
                        ShipLua::EventPayload{{"frame", static_cast<std::int64_t>(ticked.value->frame)}});
                }
            }
            // Grava o storage no máximo a cada ~5s, e só se algo mudou. Gravar
            // a cada ship.storage.set serializava o store inteiro e fazia um
            // write+rename síncronos na thread do jogo — um mod que atualize
            // contadores por segundo travava visivelmente, ainda mais com um
            // antivírus segurando o arquivo (o rename tenta de novo por dezenas
            // de ms). Flush() é no-op barato quando nada mudou.
            static int sFlushCountdown = 0;
            if (--sFlushCountdown <= 0) {
                sFlushCountdown = 300; // ~5s a 60fps
                if (gStorage != nullptr && gStorage->IsDirty()) {
                    const auto flushed = gStorage->Flush();
                    if (!flushed.isOk()) {
                        SPDLOG_WARN("ShipLua: falha ao gravar o storage: {}", flushed.message);
                    }
                }
                if (gSharedStorage != nullptr && gSharedStorage->IsDirty()) {
                    const auto flushed = gSharedStorage->Flush();
                    if (!flushed.isOk()) {
                        SPDLOG_WARN("ShipLua: falha ao gravar o storage compartilhado: {}", flushed.message);
                    }
                }
            }
        });
    // attach_model: substitui a DL da máscara-veículo pela do mod. O hook roda
    // já dentro do contexto de matriz da cabeça, então basta emitir a DL.
    // Imunidade a fogo: apaga o corpo em chamas antes que o dano contínuo seja
    // aplicado. Rodar por frame é o suficiente — Player_UpdateBodyBurn só age
    // enquanto bodyIsBurning estiver ligado.
    gFireImmunityHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>([]() {
        if (!gFireImmunity || gPlayState == nullptr) {
            return;
        }
        Player* player = GET_PLAYER(gPlayState);
        if (player != nullptr && player->bodyIsBurning) {
            player->bodyIsBurning = false;
            for (int i = 0; i < PLAYER_BODYPART_MAX; ++i) {
                player->bodyFlameTimers[i] = 0;
            }
        }
    });
    // Rolamento contínuo e dirigível, modelado no Player_Action_96 do MM:
    // - A enquanto corre inicia explicitamente o primeiro rolamento;
    // - o rolamento se re-arma sozinho enquanto houver direção no analógico
    //   (no MM não se aperta nada para continuar rolando);
    // - a direção acompanha o analógico de verdade;
    // - bater na parede continua caindo no "bonk" do próprio OoT, que roda
    //   antes deste ponto — é o "até bater em algo".
    // O primeiro hook usa a ação vanilla Player_Action_Roll; os demais fazem
    // o chain chamar Player_SetupRoll e o steer girar o player ele mesmo.
    gRollStartHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>([]() {
        // Com a maquina portada no comando, o A e dela: quem inicia a bola e
        // `TryStartRoll`. Deixar este hook rodar junto chamaria `Player_SetupRoll`
        // por baixo da fisica dela no mesmo frame.
        if (!gChainRoll || gPlayState == nullptr || MmFormOwnsRoll()) {
            return;
        }
        Player* player = GET_PLAYER(gPlayState);
        Input* input = &gPlayState->state.input[0];
        if (player == nullptr || input == nullptr || player->actionFunc == Player_Action_Roll) {
            return;
        }
        // Mantém a semântica do OoT: A só vira rolamento enquanto o jogador
        // está se deslocando no chão. Assim interações paradas continuam sendo
        // tratadas pelo jogo em vez de serem sequestradas pelo mod.
        if (!(input->press.button & BTN_A) || !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ||
            fabsf(player->linearVelocity) < 0.5f || (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE))) {
            return;
        }
        Player_SetupRoll(player, gPlayState);
    });
    gRollChainHook = REGISTER_VB_SHOULD(VB_PLAYER_ROLL_CHAIN, {
        Player* player = va_arg(args, Player*);
        PlayState* play = va_arg(args, PlayState*);
        Input* controlInput = va_arg(args, Input*);
        const s32 floorType = va_arg(args, s32);
        if (!gChainRoll || player == nullptr || play == nullptr || controlInput == nullptr || MmFormOwnsRoll()) {
            return;
        }
        // floorType 7 é a superfície onde o vanilla proíbe encadear.
        if ((player->skelAnime.curFrame < 15.0f) || (floorType == 7)) {
            return;
        }
        // Sem direção no analógico o rolamento termina naturalmente.
        const s32 stickX = controlInput->rel.stick_x;
        const s32 stickY = controlInput->rel.stick_y;
        if (((stickX * stickX) + (stickY * stickY)) < (kRollStickDeadzone * kRollStickDeadzone)) {
            return;
        }
        Player_SetupRoll(player, play);
        *should = true;
    });
    gRollSteerHook = REGISTER_VB_SHOULD(VB_PLAYER_ROLL_STEER, {
        Player* player = va_arg(args, Player*);
        [[maybe_unused]] PlayState* play = va_arg(args, PlayState*);
        const s16 yawTarget = static_cast<s16>(va_arg(args, int));
        if (!gChainRoll || player == nullptr || MmFormOwnsRoll()) {
            return;
        }
        // Passo maior que o do "improved roll" do SoH: o Goron do MM vira
        // rápido enquanto rola.
        Math_ScaledStepToS(&player->actor.shape.rot.y, yawTarget, kRollSteerStep);
        *should = false;
    });
    gMaskDrawHook = REGISTER_VB_SHOULD(VB_DRAW_PLAYER_MASK, {
        const bool transitionMask = gMaskTransitionPhase != MaskTransitionPhase::None;
        if (gAttachedHeadModel.empty() && !transitionMask) {
            return;
        }
        const uint32_t currentMask = va_arg(args, uint32_t);
        PlayState* play = va_arg(args, PlayState*);
        if (play == nullptr) {
            return;
        }
        if (!gAttachedHeadModel.empty()) {
            ShipLuaEmitDisplayList(play, gAttachedHeadModel.c_str());
            *should = false;
            return;
        }
        if (currentMask != PLAYER_MASK_GORON) {
            return;
        }

        // A DL MM é emitida dentro do callback de PLAYER_LIMB_HEAD, onde a
        // matriz correta ainda está ativa. Aqui apenas suprimimos a máscara
        // Goron do OoT para não desenhar as duas simultaneamente.
        *should = false;
    });

    // hook.oot.player.speed.run — VB_PLAYER_MODIFY_RUN_SPEED(Player*, f32* speedTarget).
    // O retorno booleano do hook é ignorado pelo próprio engine no call site
    // (chamada solta, sem if): só o *speedTarget mutado importa.
    gHookRunSpeedHook = REGISTER_VB_SHOULD(VB_PLAYER_MODIFY_RUN_SPEED, {
        va_arg(args, Player*);
        f32* speedTarget = va_arg(args, f32*);
        if (speedTarget == nullptr) {
            return;
        }
        const auto result =
            DispatchHookTransform("hook.oot.player.speed.run", ShipLua::EventPayload{
                                                                    {"speed", static_cast<double>(*speedTarget)},
                                                                });
        if (result.has_value() && std::holds_alternative<double>(result->value)) {
            *speedTarget = static_cast<f32>(std::get<double>(result->value));
        }
    });
    // hook.oot.player.fall_damage — VB_RECIEVE_FALL_DAMAGE(Actor*); default true
    // (dano aplicado). Lua devolvendo false via ship.hooks.result cancela o dano.
    gHookFallDamageHook = REGISTER_VB_SHOULD(VB_RECIEVE_FALL_DAMAGE, {
        va_arg(args, Actor*);
        const auto result = DispatchHookTransform("hook.oot.player.fall_damage", ShipLua::EventPayload{});
        if (result.has_value() && std::holds_alternative<bool>(result->value)) {
            *should = std::get<bool>(result->value);
        }
    });
    gHookItemReceiveHook =
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnItemReceive>([](GetItemEntry itemEntry) {
            DispatchHookEvent("hook.oot.item.receive",
                              ShipLua::EventPayload{
                                  {"item_id", static_cast<std::int64_t>(itemEntry.itemId)},
                                  {"get_item_id", static_cast<std::int64_t>(itemEntry.getItemId)},
                              });
        });
    gHookHealthChangeHook =
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerHealthChange>([](int16_t amount) {
            DispatchHookEvent("hook.oot.player.health_change",
                              ShipLua::EventPayload{{"amount", static_cast<std::int64_t>(amount)}});
        });
    gHookBonkHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerBonk>([]() {
        if (gPlayState != nullptr) {
            CustomBodyHandleRollWallBounce(GET_PLAYER(gPlayState), gPlayState);
        }
        DispatchHookEvent("hook.oot.player.bonk", ShipLua::EventPayload{});
    });
    // hook.oot.player.first_person_control — OnPlayerFirstPersonControl(Player*)
    // já existia (usado por Mouse.cpp), roda todo frame com mira em primeira
    // pessoa ativa. Achado minerando forks de item customizado: praticamente
    // todo item com overlay/comportamento de mira (Ball and Chain, Whip,
    // Gust Jar...) precisa saber "estou mirando agora" por frame.
    gHookFirstPersonHook =
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerFirstPersonControl>([](Player* player) {
            if (player == nullptr) {
                return;
            }
            DispatchHookEvent("hook.oot.player.first_person_control",
                              ShipLua::EventPayload{
                                  {"held_item_action", static_cast<std::int64_t>(player->heldItemAction)},
                              });
        });
    // hook.oot.player.arrow_type_select — VB_PLAYER_ARROW_MAGIC_CONSUMPTION
    // (Player*, s32 magicArrowType, s32* arrowType), já mutava arrowType por
    // referência antes de nós (é o próprio mecanismo vanilla de escolher
    // flecha de fogo/gelo/luz). Reaproveitado como ponto de escolha de
    // variante de projétil para mods, sem novo call site nativo.
    gHookArrowTypeSelectHook = REGISTER_VB_SHOULD(VB_PLAYER_ARROW_MAGIC_CONSUMPTION, {
        va_arg(args, Player*);
        int32_t magicArrowType = va_arg(args, int32_t);
        int32_t* arrowType = va_arg(args, int32_t*);
        if (arrowType == nullptr) {
            return;
        }
        const auto result = DispatchHookTransform(
            "hook.oot.player.arrow_type_select",
            ShipLua::EventPayload{
                {"magic_arrow_type", static_cast<std::int64_t>(magicArrowType)},
                {"arrow_type", static_cast<std::int64_t>(*arrowType)},
            });
        if (result.has_value() && std::holds_alternative<std::int64_t>(result->value)) {
            *arrowType = static_cast<int32_t>(std::get<std::int64_t>(result->value));
        }
    });
    // hook.oot.enemy.defeat — OnEnemyDefeat(Actor*), disparado por dezenas de
    // inimigos ao morrer (não é todo Actor_Kill; só derrota real). Fonte de XP
    // para um mod de RPG: o Lua consulta a tabela de recompensa dele pelo
    // actor_id e persiste o total via ship.storage.
    gHookEnemyDefeatHook =
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnEnemyDefeat>([](void* actorPtr) {
            Actor* actor = static_cast<Actor*>(actorPtr);
            if (actor == nullptr) {
                return;
            }
            DispatchHookEvent("hook.oot.enemy.defeat",
                              ShipLua::EventPayload{
                                  {"actor_id", static_cast<std::int64_t>(actor->id)},
                                  {"category", static_cast<std::int64_t>(actor->category)},
                                  {"pos_x", static_cast<double>(actor->world.pos.x)},
                                  {"pos_y", static_cast<double>(actor->world.pos.y)},
                                  {"pos_z", static_cast<double>(actor->world.pos.z)},
                              });
        });
    // OnPlayDrawEnd roda após o desenho do mundo (Player incluído) e antes do
    // HUD — bodyPartsPos já está resolvido para o frame atual nesse ponto.
    gHeldItemDrawHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDrawEnd>(
        []() {
            if (gPlayState != nullptr) {
                DrawHeldItemModels(gPlayState);
                DrawMaskTransitionFlash(gPlayState);
                // Por último: o overlay do mod fica por cima de tudo.
                DrawHudOverlay(gPlayState);
            }
        });

    gActorDestroyHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* actor) {
        ShipLuaPuppet_HandleActorDestroy(actor);
        if (gActorProvider == nullptr) {
            return;
        }
        const auto destroyed = gActorProvider->OnNativeActorDestroyed(actor);
        if (!destroyed.isOk()) {
            SPDLOG_ERROR("ShipLua failed to invalidate a destroyed OoT actor: {}", destroyed.message);
        }
    });
    gPlayDestroyHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDestroy>([]() {
        ShipLuaPuppet_Reset();
        CustomBodyResetForSceneChange();
        // O contexto de luzes é reconstruído na cena nova: manter o nó antigo
        // deixaria um ponteiro pendurado, e o override de ambiente apontaria
        // para settings que já não existem. Solta os dois aqui.
        gPointLightNode = nullptr;
        gPointLightActive = false;
        gEnvOverrideActive = false;
        if (gActorProvider == nullptr) {
            return;
        }
        const auto cleaned = gActorProvider->OnSceneChange();
        if (!cleaned.isOk()) {
            SPDLOG_ERROR("ShipLua failed to clean OoT actors during scene teardown: {}", cleaned.message);
        }
    });
    // Religa o corpo customizado sozinho na cena nova, se ele estava ativo na
    // cena anterior — resolve a limitação conhecida da v1 (corpo sumia em
    // toda troca de cena e exigia religar manualmente). OnPlayerUpdate (não
    // OnSceneInit) de propósito: scene init roda antes dos atores da sala
    // (Player incluído) serem spawnados — GET_PLAYER ainda seria inválido
    // ali. OnPlayerUpdate só dispara quando o Player já existe de verdade,
    // mesmo padrão já usado pelo hook de imunidade a fogo acima.
    gCustomBodyRestoreHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>([]() {
        if (!gCustomBodyPendingRestore || gPlayState == nullptr) {
            return;
        }
        if (CustomBodyActivateFromSpec(gPlayState)) {
            gCustomBodyPendingRestore = false;
        }
    });
    gMaskTransitionUpdateHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>([]() {
        if (gPlayState == nullptr || gMaskTransitionPhase == MaskTransitionPhase::None) {
            return;
        }
        Player* player = GET_PLAYER(gPlayState);
        if (player != nullptr) {
            MaskTransitionUpdate(player);
        }
    });
    auto contextResult = CreateHostContext();
    if (!contextResult.isOk()) {
        SPDLOG_ERROR("ShipLua failed to create the OoT host context: {}", contextResult.message);
        Shutdown();
        return;
    }
    ShipLua::LuaApiHostContext context = std::move(*contextResult.value);
    SPDLOG_INFO("ShipLua inicializando para {} {} (commit {})", context.gameId, context.hostVersion, gGitCommitHash);
    SetOotNativeGamepadBridge({ NativeHasGamepad, NativeGamepadButtons, NativeClearGamepadButtonBindings,
                                NativeBindGamepadButton, NativeReloadGamepadMappings, NativeGetSettingInt,
                                NativeSetSettingInt, NativeGamepadAxis, NativeBindGamepadAxis });
    SetOotNativeResourceBridge({ NativeHasResourceFile, NativeReadResourceFile, NativeListResourceFiles,
                                 NativeDirtyResources, NativeUnloadResource, NativeMountResourceArchive,
                                 NativeUnmountResourceArchive, NativeGetResourceGameVersions,
                                 NativeReadResourceFileLayers });
    gModHost = std::make_unique<ShipLua::ModHost>(context, CreateLogger(), CreateOotNativePolicy());
    SPDLOG_INFO("Link-Span: providers nativos (ABI {}.{}) e core extensions ativos; serviços linkspan.oot.*; "
                "pacotes ZIP/SHIPMOD", SHIP_NATIVE_ABI_MAJOR, SHIP_NATIVE_ABI_MINOR);
    MountCrossWorldArchives();
    MountModAssetArchives();
    // Diagnóstico da Fase 1 do port de áudio do MM (handoff OOT-AUDIO-001).
    // Só escreve no log; sai daqui assim que a Fase 1 fechar.
    ShipLua::ProbeMmSoundFonts();
    LoadModsAndDispatchReady(context);
    SPDLOG_INFO("Link-Span: motor {}", gLinkSpanEngineExtended ? "alargado (ha mod)" : "do upstream (sem mods)");
    SPDLOG_INFO("ShipLua inicializado");
}

void Shutdown() {
    if (gModHost == nullptr && gActorProvider == nullptr) {
        return;
    }

    Player* player = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
    if (gMaskForced) {
        MaskTransitionClearForcedMask(player);
    }
    if (gMaskTransitionPhase != MaskTransitionPhase::None) {
        MaskTransitionReset(player);
    }

    if (gActorProvider != nullptr) {
        const auto cleaned = gActorProvider->Shutdown();
        if (!cleaned.isOk()) {
            SPDLOG_ERROR("ShipLua failed to shut down the OoT actor provider: {}", cleaned.message);
        }
    }
    gModHost.reset();
    ResetOotNativeRegistry();
    ResetOotNativeScenes();
    ResetOotHooks();
    ResetOotNativeSave();
    ResetOotNativeItems();
    ResetOotNativeView();
    ResetOotNativeLights();
    ResetOotNativeWorld();
    ResetOotNativeSkeletons();
    InitializeOotNativeJsonTypes(std::thread::id{});
    InitializeOotNativeText(std::thread::id{});
    ClearNativeResourceArchives();
    SetOotNativeGamepadBridge({});
    SetOotNativeResourceBridge({});
    if (gLoadGameHook != 0) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnLoadGame>(gLoadGameHook);
        gLoadGameHook = 0;
    }
    if (gImportTickHook != 0) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnGameFrameUpdate>(gImportTickHook);
        gImportTickHook = 0;
    }
    gPendingSaveLoadedSlot.reset();
    gPendingHandoff.reset();
    if (gActorDestroyHook != 0) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnActorDestroy>(gActorDestroyHook);
        gActorDestroyHook = 0;
    }
    if (gPlayDestroyHook != 0) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayDestroy>(gPlayDestroyHook);
        gPlayDestroyHook = 0;
    }
    if (gCustomBodyRestoreHook != 0) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayerUpdate>(gCustomBodyRestoreHook);
        gCustomBodyRestoreHook = 0;
    }
    if (gMaskTransitionUpdateHook != 0) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnPlayerUpdate>(gMaskTransitionUpdateHook);
        gMaskTransitionUpdateHook = 0;
    }
    // Última chance de gravar: a persistência é adiada, então sem isto os
    // segundos finais de jogo se perderiam ao fechar.
    for (const auto& store : { gStorage, gSharedStorage }) {
        if (store != nullptr && store->IsDirty()) {
            const auto flushed = store->Flush();
            if (!flushed.isOk()) {
                SPDLOG_WARN("ShipLua: falha ao gravar o storage no shutdown: {}", flushed.message);
            }
        }
    }
    gStorage.reset();
    gSharedStorage.reset();
    gActorProvider.reset();
    gCapabilityRegistry.reset();
    gWorldAdapter.reset();
    gMenuRegistry.reset();
    gHotkeys.reset();
    gTimers.reset();
    SPDLOG_INFO("ShipLua finalizado");
}

ShipLua::ModHost* GetModHost() {
    return gModHost.get();
}

OotActorProvider* ActorProvider() {
    return gActorProvider.get();
}

OotHotkeyRegistry* Hotkeys() {
    return gHotkeys.get();
}

OotWorldAdapter* WorldAdapter() {
    return gWorldAdapter.get();
}

SohGui::SohMenuModRegistry* ModMenuRegistry() {
    return gMenuRegistry.get();
}

void OpenLogWindow() {
#ifdef _WIN32
    const std::filesystem::path log = RuntimeRoot() / "logs" / "Ship of Harkinian.log";
    std::filesystem::create_directories(log.parent_path());
    std::wstring command =
        L"powershell.exe -NoLogo -NoProfile -NoExit -Command \"$host.UI.RawUI.WindowTitle='Link-Span - log OoT'; "
        L"Write-Host 'Aguardando o log de OoT...'; while(-not (Test-Path -LiteralPath " +
        QuotePowerShellLiteral(log.wstring()) + L")){Start-Sleep -Milliseconds 250}; Get-Content -LiteralPath " +
        QuotePowerShellLiteral(log.wstring()) + L" -Tail 200 -Wait\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                        RuntimeRoot().c_str(), &startup, &process)) {
        SPDLOG_ERROR("ShipLua não conseguiu abrir a janela de log (erro {})", GetLastError());
        return;
    }
    SPDLOG_INFO("ShipLua abriu uma nova janela para acompanhar o log OoT");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    SPDLOG_WARN("ShipLua OpenLogWindow só está disponível no Windows");
#endif
}

} // namespace ShipLuaHost
