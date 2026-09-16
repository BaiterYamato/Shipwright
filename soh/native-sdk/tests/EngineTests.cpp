#include "OotNativeEngine.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_save.h"
#include "OotNativeSave.h"
#include "OotNativeEscape.h"
#include "oot_registry.h"
#include "oot_ocarina.h"
#include "oot_scenes.h"
#include <shiplua/manifest/ManifestParser.h>
#include <algorithm>
#include <array>
#include <cstdio>
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
#include "macros.h"
extern "C" {
#include "functions.h"
PlayState* gPlayState = nullptr;
SaveContext gSaveContext{};
u8 gItemAgeReqs[ITEM_NONE]{};
OcarinaNote sOcarinaSongNotes[OCARINA_SONG_MAX][20]{};
OcarinaSongButtons gOcarinaSongButtons[OCARINA_SONG_MAX]{};
// Tabelas reais de z_inventory.c:9-24 e z_kaleido_scope_PAL.c:971-997.
u32 gBitFlags[32]{};
u16 gEquipMasks[4]{0x000F, 0x00F0, 0x0F00, 0xF000};
u8 gEquipShifts[4]{0, 4, 8, 12};
u8 gEquipAgeReqs[4][4]{{LINK_AGE_ADULT, LINK_AGE_CHILD, LINK_AGE_ADULT, LINK_AGE_ADULT},
                       {9, LINK_AGE_CHILD, 9, LINK_AGE_ADULT},
                       {LINK_AGE_ADULT, 9, LINK_AGE_ADULT, LINK_AGE_ADULT},
                       {9, 9, LINK_AGE_ADULT, LINK_AGE_ADULT}};
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
bool hostileLockOn = false;
u16 lastPlayerSfx = 0;
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
extern "C" s32 LinkSpan_ItemButtonHidden(s32 button);
extern "C" void LinkSpan_FilterPlayerInput(Input* input);
extern "C" s32 LinkSpan_DpadHudOwned(void);
extern "C" void LinkSpan_CaptureDpadBackground(PlayState* play, s16 x, s16 y, u8 r, u8 g, u8 b, u16 alpha);
extern "C" s32 LinkSpan_OwnedDpadBackground(PlayState* play, s16* x, s16* y, u8* r, u8* g, u8* b, u8* alpha);
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
extern "C" s8 Player_ItemToItemAction(s32 item) {
    return item == ITEM_OCARINA_FAIRY ? PLAYER_IA_OCARINA_FAIRY
         : item == ITEM_OCARINA_TIME  ? PLAYER_IA_OCARINA_OF_TIME
                                      : PLAYER_IA_NONE;
}
// Ramo da ocarina de Player_ActionHandler_13 (z_player.c:6009-6012 e 6119-6145).
extern "C" s32 Player_ActionHandler_13(Player* user, PlayState*) {
    if (user->unk_6AD == 0 || !(user->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) return 0;
    user->stateFlags2 |= PLAYER_STATE2_OCARINA_PLAYING;
    user->stateFlags1 |= PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE;
    return 1;
}
extern "C" void Inventory_ChangeEquipment(s16 equipment, u16 value) {
    gSaveContext.equips.equipment =
        static_cast<u16>((gSaveContext.equips.equipment & ~gEquipMasks[equipment]) | (value << gEquipShifts[equipment]));
}
extern "C" void Player_SetEquipmentData(PlayState*, Player* user) {
    user->currentTunic = static_cast<u8>(CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) - 1);
    user->currentBoots = static_cast<u8>(CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) - 1);
}
extern "C" void func_808328EC(Player*, u16 sfxId) { lastPlayerSfx = sfxId; }
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
    } else if ((item == ITEM_OCARINA_FAIRY || item == ITEM_OCARINA_TIME) && !hostileLockOn) {
        // "Cutscene items" (z_player.c:3491-3499): lock-on hostil impede.
        user->itemAction = Player_ItemToItemAction(item);
        user->unk_6AD = 4;
    }
}
extern "C" s32 LinkSpan_KeepLensWithoutButton(PlayState* play, s32 lensOnButton);
extern "C" void LinkSpan_CaptureItemButton(PlayState* play, s32 button, s16 x, s16 y, s16 size, u16 alpha);
extern "C" void OotNative_PublishOcarinaState(u8 active, u16 availableSongFlags);
extern "C" s32 OotNative_TakePendingOcarinaSong(void);

