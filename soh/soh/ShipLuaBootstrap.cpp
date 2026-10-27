#include "ShipLuaBootstrap.h"
#include "native/OotNativeEngine.h"
#include "native/OotPrefixedArchive.h"
#include "native/OotNativeRegistry.h"
#include "native/OotNativeScenes.h"
#include "native/OotNativeHooks.h"
#include "native/OotNativeAudioMix.h"
#include "native/OotNativeItems.h"
#include "native/OotNativeView.h"
#include "native/OotNativeLights.h"
#include "native/OotNativeWorld.h"
#include "native/OotNativePlayerQuads.h"
#include "native/OotNativeSkeletons.h"
#include "native/OotNativePlayerBody.h"
#include "native/OotNativeJsonTypes.h"
#include "native/OotNativeText.h"
#include "native/OotNativeSave.h"
#include "OotActorProvider.h"
#include "OotHotkeyRegistry.h"
#include "OotWorldAdapter.h"
#include "SohGui/SohMenuModRegistry.h"
#include "soh/Enhancements/item-tables/ItemTableTypes.h"
#include "soh/Enhancements/item-tables/ItemTableManager.h"
#include "soh/SaveManager.h"
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
#include <ship/utils/StringHelper.h>
#include <shiplua/attributes/AttributeRegistry.h>
#include <shiplua/generated/ApiBindings.h>
#include <shiplua/host/ModHost.h>
#include <shiplua/manifest/ManifestParser.h>
#include <shiplua/runtime/LuaRuntime.h>
#include <shiplua/storage/AtomicFile.h>
#include <shiplua/storage/KeyValueStorage.h>
#include <shiplua/save/ModSaveStore.h>
#include <limits>
#include <shiplua/timer/FrameTimerScheduler.h>
#include <shiplua/world/WorldHandoff.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageInterfaceAddon.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/ShipUtils.h"
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
void Player_Action_Roll(Player* player, PlayState* play);

// Emite uma display list arbitrária no buffer opaco. Precisa de linkage C: o
// macro OPEN_DISPS declara FrameInterpolation_* em escopo de bloco, e dentro
// de código C++ isso vira um símbolo mangled que não existe.
// Declaradas em z_player.c; iniciam e identificam o rolamento nativo do OoT.
// O parâmetro se chama "this" no C original — aqui precisa de outro nome.
// Animacao de espera corrente do Player (z_player.c:2113, nao e static). Usada
// para devolver a pose quando a cutscene da mascara solta o corpo.
// Solta o jogador da escada e o deixa cair: limpa CLIMBING_LADDER, transiciona
// a ação e dá um empurrão para trás. É o mesmo caminho que o engine usa quando
// o jogador larga a escada (z_player.c:7890) — não é invenção nossa.
void func_8083FB7C(Player* player, PlayState* play);
void Player_Action_80845EF8(Player* player, PlayState* play);
void FileChoose_LoadGame(GameState* gameState);

static void ShipLuaEmitDisplayList(PlayState* play, const char* path) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)path);
    CLOSE_DISPS(play->state.gfxCtx);
}
}

// OOT-VANILLA-001: lida pelas funções inline de soh/src/code/linkspan_vanilla.h; 1 quando algum mod carregou no boot.
extern "C" {
uint8_t gLinkSpanEngineExtended = 0;
void Fast_SetQuantizeFloatMtx(bool on);
}

extern "C" void LinkSpan_ActorHealthDestroy(Actor* actor);
void LinkSpan_ActorHealthReset();
bool LinkSpan_ActorHealthInfo(Actor* actor, uint32_t* health, uint32_t* maxHealth, bool* extended);
ShipLua::Result<bool> LinkSpan_SetActorMaxHealth(Actor* actor, uint32_t maxHealth);

