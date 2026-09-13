#include "OotNativeEngine.h"
#include "oot_engine.h"
#include "oot_registry.h"
#include "oot_ocarina.h"
#include <shiplua/manifest/ManifestParser.h>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include "z64.h"
extern "C" {
#include "functions.h"
PlayState* gPlayState = nullptr;
SaveContext gSaveContext{};
u8 gItemAgeReqs[ITEM_NONE]{};
OcarinaSongInfo gOcarinaSongNotes[OCARINA_SONG_MAX]{};
}
namespace {
PlayState play{};
Player player{};
Actor spawned{};
Actor* killed = nullptr;
int failures = 0;
uint8_t gamepadPresent = 1;
uint32_t gamepadButtons = 0;
std::vector<uint16_t> clearedButtons;
std::vector<std::pair<uint16_t, uint8_t>> boundButtons;
int mappingReloads = 0;
int32_t settingValue = 0;
std::map<std::string, int32_t> otherSettings;
std::array<int16_t, 6> gamepadAxes{};
std::vector<std::tuple<uint16_t, uint8_t, int8_t>> boundAxes;
std::vector<int32_t> usedItems;
s32 environmentalHazard = 0;
uint8_t pacifistMode = 0;
std::map<std::string, std::vector<uint8_t>> resourceFiles{
    {"test/core.json", {'{', '"', 'o', 'k', '"', ':', '1', '}' }},
    {"test/other.bin", {1, 2, 3}},
};
std::vector<std::string> dirtiedResources;
std::vector<std::string> unloadedResources;
std::vector<std::string> mountedArchives;
std::vector<uint64_t> unmountedArchives;
uint64_t nextArchiveHandle = 10;
LinkAnimationHeader* jumpAnimation = nullptr;
uint16_t jumpSound = 0;
void Check(bool ok, const char* text) { if (!ok) { std::cerr << text << '\n'; ++failures; } }
uint8_t HasGamepad(uint8_t port) { return port == 0 ? gamepadPresent : 0; }
uint32_t GetGamepadButtons(uint8_t port) { return port == 0 ? gamepadButtons : 0; }
ShipNativeStatus ClearButton(uint8_t port, uint16_t button) {
    if (port != 0 || !button) return SHIP_NATIVE_INVALID_ARGUMENT;
    clearedButtons.push_back(button);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus BindButton(uint8_t port, uint16_t button, uint8_t physical) {
    if (port != 0 || !button || physical >= 32) return SHIP_NATIVE_INVALID_ARGUMENT;
    boundButtons.emplace_back(button, physical);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus ReloadMappings(uint8_t port) {
    if (port != 0) return SHIP_NATIVE_INVALID_ARGUMENT;
    ++mappingReloads;
    return SHIP_NATIVE_OK;
}
int32_t GetSettingInt(const char* name, int32_t fallback) {
    if (!name) return fallback;
    if (std::string(name) == "gSettings.FreeLook.Enabled") return settingValue;
    const auto found = otherSettings.find(name);
    return found == otherSettings.end() ? fallback : found->second;
}
ShipNativeStatus SetSettingInt(const char* name, int32_t value) {
    if (!name || !*name) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (std::string(name) == "gSettings.FreeLook.Enabled") settingValue = value;
    else otherSettings[name] = value;
    return SHIP_NATIVE_OK;
}
int16_t GetGamepadAxis(uint8_t port, uint8_t axis) {
    return port == 0 && axis < gamepadAxes.size() ? gamepadAxes[axis] : 0;
}
ShipNativeStatus BindAxis(uint8_t port, uint16_t button, uint8_t axis, int8_t direction) {
    if (port != 0 || !button || axis >= gamepadAxes.size()) return SHIP_NATIVE_INVALID_ARGUMENT;
    boundAxes.emplace_back(button, axis, direction);
    return SHIP_NATIVE_OK;
}
uint8_t HasResourceFile(const char* path) {
    return path && resourceFiles.contains(path) ? 1 : 0;
}
ShipNativeStatus ReadResourceFile(const char* path, uint8_t* output, uint32_t capacity, uint32_t* outputSize) {
    const auto entry = path ? resourceFiles.find(path) : resourceFiles.end();
    if (entry == resourceFiles.end()) { *outputSize = 0; return SHIP_NATIVE_UNSUPPORTED; }
    *outputSize = static_cast<uint32_t>(entry->second.size());
    if (!output && capacity == 0) return SHIP_NATIVE_OK;
    if (capacity < *outputSize) return SHIP_NATIVE_LIMIT;
    std::memcpy(output, entry->second.data(), *outputSize);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus ListResourceFiles(const char* mask, ShipOotResourcePathFn callback, void* user) {
    const std::string prefix = mask && std::string(mask) == "test/*" ? "test/" : "";
    for (const auto& [path, bytes] : resourceFiles) {
        (void)bytes;
        if (!prefix.empty() && !path.starts_with(prefix)) continue;
        const auto status = callback(user, path.c_str(), static_cast<uint32_t>(path.size()));
        if (status != SHIP_NATIVE_OK) return status;
    }
    return SHIP_NATIVE_OK;
}
ShipNativeStatus DirtyResources(const char* mask) {
    dirtiedResources.emplace_back(mask);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus UnloadResource(const char* path) {
    unloadedResources.emplace_back(path);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus MountArchive(const char* path, uint64_t* handle) {
    mountedArchives.emplace_back(path);
    *handle = nextArchiveHandle++;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus UnmountArchive(uint64_t handle) {
    unmountedArchives.push_back(handle);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus GetGameVersions(uint32_t* output, uint32_t capacity, uint32_t* count) {
    constexpr uint32_t versions[] = {0xEC7011B7u, 0xF034001Au};
    *count = 2;
    if (!output && capacity == 0) return SHIP_NATIVE_OK;
    if (capacity < 2) return SHIP_NATIVE_LIMIT;
    std::copy(std::begin(versions), std::end(versions), output);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus ReadResourceFileLayers(const char* path, ShipOotResourceLayerFn callback, void* user) {
    if (!path || (std::string(path) != "test/layers.json" && std::string(path) != "unbound/layer-probe.json")) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    static const char* archivePaths[] = { "base.o2r", "override.shipmod" };
    static const char* contents[] = { "{\"base\":1}", "{\"mod\":2}" };
    static const uint64_t hashes[] = { 0x1111111111111111ULL, 0x2222222222222222ULL };
    for (uint32_t i = 0; i < 2; ++i) {
        const ShipOotResourceLayerV2 layer{
            sizeof(ShipOotResourceLayerV2), i, 2, i ? 0u : 0xEC7011B7u, hashes[i],
            static_cast<uint32_t>(std::strlen(contents[i])),
            static_cast<uint32_t>(std::strlen(archivePaths[i]))
        };
        const auto status = callback(user, &layer, archivePaths[i],
                                     reinterpret_cast<const uint8_t*>(contents[i]));
        if (status != SHIP_NATIVE_OK) return status;
    }
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL CollectPath(void* user, const char* path, uint32_t length) {
    static_cast<std::vector<std::string>*>(user)->emplace_back(path, length);
    return SHIP_NATIVE_OK;
}
}
// Stubs apenas do motor externo: a implementação da tabela é a mesma do jogo.
extern "C" Actor* Actor_Spawn(ActorContext*, PlayState*, s16 id, f32 x, f32 y, f32 z,
                              s16 rx, s16 ry, s16 rz, s16 params) {
    spawned.id = id;
    spawned.params = params;
    spawned.world.pos = {x, y, z};
    spawned.world.rot = {rx, ry, rz};
    return &spawned;
}
extern "C" void Actor_Kill(Actor* actor) { killed = actor; }
extern "C" void Player_Action_Roll(Player*, PlayState*) {}
extern "C" void Player_SetupRoll(Player* target, PlayState*) {
    target->actionFunc = Player_Action_Roll;
}
extern "C" void func_80838940(Player* target, LinkAnimationHeader* animation, f32 velocity, PlayState*, u16 sfxId) {
    jumpAnimation = animation;
    jumpSound = sfxId;
    target->actor.velocity.y = velocity;
    target->actor.bgCheckFlags &= ~BGCHECKFLAG_GROUND;
    target->stateFlags1 |= PLAYER_STATE1_JUMPING;
}
extern "C" s8 Player_ItemToItemAction(s32) { return PLAYER_IA_NONE; }
extern "C" s32 Player_UpperAction_ChangeHeldItem(Player*, PlayState*) { return 0; }
extern "C" void Player_Action_WaitForPutAway(Player*, PlayState*) {}
extern "C" uint8_t GameInteractor_PacifistModeActive() { return pacifistMode; }
extern "C" s32 Player_GetEnvironmentalHazard(PlayState*) { return environmentalHazard; }
// Só o efeito observável dos ramos de lente e máscara de Player_UseItem (z_player.c:3451-3490).
extern "C" void Player_UseItem(PlayState* target, Player* user, s32 item) {
    usedItems.push_back(item);
    if (item == ITEM_LENS) {
        target->actorCtx.lensActive = !target->actorCtx.lensActive;
    } else if (item >= ITEM_MASK_KEATON && item <= ITEM_MASK_TRUTH) {
        user->currentMask = user->currentMask != PLAYER_MASK_NONE
            ? PLAYER_MASK_NONE
            : static_cast<u8>(item - ITEM_MASK_KEATON + PLAYER_MASK_KEATON);
    }
}
extern "C" s32 LinkSpan_KeepLensWithoutButton(PlayState* play, s32 lensOnButton);
extern "C" void LinkSpan_CaptureItemButton(PlayState* play, s32 button, s16 x, s16 y, s16 size, u16 alpha);
extern "C" void OotNative_PublishOcarinaState(u8 active, u16 availableSongFlags);
extern "C" s32 OotNative_TakePendingOcarinaSong(void);

int main(int argc, char** argv) {
    ShipLuaHost::SetOotNativeGamepadBridge(
        {HasGamepad, GetGamepadButtons, ClearButton, BindButton, ReloadMappings, GetSettingInt, SetSettingInt,
         GetGamepadAxis, BindAxis});
    ShipLuaHost::SetOotNativeResourceBridge(
        {HasResourceFile, ReadResourceFile, ListResourceFiles, DirtyResources, UnloadResource,
         MountArchive, UnmountArchive, GetGameVersions, ReadResourceFileLayers});
    auto policy = ShipLuaHost::CreateOotNativePolicy();
    Check(policy.services.size() == 7 && policy.services[0].version == LINKSPAN_OOT_ENGINE_VERSION &&
          policy.services[1].version == LINKSPAN_OOT_MOVEMENT_VERSION &&
          policy.services[2].version == LINKSPAN_OOT_MOVEMENT_VERSION_2 &&
          policy.services[3].version == LINKSPAN_OOT_RESOURCES_VERSION &&
          policy.services[4].version == LINKSPAN_OOT_RESOURCES_VERSION_2 &&
          policy.services[5].version == LINKSPAN_OOT_REGISTRY_VERSION &&
          std::string(policy.services[6].name) == LINKSPAN_OOT_OCARINA_SERVICE &&
          policy.services[6].version == LINKSPAN_OOT_OCARINA_VERSION,
          "host deve publicar engine, movement V1/V2, resources V1/V2, registry V1 e ocarina V1");
    const auto* ocarina = static_cast<const ShipOotOcarinaV1*>(policy.services[6].table);
    const auto* engine = static_cast<const ShipOotEngineV1*>(policy.services[0].table);
    const auto* movement = static_cast<const ShipOotMovementV1*>(policy.services[1].table);
    const auto* movementV2 = static_cast<const ShipOotMovementV2*>(policy.services[2].table);
    const auto* resources = static_cast<const ShipOotResourcesV1*>(policy.services[3].table);
    const auto* resourcesV2 = static_cast<const ShipOotResourcesV2*>(policy.services[4].table);
    const auto* registry = static_cast<const ShipOotRegistryV1*>(policy.services[5].table);
    Check(resourcesV2 && resourcesV2->size == sizeof(ShipOotResourcesV2) && resourcesV2->read_file_layers,
          "resources V2 deve preservar V1 e publicar leitura de camadas");
    Check(registry && registry->size == sizeof(ShipOotRegistryV1),
          "registry V1 deve estar disponível pela policy do host");
    uint32_t resourceSize = 0;
    Check(resources->has_file("test/core.json") && !resources->has_file("missing"),
          "resources V1 deve consultar o VFS");
    Check(resources->read_file("test/core.json", nullptr, 0, &resourceSize) == SHIP_NATIVE_OK && resourceSize == 8,
          "resources V1 deve consultar o tamanho antes da cópia");
    std::array<uint8_t, 8> resourceBytes{};
    Check(resources->read_file("test/core.json", resourceBytes.data(), 4, &resourceSize) == SHIP_NATIVE_LIMIT &&
          resources->read_file("test/core.json", resourceBytes.data(), resourceBytes.size(), &resourceSize) ==
              SHIP_NATIVE_OK &&
          std::string(reinterpret_cast<const char*>(resourceBytes.data()), resourceSize) == "{\"ok\":1}",
          "resources V1 deve limitar e copiar bytes sem transferir ownership");
    std::vector<std::string> listed;
    Check(resources->list_files("test/*", CollectPath, &listed) == SHIP_NATIVE_OK && listed.size() == 2,
          "resources V1 deve enumerar caminhos por callback");
    struct LayerCapture {
        std::vector<std::string> archives;
        std::vector<std::string> contents;
        std::vector<uint64_t> hashes;
    } layers;
    const auto collectLayer = [](void* user, const ShipOotResourceLayerV2* layer, const char* archive,
                                 const uint8_t* data) -> ShipNativeStatus {
        auto& capture = *static_cast<LayerCapture*>(user);
        if (!layer || layer->size < sizeof(ShipOotResourceLayerV2) ||
            layer->layer_index != capture.archives.size() || layer->layer_count != 2) {
            return SHIP_NATIVE_FAILURE;
        }
        capture.archives.emplace_back(archive, layer->archive_path_length);
        capture.contents.emplace_back(reinterpret_cast<const char*>(data), layer->data_size);
        capture.hashes.push_back(layer->content_hash);
        return SHIP_NATIVE_OK;
    };
    Check(resourcesV2->read_file_layers("test/layers.json", collectLayer, &layers) == SHIP_NATIVE_OK &&
          layers.archives == std::vector<std::string>{"base.o2r", "override.shipmod"} &&
          layers.contents == std::vector<std::string>{"{\"base\":1}", "{\"mod\":2}"} &&
          layers.hashes[0] != layers.hashes[1],
          "resources V2 deve enumerar bytes, origem e hash da menor para a maior prioridade");
    const auto stopLayer = [](void*, const ShipOotResourceLayerV2*, const char*, const uint8_t*) {
        return SHIP_NATIVE_LIMIT;
    };
    Check(resourcesV2->read_file_layers("test/layers.json", stopLayer, nullptr) == SHIP_NATIVE_LIMIT,
          "resources V2 deve propagar interrupção do callback");
    uint32_t versionCount = 0;
    std::array<uint32_t, 2> versions{};
    Check(resources->get_game_versions(nullptr, 0, &versionCount) == SHIP_NATIVE_OK && versionCount == 2 &&
          resources->get_game_versions(versions.data(), versions.size(), &versionCount) == SHIP_NATIVE_OK &&
          versions[0] == 0xEC7011B7u,
          "resources V1 deve copiar versões montadas");
    uint64_t archiveHandle = 0;
    Check(resources->mount_archive("core-assets.o2r", &archiveHandle) == SHIP_NATIVE_OK && archiveHandle == 10 &&
          resources->dirty_resources("test/*") == SHIP_NATIVE_OK &&
          resources->unload_resource("test/core.json") == SHIP_NATIVE_OK &&
          resources->unmount_archive(archiveHandle) == SHIP_NATIVE_OK && mountedArchives.size() == 1 &&
          dirtiedResources.size() == 1 && unloadedResources.size() == 1 && unmountedArchives == std::vector<uint64_t>{10},
          "resources V1 deve montar, invalidar e desmontar por handle opaco");
    Check(policy.enabled && !engine->get_player() && !engine->get_play_state(), "sem gameplay deve retornar nulo");
    Check(engine->get_save_context() == &gSaveContext, "SaveContext do host");
    Check(!engine->spawn_actor(1, 0, 0, 0, 0, 0, 0, 0), "spawn fora de gameplay");
    gPlayState = &play;
    play.actorCtx.actorLists[ACTORCAT_PLAYER].head = &player.actor;
    Check(engine->get_player() == &player && engine->get_play_state() == &play, "ponteiros reais do host");
    play.state.input[0].cur.button = BTN_A;
    play.state.input[0].press.button = BTN_A;
    play.state.input[0].rel.stick_x = -12;
    play.state.input[0].rel.stick_y = 34;
    play.state.input[0].rel.right_stick_x = 21;
    play.state.input[0].rel.right_stick_y = -43;
    player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
    Check(movement->get_input_current(0) == BTN_A && movement->get_input_pressed(0) == BTN_A &&
          movement->get_stick_x(0) == -12 && movement->get_stick_y(0) == 34 &&
          movement->get_right_stick_x(0) == 21 && movement->get_right_stick_y(0) == -43 &&
          movement->is_player_grounded(),
          "movement V1 deve expor input virtual e estado de chão");
    gamepadButtons = uint32_t{1} << 3;
    Check(movement->has_gamepad(0) && movement->get_gamepad_buttons(0) == gamepadButtons,
          "movement V1 deve expor botões físicos SDL");
    Check(movement->player_jump(0.0f) == SHIP_NATIVE_INVALID_ARGUMENT, "impulso inválido deve ser recusado");
    Check(movement->player_jump(7.0f) == SHIP_NATIVE_OK && player.actor.velocity.y == 7.0f && jumpAnimation &&
          jumpSound == NA_SE_VO_LI_AUTO_JUMP && !movement->is_player_grounded(),
          "pulo deve iniciar animação e som nativos do Player");
    player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
    Check(movement->player_roll() == SHIP_NATIVE_OK && movement->is_player_rolling(),
          "rolamento deve entrar na ação nativa do Player");
    player.actionFunc = nullptr;
    Check(movementV2 && movementV2->size == sizeof(ShipOotMovementV2) &&
              movementV2->get_input_current == movement->get_input_current &&
              movementV2->player_roll == movement->player_roll && movementV2->get_gamepad_axis &&
              movementV2->bind_gamepad_axis && movementV2->player_use_item_shortcut &&
              movementV2->get_item_button_rect,
          "movement V2 deve preservar o prefixo da V1 e publicar as funções novas");
    gamepadAxes[5] = 32000;
    Check(movementV2->get_gamepad_axis(0, 5) == 32000 && movementV2->get_gamepad_axis(4, 5) == 0,
          "movement V2 deve expor eixos físicos SDL por porta");
    Check(movementV2->bind_gamepad_axis(0, BTN_CLEFT, 5, 0) == SHIP_NATIVE_INVALID_ARGUMENT &&
              movementV2->bind_gamepad_axis(0, BTN_CLEFT, 5, 1) == SHIP_NATIVE_OK && boundAxes.size() == 1 &&
              boundAxes[0] == std::tuple<uint16_t, uint8_t, int8_t>{BTN_CLEFT, 5, 1},
          "movement V2 deve ligar metade de eixo a botão virtual e recusar direção inválida");

    int16_t rectX = 0, rectY = 0, rectSize = 0;
    uint8_t rectAlpha = 0;
    play.state.frames = 40;
    Check(movementV2->get_item_button_rect(1, &rectX, &rectY, &rectSize, &rectAlpha) == SHIP_NATIVE_UNSUPPORTED &&
              movementV2->get_item_button_rect(0, &rectX, &rectY, &rectSize, &rectAlpha) ==
                  SHIP_NATIVE_INVALID_ARGUMENT &&
              movementV2->get_item_button_rect(2, nullptr, &rectY, &rectSize, &rectAlpha) ==
                  SHIP_NATIVE_INVALID_ARGUMENT,
          "posição de botão C sem captura ou com argumento inválido deve ser recusada");
    LinkSpan_CaptureItemButton(&play, 2, 227, 18, 27, 300);
    play.state.frames = 41;
    Check(movementV2->get_item_button_rect(2, &rectX, &rectY, &rectSize, &rectAlpha) == SHIP_NATIVE_OK &&
              rectX == 227 && rectY == 18 && rectSize == 27 && rectAlpha == 255,
          "HUD Lua deve ler a posição do botão C desenhada no frame anterior");
    play.state.frames = 42;
    Check(movementV2->get_item_button_rect(2, &rectX, &rectY, &rectSize, &rectAlpha) == SHIP_NATIVE_UNSUPPORTED,
          "captura de dois frames atrás deve ser recusada");
    LinkSpan_CaptureItemButton(&play, 3, -9999, 18, 27, 255);
    Check(movementV2->get_item_button_rect(3, &rectX, &rectY, &rectSize, &rectAlpha) == SHIP_NATIVE_UNSUPPORTED,
          "botão C oculto pelo HUD deve ser recusado");

    player.actor.category = ACTORCAT_PLAYER;
    player.stateFlags1 = 0;
    gSaveContext.health = 0x30;
    gSaveContext.linkAge = LINK_AGE_ADULT;
    gItemAgeReqs[ITEM_LENS] = 9;
    gItemAgeReqs[ITEM_MASK_BUNNY] = LINK_AGE_CHILD;
    Check(movementV2->player_use_item_shortcut(ITEM_BOW) == SHIP_NATIVE_INVALID_ARGUMENT &&
              movementV2->player_use_item_shortcut(ITEM_LENS) == SHIP_NATIVE_UNSUPPORTED && usedItems.empty(),
          "atalho deve recusar item fora de lente/máscara e lente fora do inventário");
    gSaveContext.inventory.items[SLOT_LENS] = ITEM_LENS;
    Check(movementV2->player_use_item_shortcut(ITEM_LENS) == SHIP_NATIVE_OK && play.actorCtx.lensActive &&
              usedItems.back() == ITEM_LENS && LinkSpan_KeepLensWithoutButton(&play, 0) == 1,
          "atalho deve ligar a lente pelo Player_UseItem nativo e mantê-la fora dos botões");
    Check(LinkSpan_KeepLensWithoutButton(&play, 1) == 1 && LinkSpan_KeepLensWithoutButton(&play, 0) == 0,
          "lente num botão deve devolver a regra normal do jogo");
    play.actorCtx.lensActive = false;
    Check(movementV2->player_use_item_shortcut(ITEM_LENS) == SHIP_NATIVE_OK &&
              LinkSpan_KeepLensWithoutButton(&play, 0) == 1,
          "religar pelo atalho deve restaurar a exceção");
    const auto refused = [&](const char* text) {
        const auto before = usedItems.size();
        Check(movementV2->player_use_item_shortcut(ITEM_LENS) == SHIP_NATIVE_UNSUPPORTED &&
                  usedItems.size() == before,
              text);
    };
    play.interfaceCtx.restrictions.all = 1;
    refused("restrição geral da cena deve bloquear a lente");
    play.sceneNum = SCENE_TREASURE_BOX_SHOP;
    Check(movementV2->player_use_item_shortcut(ITEM_LENS) == SHIP_NATIVE_OK && !play.actorCtx.lensActive &&
              LinkSpan_KeepLensWithoutButton(&play, 0) == 0,
          "Treasure Box Shop libera a lente mesmo com restrição geral");
    play.sceneNum = 0;
    play.interfaceCtx.restrictions.all = 0;
    environmentalHazard = 2;
    refused("perigo ambiental submerso deve bloquear a lente");
    environmentalHazard = 0;
    player.stateFlags1 = PLAYER_STATE1_CLIMBING_LADDER;
    refused("escada deve bloquear a lente");
    player.stateFlags1 = PLAYER_STATE1_CARRYING_ACTOR;
    refused("carregar ator deve bloquear a lente");
    player.stateFlags1 = 0;
    play.pauseCtx.state = 1;
    refused("pausa deve bloquear a lente");
    play.pauseCtx.state = 0;
    play.csCtx.state = CS_STATE_SKIPPABLE_EXEC;
    refused("cutscene deve bloquear a lente");
    play.csCtx.state = CS_STATE_IDLE;
    pacifistMode = 1;
    refused("modo pacifista deve bloquear a lente");
    pacifistMode = 0;

    gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_BUNNY;
    Check(movementV2->player_use_item_shortcut(ITEM_MASK_BUNNY) == SHIP_NATIVE_UNSUPPORTED,
          "adulto sem AdultMasks não deve usar máscara");
    gSaveContext.linkAge = LINK_AGE_CHILD;
    Check(movementV2->player_use_item_shortcut(ITEM_MASK_BUNNY) == SHIP_NATIVE_UNSUPPORTED &&
              player.currentMask == PLAYER_MASK_NONE,
          "máscara fora dos botões sem PersistentMasks seria removida pelo jogo");
    gSaveContext.equips.buttonItems[1] = ITEM_MASK_BUNNY;
    Check(movementV2->player_use_item_shortcut(ITEM_MASK_BUNNY) == SHIP_NATIVE_OK &&
              player.currentMask == PLAYER_MASK_BUNNY,
          "máscara num botão C deve ser colocada pelo atalho");
    gSaveContext.equips.buttonItems[1] = 0;
    Check(movementV2->player_use_item_shortcut(ITEM_MASK_BUNNY) == SHIP_NATIVE_OK &&
              player.currentMask == PLAYER_MASK_NONE,
          "atalho deve tirar a máscara em uso");
    otherSettings["gEnhancements.PersistentMasks"] = 1;
    Check(movementV2->player_use_item_shortcut(ITEM_MASK_KEATON) == SHIP_NATIVE_UNSUPPORTED &&
              movementV2->player_use_item_shortcut(ITEM_MASK_BUNNY) == SHIP_NATIVE_OK &&
              player.currentMask == PLAYER_MASK_BUNNY,
          "com PersistentMasks só a máscara do slot infantil deve ser colocada");
    play.interfaceCtx.restrictions.tradeItems = 1;
    Check(movementV2->player_use_item_shortcut(ITEM_MASK_BUNNY) == SHIP_NATIVE_UNSUPPORTED,
          "restrição de itens de troca deve bloquear máscaras");
    otherSettings["gEnhancements.MMBunnyHood"] = 1;
    Check(movementV2->player_use_item_shortcut(ITEM_MASK_BUNNY) == SHIP_NATIVE_OK &&
              player.currentMask == PLAYER_MASK_NONE,
          "MMBunnyHood libera máscaras com restrição de troca");
    play.interfaceCtx.restrictions.tradeItems = 0;
    otherSettings.clear();
    gSaveContext.linkAge = LINK_AGE_ADULT;
    gamepadAxes[5] = 1234;

    gOcarinaSongNotes[OCARINA_SONG_SARIAS] = {6, {1, 2, 3, 1, 2, 3}};
    std::array<uint8_t, LINKSPAN_OOT_OCARINA_MAX_NOTES> songNotes{};
    uint32_t songNoteCount = 0;
    Check(ocarina && ocarina->size == sizeof(ShipOotOcarinaV1) && !ocarina->is_active() &&
              ocarina->get_available_song_flags() == 0 && ocarina->get_song_count() == OCARINA_SONG_SCARECROW,
          "ocarina V1 deve começar inativa com as doze músicas fixas");
    Check(ocarina->get_song_pattern(OCARINA_SONG_SARIAS, nullptr, 0, &songNoteCount) == SHIP_NATIVE_OK &&
              songNoteCount == 6 &&
              ocarina->get_song_pattern(OCARINA_SONG_SARIAS, songNotes.data(), 4, &songNoteCount) ==
                  SHIP_NATIVE_LIMIT &&
              ocarina->get_song_pattern(OCARINA_SONG_SARIAS, songNotes.data(), uint32_t(songNotes.size()),
                                        &songNoteCount) == SHIP_NATIVE_OK &&
              songNotes[0] == 1 && songNotes[2] == 3 && songNotes[5] == 3,
          "ocarina V1 deve consultar tamanho, limitar e copiar os índices de nota do jogo");
    Check(ocarina->get_song_pattern(OCARINA_SONG_MINUET, songNotes.data(), uint32_t(songNotes.size()),
                                    &songNoteCount) == SHIP_NATIVE_UNSUPPORTED &&
              ocarina->get_song_pattern(OCARINA_SONG_MEMORY_GAME, songNotes.data(), uint32_t(songNotes.size()),
                                        &songNoteCount) == SHIP_NATIVE_INVALID_ARGUMENT,
          "ocarina V1 deve recusar padrão vazio e música fora do catálogo");
    gOcarinaSongNotes[OCARINA_SONG_SCARECROW] = {8, {0, 1, 2, 3, 4, 3, 2, 1}};
    Check(ocarina->get_song_count() == OCARINA_SONG_SCARECROW + 1,
          "música gravada do Espantalho deve entrar no catálogo");
    Check(ocarina->submit_song(OCARINA_SONG_SARIAS) == SHIP_NATIVE_UNSUPPORTED &&
              OotNative_TakePendingOcarinaSong() == -1,
          "entrega com a ocarina fechada deve ser recusada");
    OotNative_PublishOcarinaState(1, uint16_t(1u << OCARINA_SONG_SARIAS));
    Check(ocarina->is_active() && ocarina->get_available_song_flags() == (1u << OCARINA_SONG_SARIAS) &&
              ocarina->submit_song(OCARINA_SONG_SUNS) == SHIP_NATIVE_UNSUPPORTED &&
              ocarina->submit_song(OCARINA_SONG_MEMORY_GAME) == SHIP_NATIVE_INVALID_ARGUMENT,
          "ocarina ativa deve aceitar só as músicas esperadas pelo jogo");
    Check(ocarina->submit_song(OCARINA_SONG_SARIAS) == SHIP_NATIVE_OK &&
              OotNative_TakePendingOcarinaSong() == OCARINA_SONG_SARIAS && OotNative_TakePendingOcarinaSong() == -1,
          "música entregue deve ser consumida uma única vez pelo update da ocarina");
    Check(ocarina->submit_song(OCARINA_SONG_SARIAS) == SHIP_NATIVE_OK, "segunda entrega deve ser aceita");
    OotNative_PublishOcarinaState(0, uint16_t(1u << OCARINA_SONG_SARIAS));
    Check(!ocarina->is_active() && ocarina->get_available_song_flags() == 0 &&
              OotNative_TakePendingOcarinaSong() == -1,
          "fechar a ocarina deve descartar a música pendente");
    OotNative_PublishOcarinaState(1, uint16_t(1u << OCARINA_SONG_SARIAS));
    std::thread worker([&] {
        Check(!engine->get_player() && !engine->get_save_context() && !movement->get_input_current(0),
              "thread externa deve ser recusada");
        uint32_t size = 0;
        Check(!resources->has_file("test/core.json") &&
              resources->read_file("test/core.json", nullptr, 0, &size) == SHIP_NATIVE_INVALID_ARGUMENT,
              "resources V1 deve recusar thread externa");
        Check(resourcesV2->read_file_layers("test/layers.json", collectLayer, &layers) ==
                  SHIP_NATIVE_INVALID_ARGUMENT,
              "resources V2 deve recusar thread externa");
        int16_t x = 0, y = 0, side = 0;
        uint8_t alpha = 0;
        Check(movementV2->get_gamepad_axis(0, 5) == 0 &&
                  movementV2->bind_gamepad_axis(0, BTN_CLEFT, 5, 1) == SHIP_NATIVE_UNSUPPORTED &&
                  movementV2->player_use_item_shortcut(ITEM_LENS) == SHIP_NATIVE_UNSUPPORTED &&
                  movementV2->get_item_button_rect(2, &x, &y, &side, &alpha) == SHIP_NATIVE_UNSUPPORTED,
              "movement V2 deve recusar thread externa");
        uint32_t count = 0;
        std::array<uint8_t, LINKSPAN_OOT_OCARINA_MAX_NOTES> notes{};
        Check(!ocarina->is_active() && ocarina->get_available_song_flags() == 0 && ocarina->get_song_count() == 0 &&
                  ocarina->get_song_pattern(OCARINA_SONG_SARIAS, notes.data(), uint32_t(notes.size()), &count) ==
                      SHIP_NATIVE_UNSUPPORTED &&
                  ocarina->submit_song(OCARINA_SONG_SARIAS) == SHIP_NATIVE_UNSUPPORTED,
              "ocarina V1 deve recusar thread externa");
    });
    OotNative_PublishOcarinaState(0, 0);
    worker.join();
    gamepadAxes[5] = 0;
    boundAxes.clear();
    Check(engine->spawn_actor(42, 1, 2, 3, 4, 5, 6, 123) == &spawned &&
          spawned.id == 42 && spawned.params == 123 && spawned.world.pos.y == 2 && spawned.world.rot.z == 6,
          "spawn deve encaminhar argumentos sem catálogo por mod");
    Check(engine->kill_actor(&spawned) == SHIP_NATIVE_INVALID_ARGUMENT, "ator fora da lista");
    play.actorCtx.actorLists[ACTORCAT_PROP].head = &spawned;
    Check(engine->kill_actor(&spawned) == SHIP_NATIVE_OK && killed == &spawned, "kill do ator presente");

    if (argc == 3) {
        const auto manifest = ShipLua::ParseManifestFile((std::filesystem::path(argv[1]) / "manifest.toml").string());
        if (!manifest.isOk()) { std::cerr << manifest.message; return 1; }
        auto loaded = ShipLua::NativeProvider::Load(*manifest.value, argv[1], policy);
        if (!loaded.isOk()) { std::cerr << loaded.message; return 1; }
        std::array<char, 1024> response{};
        player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
        auto called = (*loaded.value)->Call("jump", "", 0, response.data(), uint32_t(response.size()));
        const float expected = std::strtof(argv[2], nullptr);
        Check(called.code == ShipLua::ErrorCode::Ok && std::isfinite(expected) &&
              std::abs(player.actor.velocity.y - expected) < 0.001f, "DLL independente deve modificar Player real");
        std::cout << std::string(response.data(), called.size) << '\n';
        response.fill(0);
        const auto resourceProbe = (*loaded.value)->Call("resource_probe", "", 0, response.data(),
                                                         uint32_t(response.size()));
        Check(resourceProbe.code == ShipLua::ErrorCode::Ok &&
              std::string(response.data(), resourceProbe.size) == "{\"ok\":1}",
              "DLL independente deve ler o VFS pelo serviço resources V1");
        response.fill(0);
        const auto layerProbe = (*loaded.value)->Call("layer_probe", "", 0, response.data(),
                                                      uint32_t(response.size()));
        Check(layerProbe.code == ShipLua::ErrorCode::Ok &&
                  std::string(response.data(), layerProbe.size)
                      .starts_with("layers=2; order=base.o2r>override.shipmod; merged="),
              "DLL independente deve combinar camadas pelo serviço resources V2");
        response.fill(0);
        const auto layerRuntimeProbe =
            (*loaded.value)->Call("layer_runtime_probe", "", 0, response.data(), uint32_t(response.size()));
        Check(layerRuntimeProbe.code == ShipLua::ErrorCode::Ok &&
                  std::string(response.data(), layerRuntimeProbe.size)
                      .starts_with("layers=2; order=base.o2r>override.shipmod; merged=") &&
                  std::string(response.data(), layerRuntimeProbe.size).ends_with("; cleanup=ok"),
              "DLL independente deve montar, combinar e desmontar duas camadas");
        response.fill(0);
        const auto registryProbe =
            (*loaded.value)->Call("registry_probe", "", 0, response.data(), uint32_t(response.size()));
        Check(registryProbe.code == ShipLua::ErrorCode::Ok &&
                  std::string(response.data(), registryProbe.size) ==
                      "space=ok; entries=2; first=128; jump=example/dynamic_movement/jump:action=jump; id=128",
              "DLL independente deve criar e consultar catálogo pelo registry V1");
        auto callUpdate = [&]() {
            response.fill(0);
            const auto result = (*loaded.value)->Call("update", "", 0, response.data(), uint32_t(response.size()));
            Check(result.code == ShipLua::ErrorCode::Ok, "update do mod deve responder");
            return std::string(response.data(), result.size);
        };
        response.fill(0);
        auto configured = (*loaded.value)->Call("configure", "0,5", 3, response.data(), uint32_t(response.size()));
        Check(configured.code == ShipLua::ErrorCode::Ok && settingValue == 1 &&
                  otherSettings["gEnhancements.PersistentMasks"] == 1,
              "mod deve ativar câmera livre e PersistentMasks pelo serviço genérico de settings");
        player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
        player.actionFunc = nullptr;
        player.stateFlags1 = 0;
        play.state.input[0].cur.button = 0;
        play.state.input[0].rel.stick_y = 80;
        gamepadButtons = uint32_t{1} << 3;
        Check(callUpdate() == "jump" && !movement->is_player_grounded(), "X Nintendo deve iniciar pulo");
        gamepadButtons = 0;
        Check(callUpdate() == "airborne", "pulo deve permanecer aéreo após soltar X");
        player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
        Check(callUpdate() == "landed", "pulo deve reconhecer a aterrissagem");
        gamepadButtons = uint32_t{1} << 1;
        player.actionFunc = Player_Action_Roll;
        Check(callUpdate() == "roll", "A em movimento deve preservar o rolamento nativo");
        Check(callUpdate() == "rolling", "mod deve observar o rolamento ativo");
        player.actionFunc = nullptr;
        Check(callUpdate() == "run", "fim do rolamento com A mantido deve entrar em corrida");
        player.linearVelocity = 2.0f;
        Check(callUpdate() == "running" && player.linearVelocity == 8.0f,
              "corrida deve manter velocidade mínima com direção");
        gamepadButtons = 0;
        Check(callUpdate() == "idle", "soltar A deve rearmar a sequência");
        play.state.input[0].rel.right_stick_x = 0;
        play.state.input[0].rel.right_stick_y = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        Check(callUpdate() == "camera-auto" && settingValue == 0,
              "câmera deve voltar a seguir Link quando ele anda após o atraso configurado");
        play.state.input[0].rel.right_stick_x = 30;
        Check(callUpdate() == "camera-free" && settingValue == 1,
              "mover o analógico direito deve reativar a câmera livre");
        using Binding = std::pair<uint16_t, uint8_t>;
        using AxisBinding = std::tuple<uint16_t, uint8_t, int8_t>;
        Check(clearedButtons.size() >= 9 && boundButtons.size() >= 6 &&
                  std::vector<Binding>(boundButtons.end() - 6, boundButtons.end()) ==
                      std::vector<Binding>{{BTN_A, 1}, {BTN_B, 2}, {BTN_B, 0}, {BTN_CUP, 14}, {BTN_R, 9}, {BTN_L, 4}} &&
                  !boundAxes.empty() && boundAxes.back() == AxisBinding{BTN_CLEFT, 5, 1},
              "perfil deve mapear A, Y/B, D-pad direita=C-Up, L=escudo, -=L do N64 e ZR=C-Left");
        gSaveContext.equips.buttonItems[1] = ITEM_BOW;
        gSaveContext.equips.buttonItems[2] = ITEM_NONE;
        gSaveContext.equips.buttonItems[3] = ITEM_HOOKSHOT;
        gamepadButtons = uint32_t{1} << 10;
        Check(callUpdate() == "item-c-right" && boundAxes.back() == AxisBinding{BTN_CRIGHT, 5, 1},
              "R deve levar o ZR ao próximo C com item, pulando C vazio");
        Check(callUpdate().rfind("item-", 0) != 0, "manter R pressionado não deve trocar de novo");
        gamepadButtons = 0;
        callUpdate();
        gamepadAxes[5] = 32000;
        gamepadButtons = uint32_t{1} << 10;
        const auto axesWhileHeld = boundAxes.size();
        Check(callUpdate().rfind("item-", 0) != 0 && boundAxes.size() == axesWhileHeld,
              "R não deve trocar o C enquanto o ZR está pressionado");
        gamepadButtons = 0;
        gamepadAxes[5] = 0;
        callUpdate();
        play.state.frames = 90;
        LinkSpan_CaptureItemButton(&play, 3, 250, 20, 27, 200);
        response.fill(0);
        auto hud = (*loaded.value)->Call("hud_selection", "", 0, response.data(), uint32_t(response.size()));
        Check(hud.code == ShipLua::ErrorCode::Ok && std::string(response.data(), hud.size) == "250,20,27,200",
              "hud_selection deve devolver a posição do C selecionado");
        play.state.frames = 95;
        response.fill(0);
        hud = (*loaded.value)->Call("hud_selection", "", 0, response.data(), uint32_t(response.size()));
        Check(hud.code == ShipLua::ErrorCode::Ok && std::string(response.data(), hud.size) == "none",
              "hud_selection sem desenho recente deve responder none");
        play.actorCtx.lensActive = false;
        gamepadButtons = uint32_t{1} << 8;
        callUpdate();
        gamepadButtons = 0;
        Check(callUpdate() == "lens-on" && play.actorCtx.lensActive && usedItems.back() == ITEM_LENS,
              "toque no R3 deve ligar a lente pelo host sem exigir botão C");
        gSaveContext.linkAge = LINK_AGE_CHILD;
        gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_BUNNY;
        player.currentMask = PLAYER_MASK_NONE;
        gamepadButtons = uint32_t{1} << 8;
        callUpdate();
        std::this_thread::sleep_for(std::chrono::milliseconds(420));
        Check(callUpdate() == "mask-on" && player.currentMask == PLAYER_MASK_BUNNY,
              "segurar o R3 deve colocar a máscara do slot infantil");
        gamepadButtons = 0;
        Check(callUpdate() != "lens-on", "soltar o R3 depois de segurar não deve alternar a lente");
        loaded.value->reset();
        uint64_t removedSpace = 0;
        Check(registry->find_space("example/dynamic_movement/actions", &removedSpace) == SHIP_NATIVE_UNSUPPORTED,
              "unload da DLL deve remover seu espaço e entradas em cascata");
        Check(mappingReloads > 0, "unload deve restaurar os mapeamentos do usuário");
        Check(settingValue == 0, "unload deve restaurar a configuração de câmera livre");
        Check(otherSettings["gEnhancements.PersistentMasks"] == 0 && !play.actorCtx.lensActive,
              "unload deve restaurar PersistentMasks e desligar a lente mantida só pelo atalho");

        ShipOotEngineV1 incompatible = *engine;
        incompatible.layout_id = "incompatible";
        policy.services[0].table = &incompatible;
        Check(!ShipLua::NativeProvider::Load(*manifest.value, argv[1], policy).isOk(), "layout divergente deve ser recusado");
    }
    gPlayState = nullptr;
    Check(!engine->get_player(), "troca de cena invalida acesso ao Player");
    return failures ? 1 : 0;
}
