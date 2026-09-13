#include "OotNativeEngine.h"
#include "oot_engine.h"
#include "oot_registry.h"
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
#include <utility>
#include <vector>
#include "z64.h"
extern "C" {
#include "functions.h"
PlayState* gPlayState = nullptr;
SaveContext gSaveContext{};
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
    return name && std::string(name) == "gSettings.FreeLook.Enabled" ? settingValue : fallback;
}
ShipNativeStatus SetSettingInt(const char* name, int32_t value) {
    if (!name || std::string(name) != "gSettings.FreeLook.Enabled") return SHIP_NATIVE_INVALID_ARGUMENT;
    settingValue = value;
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

int main(int argc, char** argv) {
    ShipLuaHost::SetOotNativeGamepadBridge(
        {HasGamepad, GetGamepadButtons, ClearButton, BindButton, ReloadMappings, GetSettingInt, SetSettingInt});
    ShipLuaHost::SetOotNativeResourceBridge(
        {HasResourceFile, ReadResourceFile, ListResourceFiles, DirtyResources, UnloadResource,
         MountArchive, UnmountArchive, GetGameVersions, ReadResourceFileLayers});
    auto policy = ShipLuaHost::CreateOotNativePolicy();
    Check(policy.services.size() == 5 && policy.services[0].version == LINKSPAN_OOT_ENGINE_VERSION &&
          policy.services[1].version == LINKSPAN_OOT_MOVEMENT_VERSION &&
          policy.services[2].version == LINKSPAN_OOT_RESOURCES_VERSION &&
          policy.services[3].version == LINKSPAN_OOT_RESOURCES_VERSION_2 &&
          policy.services[4].version == LINKSPAN_OOT_REGISTRY_VERSION,
          "host deve publicar engine, movement, resources V1/V2 e registry V1");
    const auto* engine = static_cast<const ShipOotEngineV1*>(policy.services[0].table);
    const auto* movement = static_cast<const ShipOotMovementV1*>(policy.services[1].table);
    const auto* resources = static_cast<const ShipOotResourcesV1*>(policy.services[2].table);
    const auto* resourcesV2 = static_cast<const ShipOotResourcesV2*>(policy.services[3].table);
    const auto* registry = static_cast<const ShipOotRegistryV1*>(policy.services[4].table);
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
    });
    worker.join();
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
        Check(configured.code == ShipLua::ErrorCode::Ok && settingValue == 1,
              "mod deve ativar câmera livre pelo serviço genérico de settings");
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
              "dez segundos configuráveis sem câmera devem restaurar o comportamento automático");
        play.state.input[0].rel.right_stick_x = 30;
        Check(callUpdate() == "camera-free" && settingValue == 1,
              "mover o analógico direito deve reativar a câmera livre");
        Check(clearedButtons.size() >= 6 && boundButtons.size() >= 3 &&
              boundButtons[boundButtons.size() - 3] == std::pair<uint16_t, uint8_t>{BTN_A, 1} &&
              boundButtons[boundButtons.size() - 2] == std::pair<uint16_t, uint8_t>{BTN_B, 2} &&
              boundButtons[boundButtons.size() - 1] == std::pair<uint16_t, uint8_t>{BTN_B, 0},
              "perfil deve mapear A contextual, Y espada e B cancelar");
        loaded.value->reset();
        uint64_t removedSpace = 0;
        Check(registry->find_space("example/dynamic_movement/actions", &removedSpace) == SHIP_NATIVE_UNSUPPORTED,
              "unload da DLL deve remover seu espaço e entradas em cascata");
        Check(mappingReloads > 0, "unload deve restaurar os mapeamentos do usuário");
        Check(settingValue == 0, "unload deve restaurar a configuração de câmera livre");

        ShipOotEngineV1 incompatible = *engine;
        incompatible.layout_id = "incompatible";
        policy.services[0].table = &incompatible;
        Check(!ShipLua::NativeProvider::Load(*manifest.value, argv[1], policy).isOk(), "layout divergente deve ser recusado");
    }
    gPlayState = nullptr;
    Check(!engine->get_player(), "troca de cena invalida acesso ao Player");
    return failures ? 1 : 0;
}