int main(int argc, char** argv) {
    for (int bit = 0; bit < 32; ++bit) gBitFlags[bit] = uint32_t{1} << bit;
    ShipLuaHost::SetOotNativeGamepadBridge(
        {HasGamepad, GetGamepadButtons, ClearButton, BindButton, ReloadMappings, GetSettingInt, SetSettingInt,
         GetGamepadAxis, BindAxis});
    ShipLuaHost::SetOotNativeResourceBridge(
        {HasResourceFile, ReadResourceFile, ListResourceFiles, DirtyResources, UnloadResource,
         MountArchive, UnmountArchive, GetGameVersions, ReadResourceFileLayers});
    auto policy = ShipLuaHost::CreateOotNativePolicy();
    Check(policy.services.size() == 9 && policy.services[0].version == LINKSPAN_OOT_ENGINE_VERSION &&
          policy.services[1].version == LINKSPAN_OOT_MOVEMENT_VERSION &&
          policy.services[2].version == LINKSPAN_OOT_MOVEMENT_VERSION_2 &&
          policy.services[3].version == LINKSPAN_OOT_RESOURCES_VERSION &&
          policy.services[4].version == LINKSPAN_OOT_RESOURCES_VERSION_2 &&
          policy.services[5].version == LINKSPAN_OOT_REGISTRY_VERSION &&
          std::string(policy.services[6].name) == LINKSPAN_OOT_OCARINA_SERVICE &&
          policy.services[6].version == LINKSPAN_OOT_OCARINA_VERSION &&
          std::string(policy.services[7].name) == LINKSPAN_OOT_SCENES_SERVICE &&
          policy.services[7].version == LINKSPAN_OOT_SCENES_VERSION &&
          policy.services[7].size == sizeof(ShipOotScenesV1) &&
          std::string(policy.services[8].name) == LINKSPAN_OOT_SAVE_SERVICE &&
          policy.services[8].version == LINKSPAN_OOT_SAVE_VERSION &&
          policy.services[8].size == sizeof(ShipOotSaveV1),
          "host deve publicar engine, movement V1/V2, resources V1/V2, registry V1, ocarina V1, scenes V1 e save V1");
    {
        Check(!policy.escapeHatch, "sem binding do jogo não há escape hatch");
        const std::string sha(64, 'c');
        const std::string text = "linkspan-symbols 1\nsha256 " + sha + "\n" +
                                 "10a0\t-\tz_player.c\tPlayer_Action_Roll\n" +
                                 "20b0\t-\tz_en_a.c\tInit\n" +
                                 "30c0\t-\tz_en_b.c\tInit\n" +
                                 "40d0\tfolded\tz_lib.c\tMath_Nop\n" +
                                 "40d0\tfolded\tz_lib.c\tMath_Nop2\r\n";
        ShipLuaHost::OotSymbolTable table;
        std::string error;
        uint64_t rva = 0;
        Check(table.Parse(text, error) && table.Fingerprint() == sha && table.Size() == 5, "soh.symbols válido");
        Check(table.Resolve("Player_Action_Roll", rva) == SHIP_NATIVE_OK && rva == 0x10a0, "nome único");
        Check(table.Resolve("z_player.c!Player_Action_Roll", rva) == SHIP_NATIVE_OK && rva == 0x10a0,
              "nome com arquivo");
        Check(table.Resolve("Init", rva) == SHIP_NATIVE_UNSUPPORTED, "static repetido exige arquivo");
        Check(table.Resolve("z_en_b.c!Init", rva) == SHIP_NATIVE_OK && rva == 0x30c0, "static por arquivo");
        Check(table.Resolve("Math_Nop", rva) == SHIP_NATIVE_UNSUPPORTED, "RVA dobrado por ICF é recusado");
        Check(table.Resolve("Nada", rva) == SHIP_NATIVE_UNSUPPORTED, "nome ausente");
        Check(table.Resolve("!Init", rva) == SHIP_NATIVE_INVALID_ARGUMENT, "arquivo vazio");
        Check(!table.Parse("linkspan-symbols 2\n", error), "versão desconhecida");
        Check(!table.Parse("linkspan-symbols 1\nsha256 " + sha + "\nzz\t-\ta.c\tX\n", error), "RVA inválido");
        Check(!table.Parse("linkspan-symbols 1\nsha256 " + sha + "\n10\tquente\ta.c\tX\n", error),
              "flag inválida");
    }
    {
        for (const char* point : {LINKSPAN_OOT_HOOK_SAVE_LOADED, LINKSPAN_OOT_HOOK_SAVE_SAVING,
                                  LINKSPAN_OOT_HOOK_SAVE_DELETED, LINKSPAN_OOT_HOOK_SAVE_COPIED}) {
            Check(policy.hooks->FindPoint(point, LINKSPAN_OOT_HOOKS_VERSION) != 0, point);
        }
        const auto* save = static_cast<const ShipOotSaveV1*>(policy.services[8].table);
        ShipLuaHost::ClearOotSaveData();
        uint64_t mod = 0, again = 0, bad = 0;
        Check(save->open_namespace("autor.mod", 2, &mod) == SHIP_NATIVE_OK && mod, "abre namespace");
        Check(save->open_namespace("autor.mod", 2, &again) == SHIP_NATIVE_OK && again == mod,
              "mesmo namespace e versão devolvem o mesmo handle");
        Check(save->open_namespace("autor.mod", 3, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "versão divergente");
        for (const char* name : {"semponto", "linkspan.host", ".a.b", "a.b.", "a b.c", ""}) {
            Check(save->open_namespace(name, 1, &bad) == SHIP_NATIVE_INVALID_ARGUMENT,
                  name);
        }
        uint32_t size = 7;
        Check(save->read(mod, nullptr, 0, &size) == SHIP_NATIVE_UNSUPPORTED && size == 0, "sem bloco");
        const std::string first = R"({"coins": 3, "items": [1, 2]})";
        Check(save->write(mod, first.data(), static_cast<uint32_t>(first.size())) == SHIP_NATIVE_OK, "write");
        Check(save->write(mod, "{oops", 5) == SHIP_NATIVE_INVALID_ARGUMENT, "JSON inválido recusado");
        Check(save->read(mod, nullptr, 0, &size) == SHIP_NATIVE_OK && size > 0, "tamanho do bloco");
        std::string text(size, ' ');
        Check(save->read(mod, text.data(), 3, &size) == SHIP_NATIVE_LIMIT, "capacidade curta");
        Check(save->read(mod, text.data(), size, &size) == SHIP_NATIVE_OK &&
                  nlohmann::json::parse(text) == nlohmann::json::parse(first),
              "read devolve o JSON escrito");
        uint32_t stored = 0;
        Check(save->get_stored_version(mod, &stored) == SHIP_NATIVE_OK && stored == 2, "versão gravada");

        Check(save->begin(mod) == SHIP_NATIVE_OK && save->begin(mod) == SHIP_NATIVE_INVALID_ARGUMENT,
              "uma transação por namespace");
        Check(save->write(mod, "42", 2) == SHIP_NATIVE_OK, "write em transação");
        auto during = ShipLuaHost::ExportOotSaveSection();
        Check(during["namespaces"]["autor.mod"]["data"] == nlohmann::json::parse(first),
              "save durante transação grava o estado anterior");
        Check(save->rollback(mod) == SHIP_NATIVE_OK && save->read(mod, text.data(), size, &size) == SHIP_NATIVE_OK &&
                  nlohmann::json::parse(text.substr(0, size)) == nlohmann::json::parse(first),
              "rollback restaura");
        Check(save->begin(mod) == SHIP_NATIVE_OK && save->write(mod, "[5]", 3) == SHIP_NATIVE_OK &&
                  save->commit(mod) == SHIP_NATIVE_OK && save->commit(mod) == SHIP_NATIVE_INVALID_ARGUMENT,
              "commit mantém");
        Check(save->set_required(mod, 1) == SHIP_NATIVE_OK, "required");
        ShipLuaHost::SetOotHostSaveBlock("linkspan.scenes", 1, nlohmann::json{{"x.y", {1, 2, 3, 4, 5, 6, 7}}});

        auto exported = ShipLuaHost::ExportOotSaveSection();
        exported["namespaces"]["outro.mod"] = {{"version", 4}, {"required", true}, {"data", {{"k", "v"}}}};
        exported["namespaces"]["lixo"] = 12;
        ShipLuaHost::ClearOotSaveData();
        Check(save->read(mod, nullptr, 0, &size) == SHIP_NATIVE_UNSUPPORTED, "arquivo novo começa vazio");
        ShipLuaHost::ImportOotSaveSection(exported);
        Check(save->read(mod, text.data(), static_cast<uint32_t>(text.size()), &size) == SHIP_NATIVE_OK &&
                  text.substr(0, size) == "[5]",
              "import restaura o bloco do mod");
        nlohmann::json host;
        uint32_t hostVersion = 0;
        Check(ShipLuaHost::GetOotHostSaveBlock("linkspan.scenes", host, hostVersion) && hostVersion == 1 &&
                  host["x.y"][6] == 7,
              "bloco do host sobrevive ao arquivo");
        const auto missing = ShipLuaHost::MissingRequiredOotNamespaces();
        Check(missing.size() == 1 && missing[0] == "outro.mod", "obrigatório sem mod é detectado");
        const auto roundTrip = ShipLuaHost::ExportOotSaveSection();
        Check(roundTrip["namespaces"]["outro.mod"]["data"]["k"] == "v" && !roundTrip["namespaces"].contains("lixo"),
              "bloco desconhecido preservado e entrada malformada descartada");
        Check(save->erase(mod) == SHIP_NATIVE_OK && save->get_stored_version(mod, &stored) == SHIP_NATIVE_OK &&
                  stored == 0,
              "erase");
        std::thread([&] {
            Check(save->open_namespace("autor.outro", 1, &bad) == SHIP_NATIVE_FAILURE, "save só na thread do jogo");
        }).join();
        ShipLuaHost::ClearOotSaveData();
    }
    Check(policy.hooks && policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_PLAY_UPDATE, LINKSPAN_OOT_HOOKS_VERSION) &&
              policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_ACTOR_UPDATE, LINKSPAN_OOT_HOOKS_VERSION) &&
              policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_ACTOR_DRAW, LINKSPAN_OOT_HOOKS_VERSION) &&
              policy.hooks->HookCount() == 0,
          "host deve declarar oot.play.update, oot.actor.update e oot.actor.draw v1 sem hooks");
    {
        struct Seen { int calls = 0; int16_t id = 0; } seen;
        const ShipNativeHookSpec spec{sizeof(ShipNativeHookSpec), LINKSPAN_OOT_HOOK_ACTOR_UPDATE,
                                      LINKSPAN_OOT_HOOKS_VERSION, sizeof(ShipOotActorHookV1),
                                      SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_BEFORE, 0,
                                      [](void* user, const ShipNativeHookCall* call) -> ShipNativeStatus {
                                          auto* data = static_cast<Seen*>(user);
                                          ++data->calls;
                                          data->id = static_cast<const ShipOotActorHookV1*>(call->payload)->actor_id;
                                          return SHIP_NATIVE_OK;
                                      },
                                      &seen};
        uint64_t handle = 0;
        const auto transform = [&] {
            auto copy = spec;
            copy.mode = SHIP_NATIVE_HOOK_TRANSFORM;
            copy.phase = 0;
            return copy;
        }();
        Check(policy.hooks->Register("test", transform, &handle) == SHIP_NATIVE_UNSUPPORTED,
              "pontos de ator do OoT recusam transform");
        Check(policy.hooks->Register("test", spec, &handle) == SHIP_NATIVE_OK, "observe em oot.actor.update");
        ShipOotActorHookV1 payload{sizeof(ShipOotActorHookV1), nullptr, nullptr, 7, 0};
        Check(policy.hooks->Dispatch(policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_ACTOR_UPDATE, 1), &payload,
                                     sizeof(payload), nullptr, nullptr) == SHIP_NATIVE_OK &&
                  seen.calls == 1 && seen.id == 7,
              "despacho do ponto de ator entrega o payload");
        Check(policy.hooks->Unregister("test", handle) == SHIP_NATIVE_OK, "remove hook de teste");
    }
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

    const auto shortcut = movementV2->player_use_item_shortcut;
    const auto resetPlayerAction = [&] {
        player.stateFlags1 = 0;
        player.stateFlags2 = 0;
        player.itemAction = player.heldItemAction;
        player.unk_6AD = 0;
    };
    player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
    Check(shortcut(ITEM_OCARINA_TIME) == SHIP_NATIVE_UNSUPPORTED, "ocarina fora do inventário deve ser recusada");
    gSaveContext.inventory.items[SLOT_OCARINA] = ITEM_OCARINA_TIME;
    play.interfaceCtx.restrictions.ocarina = 1;
    Check(shortcut(ITEM_OCARINA_TIME) == SHIP_NATIVE_UNSUPPORTED, "restrição de ocarina da cena deve bloquear");
    play.interfaceCtx.restrictions.ocarina = 0;
    hostileLockOn = true;
    Check(shortcut(ITEM_OCARINA_TIME) == SHIP_NATIVE_UNSUPPORTED && player.unk_6AD == 0 &&
              !(player.stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING),
          "lock-on hostil deve impedir a ocarina como no botão C");
    hostileLockOn = false;
    player.actor.bgCheckFlags = 0;
    Check(shortcut(ITEM_OCARINA_TIME) == SHIP_NATIVE_UNSUPPORTED && player.unk_6AD == 0 &&
              player.itemAction == player.heldItemAction,
          "ocarina recusada fora do chão não deve deixar pedido pendente");
    player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
    Check(shortcut(ITEM_OCARINA_FAIRY) == SHIP_NATIVE_UNSUPPORTED,
          "ocarina diferente da do inventário deve ser recusada");
    Check(shortcut(ITEM_OCARINA_TIME) == SHIP_NATIVE_OK && (player.stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING) &&
              usedItems.back() == ITEM_OCARINA_TIME,
          "atalho deve tirar a ocarina no mesmo frame pelo handler nativo");
    resetPlayerAction();
    gSaveContext.eventInf[0] = 1;
    Check(shortcut(ITEM_LENS) == SHIP_NATIVE_UNSUPPORTED && shortcut(ITEM_OCARINA_TIME) == SHIP_NATIVE_OK,
          "evento de arco a cavalo deve liberar só a ocarina");
    gSaveContext.eventInf[0] = 0;
    resetPlayerAction();

    gSaveContext.equips.equipment =
        static_cast<u16>((EQUIP_VALUE_TUNIC_KOKIRI << 8) | (EQUIP_VALUE_BOOTS_KOKIRI << 12));
    gSaveContext.inventory.equipment = static_cast<u16>(OWNED_EQUIP_FLAG(EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_KOKIRI) |
                                                        OWNED_EQUIP_FLAG(EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_KOKIRI));
    Check(shortcut(ITEM_TUNIC_GORON) == SHIP_NATIVE_UNSUPPORTED, "traje não obtido deve ser recusado");
    gSaveContext.inventory.equipment |= static_cast<u16>(OWNED_EQUIP_FLAG(EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_GORON) |
                                                         OWNED_EQUIP_FLAG(EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_IRON));
    player.stateFlags1 = PLAYER_STATE1_SHIELDING;
    Check(shortcut(ITEM_TUNIC_GORON) == SHIP_NATIVE_OK &&
              CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_GORON && lastPlayerSfx == NA_SE_PL_CHANGE_ARMS,
          "traje Goron deve ser vestido mesmo com escudo erguido");
    Check(shortcut(ITEM_TUNIC_KOKIRI) == SHIP_NATIVE_OK &&
              CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_KOKIRI &&
              shortcut(ITEM_TUNIC_KOKIRI) == SHIP_NATIVE_UNSUPPORTED,
          "Kokiri deve voltar do Goron e não mudar nada quando já vestido");
    Check(shortcut(ITEM_TUNIC_GORON) == SHIP_NATIVE_OK && shortcut(ITEM_TUNIC_GORON) == SHIP_NATIVE_OK &&
              CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_KOKIRI,
          "usar o traje vestido deve voltar ao Kokiri, como no SoH");
    player.stateFlags1 = 0;
    environmentalHazard = 3;
    Check(shortcut(ITEM_BOOTS_IRON) == SHIP_NATIVE_OK &&
              CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) == EQUIP_VALUE_BOOTS_IRON &&
              lastPlayerSfx == NA_SE_PL_WALK_HEAVYBOOTS && player.currentBoots == EQUIP_VALUE_BOOTS_IRON - 1,
          "botas de ferro devem calçar submerso com o som pesado");
    environmentalHazard = 0;
    gSaveContext.linkAge = LINK_AGE_CHILD;
    Check(shortcut(ITEM_BOOTS_IRON) == SHIP_NATIVE_UNSUPPORTED && shortcut(ITEM_BOOTS_KOKIRI) == SHIP_NATIVE_OK &&
              CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) == EQUIP_VALUE_BOOTS_KOKIRI,
          "criança não deve calçar ferro, mas volta às Kokiri");
    gSaveContext.linkAge = LINK_AGE_ADULT;
    player.stateFlags1 = PLAYER_STATE1_TALKING;
    Check(shortcut(ITEM_TUNIC_GORON) == SHIP_NATIVE_UNSUPPORTED, "conversa deve bloquear a troca de traje");
    player.stateFlags1 = PLAYER_STATE1_ON_HORSE;
    Check(shortcut(ITEM_TUNIC_GORON) == SHIP_NATIVE_UNSUPPORTED, "cavalo deve bloquear a troca de traje");
    player.stateFlags1 = 0;
    player.stateFlags2 = PLAYER_STATE2_OCARINA_PLAYING;
    Check(shortcut(ITEM_TUNIC_GORON) == SHIP_NATIVE_UNSUPPORTED, "tocando ocarina não deve trocar de traje");
    player.stateFlags2 = 0;
    play.pauseCtx.state = 1;
    Check(shortcut(ITEM_BOOTS_IRON) == SHIP_NATIVE_UNSUPPORTED, "pausa deve bloquear a troca de botas");
    play.pauseCtx.state = 0;
    gamepadAxes[5] = 1234;

    gOcarinaSongButtons[OCARINA_SONG_SARIAS] = {6, {1, 2, 3, 1, 2, 3}};
    // Estado inicial de code_800EC960.c: o Espantalho já declara 8 botões, mas sem gravação a nota [1]
    // mantém volume 0xFF, e isso sozinho não pode anunciar a música.
    gOcarinaSongButtons[OCARINA_SONG_SCARECROW_SPAWN] = {8, {0}};
    sOcarinaSongNotes[OCARINA_SONG_SCARECROW_SPAWN][1].volume = 0xFF;
    std::array<uint8_t, LINKSPAN_OOT_OCARINA_MAX_NOTES> songNotes{};
    uint32_t songNoteCount = 0;
    Check(ocarina && ocarina->size == sizeof(ShipOotOcarinaV1) && !ocarina->is_active() &&
              ocarina->get_available_song_flags() == 0 &&
              ocarina->get_song_count() == OCARINA_SONG_SCARECROW_SPAWN,
          "ocarina V1 deve começar inativa com as doze músicas fixas");
    Check(ocarina->get_song_pattern(OCARINA_SONG_SCARECROW_SPAWN, songNotes.data(), uint32_t(songNotes.size()),
                                    &songNoteCount) == SHIP_NATIVE_UNSUPPORTED,
          "Espantalho sem gravação não deve expor padrão");
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
    gOcarinaSongButtons[OCARINA_SONG_SCARECROW_SPAWN] = {8, {0, 1, 2, 3, 4, 3, 2, 1}};
    sOcarinaSongNotes[OCARINA_SONG_SCARECROW_SPAWN][1].volume = 80;
    Check(ocarina->get_song_count() == OCARINA_SONG_SCARECROW_SPAWN + 1 &&
              ocarina->get_song_pattern(OCARINA_SONG_SCARECROW_SPAWN, songNotes.data(), uint32_t(songNotes.size()),
                                        &songNoteCount) == SHIP_NATIVE_OK &&
              songNoteCount == 8 && songNotes[4] == 4,
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
    Check(movementV2->get_setting_int("linkspan.transient_settings", 0) == 7 &&
              movementV2->set_setting_int("linkspan.hud.hide_item_button.c_down", 1) == SHIP_NATIVE_OK &&
              movementV2->get_setting_int("linkspan.hud.hide_item_button.c_down", 0) == 1 &&
              LinkSpan_ItemButtonHidden(2) == 1 && LinkSpan_ItemButtonHidden(1) == 0 &&
              otherSettings.find("linkspan.hud.hide_item_button.c_down") == otherSettings.end() &&
              movementV2->set_setting_int("linkspan.transient_settings", 0) == SHIP_NATIVE_INVALID_ARGUMENT,
          "ocultação dos botões C deve ficar só em memória no host");
    Check(movementV2->set_setting_int("linkspan.hud.hide_item_button.c_down", 0) == SHIP_NATIVE_OK &&
              LinkSpan_ItemButtonHidden(2) == 0,
          "botão C deve voltar a aparecer quando a ocultação é desligada");
    {
        using Buttons = std::pair<u16, u16>;
        const auto filter = [](u16 cur, u16 press) {
            Input input{};
            input.cur.button = cur;
            input.press.button = press;
            LinkSpan_FilterPlayerInput(&input);
            return Buttons{ input.cur.button, input.press.button };
        };
        const u16 shieldAndSword = BTN_R | BTN_B;
        Check(filter(shieldAndSword, BTN_B) == Buttons{ shieldAndSword, BTN_B },
              "sem o setting, escudo e espada chegam ao Player como vieram");
        Check(movementV2->set_setting_int("linkspan.input.sword_over_shield", 1) == SHIP_NATIVE_OK &&
                  movementV2->get_setting_int("linkspan.input.sword_over_shield", 0) == 1 &&
                  otherSettings.find("linkspan.input.sword_over_shield") == otherSettings.end(),
              "espada acima do escudo deve ficar só em memória no host");
        Check(filter(BTN_R, BTN_R) == Buttons{ BTN_R, BTN_R } && filter(shieldAndSword, BTN_B) == Buttons{ BTN_B, 0 } &&
                  filter(shieldAndSword, 0) == Buttons{ BTN_B, BTN_B } && filter(shieldAndSword, 0) == Buttons{ BTN_B, 0 } &&
                  filter(BTN_R, 0) == Buttons{ BTN_R, 0 },
              "B com o escudo erguido deve baixar o escudo, atacar no frame seguinte e devolver o escudo ao soltar");
        Check(filter(BTN_R, 0) == Buttons{ BTN_R, 0 } && filter(shieldAndSword, BTN_B) == Buttons{ BTN_B, 0 } &&
                  filter(BTN_R, 0) == Buttons{ 0, BTN_B } && filter(BTN_R, 0) == Buttons{ BTN_R, 0 },
              "toque rápido em B com o escudo erguido não pode perder o ataque");
        Check(filter(0, 0) == Buttons{ 0, 0 } && filter(shieldAndSword, shieldAndSword) == Buttons{ BTN_B, BTN_B },
              "B junto com o R, sem escudo erguido antes, ataca no mesmo frame");
        Check(movementV2->set_setting_int("linkspan.input.sword_over_shield", 0) == SHIP_NATIVE_OK &&
                  filter(shieldAndSword, 0) == Buttons{ shieldAndSword, 0 },
              "desligar a espada acima do escudo deve devolver o R ao Player");
    }
    Check(movementV2->set_setting_int("linkspan.hud.dpad", 1) == SHIP_NATIVE_OK &&
              movementV2->get_setting_int("linkspan.hud.dpad", 0) == 1 && LinkSpan_DpadHudOwned() == 1 &&
              otherSettings.find("linkspan.hud.dpad") == otherSettings.end() &&
              movementV2->set_setting_int("linkspan.hud.dpad", 0) == SHIP_NATIVE_OK && LinkSpan_DpadHudOwned() == 0,
          "D-pad do HUD tomado por provider deve ficar só em memória no host");
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
                  movementV2->player_use_item_shortcut(ITEM_TUNIC_GORON) == SHIP_NATIVE_UNSUPPORTED &&
                  movementV2->player_use_item_shortcut(ITEM_OCARINA_TIME) == SHIP_NATIVE_UNSUPPORTED &&
                  movementV2->get_item_button_rect(2, &x, &y, &side, &alpha) == SHIP_NATIVE_UNSUPPORTED,
              "movement V2 deve recusar thread externa");
        uint32_t count = 0;
        std::array<uint8_t, LINKSPAN_OOT_OCARINA_MAX_NOTES> notes{};
        Check(!ocarina->is_active() && ocarina->get_available_song_flags() == 0 && ocarina->get_song_count() == 0 &&
                  ocarina->get_song_pattern(OCARINA_SONG_SARIAS, notes.data(), uint32_t(notes.size()), &count) ==
                      SHIP_NATIVE_UNSUPPORTED &&
                  ocarina->submit_song(OCARINA_SONG_SARIAS) == SHIP_NATIVE_UNSUPPORTED,
              "ocarina V1 deve recusar thread externa");
        Check(movementV2->set_setting_int("linkspan.hud.hide_item_button.c_left", 1) == SHIP_NATIVE_UNSUPPORTED &&
                  movementV2->set_setting_int("linkspan.input.sword_over_shield", 1) == SHIP_NATIVE_UNSUPPORTED &&
                  movementV2->set_setting_int("linkspan.hud.dpad", 1) == SHIP_NATIVE_UNSUPPORTED &&
                  movementV2->get_setting_int("linkspan.transient_settings", 7) == 7,
              "settings transitórios devem recusar thread externa");
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
                  otherSettings["gEnhancements.PersistentMasks"] == 1 &&
                  otherSettings["gSettings.FreeLook.InvertYAxis"] == 0,
              "mod deve ativar câmera livre sem inverter o eixo vertical e PersistentMasks");
        Check(otherSettings["gSettings.Controls.RightStickAim"] == 1 &&
                  otherSettings["gSettings.MoveInFirstPerson"] == 1 &&
                  otherSettings["gSettings.Controls.InvertAimingYAxis"] == 0 &&
                  otherSettings["gSettings.Controls.InvertZAimingYAxis"] == 0,
              "primeira pessoa e mira: analógico direito olha com o vertical normal e o esquerdo anda");
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
        player.stateFlags1 = PLAYER_STATE1_FIRST_PERSON;
        Check(callUpdate() != "camera-free" && settingValue == 0,
              "em primeira pessoa o analógico direito mira e a câmera livre fica desligada");
        player.stateFlags1 = 0;
        Check(callUpdate() != "camera-free" && settingValue == 0,
              "ao sair da primeira pessoa a câmera livre espera o analógico voltar ao centro");
        play.state.input[0].rel.right_stick_x = 0;
        callUpdate();
        play.state.input[0].rel.right_stick_x = 30;
        Check(callUpdate() == "camera-free" && settingValue == 1,
              "com o analógico de volta ao centro a câmera livre responde de novo");
        using Binding = std::pair<uint16_t, uint8_t>;
        using AxisBinding = std::tuple<uint16_t, uint8_t, int8_t>;
        const bool shieldOnZl =
            std::find(boundAxes.begin(), boundAxes.end(), AxisBinding{BTN_R, 4, 1}) != boundAxes.end();
        Check(clearedButtons.size() >= 9 && boundButtons.size() >= 5 &&
                  std::vector<Binding>(boundButtons.end() - 5, boundButtons.end()) ==
                      std::vector<Binding>{{BTN_A, 1}, {BTN_B, 2}, {BTN_B, 0}, {BTN_CUP, 14}, {BTN_L, 4}} &&
                  shieldOnZl && boundAxes.back() == AxisBinding{BTN_CLEFT, 5, 1},
              "perfil deve mapear A, Y/B, D-pad direita=C-Up, ZL=escudo, -=L do N64 e ZR=C-Left");
        Check(LinkSpan_ItemButtonHidden(1) == 0 && LinkSpan_ItemButtonHidden(2) == 1 &&
                  LinkSpan_ItemButtonHidden(3) == 1 &&
                  otherSettings.find("gCosmetics.HUD.CDownButton.PosType") == otherSettings.end(),
              "HUD deve mostrar só o C equipado sem mexer nas CVars de cosméticos");
        Input swordInput{};
        swordInput.cur.button = BTN_R | BTN_B;
        LinkSpan_FilterPlayerInput(&swordInput);
        Check(swordInput.cur.button == BTN_B, "perfil com escudo no ZL deve dar prioridade à espada");
        Check(LinkSpan_DpadHudOwned() == 1, "perfil Nintendo deve tomar o D-pad do HUD");
        const auto itemMenuState = [&] {
            response.fill(0);
            const auto result = (*loaded.value)->Call("hud_item_menu", "", 0, response.data(), uint32_t(response.size()));
            return result.code == ShipLua::ErrorCode::Ok ? std::string(response.data(), result.size) : std::string("erro");
        };
        gSaveContext.equips.buttonItems[1] = ITEM_BOW;
        gSaveContext.equips.buttonItems[2] = ITEM_NONE;
        gSaveContext.equips.buttonItems[3] = ITEM_HOOKSHOT;
        Check(itemMenuState() == "none", "menu de itens deve ficar fechado sem segurar o R");
        gamepadButtons = uint32_t{1} << 10;
        Check(callUpdate() == "item-menu" && itemMenuState() == "1;1:3,3:10" && settingValue == 0,
              "segurar R deve abrir o menu no C equipado, só com os C que têm item, e pausar a câmera livre");
        play.state.input[0].rel.right_stick_x = 60;
        callUpdate();
        Check(itemMenuState() == "3;1:3,3:10", "analógico direito para a direita deve pular o C vazio");
        callUpdate();
        Check(itemMenuState() == "3;1:3,3:10", "analógico mantido inclinado não deve andar de novo");
        play.state.input[0].rel.right_stick_x = 0;
        gamepadButtons = 0;
        Check(callUpdate() == "item-c-right" && boundAxes.back() == AxisBinding{BTN_CRIGHT, 5, 1} &&
                  itemMenuState() == "none" && LinkSpan_ItemButtonHidden(1) == 1 && LinkSpan_ItemButtonHidden(3) == 0,
              "soltar o R deve equipar o destacado no ZR e deixar só ele no HUD");
        gamepadAxes[5] = 32000;
        gamepadButtons = uint32_t{1} << 10;
        callUpdate();
        play.state.input[0].rel.right_stick_x = -60;
        callUpdate();
        play.state.input[0].rel.right_stick_x = 0;
        gamepadButtons = 0;
        const auto axesWhileHeld = boundAxes.size();
        Check(callUpdate() == "item-zr-held" && boundAxes.size() == axesWhileHeld,
              "o menu não deve trocar o C enquanto o ZR está pressionado");
        gamepadAxes[5] = 0;
        callUpdate();
        play.state.frames = 90;
        LinkSpan_CaptureItemButton(&play, 3, 250, 20, 27, 200);
        response.fill(0);
        auto hud = (*loaded.value)->Call("hud_selection", "", 0, response.data(), uint32_t(response.size()));
        Check(hud.code == ShipLua::ErrorCode::Ok && std::string(response.data(), hud.size) == "250,20,27,200",
              "hud_selection deve devolver a posição do C selecionado");
        const int16_t dpadSlots[4][2] = { { 290, 40 }, { 290, 72 }, { 274, 56 }, { 306, 56 } };
        for (int slot = 0; slot < 4; ++slot) {
            LinkSpan_CaptureItemButton(&play, 4 + slot, dpadSlots[slot][0], dpadSlots[slot][1], 16, 180);
        }
        response.fill(0);
        const auto dpad = (*loaded.value)->Call("hud_dpad", "", 0, response.data(), uint32_t(response.size()));
        char expectedDpad[96];
        std::snprintf(expectedDpad, sizeof(expectedDpad),
                      "290,40,16,180;290,72,16,180;274,56,16,180;306,56,16,180;%d,%d,%u",
                      CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC), CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS),
                      unsigned(gSaveContext.inventory.items[SLOT_OCARINA]));
        int16_t rectX = 0;
        int16_t rectY = 0;
        int16_t rectSide = 0;
        uint8_t rectAlpha = 0;
        Check(dpad.code == ShipLua::ErrorCode::Ok && std::string(response.data(), dpad.size) == expectedDpad &&
                  movementV2->get_item_button_rect(8, &rectX, &rectY, &rectSide, &rectAlpha) ==
                      SHIP_NATIVE_INVALID_ARGUMENT,
              "hud_dpad deve devolver as quatro direções do D-pad com traje, botas e ocarina");
        LinkSpan_CaptureDpadBackground(&play, 271, 55, 255, 200, 100, 180);
        s16 backgroundX = 0;
        s16 backgroundY = 0;
        u8 backgroundR = 0;
        u8 backgroundG = 0;
        u8 backgroundB = 0;
        u8 backgroundAlpha = 0;
        Check(LinkSpan_OwnedDpadBackground(&play, &backgroundX, &backgroundY, &backgroundR, &backgroundG, &backgroundB,
                                           &backgroundAlpha) == 1 &&
                  backgroundX == 271 && backgroundY == 55 && backgroundR == 255 && backgroundG == 200 &&
                  backgroundB == 100 && backgroundAlpha == 180,
              "fundo do D-pad tomado deve ficar para o hook do HUD Lua desenhar antes dos ícones");
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

        const auto pressAndRelease = [&](uint8_t sdlButton) {
            gamepadButtons = uint32_t{1} << sdlButton;
            callUpdate();
            gamepadButtons = 0;
            return callUpdate();
        };
        const auto quickSwapState = [&] {
            response.fill(0);
            const auto result = (*loaded.value)->Call("hud_quick_swap", "", 0, response.data(), uint32_t(response.size()));
            return result.code == ShipLua::ErrorCode::Ok ? std::string(response.data(), result.size) : std::string("erro");
        };
        gSaveContext.linkAge = LINK_AGE_ADULT;
        player.currentMask = PLAYER_MASK_NONE;
        player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
        resetPlayerAction();
        gamepadButtons = uint32_t{1} << 13;
        Check(callUpdate() == "ocarina" && (player.stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING),
              "D-pad esquerda deve tirar a ocarina do inventário");
        gamepadButtons = 0;
        callUpdate();
        resetPlayerAction();
        Check(pressAndRelease(11) == "tunic-goron" && CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_GORON,
              "toque no D-pad cima sem histórico deve vestir o primeiro traje especial");
        Check(pressAndRelease(11) == "tunic-kokiri" && CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_KOKIRI,
              "novo toque no D-pad cima deve voltar ao último traje");
        gSaveContext.inventory.equipment |= static_cast<u16>(OWNED_EQUIP_FLAG(EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_ZORA));
        Check(quickSwapState() == "none", "HUD da troca rápida deve ficar fechado sem segurar");
        gamepadButtons = uint32_t{1} << 11;
        callUpdate();
        std::this_thread::sleep_for(std::chrono::milliseconds(430));
        Check(callUpdate() == "tunic-quick-swap" && quickSwapState() == "tunic;1;1,2,3",
              "segurar o D-pad cima deve abrir o menu no traje equipado");
        std::this_thread::sleep_for(std::chrono::milliseconds(470));
        callUpdate();
        Check(quickSwapState() == "tunic;1;1,2,3", "o menu não deve avançar sozinho");
        play.state.input[0].rel.right_stick_x = 60;
        callUpdate();
        play.state.input[0].rel.right_stick_x = 0;
        callUpdate();
        play.state.input[0].rel.right_stick_x = 60;
        callUpdate();
        Check(quickSwapState() == "tunic;3;1,2,3", "cada inclinação do analógico direito deve andar uma opção");
        play.state.input[0].rel.right_stick_x = 0;
        gamepadButtons = 0;
        Check(callUpdate() == "tunic-zora" && CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_ZORA &&
                  quickSwapState() == "none",
              "soltar deve vestir o traje destacado e fechar o HUD");
        Check(pressAndRelease(12) == "boots-iron" && CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) == EQUIP_VALUE_BOOTS_IRON,
              "toque no D-pad baixo com Kokiri deve calçar as primeiras botas especiais");
        Check(pressAndRelease(12) == "boots-kokiri" && CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) == EQUIP_VALUE_BOOTS_KOKIRI,
              "novo toque no D-pad baixo deve voltar às Kokiri");
        loaded.value->reset();
        uint64_t removedSpace = 0;
        Check(registry->find_space("example/dynamic_movement/actions", &removedSpace) == SHIP_NATIVE_UNSUPPORTED,
              "unload da DLL deve remover seu espaço e entradas em cascata");
        Check(mappingReloads > 0, "unload deve restaurar os mapeamentos do usuário");
        Check(settingValue == 0, "unload deve restaurar a configuração de câmera livre");
        Check(otherSettings["gEnhancements.PersistentMasks"] == 0 && !play.actorCtx.lensActive,
              "unload deve restaurar PersistentMasks e desligar a lente mantida só pelo atalho");
        Check(otherSettings["gSettings.FreeLook.InvertYAxis"] == 1 && LinkSpan_ItemButtonHidden(1) == 0 &&
                  LinkSpan_ItemButtonHidden(2) == 0 && LinkSpan_ItemButtonHidden(3) == 0,
              "unload deve restaurar o eixo vertical e mostrar de novo todos os botões C");
        Check(otherSettings["gSettings.Controls.RightStickAim"] == 0 &&
                  otherSettings["gSettings.MoveInFirstPerson"] == 0 &&
                  otherSettings["gSettings.Controls.InvertAimingYAxis"] == 1 &&
                  otherSettings["gSettings.Controls.InvertZAimingYAxis"] == 1,
              "unload deve devolver a primeira pessoa e a mira às opções anteriores");
        Input unloadedInput{};
        unloadedInput.cur.button = BTN_R | BTN_B;
        LinkSpan_FilterPlayerInput(&unloadedInput);
        Check(unloadedInput.cur.button == (BTN_R | BTN_B), "unload deve devolver o escudo ao R");
        Check(LinkSpan_DpadHudOwned() == 0, "unload deve devolver o D-pad do HUD");
        LinkSpan_CaptureDpadBackground(&play, 271, 55, 255, 200, 100, 180);
        Check(LinkSpan_OwnedDpadBackground(&play, &backgroundX, &backgroundY, &backgroundR, &backgroundG, &backgroundB,
                                           &backgroundAlpha) == 0,
              "sem D-pad tomado o fundo fica com o Interface_Draw");

        ShipOotEngineV1 incompatible = *engine;
        incompatible.layout_id = "incompatible";
        policy.services[0].table = &incompatible;
        Check(!ShipLua::NativeProvider::Load(*manifest.value, argv[1], policy).isOk(), "layout divergente deve ser recusado");
    }
    gPlayState = nullptr;
    Check(!engine->get_player(), "troca de cena invalida acesso ao Player");
    return failures ? 1 : 0;
}