namespace ShipLuaHost {
namespace {

std::unique_ptr<ShipLua::ModHost> gModHost;
std::vector<std::filesystem::path> gToolDirectories; // RFC 0032: --linkspan-tool
std::shared_ptr<OotActorProvider> gActorProvider;
const std::function<bool(void*)>* gActorSpawnBinder = nullptr;

extern "C" const void* ShipLua_TakeActorSpawnBinding(void) {
    const auto* bind = gActorSpawnBinder;
    gActorSpawnBinder = nullptr;
    return bind;
}

extern "C" int ShipLua_BindSpawnedActor(const void* binding, Actor* actor) {
    const auto* bind = static_cast<const std::function<bool(void*)>*>(binding);
    try {
        return bind == nullptr || (*bind)(actor);
    } catch (...) { return false; }
}

extern "C" void ShipLua_FinishActorSceneTeardown(void) {
    if (gLinkSpanEngineExtended == 0) {
        return;
    }
    try {
        if (gActorProvider != nullptr) {
            (void)gActorProvider->FinishSceneTeardown();
        }
    } catch (...) {
        // Nenhuma exceção atravessa a fronteira C.
    }
    try {
        LinkSpan_ActorHealthReset();
    } catch (...) {
        // Mantém a fronteira C mesmo se a implementação da conta mudar.
    }
}
// Observe só quando algum mod recebe eventos; falha de handle omite o snapshot.
ShipLua::EventValue::Object ActorSnapshot(Actor* actor) {
    if (actor == nullptr || gActorProvider == nullptr || gModHost == nullptr || gModHost->SubscriptionCount() == 0) {
        return {};
    }
    const auto observed = gActorProvider->Observe(actor);
    if (!observed.isOk()) {
        SPDLOG_WARN("ShipLua: snapshot de ator indisponível: {}", observed.message);
        return {};
    }
    const auto& handle = *observed.value;
    return {
        { "handle",
          ShipLua::EventValue::Object{
              { "kind", ShipLua::HandleKindName(handle.kind) },
              { "slot", static_cast<std::int64_t>(handle.slot) },
              { "generation", static_cast<std::int64_t>(handle.generation) },
              { "scene_generation", static_cast<std::int64_t>(handle.sceneGeneration) },
          } },
        { "actor_id", static_cast<std::int64_t>(actor->id) },
        { "category", static_cast<std::int64_t>(actor->category) },
        { "params", static_cast<std::int64_t>(actor->params) },
    };
}

void AddActorSnapshot(ShipLua::EventPayload& payload, const char* field, Actor* actor) {
    auto snapshot = ActorSnapshot(actor);
    if (!snapshot.empty()) {
        payload.emplace(field, std::move(snapshot));
    }
}
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

ShipNativeStatus NativeReadResourceFile(const char* path, uint8_t* output, uint32_t capacity, uint32_t* outputSize) {
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    const auto file = archives ? archives->LoadFile(path) : nullptr;
    if (!file || !file->IsLoaded || !file->Buffer || file->Buffer->size() > UINT32_MAX) {
        *outputSize = 0;
        return SHIP_NATIVE_UNSUPPORTED;
    }
    *outputSize = static_cast<uint32_t>(file->Buffer->size());
    if (!output && capacity == 0)
        return SHIP_NATIVE_OK;
    if (capacity < *outputSize)
        return SHIP_NATIVE_LIMIT;
    if (*outputSize)
        std::memcpy(output, file->Buffer->data(), *outputSize);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeListResourceFiles(const char* mask, ShipOotResourcePathFn callback, void* user) {
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    const auto files = archives ? archives->ListFiles(mask) : nullptr;
    if (!files)
        return SHIP_NATIVE_UNSUPPORTED;
    auto ordered = *files;
    std::sort(ordered.begin(), ordered.end());
    for (const auto& path : ordered) {
        if (path.size() > UINT32_MAX)
            return SHIP_NATIVE_LIMIT;
        const auto status = callback(user, path.c_str(), static_cast<uint32_t>(path.size()));
        if (status != SHIP_NATIVE_OK)
            return status;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeDirtyResources(const char* mask) {
    const auto manager = NativeResourceManager();
    if (!manager)
        return SHIP_NATIVE_UNSUPPORTED;
    manager->DirtyResources(mask);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeUnloadResource(const char* path) {
    const auto manager = NativeResourceManager();
    if (!manager)
        return SHIP_NATIVE_UNSUPPORTED;
    manager->UnloadResource(path);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeMountResourceArchive(const char* archivePath, uint64_t* handle) {
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    if (!archives)
        return SHIP_NATIVE_UNSUPPORTED;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(archivePath, error).lexically_normal();
    if (error ||
        (!std::filesystem::is_regular_file(absolute, error) && !std::filesystem::is_directory(absolute, error))) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    // Barras normais: o FolderArchive casa o caminho base com a listagem em generic_string, e com barra
    // invertida a pasta montava sem nenhum arquivo.
    const auto mounted = archives->AddArchive(absolute.generic_string());
    if (!mounted)
        return SHIP_NATIVE_FAILURE;
    for (const auto& [hash, path] : *mounted->ListFiles()) {
        (void)hash;
        manager->UnloadResource(path);
    }
    const uint64_t next = gNextNativeResourceArchive++;
    if (!next)
        return SHIP_NATIVE_LIMIT;
    gNativeResourceArchives.emplace(next, mounted);
    *handle = next;
    return SHIP_NATIVE_OK;
}

// HOST-SLIM-001 (RFC 0029): linkspan.oot.resources V4. O handle é do mesmo mapa do mount_archive.
ShipNativeStatus NativeMountPrefixedResourceArchive(const ShipLuaHost::OotPrefixedMountRequest& request,
                                                    uint64_t* handle, ShipOotMountInfoV1* info) {
    *handle = 0;
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    if (!archives)
        return SHIP_NATIVE_UNSUPPORTED;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(request.archivePath, error).lexically_normal();
    if (error || !std::filesystem::is_regular_file(absolute, error) ||
        !StringHelper::IEquals(absolute.extension().string(), ".o2r"))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    const uint64_t next = gNextNativeResourceArchive;
    if (!next)
        return SHIP_NATIVE_LIMIT;
    const auto mounted =
        std::make_shared<ShipLuaHost::OotPrefixedArchive>(request, absolute.generic_string(), next, archives.get());
    mounted->Load();
    if (!mounted->IsLoaded())
        return mounted->Refusal() == SHIP_NATIVE_OK ? SHIP_NATIVE_FAILURE : mounted->Refusal();
    // AddArchive pode lançar depois de publicar parte do índice. A limpeza cobre também essa chamada.
    const auto rollback = [&] {
        try {
            mounted->Retire();
        } catch (...) {}
        try {
            archives->RemoveArchive(mounted);
        } catch (...) {}
    };
    try {
        if (!archives->AddArchive(mounted)) {
            rollback();
            return SHIP_NATIVE_FAILURE;
        }
        for (const auto& [hash, path] : *mounted->ListFiles()) {
            (void)hash;
            manager->UnloadResource(path);
        }
        if (!gNativeResourceArchives.emplace(next, mounted).second) {
            rollback();
            return SHIP_NATIVE_FAILURE;
        }
    } catch (...) {
        rollback();
        return SHIP_NATIVE_FAILURE;
    }
    ++gNextNativeResourceArchive;
    *handle = next;
    *info = mounted->Info();
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeUnmountResourceArchive(uint64_t handle) {
    const auto entry = gNativeResourceArchives.find(handle);
    if (entry == gNativeResourceArchives.end())
        return SHIP_NATIVE_INVALID_ARGUMENT;
    const auto mounted = entry->second;
    // HOST-SLIM-001: aposenta antes de consultar o manager. Falha posterior não reativa o transform.
    if (const auto prefixed = std::dynamic_pointer_cast<ShipLuaHost::OotPrefixedArchive>(mounted))
        prefixed->Retire();
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    if (!archives)
        return SHIP_NATIVE_UNSUPPORTED;
    const auto files = *mounted->ListFiles();
    if (archives->RemoveArchive(mounted) != 1)
        return SHIP_NATIVE_FAILURE;
    for (const auto& [hash, path] : files) {
        (void)hash;
        manager->UnloadResource(path);
    }
    SPDLOG_INFO("Link-Span desmontou archive {} de '{}'", handle, mounted->GetPath());
    gNativeResourceArchives.erase(entry);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus NativeGetResourceGameVersions(uint32_t* output, uint32_t capacity, uint32_t* outputCount) {
    const auto manager = NativeResourceManager();
    const auto archives = manager ? manager->GetArchiveManager() : nullptr;
    if (!archives)
        return SHIP_NATIVE_UNSUPPORTED;
    auto versions = archives->GetGameVersions();
    std::sort(versions.begin(), versions.end());
    versions.erase(std::unique(versions.begin(), versions.end()), versions.end());
    if (versions.size() > UINT32_MAX)
        return SHIP_NATIVE_LIMIT;
    *outputCount = static_cast<uint32_t>(versions.size());
    if (!output && capacity == 0)
        return SHIP_NATIVE_OK;
    if (capacity < *outputCount)
        return SHIP_NATIVE_LIMIT;
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
    if (!archives)
        return SHIP_NATIVE_UNSUPPORTED;

    struct Layer {
        std::shared_ptr<Ship::Archive> archive;
        std::shared_ptr<Ship::File> file;
    };
    std::vector<Layer> layers;
    for (const auto& archive : *archives) {
        if (!archive || !archive->HasFile(path))
            continue;
        auto file = archive->LoadFile(path);
        if (file && file->IsLoaded && file->Buffer && file->Buffer->size() <= UINT32_MAX) {
            layers.push_back({ archive, std::move(file) });
        }
    }
    if (layers.empty() || layers.size() > UINT32_MAX)
        return SHIP_NATIVE_UNSUPPORTED;

    const uint32_t layerCount = static_cast<uint32_t>(layers.size());
    for (uint32_t i = 0; i < layerCount; ++i) {
        const auto& archive = layers[i].archive;
        const auto& buffer = *layers[i].file->Buffer;
        const auto* data = reinterpret_cast<const uint8_t*>(buffer.data());
        const auto& archivePath = archive->GetPath();
        if (archivePath.size() > UINT32_MAX)
            return SHIP_NATIVE_LIMIT;
        const ShipOotResourceLayerV2 info{ sizeof(ShipOotResourceLayerV2),
                                           i,
                                           layerCount,
                                           archive->HasGameVersion() ? archive->GetGameVersion() : 0,
                                           NativeResourceContentHash(data, buffer.size()),
                                           static_cast<uint32_t>(buffer.size()),
                                           static_cast<uint32_t>(archivePath.size()) };
        const auto status = callback(user, &info, archivePath.c_str(), data);
        if (status != SHIP_NATIVE_OK)
            return status;
    }
    return SHIP_NATIVE_OK;
}

void ClearNativeResourceArchives() {
    std::vector<uint64_t> handles;
    for (const auto& [handle, archive] : gNativeResourceArchives) {
        (void)archive;
        handles.push_back(handle);
    }
    for (const auto handle : handles)
        NativeUnmountResourceArchive(handle);
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

// Opções do SoH registram hooks COND_* com o valor do CVar no ShipInit::InitAll, e só o menu chama ShipInit::Init
// ao mudar o valor. O que o Link-Span grava por código (mods nativos, API Lua) entra nesta fila, reaplicada no
// começo e no fim do frame por LinkSpan_ApplyPendingSettings (game.c), fora de despacho do GameInteractor:
// registrar um hook no mesmo mapa durante o despacho dele invalidaria o iterador. Antes do InitAll não roda frame;
// a primeira volta só reaplica o que o InitAll acabou de registrar.
std::vector<std::string> gPendingShipInit;

void SetSohCVarInt(const char* name, int32_t value) {
    const bool changed = CVarGetInteger(name, ~value) != value;
    CVarSetInteger(name, value);
    if (changed && std::find(gPendingShipInit.begin(), gPendingShipInit.end(), name) == gPendingShipInit.end()) {
        gPendingShipInit.emplace_back(name);
    }
}

ShipNativeStatus NativeSetSettingInt(const char* name, int32_t value) {
    SetSohCVarInt(name, value);
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
    hooks.spawn = [](const OotActorDefinition& definition, const ShipLua::ActorSpawnRequest& request,
                     const std::function<bool(void*)>& bind) -> void* {
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
        struct BindScope {
            const std::function<bool(void*)>* previous;
            explicit BindScope(const std::function<bool(void*)>& callback) : previous(gActorSpawnBinder) {
                gActorSpawnBinder = &callback;
            }
            ~BindScope() {
                gActorSpawnBinder = previous;
            }
        } scope(bind);
        Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, definition.actorId, x, y, z,
                                     DegreesToBinang(request.rotationX), rotationY, DegreesToBinang(request.rotationZ),
                                     definition.params);
        // Init pode ter removido o ator ou a cena; não desreferencia um retorno antigo.
        if (spawned != nullptr && !bind(spawned)) {
            return nullptr;
        }
        return spawned;
    };
    hooks.kill = [](void* actor) {
        if (actor != nullptr) {
            Actor_Kill(static_cast<Actor*>(actor));
        }
    };
    hooks.readField = [](void* native, const std::string& field) -> ShipLua::Result<ShipLua::ActorFieldValue> {
        using Value = ShipLua::ActorFieldValue;
        using Result = ShipLua::Result<Value>;
        auto* actor = static_cast<Actor*>(native);
        if (actor == nullptr || gPlayState == nullptr) {
            return Result::ok(Value{ std::monostate{} });
        }
        if (field == "actor_id") {
            return Result::ok(Value{ static_cast<std::int64_t>(actor->id) });
        }
        if (field == "category") {
            return Result::ok(Value{ static_cast<std::int64_t>(actor->category) });
        }
        if (field == "params") {
            return Result::ok(Value{ static_cast<std::int64_t>(actor->params) });
        }
        if (field == "health" || field == "max_health" || field == "extended") {
            uint32_t health = 0;
            uint32_t maxHealth = 0;
            bool extended = false;
            if (!LinkSpan_ActorHealthInfo(actor, &health, &maxHealth, &extended)) {
                return Result::ok(Value{ std::monostate{} });
            }
            if (field == "extended") {
                return Result::ok(Value{ extended });
            }
            return Result::ok(Value{ static_cast<std::int64_t>(field == "health" ? health : maxHealth) });
        }
        if (field == "pos_x" || field == "pos_y" || field == "pos_z") {
            return Result::ok(Value{ static_cast<double>(field == "pos_x"   ? actor->world.pos.x
                                                         : field == "pos_y" ? actor->world.pos.y
                                                                            : actor->world.pos.z) });
        }
        if (field == "screen_x" || field == "screen_y") {
            // W cru do ator: Actor_ProjectPos já o transforma e corta negativos.
            if (!(actor->projectedW > 0.0f)) {
                return Result::ok(Value{ std::monostate{} });
            }
            s16 x = 0;
            s16 y = 0;
            Actor_GetScreenPos(gPlayState, actor, &x, &y);
            return Result::ok(Value{ static_cast<std::int64_t>(field == "screen_x" ? x : y) });
        }
        return Result::err(ShipLua::ErrorCode::InvalidArgument, "unknown actor field");
    };
    hooks.setMaxHealth = [](void* actor, uint32_t maxHealth) {
        return LinkSpan_SetActorMaxHealth(static_cast<Actor*>(actor), maxHealth);
    };
    std::vector<OotActorDefinition> allowlist{
        { "oot.en_dog", ACTOR_EN_DOG, OBJECT_DOG, static_cast<std::int16_t>(0x8000) },
        { "oot.en_torch2", ACTOR_EN_TORCH2, OBJECT_TORCH2, 0 },
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
const char* CurrentGameStateMode();

bool IsReservedOotModSaveId(const std::string& modId) {
    // Mesma regra de IsHostName em native/OotNativeSave.cpp (namespace anônimo).
    return modId.rfind("linkspan.", 0) == 0;
}

class OotModSaveBackend final : public ShipLua::ModSaveBackend {
  public:
    bool Available() const override {
        return GetOotNativeSaveService().get_slot() >= 0 && gPlayState != nullptr;
    }

    ShipLua::Result<ShipLua::ModSaveValues> Load(const std::string& modId) override {
        using Result = ShipLua::Result<ShipLua::ModSaveValues>;
        if (!Available()) {
            return Result::err(ShipLua::ErrorCode::InvalidState, "no save file is loaded");
        }
        if (IsReservedOotModSaveId(modId)) {
            return Result::err(
                ShipLua::ErrorCode::InvalidArgument,
                "mod ids starting with 'linkspan.' are reserved for host save blocks; use another id prefix");
        }
        try {
            nlohmann::json data;
            uint32_t version = 0;
            ShipLua::ModSaveValues values;
            // O helper já extrai o objeto data, sem o envelope version/required.
            if (!ExportOotSaveNamespace(modId, data, version) || !data.is_object()) {
                return Result::ok(std::move(values));
            }
            for (const auto& [key, value] : data.items()) {
                if (value.is_boolean()) {
                    values.emplace(key, value.get<bool>());
                } else if (value.is_number_unsigned()) {
                    const auto number = value.get<uint64_t>();
                    if (number <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
                        values.emplace(key, static_cast<int64_t>(number));
                    }
                } else if (value.is_number_integer()) {
                    values.emplace(key, value.get<int64_t>());
                } else if (value.is_number_float()) {
                    const auto number = value.get<double>();
                    if (std::isfinite(number)) {
                        values.emplace(key, number);
                    }
                } else if (value.is_string()) {
                    values.emplace(key, value.get<std::string>());
                }
            }
            return Result::ok(std::move(values));
        } catch (...) { return Result::err(ShipLua::ErrorCode::HostFailure, "cannot read mod save data"); }
    }

    ShipLua::Result<void> Store(const std::string& modId, const ShipLua::ModSaveValues& values) override {
        using Result = ShipLua::Result<void>;
        if (!Available()) {
            return Result::err(ShipLua::ErrorCode::InvalidState, "no save file is loaded");
        }
        if (IsReservedOotModSaveId(modId)) {
            return Result::err(
                ShipLua::ErrorCode::InvalidArgument,
                "mod ids starting with 'linkspan.' are reserved for host save blocks; use another id prefix");
        }
        try {
            auto data = nlohmann::json::object();
            // Sem structured binding: o clang do Xcode 15 não deixa o lambda capturá-lo.
            for (const auto& entry : values) {
                std::visit([&](const auto& scalar) { data[entry.first] = scalar; }, entry.second);
            }
            if (!ReplaceOotSaveNamespace(modId, 1, std::move(data))) {
                return Result::err(ShipLua::ErrorCode::HostFailure, "cannot replace mod save namespace");
            }
            return Result::ok();
        } catch (...) { return Result::err(ShipLua::ErrorCode::HostFailure, "cannot write mod save data"); }
    }
};

PlayState* gSfxPlay = nullptr;
uint32_t gSfxFrame = 0;
uint32_t gSfxCalls = 0;

ShipLua::Result<bool> PlayHostSfx(std::int64_t id) {
    using Result = ShipLua::Result<bool>;
    try {
        if (id < 0 || id > 0xFFFF || std::strcmp(CurrentGameStateMode(), "gameplay") != 0) {
            return Result::ok(false);
        }
        if (gSfxPlay != gPlayState || gSfxFrame != gPlayState->gameplayFrames) {
            gSfxPlay = gPlayState;
            gSfxFrame = gPlayState->gameplayFrames;
            gSfxCalls = 0;
        }
        if (gSfxCalls >= 8) {
            return Result::ok(false);
        }
        ++gSfxCalls;
        Audio_PlaySfxGeneral(static_cast<u16>(id), &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        return Result::ok(true);
    } catch (...) { return Result::err(ShipLua::ErrorCode::HostFailure, "cannot play sound effect"); }
}

ShipLua::Result<bool> PlayHostFanfare(std::int64_t id) {
    using Result = ShipLua::Result<bool>;
    try {
        constexpr std::array<std::int64_t, 6> allowed{ 0x22, 0x24, 0x2B, 0x39, 0x3B, 0x43 };
        if (std::strcmp(CurrentGameStateMode(), "gameplay") != 0 ||
            std::find(allowed.begin(), allowed.end(), id) == allowed.end()) {
            return Result::ok(false);
        }
        Audio_PlayFanfare(static_cast<u16>(id));
        return Result::ok(true);
    } catch (...) { return Result::err(ShipLua::ErrorCode::HostFailure, "cannot play fanfare"); }
}

ShipLua::Result<ShipLua::LuaApiHostContext> CreateHostContext() {
    ShipLua::LuaApiHostContext context;
    context.gameId = "oot";
    context.resourceExists = [](const std::string& path) { return NativeHasResourceFile(path.c_str()) != 0; };
    context.hostVersion = GetHostVersion();
    context.capabilities = { "oot.player.jump",
                             "oot.player.mask",
                             "player.speed",
                             "player.fields",
                             "oot.player.attach_model",
                             "mod.assets",
                             "oot.player.immunity",
                             "oot.player.weight",
                             "oot.player.roll",
                             "hooks.bridge",
                             "oot.player.custom_body",
                             "oot.player.held_item_model",
                             "hud.draw",
                             "hud.icons",
                             "game.state",
                             "input.actions",
                             "oot.env",
                             "save.events",
                             "oot.cutscene" };
    context.hotkeys = gHotkeys;
    context.capabilityRegistry = gCapabilityRegistry;
    context.actors = gActorProvider;
    context.saveData = std::make_shared<ShipLua::ModSaveStore>(std::make_shared<OotModSaveBackend>());
    context.playSfx = PlayHostSfx;
    context.playFanfare = PlayHostFanfare;
    // actor.observe e actor.health são anunciadas pelo registry do provider (B1).
    context.capabilities.insert(context.capabilities.end(),
                                { "core.modules", "core.save", "audio.play", "actor.events" });
    context.timers = gTimers;
    context.setSpeedMultiplier = ApplyPlayerSpeedMultiplier;
    context.capabilities.push_back("core.timers");
    auto registered = RegisterHostCapability("oot.player.jump", "Apply a validated jump impulse to OoT Link.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    const std::pair<const char*, const char*> actorOffers[] = {
        { "core.modules", "Load Lua modules from the mod package." },
        { "core.save", "Persist scalar mod data in the loaded save file." },
        { "audio.play", "Play bounded sound effects and allowlisted fanfares during gameplay." },
        { "actor.events", "Publish actor.init and actor.destroy with instance handles." },
    };
    for (const auto& [id, description] : actorOffers) {
        registered = RegisterHostCapability(id, description);
        if (!registered.isOk()) {
            return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
        }
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
    registered = RegisterHostCapability("hud.draw",
                                        "Draw arbitrary rectangles and text over the HUD from hook.<game>.hud.draw.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability("hud.icons", "Draw validated resource textures as bounded HUD icons.");
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
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "oot.cutscene", "Take over the camera with a dedicated sub-camera for a bounded number of frames.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "oot.player.mask", "Equip any OoT mask by logical name with set_mask; no transformation choreography.");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }
    registered = RegisterHostCapability(
        "oot.player.custom_body",
        "Replace only the player's visual body with a mod-provided skeleton, animations and segments, restored "
        "across scene changes; grants no form abilities.");
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
    registered = RegisterHostCapability("mod.assets", "Mount mod-provided archives under the mod/<name>/ namespace.");
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
    registered = RegisterHostCapability("oot.player.roll", "Veto vanilla rolling with set_roll_blocked.");
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
            SPDLOG_WARN("ShipLua: storage persistente corrompido ({}), recomeçando vazio: {}", storagePath.string(),
                        loaded.message);
        }
        gStorage = storage;
        context.storage = storage;
        context.capabilities.push_back("core.storage");
        registered =
            RegisterHostCapability("core.storage", "Per-mod key-value storage persisted to disk across sessions.");
        if (!registered.isOk()) {
            return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
        }
    }

    if (gMenuRegistry != nullptr && context.storage != nullptr) {
        context.menu = gMenuRegistry;
        context.capabilities.push_back("core.menu");
        registered =
            RegisterHostCapability("core.menu", "Declarative per-mod menu pages backed exclusively by ship.storage.");
        if (!registered.isOk()) {
            return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
        }
    }

    // ship.attributes (RFC 0030): registro do core, passivo enquanto nenhum mod declara atributos. O ModHost liga o
    // serviço nativo linkspan.attributes a partir daqui.
    context.attributes = std::make_shared<ShipLua::AttributeRegistry>();
    context.capabilities.push_back("core.attributes");
    registered =
        RegisterHostCapability("core.attributes", "Item attributes, modifiers and tags shared across mods (RFC 0030).");
    if (!registered.isOk()) {
        return ShipLua::Result<ShipLua::LuaApiHostContext>::err(registered.code, registered.message);
    }

    // ship.storage.shared: store COMPARTILHADO entre os dois jogos, num arquivo
    // no diretório de sessão do launcher. Só existe sob o launcher (BridgeConfig);
    // standalone não tem para onde compartilhar, então fica ausente e degrada.
    if (const auto& bridge = GetBridgeConfig(); bridge.has_value()) {
        auto shared = std::make_shared<ShipLua::KeyValueStorage>();
        const std::filesystem::path sharedPath = bridge->sessionDirectory / "shared-storage.bin";
        const auto loaded = shared->EnablePersistence(sharedPath);
        if (!loaded.isOk()) {
            SPDLOG_WARN("ShipLua: storage compartilhado corrompido ({}), recomeçando vazio: {}", sharedPath.string(),
                        loaded.message);
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
    std::string skeletonPath;
    std::map<std::string, std::string> anims;
    std::map<int, std::string> segments;
    std::set<std::string> reverseAnims;
    std::string defaultAnim;
    f32 groundOffset = 0.0f;
    std::string skeletonPathPrefixed;
    std::map<std::string, std::string> animsPrefixed;
    std::map<int, std::string> segmentsPrefixed;
};

bool gCustomBodyActive = false;
Actor* gCustomBodyActor = nullptr;
SkelAnime gCustomBodySkelAnime{};
std::string gCustomBodyCurrentAnim;
CustomBodySpec gCustomBodySpec;

// ship.oot.player.set_roll_blocked(bool): veta o rolamento no portão nativo
// (Player_TryRoll). Um sistema de stamina precisa disto — só drenar o medidor
// não impede a ação; sem bloquear, rolar com a barra vazia continua saindo.
// Angulo acumulado da bola durante o rolamento. Vive aqui, e nao numa estatica
// dentro do draw, para poder ser zerado ao sair do rolamento.

bool gRollBlocked = false;

// Estilos da cutscene genérica: "jump" e "orbit".
enum class CutsceneStyle { Jump, Orbit };
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
        // Devolve o controle à câmera principal antes de descartar a nossa.
        func_800C08AC(play, gCutsceneCamId, 0);
        gCutsceneCamId = 0;
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
            play->envCtx.lightSettings.fogNear =
                static_cast<s16>(nearValue < 0 ? 0 : (nearValue > 1000 ? 1000 : nearValue));
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
    const auto result =
        DispatchHookTransform("hook.oot.item.give", ShipLua::EventPayload{
                                                        { "item_id", static_cast<std::int64_t>(entry->itemId) },
                                                        { "get_item_id", static_cast<std::int64_t>(entry->getItemId) },
                                                    });
    if (!result.has_value() || !std::holds_alternative<std::int64_t>(result->value)) {
        return;
    }
    const std::int64_t replacement = std::get<std::int64_t>(result->value);
    if (replacement == entry->getItemId || replacement <= 0 || replacement > 0xFFFF) {
        return;
    }
    GetItemEntry replaced = ItemTableManager::Instance->RetrieveItemEntry(MOD_NONE, static_cast<uint16_t>(replacement));
    if (replaced.itemId != ITEM_NONE) {
        *entry = replaced;
    }
}

// hook.oot.actor.damage: depois dos hooks nativos e antes de somar o dano ao alvo.
// Só um resultado numérico finito substitui damage; bool e string não são números.
extern "C" float ShipLua_TransformActorDamage(Actor* target, Actor* attacker, uint32_t dmgFlags, uint8_t damageEffect,
                                              uint8_t attackerIsPlayer, float baseDamage, float damage) {
    if (gModHost == nullptr || target == nullptr || gModHost->SubscriptionCount() == 0) {
        return damage;
    }
    try {
        uint32_t health = 0;
        uint32_t maxHealth = 0;
        bool extended = false;
        LinkSpan_ActorHealthInfo(target, &health, &maxHealth, &extended);
        ShipLua::EventPayload payload{
            { "target_id", static_cast<std::int64_t>(target->id) },
            { "target_category", static_cast<std::int64_t>(target->category) },
            { "attacker_id", attacker != nullptr ? static_cast<std::int64_t>(attacker->id) : std::int64_t{ -1 } },
            { "attacker_category",
              attacker != nullptr ? static_cast<std::int64_t>(attacker->category) : std::int64_t{ -1 } },
            { "attacker_is_player", attackerIsPlayer != 0 },
            { "dmg_flags", static_cast<std::int64_t>(dmgFlags) },
            { "damage_effect", static_cast<std::int64_t>(damageEffect) },
            { "base_damage", static_cast<double>(baseDamage) },
            { "damage", static_cast<double>(damage) },
            { "target_health", static_cast<std::int64_t>(health) },
            { "target_max_health", static_cast<std::int64_t>(maxHealth) },
            { "target_extended", extended },
        };
        AddActorSnapshot(payload, "target", target);
        if (attacker != nullptr) {
            AddActorSnapshot(payload, "attacker", attacker);
        }
        const auto result = DispatchHookTransform("hook.oot.actor.damage", std::move(payload));
        if (!result.has_value()) {
            return damage;
        }
        double replacement = 0.0;
        if (const auto* integer = std::get_if<std::int64_t>(&result->value)) {
            replacement = static_cast<double>(*integer);
        } else if (const auto* number = std::get_if<double>(&result->value)) {
            replacement = *number;
        } else {
            return damage;
        }
        if (!std::isfinite(replacement)) {
            return damage;
        }
        // Corte antes do cast, nas unidades reais do alvo.
        return static_cast<float>(std::clamp(replacement, 0.0, extended ? 65535.0 : 255.0));
    } catch (...) { return damage; }
}

std::optional<std::string> gCustomBodyOverrideAnim;
bool gCustomBodyOverrideOnce = false;
bool gCustomBodyPendingRestore = false;

extern "C" u8 ShipLua_CustomBodyInUse(void) {
    return gCustomBodyActive || gCustomBodyPendingRestore;
}

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
}

void CustomBodyDeactivate(PlayState* play, bool clearSpec) {
    if (gCustomBodyActive && play != nullptr) {
        if (Player* player = GET_PLAYER(play); player != nullptr) {
            player->stateFlags2 &= ~PLAYER_STATE2_DISABLE_DRAW;
        }
        if (gCustomBodyActor != nullptr) {
            Actor_Kill(gCustomBodyActor);
        }
    }
    gCustomBodyActive = false;
    gCustomBodyActor = nullptr;
    CustomBodyFreeSkelBuffers();
    gCustomBodyCurrentAnim.clear();
    gCustomBodyOverrideAnim.reset();
    gCustomBodyOverrideOnce = false;
    if (clearSpec) {
        gCustomBodySpec = CustomBodySpec{};
        gCustomBodyPendingRestore = false;
    }
}

void CustomBodyResetForSceneChange() {
    gCutsceneActive = false;
    gCutsceneCamId = 0;
    gCutsceneElapsed = 0;
    gCustomBodyPendingRestore = gCustomBodyActive;
    gCustomBodyActive = false;
    gCustomBodyActor = nullptr;
    // A arena da cena já foi descartada pelo jogo.
    gCustomBodySkelAnime = {};
    gCustomBodyCurrentAnim.clear();
    gCustomBodyOverrideAnim.reset();
    gCustomBodyOverrideOnce = false;
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

bool CustomBodyActivateFromSpec(PlayState* play);

extern "C" void CustomBodyActorUpdate(Actor* actor, PlayState* play) {
    if (!gCustomBodyActive) {
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        CustomBodyDeactivate(play, false);
        return;
    }
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
    actor->world.pos = player->actor.world.pos;
    actor->shape.rot = player->actor.shape.rot;
    Actor_SetScale(actor, 0.01f);
    actor->shape.yOffset = gCustomBodySpec.groundOffset / actor->scale.y;

    std::string wanted;
    const f32 speed = fabsf(player->linearVelocity);
    const bool overridden = gCustomBodyOverrideAnim.has_value();
    const bool climbing = (player->stateFlags1 & PLAYER_STATE1_CLIMBING_LADDER) != 0;
    const s16 climbInput = play->state.input[0].rel.stick_y;
    // Escada: com os nomes convencionais declarados na spec, a pose segue o passo do Player antes de qualquer
    // override (que fica guardado para depois da escada). Sem eles, a escolha cai no hook Lua normalmente.
    const char* climbAnim = nullptr;
    if (climbing) {
        const bool rightStep = (player->av2.actionVar2 & 1) != 0;
        if (player->av2.actionVar2 < 0) {
            climbAnim = player->av2.actionVar2 == -4 ? "climb_start_b" : "climb_start_a";
        } else if (climbInput > 0) {
            climbAnim = rightStep ? "climb_up_r" : "climb_up_l";
        } else if (climbInput < 0) {
            climbAnim = rightStep ? "climb_down_r" : "climb_down_l";
        } else {
            climbAnim = "climb_wait";
        }
    }
    const bool climbOwnsPose = climbAnim != nullptr && gCustomBodySpec.anims.count(climbAnim) != 0;
    if (climbOwnsPose) {
        wanted = climbAnim;
    } else if (overridden) {
        wanted = *gCustomBodyOverrideAnim;
    } else {
        const bool handleDoorOpening = player->actionFunc == Player_Action_80845EF8 && player->doorActor != nullptr &&
                                       player->doorType == PLAYER_DOORTYPE_HANDLE;
        const bool chestOpening = (player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) != 0 &&
                                  player->interactRangeActor != nullptr &&
                                  player->interactRangeActor->id == ACTOR_EN_BOX;
        const auto result =
            DispatchHookTransform("hook.oot.player.body_anim_select",
                                  ShipLua::EventPayload{
                                      { "speed", static_cast<double>(speed) },
                                      { "on_ground", (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) != 0 },
                                      { "falling", (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) == 0 &&
                                                       player->actor.velocity.y < 0.0f },
                                      { "landing", (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND_TOUCH) != 0 },
                                      { "climbing", climbing },
                                      { "climb_direction", std::string(!climbing        ? "none"
                                                                       : climbInput > 0 ? "up"
                                                                       : climbInput < 0 ? "down"
                                                                                        : "idle") },
                                      { "climb_step", static_cast<std::int64_t>(player->av2.actionVar2 & 1) },
                                      { "climb_starting", climbing && player->av2.actionVar2 < 0 },
                                      { "rolling", player->actionFunc == Player_Action_Roll },
                                      { "door_opening", handleDoorOpening },
                                      { "door_direction", std::string(!handleDoorOpening          ? "none"
                                                                      : player->doorDirection < 0 ? "left"
                                                                                                  : "right") },
                                      { "chest_opening", chestOpening },
                                      { "instrument", (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) != 0 },
                                      { "attacking", player->meleeWeaponState != 0 },
                                      { "shielding", (player->stateFlags1 & PLAYER_STATE1_SHIELDING) != 0 },
                                      { "attack_animation", static_cast<std::int64_t>(player->meleeWeaponAnimation) },
                                  });
        if (result.has_value() && std::holds_alternative<std::string>(result->value)) {
            const auto& requested = std::get<std::string>(result->value);
            if (gCustomBodySpec.anims.count(requested)) {
                wanted = requested;
            }
        }
    }
    if (wanted.empty()) {
        wanted = speed > 4.0f && gCustomBodySpec.anims.count("run")    ? "run"
                 : speed > 0.5f && gCustomBodySpec.anims.count("walk") ? "walk"
                                                                       : gCustomBodySpec.defaultAnim;
    }
    if (wanted != gCustomBodyCurrentAnim) {
        const auto previousOverride = gCustomBodyOverrideAnim;
        const bool previousOnce = gCustomBodyOverrideOnce;
        if (CustomBodyStartAnimation(play, wanted, 1.0f, false, gCustomBodySpec.reverseAnims.count(wanted) != 0)) {
            if (climbOwnsPose) {
                gCustomBodyOverrideAnim = previousOverride;
                gCustomBodyOverrideOnce = previousOnce;
            } else if (!overridden) {
                gCustomBodyOverrideAnim.reset();
                gCustomBodyOverrideOnce = false;
            }
        }
    }
    if (climbing) {
        gCustomBodySkelAnime.playSpeed = player->skelAnime.playSpeed;
    }
    if (LinkAnimation_Update(play, &gCustomBodySkelAnime) && gCustomBodyOverrideOnce && !climbOwnsPose) {
        gCustomBodyOverrideAnim.reset();
        gCustomBodyOverrideOnce = false;
    }
    // LinkAnimation_Update só enfileira a pose; a fila roda depois de Actor_UpdateAll. MOVEACTOR enfileirado
    // depois dela neutraliza jointTable[0] antes do draw: UPDATEY devolve a raiz vertical a baseTransl e NOMOVE
    // impede que a translação de uma animação declarada por Lua mova o Player ou desloque a malha.
    gCustomBodySkelAnime.movementFlags |= ANIM_FLAG_UPDATEY | ANIM_FLAG_NOMOVE;
    AnimationContext_SetMoveActor(play, &player->actor, &gCustomBodySkelAnime, 0.0f);
}

extern "C" void CustomBodyActorDraw(Actor* actor, PlayState* play) {
    if (!gCustomBodyActive) {
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        return;
    }
    actor->world.pos = player->actor.world.pos;
    actor->shape.rot = player->actor.shape.rot;
    Matrix_SetTranslateRotateYXZ(actor->world.pos.x, actor->world.pos.y + actor->shape.yOffset * actor->scale.y,
                                 actor->world.pos.z, &actor->shape.rot);
    Matrix_Scale(actor->scale.x, actor->scale.y, actor->scale.z, MTXMODE_APPLY);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    for (const auto& [segment, path] : gCustomBodySpec.segmentsPrefixed) {
        if (char* data = ResourceMgr_GetResourceDataByNameHandlingMQ(path.c_str()); data != nullptr) {
            gSPSegment(POLY_OPA_DISP++, segment, reinterpret_cast<uintptr_t>(data));
        }
    }
    SkelAnime_DrawFlexOpa(play, gCustomBodySkelAnime.skeleton, gCustomBodySkelAnime.jointTable,
                          gCustomBodySkelAnime.dListCount, nullptr, nullptr, actor);
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
    for (const auto& [segment, path] : gCustomBodySpec.segmentsPrefixed) {
        if (!ResourceMgr_FileExists(path.c_str())) {
            SPDLOG_WARN("ShipLua set_body: segmento 0x{:02X} indispon\xC3\xADvel ('{}')", segment, path);
            return false;
        }
    }
    CustomBodyFreeSkelBuffers();
    // flags=9: mesmo modo usado pelo próprio Player e pelo puppet do Kafei —
    // buffers do chamador quando não-NULL, mas aqui passamos NULL para os
    // dois: aloca do tamanho real do header do skeleton fornecido pelo mod,
    // não de PLAYER_LIMB_MAX.
    SkelAnime_InitLink(play, &gCustomBodySkelAnime, (FlexSkeletonHeader*)gCustomBodySpec.skeletonPathPrefixed.c_str(),
                       (LinkAnimationHeader*)defaultAnimPath->second.c_str(), 9, nullptr, nullptr, 0);
    // SkelAnime_InitLink nao inicializa baseTransl. O skeleton de MM pertence a
    // mesma familia do Player e usa a base canonica definida em z_player.c.
    // Sem ela, a neutralizacao enfileirada da raiz colocaria a malha em Y=0.
    gCustomBodySkelAnime.baseTransl = { -57, 3377, 0 };
    gCustomBodySkelAnime.prevTransl = gCustomBodySkelAnime.baseTransl;
    gCustomBodyCurrentAnim = gCustomBodySpec.defaultAnim;

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
    gCustomBodyActor = actor;
    gCustomBodyActive = true;
    player->stateFlags2 |= PLAYER_STATE2_DISABLE_DRAW;
    return true;
}

// Corpo genérico: skeleton, anims, reverse_anims, segments, default_anim e ground_offset.
int LuaSetPlayerBody(lua_State* state) {
    PlayState* play = gPlayState;
    if (lua_isnil(state, 1)) {
        CustomBodyDeactivate(play, true);
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
    for (const char* field : { "models", "roll_offset", "shield", "water_void", "block_ledge_grab" }) {
        lua_getfield(state, 1, field);
        const bool removedField = !lua_isnil(state, -1);
        lua_pop(state, 1);
        if (removedField) {
            SPDLOG_WARN("ShipLua set_body: '{}' pertence ao mod de formas; use apenas o corpo genérico", field);
            lua_pushboolean(state, 0);
            return 1;
        }
    }
    if (ShipLuaHost::OotPlayerBodyInUse()) {
        SPDLOG_WARN("ShipLua set_body: um mod nativo controla o corpo do Link");
        lua_pushboolean(state, 0);
        return 1;
    }

    if (ShipLuaHost::OotPlayerQuadsInUse()) {
        SPDLOG_WARN("ShipLua set_body: um mod nativo arrendou os quads do Link");
        lua_pushboolean(state, 0);
        return 1;
    }

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
    for (const auto& name : gCustomBodySpec.reverseAnims) {
        lua_pushboolean(state, 1);
        lua_setfield(state, -2, name.c_str());
    }
    lua_setfield(state, -2, "reverse_anims");
    lua_pushnumber(state, gCustomBodySpec.groundOffset);
    lua_setfield(state, -2, "ground_offset");
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
    LinkAnimation_Change(
        play, &gCustomBodySkelAnime, reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(path->second.c_str())),
        reverseOnce ? -static_cast<f32>(requestedSpeed) : static_cast<f32>(requestedSpeed),
        reverseOnce ? lastFrame : 0.0f, reverseOnce ? 0.0f : lastFrame, loop ? ANIMMODE_LOOP : ANIMMODE_ONCE, 0.0f);
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

void ClearForcedMask(Player* player) {
    if (player != nullptr) {
        player->currentMask = PLAYER_MASK_NONE;
    }
    gSaveContext.ship.maskMemory = PLAYER_MASK_NONE;
    if (gMaskForced) {
        SetSohCVarInt(CVAR_ENHANCEMENT("PersistentMasks"), gMaskPreviousPersistent);
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
        player->currentMask = PLAYER_MASK_NONE;
        gSaveContext.ship.maskMemory = PLAYER_MASK_NONE;
        if (gMaskForced) {
            SetSohCVarInt(CVAR_ENHANCEMENT("PersistentMasks"), gMaskPreviousPersistent);
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
    SetSohCVarInt(CVAR_ENHANCEMENT("PersistentMasks"), 1);
    player->currentMask = found->mask;
    gSaveContext.ship.maskMemory = found->mask;
    SPDLOG_INFO("ShipLua set_mask: máscara '{}' equipada", requested);
    lua_pushboolean(state, 1);
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
    // Um callback pode gravar o resultado e depois falhar, e o core só anota a
    // falha. Qualquer falha descarta o resultado de todo o despacho.
    if (!outcome.value->failures.empty()) {
        for (const ShipLua::CallbackFailure& failure : outcome.value->failures) {
            SPDLOG_ERROR("ShipLua [{}] falhou em {}: {}", failure.modId, name, failure.message);
        }
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
    constexpr std::array<Direction, 8> kDirections = { {
        { BTN_DUP, "dpad_up" },
        { BTN_DDOWN, "dpad_down" },
        { BTN_DLEFT, "dpad_left" },
        { BTN_DRIGHT, "dpad_right" },
        { BTN_CUP, "c_up" },
        { BTN_CDOWN, "c_down" },
        { BTN_CLEFT, "c_left" },
        { BTN_CRIGHT, "c_right" },
    } };

    Input& input = play->state.input[0];
    for (const auto& direction : kDirections) {
        if (!CHECK_BTN_ALL(input.press.button, direction.mask)) {
            continue;
        }
        const auto accepted = DispatchHookTransform("input.action", ShipLua::EventPayload{
                                                                        { "action", direction.action },
                                                                        { "pressed", true },
                                                                        { "source", "controller" },
                                                                    });
        if (accepted.has_value() && std::holds_alternative<bool>(accepted->value) && std::get<bool>(accepted->value)) {
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

extern "C" void ShipLua_EmitActorInit(Actor* actor) {
    if (gModHost == nullptr || gModHost->SubscriptionCount() == 0 || actor == nullptr) {
        return;
    }
    try {
        ShipLua::EventPayload payload;
        AddActorSnapshot(payload, "actor", actor);
        DispatchHookEvent("actor.init", std::move(payload));
    } catch (...) {
        // Falha de notificação não atravessa a fronteira C.
    }
}

extern "C" void ShipLua_EmitActorDestroy(Actor* actor) {
    if (gModHost == nullptr || gModHost->SubscriptionCount() == 0 || actor == nullptr) {
        return;
    }
    try {
        const auto snapshot = ActorSnapshot(actor);
        ShipLua::EventPayload payload;
        const auto handle = snapshot.find("handle");
        if (handle == snapshot.end()) {
            return;
        }
        payload.emplace("handle", handle->second);
        DispatchHookEvent("actor.destroy", std::move(payload));
    } catch (...) {
        // A invalidação e a limpeza rodam no chamador mesmo sem evento.
    }
}

extern "C" void ShipLua_EmitActorDamaged(Actor* actor, uint32_t amount) {
    if (gModHost == nullptr || gModHost->SubscriptionCount() == 0 || actor == nullptr || amount == 0) {
        return;
    }
    try {
        uint32_t health = 0;
        uint32_t maxHealth = 0;
        bool extended = false;
        if (!LinkSpan_ActorHealthInfo(actor, &health, &maxHealth, &extended)) {
            return;
        }
        ShipLua::EventPayload payload{
            { "amount", static_cast<std::int64_t>(amount) },
            { "health", static_cast<std::int64_t>(health) },
            { "max_health", static_cast<std::int64_t>(maxHealth) },
            { "extended", extended },
        };
        AddActorSnapshot(payload, "target", actor);
        DispatchHookEvent("hook.oot.actor.damaged", std::move(payload));
    } catch (...) {
        // O dano já foi confirmado.
    }
}

extern "C" void ShipLua_EmitSaveSaving(int32_t fileNum) {
    if (gModHost == nullptr || gModHost->SubscriptionCount() == 0) {
        return;
    }
    try {
        DispatchHookEvent("save.saving", ShipLua::EventPayload{ { "slot", static_cast<std::int64_t>(fileNum) } });
    } catch (...) {
        // A gravação do jogo continua mesmo se a notificação falhar.
    }
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
    SetSohCVarInt(CVAR_ENHANCEMENT("PersistentMasks"), 1);
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
    if (path == nullptr || *path == '\0' || w < 1 || w > 64 || h < 1 || h > 64 || !ResourceMgr_FileExists(path)) {
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
    gDPLoadTextureBlock(OVERLAY_DISP++, texture, G_IM_FMT_RGBA, G_IM_SIZ_32b, texW, texH, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    const int dsdx = std::max(1, (static_cast<int>(texW) << 10) / w);
    const int dtdy = std::max(1, (static_cast<int>(texH) << 10) / h);
    gSPWideTextureRectangle(OVERLAY_DISP++, x << 2, y << 2, (x + w) << 2, (y + h) << 2, G_TX_RENDERTILE, 0, 0, dsdx,
                            dtdy);
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
                gDPLoadTextureBlock_4b(OVERLAY_DISP++, texture, G_IM_FMT_I, FONT_CHAR_TEX_WIDTH, FONT_CHAR_TEX_HEIGHT,
                                       0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                                       G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, clamp8(a));
                gSPTextureRectangle(OVERLAY_DISP++, (x + kerning + 1) << 2, (y + lineOffset + 1) << 2,
                                    (x + kerning + 1 + charSize) << 2, (y + lineOffset + 1 + charSize) << 2,
                                    G_TX_RENDERTILE, 0, 0, texScale, texScale);
                gDPPipeSync(OVERLAY_DISP++);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, clamp8(r), clamp8(g), clamp8(b), clamp8(a));
                gSPTextureRectangle(OVERLAY_DISP++, (x + kerning) << 2, (y + lineOffset) << 2,
                                    (x + kerning + charSize) << 2, (y + lineOffset + charSize) << 2, G_TX_RENDERTILE, 0,
                                    0, texScale, texScale);
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
        Matrix_Translate(player->bodyPartsPos[PLAYER_BODYPART_L_HAND].x, player->bodyPartsPos[PLAYER_BODYPART_L_HAND].y,
                         player->bodyPartsPos[PLAYER_BODYPART_L_HAND].z, MTXMODE_NEW);
        gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gHeldItemModelLeft.c_str());
    }
    if (!gHeldItemModelRight.empty()) {
        Matrix_Translate(player->bodyPartsPos[PLAYER_BODYPART_R_HAND].x, player->bodyPartsPos[PLAYER_BODYPART_R_HAND].y,
                         player->bodyPartsPos[PLAYER_BODYPART_R_HAND].z, MTXMODE_NEW);
        gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
                  G_MTX_MODELVIEW | G_MTX_LOAD);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gHeldItemModelRight.c_str());
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// Mesmo passe Xlu de tela inteira usado pela maquina de transformacao de
// OoTMM. Roda antes do HUD, para que os botoes/hearts nunca sejam apagados.

int LuaSetHeldItemModel(lua_State* state) {
    const char* slot = luaL_checkstring(state, 1);
    const char* path = luaL_optstring(state, 2, nullptr);
    std::string* target = nullptr;
    if (std::strcmp(slot, "left_hand") == 0) {
        target = &gHeldItemModelLeft;
    } else if (std::strcmp(slot, "right_hand") == 0) {
        target = &gHeldItemModelRight;
    } else {
        SPDLOG_WARN("ShipLua set_held_item_model: slot '{}' n\xC3\xA3o suportado (use 'left_hand'/'right_hand')", slot);
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
    Health,
    HealthCapacity,
    Age,
    GsTokens,
    Magic,
    Rupees,
    PosX,
    PosY,
    PosZ,
    RotY,
    Speed,
    VelX,
    VelY,
    VelZ,
    OnGround,
    // Estados de ação, somente leitura. Semânticos de propósito: expor
    // stateFlags cru amarraria os mods a bits internos do engine.
    Rolling,
    Climbing,
    Swimming,
    // Posição do jogador projetada na tela — permite ancorar um medidor ao
    // personagem (roda de stamina ao estilo BotW) em vez de fixá-lo num canto.
    ScreenX,
    ScreenY
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
    { "age", FieldKind::Age, 0, 1, false },
    { "gs_tokens", FieldKind::GsTokens, 0, 100, false },
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
        case FieldKind::Age:
            return gSaveContext.linkAge;
        case FieldKind::GsTokens:
            return gSaveContext.inventory.gsTokens;
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
        case FieldKind::Age:
        case FieldKind::GsTokens:
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
    lua_getfield(state, ootTable, "player");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
    }
    lua_pushcfunction(state, LuaPlayerJump);
    lua_setfield(state, -2, "jump");
    lua_pushcfunction(state, LuaSetMask);
    lua_setfield(state, -2, "set_mask");
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

// PASSO 1 — assets próprios de mod.
// Camada Unbound e a pasta do mod que a contém (vazia para .o2r solto na raiz de mods/).
struct PendingUnboundArchive {
    std::filesystem::path archive;
    std::filesystem::path modDirectory;
};
std::vector<PendingUnboundArchive> gPendingUnboundArchives;
// HOST-SLIM-001: todo .o2r/.otr achado em mods/ e mods/<pasta>/assets/, na ordem de montagem, para
// oot.mods.archives. `privatePrefix` vazio quando o host não montou (camada adiada ou falha).
struct ModArchiveCandidate {
    std::filesystem::path archive;
    std::filesystem::path modDirectory;
    std::string privatePrefix;
};
std::vector<ModArchiveCandidate> gModArchiveCandidates;
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

// Archive de assets de um mod: toda entrada fica sob "mod/<id>/" e as de objects/ e textures/ cujo caminho
// original está livre no VFS ganham também alias no hash original, para as referências internas (DL→textura,
// gravadas por hash do caminho sem prefixo) resolverem sozinhas. A decisão é arquivo a arquivo: uma colisão
// não derruba o alias das outras entradas, ao contrário das regras do resources V4. Sem tradução de bytes.
class ModAssetArchive final : public Ship::Archive {
  public:
    ModAssetArchive(const std::string& path, Ship::ArchiveManager* manager, std::string prefix)
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
            // Só dados de render: o áudio do SoH enumera o índice global (audio/*) e quebraria com entradas
            // alheias; objects/ e textures/ só são resolvidos por referência direta.
            const bool renderData = filePath.rfind("objects/", 0) == 0 || filePath.rfind("textures/", 0) == 0;
            if (renderData && mManager != nullptr && !mManager->HasFile(filePath)) {
                IndexFile(filePath);
                ++aliased;
            } else {
                ++blocked;
            }
        }
        mOwnIndex = ListFiles();
        SPDLOG_INFO("ShipLua exp\xC3\xB4s {} assets sob '{}' ({} com alias direto, {} sem alias)", files->size(),
                    mPrefix, aliased, blocked);
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
        if (filePath.rfind(mPrefix, 0) == 0) {
            return mInner->LoadFile(filePath.substr(mPrefix.size()));
        }
        return mInner->LoadFile(filePath);
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

// Archives de assets comuns ficam sob "mod/<nome>/", sem colidir com o jogo.
// Camadas Unbound v2 precisam preservar os caminhos originais para que cenas,
// colisões e entradas sejam mescladas; elas são montadas após o provider.
void MountModAssetArchives() {
    gPendingUnboundArchives.clear();
    gModArchiveCandidates.clear();
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
        const auto ownerEntry = archiveModDirectory.find(archivePath);
        gModArchiveCandidates.push_back(
            { archivePath,
              ownerEntry == archiveModDirectory.end() ? std::filesystem::path{} : ownerEntry->second,
              {} });
        if (archivePath.extension() == ".o2r" && IsUnboundLayerArchive(archivePath)) {
            const auto owner = archiveModDirectory.find(archivePath);
            gPendingUnboundArchives.push_back(
                { archivePath, owner == archiveModDirectory.end() ? std::filesystem::path{} : owner->second });
            continue;
        }
        const std::string id = archivePath.stem().string();
        const auto modArchive =
            std::make_shared<ModAssetArchive>(archivePath.string(), archiveManager.get(), "mod/" + id + "/");
        modArchive->Load();
        if (modArchive->IsLoaded() && archiveManager->AddArchive(modArchive) != nullptr) {
            ++mounted;
            SPDLOG_INFO("ShipLua montou assets do mod '{}' sob 'mod/{}/': {}", id, id, archivePath.string());
            gModArchiveCandidates.back().privatePrefix = "mod/" + id + "/";
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
    if (!archives)
        return false;
    for (const auto& archive : *archives) {
        if (!archive)
            continue;
        for (const auto& part : std::filesystem::path(archive->GetPath())) {
            if (part == "mods")
                return true;
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
    std::vector<std::filesystem::path> tools;
    for (const auto& tool : gToolDirectories) {
        if (tool.empty())
            SPDLOG_WARN("ShipLua ignorou --linkspan-tool sem pasta");
        else
            tools.push_back(tool);
    }
    gModHost->SetToolDirectories(std::move(tools));
    auto loaded = gModHost->LoadModsFromRoot(modsRoot, cacheRoot);
    if (!loaded.isOk()) {
        SPDLOG_ERROR("ShipLua n\xC3\xA3o conseguiu carregar a pasta de mods '{}': {}", modsRoot.string(),
                     loaded.message);
        return;
    }
    if (!loaded.value->loadedIds.empty())
        SetEngineExtended(true);
    // Ferramentas do SDK (RFC 0032) ficam fora de loadedIds: não ligam o motor alargado nem entram no menu.
    for (const auto& toolId : loaded.value->toolIds)
        SPDLOG_INFO("ShipLua carregou a ferramenta '{}'", toolId);
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
    // HOST-SLIM-001 (RFC 0029): archives aceitos para quem monta camadas (o Unbound, na F4). Pasta de mod rejeitado
    // fica fora. A origem deve ser a pasta efetivamente carregada, não apenas outro mod com o mesmo id.
    // Sem observer (o caso sem mod de camadas), nada de reler manifestos nem logar: o boot fica como antes.
    if (ShipLuaHost::HasOotHooks(ShipLuaHost::GetOotHookPoints().modsArchives)) {
        std::vector<ShipLuaHost::OotModArchive> acceptedArchives;
        for (const auto& candidate : gModArchiveCandidates) {
            std::string owner;
            if (!candidate.modDirectory.empty()) {
                const auto manifest = ShipLua::ParseManifestFile((candidate.modDirectory / "manifest.toml").string());
                bool accepted = false;
                if (manifest.isOk()) {
                    const auto& directories = loaded.value->loadedDirectories;
                    const auto origin = directories.find(manifest.value->id);
                    if (origin != directories.end()) {
                        std::error_code originError, candidateError;
                        const auto loadedDirectory =
                            std::filesystem::absolute(origin->second, originError).lexically_normal();
                        const auto candidateDirectory =
                            std::filesystem::absolute(candidate.modDirectory, candidateError).lexically_normal();
                        accepted = !originError && !candidateError && loadedDirectory == candidateDirectory;
                    }
                }
                if (!accepted) {
                    SPDLOG_INFO("ShipLua: '{}' fora de oot.mods.archives: o mod da pasta '{}' não carregou",
                                candidate.archive.string(), candidate.modDirectory.string());
                    continue;
                }
                owner = manifest.value->id;
            }
            // Revisão do Codex: o modsRoot pode ser relativo ("./mods"); o contrato promete caminho absoluto.
            std::error_code pathError;
            const auto archivePath = std::filesystem::absolute(candidate.archive, pathError).lexically_normal();
            acceptedArchives.push_back(
                { (pathError ? candidate.archive : archivePath).generic_string(), owner, candidate.privatePrefix });
        }
        ShipLuaHost::DispatchOotModArchives(acceptedArchives);
    }
    gModArchiveCandidates.clear();
    SPDLOG_INFO("ShipLua carregou {} mod(s) de '{}'", loaded.value->loadedIds.size(), modsRoot.string());
    if (gMenuRegistry != nullptr)
        gMenuRegistry->SetLoadedMods(loaded.value->loadedIds);

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

// Chamada duas vezes por volta em GameState_Update (game.c), fora de despacho do GameInteractor: antes do update (o
// que hotkeys e menu gravaram no Graph_StartFrame) e no fim, depois do OnGameFrameUpdate (timers e game.frame).
// Sobra uma janela: gravação feita em callback de ator antes do update do jogador só vale no frame seguinte.
// Sem mod nada grava, e a fila fica vazia.
extern "C" void LinkSpan_ApplyPendingSettings(void) {
    ShipLuaHost::PumpOotAudioMix();
    if (gPendingShipInit.empty()) {
        return;
    }
    std::vector<std::string> pending;
    pending.swap(gPendingShipInit);
    for (const auto& name : pending) {
        ShipInit::Init(name);
    }
}

void SetToolDirectories(std::vector<std::filesystem::path> directories) {
    gToolDirectories = std::move(directories);
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
    console->AddCommand("shiplua_goto_file_select",
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
                          "Vai para a tela de selecao usando a transicao nativa de GameState.",
                          {} });
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
             if (!SaveManager::Instance->SaveFile_Exist(slot)) {
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
                 } catch (const std::exception&) {}
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
    gImportTickHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
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
            DispatchHookEvent("save.loaded", ShipLua::EventPayload{ { "slot", static_cast<std::int64_t>(fileNum) } });
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
                DispatchHookEvent("game.frame",
                                  ShipLua::EventPayload{ { "frame", static_cast<std::int64_t>(ticked.value->frame) } });
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
    gMaskDrawHook = REGISTER_VB_SHOULD(VB_DRAW_PLAYER_MASK, {
        if (gAttachedHeadModel.empty()) {
            return;
        }
        va_arg(args, uint32_t);
        PlayState* play = va_arg(args, PlayState*);
        if (play == nullptr) {
            return;
        }
        if (!gAttachedHeadModel.empty()) {
            ShipLuaEmitDisplayList(play, gAttachedHeadModel.c_str());
            *should = false;
            return;
        }
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
                                                                   { "speed", static_cast<double>(*speedTarget) },
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
                                  { "item_id", static_cast<std::int64_t>(itemEntry.itemId) },
                                  { "get_item_id", static_cast<std::int64_t>(itemEntry.getItemId) },
                              });
        });
    gHookHealthChangeHook =
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerHealthChange>([](int16_t amount) {
            DispatchHookEvent("hook.oot.player.health_change",
                              ShipLua::EventPayload{ { "amount", static_cast<std::int64_t>(amount) } });
        });
    gHookBonkHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerBonk>(
        []() { DispatchHookEvent("hook.oot.player.bonk", ShipLua::EventPayload{}); });
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
                                  { "held_item_action", static_cast<std::int64_t>(player->heldItemAction) },
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
        const auto result = DispatchHookTransform("hook.oot.player.arrow_type_select",
                                                  ShipLua::EventPayload{
                                                      { "magic_arrow_type", static_cast<std::int64_t>(magicArrowType) },
                                                      { "arrow_type", static_cast<std::int64_t>(*arrowType) },
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
            if (actor == nullptr || gModHost == nullptr || gModHost->SubscriptionCount() == 0) {
                return;
            }
            try {
                ShipLua::EventPayload payload{
                    { "actor_id", static_cast<std::int64_t>(actor->id) },
                    { "category", static_cast<std::int64_t>(actor->category) },
                    { "pos_x", static_cast<double>(actor->world.pos.x) },
                    { "pos_y", static_cast<double>(actor->world.pos.y) },
                    { "pos_z", static_cast<double>(actor->world.pos.z) },
                };
                AddActorSnapshot(payload, "actor", actor);
                DispatchHookEvent("hook.oot.enemy.defeat", std::move(payload));
            } catch (...) {
                // Não interrompe a morte do inimigo por falha de notificação.
            }
        });
    // OnPlayDrawEnd roda após o desenho do mundo (Player incluído) e antes do
    // HUD — bodyPartsPos já está resolvido para o frame atual nesse ponto.
    gHeldItemDrawHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDrawEnd>([]() {
        if (gPlayState != nullptr) {
            DrawHeldItemModels(gPlayState);
            // Por último: o overlay do mod fica por cima de tudo.
            DrawHudOverlay(gPlayState);
        }
    });

    gActorDestroyHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* actor) {
        if (gLinkSpanEngineExtended == 0) {
            return;
        }
        ShipLua_EmitActorDestroy(static_cast<Actor*>(actor));
        try {
            if (gActorProvider != nullptr) {
                const auto destroyed = gActorProvider->OnNativeActorDestroyed(actor);
                if (!destroyed.isOk()) {
                    SPDLOG_ERROR("ShipLua failed to invalidate a destroyed OoT actor: {}", destroyed.message);
                }
            }
        } catch (...) {
            // A limpeza da conta não depende de alocação/log do provider.
        }
        LinkSpan_ActorHealthDestroy(static_cast<Actor*>(actor));
    });
    gPlayDestroyHook = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDestroy>([]() {
        if (gActorProvider != nullptr) {
            (void)gActorProvider->BeginSceneTeardown();
        }
        gSfxPlay = nullptr;
        gSfxCalls = 0;
        CustomBodyResetForSceneChange();
        // O contexto de luzes é reconstruído na cena nova: manter o nó antigo
        // deixaria um ponteiro pendurado, e o override de ambiente apontaria
        // para settings que já não existem. Solta os dois aqui.
        gPointLightNode = nullptr;
        gPointLightActive = false;
        gEnvOverrideActive = false;
        // Handles e vida só expiram após Actor_CleanupContext em Play_Destroy.
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
                                 NativeReadResourceFileLayers, NativeMountPrefixedResourceArchive });
    gModHost = std::make_unique<ShipLua::ModHost>(context, CreateLogger(), CreateOotNativePolicy());
    SPDLOG_INFO("Link-Span: providers nativos (ABI {}.{}) e core extensions ativos; serviços linkspan.oot.*; "
                "pacotes ZIP/SHIPMOD",
                SHIP_NATIVE_ABI_MAJOR, SHIP_NATIVE_ABI_MINOR);
    MountModAssetArchives();
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
        ClearForcedMask(player);
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
