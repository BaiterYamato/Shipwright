#include "OotNativeEngine.h"
#include "OotNativeActorModels.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_save.h"
#include "OotNativeSave.h"
#include "OotNativeEscape.h"
#include "oot_registry.h"
#include "oot_ocarina.h"
#include "oot_scenes.h"
#include "oot_items.h"
#include "oot_actors.h"
#include "OotNativeItems.h"
#include "oot_camera.h"
#include "oot_render.h"
#include "OotNativeView.h"
#include "oot_lights.h"
#include "OotNativeLights.h"
#include "oot_world.h"
#include "oot_colliders.h"
#include "OotNativeWorld.h"
#include "oot_skeletons.h"
#include "OotNativeSkeletons.h"
#include "OotNativeHooks.h"
#include "OotNativePlayerAction.h"
#include "OotNativePlayerBody.h"
#include "oot_player_body.h"
#include "oot_audio.h"
#include "OotNativeAudioMix.h"
#include "oot_text.h"
#include "OotNativeText.h"
#include "oot_randomizer.h"
#include "OotNativeRando.h"
#include "oot_anchor.h"
#include "OotNativeAnchor.h"
#include "oot_resources.h"
#include "OotNativeJsonTypes.h"
#include "OotNativePublicSymbols.h"
#include "oot_layout_id.h"
#include <shiplua/manifest/ManifestParser.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include "z64.h"
#include "macros.h"

// Este teste não liga o driver gráfico do jogo. A integração verifica a tabela;
// modelos, animação e diálogo são exercitados no runtime.
namespace ShipLuaHost {
void InitializeOotNativeActorModels(std::thread::id) {
}
const ShipOotActorModelsV1& GetOotNativeActorModelsService() {
    static const ShipOotActorModelsV1 service{ sizeof(ShipOotActorModelsV1), nullptr, nullptr, nullptr };
    return service;
}
void ReleaseOotActorModelOwner(std::string_view) {
}
} // namespace ShipLuaHost
extern "C" {
#include "functions.h"
PlayState* gPlayState = nullptr;
SaveContext gSaveContext{};
u8 gItemAgeReqs[ITEM_NONE]{};
OcarinaNote sOcarinaSongNotes[OCARINA_SONG_MAX][20]{};
OcarinaSongButtons gOcarinaSongButtons[OCARINA_SONG_MAX]{};
// Tabelas reais de z_inventory.c:9-24 e z_kaleido_scope_PAL.c:971-997.
u32 gBitFlags[32]{};
u16 gEquipMasks[4]{ 0x000F, 0x00F0, 0x0F00, 0xF000 };
u8 gEquipShifts[4]{ 0, 4, 8, 12 };
#include "OotNativePublicSymbols.h"
static f32 PublicProbeSinS(s16 angle) {
    return sinf(angle * (3.14159265358979323846f / 32768.0f));
}
static void* gPublicProbeIcons[1] = { &gPublicProbeIcons };
const LinkSpanPublicSymbol LinkSpan_PublicSymbols[] = {
    { "Math_SinS", (const void*)&PublicProbeSinS, LINKSPAN_PUBLIC_FUNCTION },
    { "gItemIcons", (const void*)&gPublicProbeIcons, LINKSPAN_PUBLIC_VARIABLE },
};
const size_t LinkSpan_PublicSymbolCount = 2;
u8 gEquipAgeReqs[4][4]{ { LINK_AGE_ADULT, LINK_AGE_CHILD, LINK_AGE_ADULT, LINK_AGE_ADULT },
                        { 9, LINK_AGE_CHILD, 9, LINK_AGE_ADULT },
                        { LINK_AGE_ADULT, 9, LINK_AGE_ADULT, LINK_AGE_ADULT },
                        { 9, 9, LINK_AGE_ADULT, LINK_AGE_ADULT } };
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
int settingWrites = 0;
std::map<std::string, int32_t> otherSettings;
std::array<int16_t, 6> gamepadAxes{};
std::vector<std::tuple<uint16_t, uint8_t, int8_t>> boundAxes;
std::vector<int32_t> usedItems;
s32 environmentalHazard = 0;
uint8_t pacifistMode = 0;
bool hostileLockOn = false;
u16 lastPlayerSfx = 0;
std::map<std::string, std::vector<uint8_t>> resourceFiles{
    { "test/core.json", { '{', '"', 'o', 'k', '"', ':', '1', '}' } },
    { "test/other.bin", { 1, 2, 3 } },
};
std::vector<std::string> dirtiedResources;
std::vector<std::string> unloadedResources;
std::vector<std::string> mountedArchives;
std::vector<uint64_t> unmountedArchives;
uint64_t nextArchiveHandle = 10;
LinkAnimationHeader* jumpAnimation = nullptr;
uint16_t jumpSound = 0;
void Check(bool ok, const char* text) {
    if (!ok) {
        std::cerr << text << '\n';
        ++failures;
    }
}
uint8_t HasGamepad(uint8_t port) {
    return port == 0 ? gamepadPresent : 0;
}
uint32_t GetGamepadButtons(uint8_t port) {
    return port == 0 ? gamepadButtons : 0;
}
ShipNativeStatus ClearButton(uint8_t port, uint16_t button) {
    if (port != 0 || !button)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    clearedButtons.push_back(button);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus BindButton(uint8_t port, uint16_t button, uint8_t physical) {
    if (port != 0 || !button || physical >= 32)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    boundButtons.emplace_back(button, physical);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus ReloadMappings(uint8_t port) {
    if (port != 0)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    ++mappingReloads;
    return SHIP_NATIVE_OK;
}
int32_t GetSettingInt(const char* name, int32_t fallback) {
    if (!name)
        return fallback;
    if (std::string(name) == "gSettings.FreeLook.Enabled")
        return settingValue;
    const auto found = otherSettings.find(name);
    return found == otherSettings.end() ? fallback : found->second;
}
ShipNativeStatus SetSettingInt(const char* name, int32_t value) {
    if (!name || !*name)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    ++settingWrites;
    if (std::string(name) == "gSettings.FreeLook.Enabled")
        settingValue = value;
    else
        otherSettings[name] = value;
    return SHIP_NATIVE_OK;
}
int16_t GetGamepadAxis(uint8_t port, uint8_t axis) {
    return port == 0 && axis < gamepadAxes.size() ? gamepadAxes[axis] : 0;
}
ShipNativeStatus BindAxis(uint8_t port, uint16_t button, uint8_t axis, int8_t direction) {
    if (port != 0 || !button || axis >= gamepadAxes.size())
        return SHIP_NATIVE_INVALID_ARGUMENT;
    boundAxes.emplace_back(button, axis, direction);
    return SHIP_NATIVE_OK;
}
uint8_t HasResourceFile(const char* path) {
    return path && resourceFiles.contains(path) ? 1 : 0;
}
ShipNativeStatus ReadResourceFile(const char* path, uint8_t* output, uint32_t capacity, uint32_t* outputSize) {
    const auto entry = path ? resourceFiles.find(path) : resourceFiles.end();
    if (entry == resourceFiles.end()) {
        *outputSize = 0;
        return SHIP_NATIVE_UNSUPPORTED;
    }
    *outputSize = static_cast<uint32_t>(entry->second.size());
    if (!output && capacity == 0)
        return SHIP_NATIVE_OK;
    if (capacity < *outputSize)
        return SHIP_NATIVE_LIMIT;
    std::memcpy(output, entry->second.data(), *outputSize);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus ListResourceFiles(const char* mask, ShipOotResourcePathFn callback, void* user) {
    const std::string prefix = mask && std::string(mask) == "test/*" ? "test/" : "";
    for (const auto& [path, bytes] : resourceFiles) {
        (void)bytes;
        if (!prefix.empty() && !path.starts_with(prefix))
            continue;
        const auto status = callback(user, path.c_str(), static_cast<uint32_t>(path.size()));
        if (status != SHIP_NATIVE_OK)
            return status;
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
    constexpr uint32_t versions[] = { 0xEC7011B7u, 0xF034001Au };
    *count = 2;
    if (!output && capacity == 0)
        return SHIP_NATIVE_OK;
    if (capacity < 2)
        return SHIP_NATIVE_LIMIT;
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
        const ShipOotResourceLayerV2 layer{ sizeof(ShipOotResourceLayerV2),
                                            i,
                                            2,
                                            i ? 0u : 0xEC7011B7u,
                                            hashes[i],
                                            static_cast<uint32_t>(std::strlen(contents[i])),
                                            static_cast<uint32_t>(std::strlen(archivePaths[i])) };
        const auto status = callback(user, &layer, archivePaths[i], reinterpret_cast<const uint8_t*>(contents[i]));
        if (status != SHIP_NATIVE_OK)
            return status;
    }
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL CollectPath(void* user, const char* path, uint32_t length) {
    static_cast<std::vector<std::string>*>(user)->emplace_back(path, length);
    return SHIP_NATIVE_OK;
}
} // namespace
// Stubs apenas do motor externo: a implementação da tabela é a mesma do jogo.
extern "C" Actor* Actor_Spawn(ActorContext*, PlayState*, s16 id, f32 x, f32 y, f32 z, s16 rx, s16 ry, s16 rz,
                              s16 params) {
    spawned.id = id;
    spawned.params = params;
    spawned.world.pos = { x, y, z };
    spawned.world.rot = { rx, ry, rz };
    return &spawned;
}
extern "C" void Actor_Kill(Actor* actor) {
    killed = actor;
}
extern "C" s32 LinkSpan_ItemButtonHidden(s32 button);
extern "C" void LinkSpan_FilterPlayerInput(Input* input);
extern "C" s32 LinkSpan_DpadHudOwned(void);
extern "C" uint8_t LinkSpan_PauseEquipSelectorEnabled(void);
extern "C" void LinkSpan_CaptureDpadBackground(PlayState* play, s16 x, s16 y, u8 r, u8 g, u8 b, u16 alpha);
extern "C" s32 LinkSpan_OwnedDpadBackground(PlayState* play, s16* x, s16* y, u8* r, u8* g, u8* b, u8* alpha);
extern "C" void Player_Action_Roll(Player*, PlayState*) {
}
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
    return item == ITEM_OCARINA_FAIRY  ? PLAYER_IA_OCARINA_FAIRY
           : item == ITEM_OCARINA_TIME ? PLAYER_IA_OCARINA_OF_TIME
                                       : PLAYER_IA_NONE;
}
// Ramo da ocarina de Player_ActionHandler_13 (z_player.c:6009-6012 e 6119-6145).
extern "C" s32 Player_ActionHandler_13(Player* user, PlayState*) {
    if (user->unk_6AD == 0 || !(user->actor.bgCheckFlags & BGCHECKFLAG_GROUND))
        return 0;
    user->stateFlags2 |= PLAYER_STATE2_OCARINA_PLAYING;
    user->stateFlags1 |= PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE;
    return 1;
}
extern "C" void Inventory_ChangeEquipment(s16 equipment, u16 value) {
    gSaveContext.equips.equipment = static_cast<u16>((gSaveContext.equips.equipment & ~gEquipMasks[equipment]) |
                                                     (value << gEquipShifts[equipment]));
}
extern "C" void Player_SetEquipmentData(PlayState*, Player* user) {
    user->currentTunic = static_cast<u8>(CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) - 1);
    user->currentBoots = static_cast<u8>(CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) - 1);
}
extern "C" void func_808328EC(Player*, u16 sfxId) {
    lastPlayerSfx = sfxId;
}
extern "C" s32 Player_UpperAction_ChangeHeldItem(Player*, PlayState*) {
    return 0;
}
extern "C" void Player_Action_WaitForPutAway(Player*, PlayState*) {
}
extern "C" uint8_t GameInteractor_PacifistModeActive() {
    return pacifistMode;
}
extern "C" s32 Player_GetEnvironmentalHazard(PlayState*) {
    return environmentalHazard;
}
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

namespace FakeItems {
std::array<uint8_t, 4> buttons{ 0xFF, 0xFF, 0xFF, 0xFF };
std::map<uint8_t, std::string> icons;
std::map<std::string, int32_t> actorIds;
std::vector<std::pair<int16_t, uint8_t>> killedTypes;
std::string lastDraw;
uint32_t uses = 0;

void SetItemVisual(uint8_t item, const char* icon, uint8_t) {
    if (icon) {
        icons[item] = icon;
    } else {
        icons.erase(item);
    }
}
uint8_t GetButton(uint8_t button) {
    return buttons[button];
}
void SetButton(uint8_t button, uint8_t item) {
    buttons[button] = item;
}
int32_t AddActor(const char* name, const ShipOotActorTypeSpecV1&) {
    const auto found = actorIds.find(name);
    if (found != actorIds.end()) {
        return found->second;
    }
    const int32_t id = 0x1D7 + static_cast<int32_t>(actorIds.size());
    actorIds[name] = id;
    return id;
}
void Kill(int16_t id, uint8_t category) {
    killedTypes.emplace_back(id, category);
}
ShipNativeStatus Draw(void*, const char* path, uint8_t) {
    lastDraw = path;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Use(void* user, uint8_t, uint8_t) {
    ++*static_cast<uint32_t*>(user);
    return SHIP_NATIVE_OK;
}
} // namespace FakeItems

namespace FakeView {
bool gameplay = true;
bool active = false;
int16_t nextCamera = 2;
int created = 0;
int destroyed = 0;
int applied = 0;
ShipOotCameraViewV1 lastApplied{};
int depth = 0;
std::vector<std::string> ops;
int playTag = 0;

ShipNativeStatus Create(int16_t* camera, void** play) {
    if (!gameplay) {
        return SHIP_NATIVE_LIMIT;
    }
    ++created;
    active = true;
    *camera = nextCamera;
    *play = &playTag;
    return SHIP_NATIVE_OK;
}
bool Valid(int16_t camera, void* play) {
    return active && camera == nextCamera && play == &playTag;
}
void Destroy(int16_t, void*) {
    ++destroyed;
    active = false;
}
int16_t lastRoll = 0;
bool rollApplied = false;
void Apply(int16_t, void*, const ShipOotCameraViewV2& view, bool roll) {
    ++applied;
    lastApplied = {
        sizeof(lastApplied), { view.eye[0], view.eye[1], view.eye[2] }, { view.at[0], view.at[1], view.at[2] }, view.fov
    };
    rollApplied = roll;
    if (roll)
        lastRoll = view.roll;
}
ShipNativeStatus Read(ShipOotCameraViewV2* view) {
    view->fov = 60.0f;
    view->roll = lastRoll;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus Draw(void*, const char* path, uint8_t layer) {
    ops.push_back(std::string("draw:") + path + ":" + std::to_string(layer));
    return SHIP_NATIVE_OK;
}
void Push() {
    ++depth;
}
void Pop() {
    --depth;
}
void Translate(float, float, float) {
    ops.push_back("translate");
}
void Scale(float, float, float) {
    ops.push_back("scale");
}
void Rotate(int16_t, int16_t, int16_t) {
    ops.push_back("rotate");
}
} // namespace FakeView

namespace FakeRenderV3 {
std::vector<std::string> ops;
int depth = 0;
int children = 0;
int gfxTag = 0;
int mtxTag = 0;
float lastAxis[3]{};
void Push() {
    ++depth;
}
void Pop() {
    --depth;
}
void TranslateNew(float x, float, float) {
    ops.push_back("new:" + std::to_string(static_cast<int>(x)));
}
void RotateAxis(float, float x, float y, float z) {
    lastAxis[0] = x;
    lastAxis[1] = y;
    lastAxis[2] = z;
    ops.push_back("axis");
}
const void* Export(void* gfx) {
    return gfx == &gfxTag ? &mtxTag : nullptr;
}
void DrawNative(void* gfx, const void*, uint8_t layer) {
    ops.push_back(std::string(gfx == &gfxTag ? "native:" : "native?:") + std::to_string(layer));
}
void SetToon(void*, bool enabled) {
    ops.push_back(enabled ? "toon:1" : "toon:0");
}
ShipNativeStatus Frame(ShipOotRenderFrameInfoV1* info) {
    info->frame = 42;
    info->camera_epoch = 3;
    info->delta_seconds = 0.05f;
    info->aspect_ratio = 2.0f;
    return SHIP_NATIVE_OK;
}
void Open(const void*, int32_t) {
    ++children;
}
void Close() {
    --children;
}
size_t Count(const std::string& op) {
    return static_cast<size_t>(std::count(ops.begin(), ops.end(), op));
}
} // namespace FakeRenderV3

namespace FakeLights {
int playTag = 0;
int otherPlay = 0;
void* current = &playTag;
bool full = false;
std::vector<std::string> ops;
ShipOotPointLightV1 lastLight{};
int infos[LINKSPAN_OOT_LIGHTS_MAX]{};
// Simula um callback de oot.light.point_color que volta ao serviço no meio da bridge (Lights_PointSetInfo).
const ShipOotLightsV1* reenter = nullptr;
uint64_t reenterHandle = 0;
std::vector<ShipNativeStatus> reentered;
void* Gameplay() {
    return current;
}
void Reenter(const ShipOotPointLightV1& light) {
    if (!reenter)
        return;
    uint64_t inner = 0;
    reentered.push_back(reenter->create_point_light("x.mod", &light, &inner));
    reentered.push_back(reenter->update_point_light(reenterHandle, &light));
    reentered.push_back(reenter->destroy_point_light(reenterHandle));
}
bool Insert(void*, uint32_t slot, const ShipOotPointLightV1& light) {
    if (full)
        return false;
    ops.push_back("insert:" + std::to_string(slot));
    lastLight = light;
    Reenter(light);
    return true;
}
void Update(uint32_t slot, const ShipOotPointLightV1& light) {
    ops.push_back("update:" + std::to_string(slot));
    lastLight = light;
    Reenter(light);
}
void Remove(void* play, uint32_t slot) {
    ops.push_back("remove:" + std::to_string(slot) + (play == &playTag ? ":a" : ":b"));
}
const void* Info(uint32_t slot) {
    return &infos[slot];
}
} // namespace FakeLights

namespace FakeWorld {
int playTag = 0;
bool inGameplay = true;
int created = 0;
std::vector<std::pair<int, bool>> freed; // (id, com play)
int submitted = 0;
int reads = 0;
int knockbacks = 0;
uint8_t lastLarge = 0;
std::vector<int*> storage;

void* Gameplay() {
    return inGameplay ? &playTag : nullptr;
}
ShipNativeStatus Floor(void*, float x, float, float z, ShipOotWorldHitV1* hit) {
    hit->hit = 1;
    hit->pos[0] = x;
    hit->pos[1] = -5.0f;
    hit->pos[2] = z;
    hit->normal[1] = 1.0f;
    hit->bg_id = 50;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus Line(void*, const float*, const float* to, uint32_t surfaces, ShipOotWorldHitV1* hit) {
    if (surfaces & LINKSPAN_OOT_WORLD_LINE_WALL) {
        hit->hit = 1;
        hit->pos[0] = to[0] / 2.0f;
    }
    return SHIP_NATIVE_OK;
}
ShipNativeStatus Wall(void*, const float*, const float* to, float, float, ShipOotWorldHitV1* hit) {
    hit->pos[0] = to[0];
    return SHIP_NATIVE_OK;
}
ShipNativeStatus Water(void*, float x, float, float* y) {
    if (x < 0.0f) {
        return SHIP_NATIVE_FAILURE;
    }
    *y = 12.0f;
    return SHIP_NATIVE_OK;
}
void* Create(void*, const ShipOotCylinderSpecV1&) {
    storage.push_back(new int(++created));
    return storage.back();
}
void Free(void* play, void* collider) {
    auto* id = static_cast<int*>(collider);
    freed.emplace_back(*id, play != nullptr);
    delete id;
}
void Submit(void*, void*) {
    ++submitted;
}
void Read(void* collider, ShipOotColliderHitsV1* hits) {
    ++reads;
    hits->ac_hit = *static_cast<int*>(collider) == 1 ? 1 : 0;
    hits->ac_dmg_flags = 0x100;
}
void Knockback(void*, void*, float, int16_t, float, uint32_t, uint8_t large) {
    ++knockbacks;
    lastLarge = large;
}
} // namespace FakeWorld

namespace FakeGetItem {
std::vector<uint8_t> given;
bool playerBusy = false;
uint32_t received = 0;

ShipNativeStatus Give(uint8_t item) {
    if (playerBusy) {
        return SHIP_NATIVE_LIMIT;
    }
    given.push_back(item);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Receive(void* user, uint8_t) {
    ++*static_cast<uint32_t*>(user);
    return SHIP_NATIVE_OK;
}
} // namespace FakeGetItem

namespace FakeText {
struct Message {
    uint8_t typePos = 0;
    std::string bytes;
};
std::map<uint16_t, Message> table;
int32_t resets = 0;

int32_t Set(int32_t language, uint16_t id, uint8_t typePos, const char* bytes, uint32_t size) {
    if (language != 0) {
        return 0;
    }
    table[id] = { typePos, std::string(bytes ? bytes : "", size) };
    return 1;
}
int32_t Remove(int32_t, uint16_t id) {
    return table.erase(id) ? 1 : 0;
}
int32_t Clear(int32_t) {
    table.clear();
    return 1;
}
int32_t Reset(int32_t) {
    ++resets;
    table = { { 0x0001, { 0x00, "a" } } };
    return 1;
}
int32_t ForEach(int32_t, int32_t (*visitor)(void*, uint16_t, uint8_t, const char*, uint32_t), void* user) {
    for (const auto& [id, message] : table) {
        if (const int32_t result =
                visitor(user, id, message.typePos, message.bytes.data(), static_cast<uint32_t>(message.bytes.size()))) {
            return result;
        }
    }
    return 0;
}
ShipNativeStatus SHIP_NATIVE_CALL Collect(void* user, uint32_t id, uint8_t boxType, uint8_t boxPos,
                                          const uint8_t* bytes, uint32_t length) {
    auto& seen = *static_cast<std::vector<std::string>*>(user);
    seen.push_back(std::to_string(id) + ":" + std::to_string(boxType) + "/" + std::to_string(boxPos) + ":" +
                   std::string(reinterpret_cast<const char*>(bytes), length));
    return seen.size() == 2 ? SHIP_NATIVE_LIMIT : SHIP_NATIVE_OK;
}
} // namespace FakeText

namespace FakeJson {
std::vector<std::string> declared;
std::string lastPath;
uint32_t lastVersion = 0;

bool Declare(const std::string& type, uint32_t minVersion, uint32_t maxVersion) {
    if (type == "Scene") {
        return false; // nome de tipo do host
    }
    declared.push_back(type + "/" + std::to_string(minVersion) + "-" + std::to_string(maxVersion));
    return true;
}
ShipNativeStatus SHIP_NATIVE_CALL Transcode(void*, const char* path, uint32_t version, ShipNativeWriteFn write,
                                            void* writer) {
    lastPath = path;
    lastVersion = version;
    if (write(writer, "<Path ", 6) != SHIP_NATIVE_OK) {
        return SHIP_NATIVE_FAILURE;
    }
    return write(writer, "Version=\"0\"/>", 13);
}
} // namespace FakeJson

namespace FakeSkeletons {
struct Instance {
    std::string skeleton;
    std::string animation;
    float speed = 1.0f;
    uint8_t mode = 0;
    int updates = 0;
    int draws = 0;
};
int destroyed = 0;

ShipNativeStatus Create(const char* skeleton, const char* animation, void** out) {
    if (std::string(skeleton).find("curva") != std::string::npos) {
        return SHIP_NATIVE_FAILURE;
    }
    *out = new Instance{ skeleton, animation };
    return SHIP_NATIVE_OK;
}
void Destroy(void* instance) {
    ++destroyed;
    delete static_cast<Instance*>(instance);
}
ShipNativeStatus Play(void* instance, const char* animation, float speed, uint8_t mode, float) {
    auto* skeleton = static_cast<Instance*>(instance);
    skeleton->animation = animation;
    skeleton->speed = speed;
    skeleton->mode = mode;
    return SHIP_NATIVE_OK;
}
uint8_t Update(void* instance) {
    auto* skeleton = static_cast<Instance*>(instance);
    return ++skeleton->updates >= 3 && skeleton->mode == LINKSPAN_OOT_ANIM_ONCE ? 1 : 0;
}
void Frame(void* instance, float* frame, float* last) {
    *frame = static_cast<float>(static_cast<Instance*>(instance)->updates);
    *last = 9.0f;
}
void Draw(void*, void* instance) {
    ++static_cast<Instance*>(instance)->draws;
}
} // namespace FakeSkeletons

namespace HostSlimHooks {
std::vector<std::string> trace;
void* seenPlay = nullptr;
void* seenPlayer = nullptr;
ShipNativeStatus SHIP_NATIVE_CALL Voice(void*, const ShipNativeHookCall* call) {
    auto* payload = static_cast<ShipOotPlayerVoiceHookV1*>(call->payload);
    if (payload->requested_sfx == 0x6800)
        payload->sfx = 0x6810;
    if (payload->requested_sfx == 0x6801)
        payload->consumed = 1;
    if (payload->requested_sfx == 0x6802) {
        payload->sfx = 0x1234;
        return SHIP_NATIVE_FAILURE; // o host restaura o payload
    }
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Veto(void* user, const ShipNativeHookCall* call) {
    static_cast<ShipOotPlayerGateHookV1*>(call->payload)->allowed = *static_cast<uint8_t*>(user);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Action(void*, const ShipNativeHookCall* call) {
    const auto* payload = static_cast<const ShipOotPlayerActionHookV1*>(call->payload);
    trace.push_back(std::string(call->phase == SHIP_NATIVE_HOOK_BEFORE ? "before:" : "after:") +
                    char('0' + payload->vanilla_allowed) + char('0' + payload->executed));
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Mask(void*, const ShipNativeHookCall* call) {
    const auto* payload = static_cast<const ShipOotPlayerMaskDrawHookV1*>(call->payload);
    if (payload->mask == 1)
        return call->call_original(call);
    trace.push_back("own:" + std::to_string(payload->mask));
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Seen(void*, const ShipNativeHookCall* call) {
    if (std::strcmp(call->point, LINKSPAN_OOT_HOOK_PLAY_DESTROY) == 0) {
        const auto* payload = static_cast<const ShipOotPlayHookV1*>(call->payload);
        seenPlay = payload->play_state;
        seenPlayer = nullptr;
    } else if (std::strcmp(call->point, LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END) == 0) {
        const auto* payload = static_cast<const ShipOotPlayerTickHookV1*>(call->payload);
        seenPlay = payload->play_state;
        seenPlayer = payload->player;
    } else {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    trace.push_back(call->point);
    return SHIP_NATIVE_OK;
}
void Run(void* context) {
    trace.push_back(static_cast<const char*>(context));
}
uint64_t Hook(const char* point, uint32_t size, uint32_t mode, uint32_t phase, ShipNativeHookFn callback,
              void* user = nullptr) {
    const ShipNativeHookSpec spec{ sizeof(spec), point, 1, size, mode, phase, 0, callback, user };
    uint64_t handle = 0;
    return ShipLuaHost::GetOotHookRegistry()->Register("hooks.test", spec, &handle) == SHIP_NATIVE_OK ? handle : 0;
}
} // namespace HostSlimHooks

namespace HostSlimAudio {
struct Add {
    int16_t delta;
    int calls = 0;
    ShipNativeStatus status = SHIP_NATIVE_OK;
};
ShipNativeStatus SHIP_NATIVE_CALL Transform(void* user, const ShipNativeHookCall* call) {
    auto* add = static_cast<Add*>(user);
    const auto* payload = static_cast<const ShipOotAudioMixHookV1*>(call->payload);
    ++add->calls;
    for (uint32_t index = 0; index < payload->frames * payload->channels; ++index)
        payload->samples[index] = int16_t(std::clamp(payload->samples[index] + int32_t(add->delta), -32768, 32767));
    return add->status;
}
std::vector<std::vector<int16_t>> seen;
ShipNativeStatus SHIP_NATIVE_CALL Observe(void*, const ShipNativeHookCall* call) {
    const auto* payload = static_cast<const ShipOotAudioMixHookV1*>(call->payload);
    seen.emplace_back(payload->samples, payload->samples + payload->frames * payload->channels);
    return SHIP_NATIVE_OK;
}
std::atomic<bool> entered{ false };
std::atomic<bool> release{ false };
std::atomic<bool> exited{ false };
std::atomic<bool> unregisterStarted{ false };
std::atomic<bool> sawWait{ false };
ShipNativeStatus SHIP_NATIVE_CALL Slow(void*, const ShipNativeHookCall*) {
    entered.store(true); // persistente: a thread do jogo não perde a entrada
    while (!release.load())
        std::this_thread::yield();
    exited.store(true);
    return SHIP_NATIVE_OK;
}
uint64_t Hook(uint32_t mode, uint32_t phase, int32_t priority, ShipNativeHookFn callback, void* user) {
    const ShipNativeHookSpec spec{ sizeof(spec),
                                   LINKSPAN_OOT_HOOK_AUDIO_MIX,
                                   LINKSPAN_OOT_HOOK_AUDIO_MIX_VERSION,
                                   sizeof(ShipOotAudioMixHookV1),
                                   mode,
                                   phase,
                                   priority,
                                   callback,
                                   user };
    uint64_t handle = 0;
    return ShipLuaHost::GetOotHookRegistry()->Register("audio.test", spec, &handle) == SHIP_NATIVE_OK ? handle : 0;
}
} // namespace HostSlimAudio

namespace HostSlimArchives {
struct Seen {
    std::string path, owner, prefix;
    uint32_t order;
};
std::vector<Seen> seen;
uint32_t stride = 0;
int calls = 0;
ShipNativeStatus SHIP_NATIVE_CALL Observe(void*, const ShipNativeHookCall* call) {
    const auto* payload = static_cast<const ShipOotModArchivesHookV1*>(call->payload);
    ++calls;
    stride = payload->archive_stride;
    const auto* bytes = reinterpret_cast<const uint8_t*>(payload->archives);
    for (uint32_t index = 0; index < payload->count; ++index) {
        const auto* entry = reinterpret_cast<const ShipOotModArchiveV1*>(bytes + index * payload->archive_stride);
        seen.push_back({ entry->path, entry->owner, entry->private_prefix, entry->order });
    }
    return SHIP_NATIVE_OK;
}
} // namespace HostSlimArchives

namespace HostSlimMovement {
int playTag = 0, playerTag = 0, inputTag = 0, idleTag = 0, walkTag = 0;
int otherPlayTag = 0, otherPlayerTag = 0;
void* snapshotPlay = &playTag;
void* snapshotPlayer = &playerTag;
bool gameplay = true, eligible = true, neutral = true;
const void* action = &idleTag;
uint64_t tick = 10;
int restores = 0, idleRestores = 0, vanillaRuns = 0, revokes = 0;
uint64_t lastRevoked = 0;

bool Snapshot(ShipLuaHost::OotActionSnapshot* out) {
    if (!gameplay)
        return false;
    out->play = snapshotPlay;
    out->player = snapshotPlayer;
    out->action = action;
    out->tick = tick;
    out->flags = action == &idleTag ? SHIP_OOT_ACTION_STATE_IDLE : 0;
    out->eligible = eligible;
    out->neutral = eligible && neutral;
    return true;
}
ShipNativeStatus Restore(const ShipLuaHost::OotActionSnapshot&, bool releaseIdle) {
    ++restores;
    if (releaseIdle) {
        ++idleRestores;
        if (action != &idleTag) {
            action = &idleTag;
            ShipLuaHost::NotifyOotPlayerActionChanged(&playerTag);
        }
    }
    return SHIP_NATIVE_OK;
}
const ShipOotMovementV3* revokeMovement = nullptr;
ShipOotPlayerActionSpecV1 revokeSpec{};
bool acquireOnRevoke = false, addOnRevoke = false;
ShipNativeStatus acquireDuringRevoke = SHIP_NATIVE_OK;
uint64_t tokenDuringRevoke = 0;
int lateRevokes = 0;

void LateRevoked(uint64_t) {
    ++lateRevokes;
}
void Revoked(uint64_t token) {
    ++revokes;
    lastRevoked = token;
    if (acquireOnRevoke) {
        tokenDuringRevoke = 7;
        acquireDuringRevoke = revokeMovement->acquire_action("move.listener", &revokeSpec, &tokenDuringRevoke);
    }
    if (addOnRevoke)
        ShipLuaHost::AddOotActionRevokeListener(LateRevoked);
}
void Vanilla(void*) {
    ++vanillaRuns;
}

struct Owner {
    const ShipOotMovementV3* movement = nullptr;
    uint64_t token = 0;
    int calls = 0;
    uint32_t disposition = SHIP_OOT_ACTION_PASS;
    ShipNativeStatus status = SHIP_NATIVE_OK;
    uint8_t expectedAllowed = 1;
    bool releaseInside = false;
    bool changeActionInside = false;
    bool directActionInside = false;
    bool loseContextInside = false;
    bool changePlayInside = false;
    bool changePlayerInside = false;
    bool recreatePlayerInside = false;
    bool throwInside = false;
    bool ok = true;
};
ShipNativeStatus SHIP_NATIVE_CALL Tick(void* user, const ShipOotPlayerActionTickV1* tick, uint32_t* disposition) {
    auto& owner = *static_cast<Owner*>(user);
    ++owner.calls;
    owner.ok = owner.ok && tick->size == sizeof(*tick) && tick->action_token == owner.token &&
               tick->play_state == &playTag && tick->player == &playerTag && tick->input == &inputTag &&
               tick->state.owned && tick->state.scene_epoch == ShipLuaHost::OotSceneEpoch() &&
               tick->vanilla_allowed == owner.expectedAllowed;
    *disposition = owner.disposition;
    if (owner.releaseInside) {
        const int before = restores;
        const ShipOotPlayerActionSpecV1 spec{ sizeof(spec), 0, Tick, user };
        uint64_t again = 7;
        owner.ok = owner.ok && owner.movement->release_action(owner.token) == SHIP_NATIVE_OK && restores == before &&
                   owner.movement->acquire_action("move.other", &spec, &again) == SHIP_NATIVE_LIMIT && again == 0;
    }
    if (owner.changeActionInside) {
        action = &walkTag;
        ShipLuaHost::NotifyOotPlayerActionChanged(&playerTag);
    }
    if (owner.directActionInside)
        action = &walkTag; // escrita direta, sem notificação
    if (owner.loseContextInside)
        gameplay = false;
    if (owner.changePlayInside)
        snapshotPlay = &otherPlayTag;
    if (owner.changePlayerInside)
        snapshotPlayer = &otherPlayerTag;
    if (owner.recreatePlayerInside)
        ShipLuaHost::NotifyOotPlayerCreated(&playerTag);
    if (owner.throwInside)
        throw 7;
    return owner.status;
}
uint8_t Dispatch(uint8_t allowed = 1) {
    return ShipLuaHost::DispatchOotPlayerAction(&playTag, &playerTag, &inputTag, allowed, Vanilla, nullptr);
}
const ShipOotMovementV3* Find(const ShipLua::NativeProviderPolicy& policy) {
    const ShipOotMovementV3* found = nullptr;
    int matches = 0;
    for (const auto& service : policy.services) {
        if (service.name == LINKSPAN_OOT_MOVEMENT_SERVICE && service.version == LINKSPAN_OOT_MOVEMENT_VERSION_3) {
            ++matches;
            if (service.size == sizeof(ShipOotMovementV3))
                found = static_cast<const ShipOotMovementV3*>(service.table);
        }
    }
    return matches == 1 ? found : nullptr;
}
} // namespace HostSlimMovement

namespace HostSlimSkeletons {
struct Instance {
    uint32_t format = SHIP_OOT_SKELETON_STANDARD;
    std::string animation;
    float frame = 0.0f;
    float speed = 1.0f;
    uint8_t mode = LINKSPAN_OOT_ANIM_LOOP;
    bool reverse = false;
};
int created = 0, destroyed = 0, plays = 0, draws = 0;
std::vector<std::string> playLog;
std::set<std::string> notLoaded; // caminhos "__OTR__..." que não são animação do formato
bool failCreate = false;

ShipNativeStatus CreateEx(uint32_t format, const char*, const char* animation, const int16_t*, void** out,
                          float* assetLastFrame) {
    if (failCreate)
        return SHIP_NATIVE_FAILURE;
    auto* instance = new Instance;
    instance->format = format;
    instance->animation = animation;
    ++created;
    *out = instance;
    *assetLastFrame = 9.0f;
    return SHIP_NATIVE_OK;
}
void Destroy(void* instance) {
    ++destroyed;
    delete static_cast<Instance*>(instance);
}
ShipNativeStatus PlayEx(void* raw, const char* animation, float speed, uint8_t mode, float, bool reverse,
                        float* assetLastFrame) {
    auto* instance = static_cast<Instance*>(raw);
    instance->animation = animation;
    instance->speed = reverse ? -speed : speed;
    instance->mode = mode;
    instance->reverse = reverse;
    instance->frame = reverse ? 9.0f : 0.0f;
    ++plays;
    playLog.push_back(animation);
    *assetLastFrame = 9.0f;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus Info(void*, const char* animation, ShipOotAnimationInfoV2* info) {
    info->present = 1;
    info->loaded = notLoaded.count(animation) ? 0 : 1;
    info->asset_last_frame = 9.0f;
    return SHIP_NATIVE_OK;
}
uint8_t Update(void* raw) {
    auto* instance = static_cast<Instance*>(raw);
    instance->frame += instance->speed;
    const bool done = instance->mode == LINKSPAN_OOT_ANIM_ONCE &&
                      (instance->reverse ? instance->frame <= 0.0f : instance->frame >= 9.0f);
    return done ? 1 : 0;
}
void Frame(void* raw, float* frame, float* lastFrame) {
    *frame = static_cast<Instance*>(raw)->frame;
    *lastFrame = 9.0f;
}
void State(void* raw, float* frame, float* endFrame, float* speed) {
    const auto* instance = static_cast<Instance*>(raw);
    *frame = instance->frame;
    *endFrame = instance->reverse ? 0.0f : 9.0f;
    *speed = instance->speed;
}
void DrawEx(void*, void*, void*, void (*limb)(void* context, int32_t limb), void* context) {
    ++draws;
    for (int32_t index = 0; index < 3; ++index)
        limb(context, index);
}
uint64_t Tick() {
    return 77;
}
void Install() {
    ShipLuaHost::OotSkeletonsBridge bridge;
    bridge.destroy = Destroy;
    bridge.update = Update;
    bridge.frame = Frame;
    bridge.createEx = CreateEx;
    bridge.playEx = PlayEx;
    bridge.animationInfo = Info;
    bridge.state = State;
    bridge.drawEx = DrawEx;
    bridge.tick = Tick;
    ShipLuaHost::SetOotSkeletonsBridge(bridge);
}

struct Limbs {
    const ShipOotSkeletonsV2* skeletons = nullptr;
    uint64_t skeleton = 0;
    std::vector<int32_t> seen;
    ShipNativeStatus status = SHIP_NATIVE_OK;
    bool destroyInside = false;
    bool throwInside = false;
    bool replaceInside = false;
    bool reenterInside = false;
    std::vector<int32_t> recovered;
    bool ok = true;
};
int playTag = 0, actorTag = 0;
ShipNativeStatus SHIP_NATIVE_CALL RecoveredLimb(void* user, const ShipOotSkeletonLimbV2* limb) {
    static_cast<Limbs*>(user)->recovered.push_back(limb->limb);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Limb(void* user, const ShipOotSkeletonLimbV2* limb) {
    auto& limbs = *static_cast<Limbs*>(user);
    limbs.ok = limbs.ok && limb->size == sizeof(*limb) && limb->skeleton == limbs.skeleton &&
               limb->play_state == &playTag && limb->actor == &actorTag &&
               limb->scene_epoch == ShipLuaHost::OotSceneEpoch();
    limbs.seen.push_back(limb->limb);
    if (limbs.destroyInside)
        limbs.ok = limbs.ok && limbs.skeletons->destroy(limbs.skeleton) == SHIP_NATIVE_OK;
    if (limbs.replaceInside) {
        limbs.replaceInside = false;
        limbs.ok = limbs.ok && limbs.skeletons->set_post_limb(limbs.skeleton, RecoveredLimb, &limbs) == SHIP_NATIVE_OK;
        if (limbs.reenterInside)
            limbs.ok =
                limbs.ok && limbs.skeletons->draw_ex(limb->play_state, limb->actor, limbs.skeleton) == SHIP_NATIVE_OK;
    }
    if (limbs.throwInside)
        throw 7;
    return limbs.status;
}
const ShipOotSkeletonsV2* Find(const ShipLua::NativeProviderPolicy& policy) {
    for (const auto& service : policy.services) {
        if (service.name == LINKSPAN_OOT_SKELETONS_SERVICE && service.version == LINKSPAN_OOT_SKELETONS_VERSION_2 &&
            service.size == sizeof(ShipOotSkeletonsV2))
            return static_cast<const ShipOotSkeletonsV2*>(service.table);
    }
    return nullptr;
}
} // namespace HostSlimSkeletons

namespace HostSlimBody {
int playTag = 0, playerTag = 0, actorTag = 0, gameTag = 0;
bool luaBody = false, failSegments = false;
int activated = 0, deactivated = 0, shownLink = 0, roots = 0, rootMoves = 0, rootRestarts = 0, rootStops = 0;
std::map<uint32_t, std::string> segments;

bool LuaBody() {
    return luaBody;
}
ShipNativeStatus Activate(uint64_t, float, void** out) {
    ++activated;
    *out = &gameTag;
    return SHIP_NATIVE_OK;
}
void Deactivate(void*, bool inScene, bool showLink) {
    ++deactivated;
    if (inScene && showLink)
        ++shownLink;
}
ShipNativeStatus Segments(void*, const std::map<uint32_t, std::string>& next) {
    if (failSegments)
        return SHIP_NATIVE_FAILURE;
    segments = next;
    return SHIP_NATIVE_OK;
}
void Root(void*, bool move, bool restart) {
    ++roots;
    rootMoves += move ? 1 : 0;
    rootRestarts += restart ? 1 : 0;
}
void RootStop(void*) {
    ++rootStops;
}

struct Controller {
    const ShipOotPlayerBodyV1* body = nullptr;
    uint64_t control = 0;
    uint64_t action = 0;
    uint32_t rootMode = SHIP_OOT_ROOT_DISABLED;
    bool fallbackWalk = false;
    int ticks = 0, posts = 0, draws = 0;
    uint8_t drawDefault = 1;
    ShipNativeStatus tickStatus = SHIP_NATIVE_OK;
    ShipNativeStatus drawStatus = SHIP_NATIVE_OK;
    bool throwTick = false, throwPost = false, throwDraw = false;
    bool endSceneTick = false, endScenePost = false, endSceneDraw = false;
    bool ok = true;
};
ShipNativeStatus SHIP_NATIVE_CALL Tick(void* user, const ShipOotBodyTickV1* tick) {
    auto& c = *static_cast<Controller*>(user);
    ++c.ticks;
    c.ok = c.ok && tick->size == sizeof(*tick) && tick->control == c.control && tick->play_state == &playTag &&
           tick->player == &playerTag && tick->state.active && tick->state.controlled;
    if (c.rootMode != SHIP_OOT_ROOT_DISABLED)
        c.ok = c.ok && c.body->set_root_motion(c.control, c.action, c.rootMode) == SHIP_NATIVE_OK;
    if (c.fallbackWalk) {
        const ShipOotAnimationPlayV2 play{ sizeof(play), 1.0f, 0.0f, LINKSPAN_OOT_ANIM_LOOP, 0, { 0, 0 } };
        c.ok = c.ok && c.body->start_animation(c.control, "andar", &play, SHIP_OOT_ANIM_FALLBACK) == SHIP_NATIVE_OK;
    }
    if (c.endSceneTick)
        ShipLuaHost::OotPlayerBodySceneEnded();
    if (c.throwTick)
        throw 7;
    return c.tickStatus;
}
ShipNativeStatus SHIP_NATIVE_CALL PostLimb(void* user, const ShipOotSkeletonLimbV2*) {
    auto& c = *static_cast<Controller*>(user);
    ++c.posts;
    if (c.endScenePost)
        ShipLuaHost::OotPlayerBodySceneEnded();
    if (c.throwPost)
        throw 7;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Draw(void* user, const ShipOotBodyDrawV1* draw, uint8_t* drawDefault) {
    auto& c = *static_cast<Controller*>(user);
    ++c.draws;
    c.ok = c.ok && draw->size == sizeof(*draw) && draw->actor == &actorTag && draw->state.active;
    *drawDefault = c.drawDefault;
    if (c.endSceneDraw)
        ShipLuaHost::OotPlayerBodySceneEnded();
    if (c.throwDraw)
        throw 7;
    return c.drawStatus;
}
const ShipOotPlayerBodyV1* Find(const ShipLua::NativeProviderPolicy& policy) {
    for (const auto& service : policy.services) {
        if (service.name == LINKSPAN_OOT_PLAYER_BODY_SERVICE && service.version == LINKSPAN_OOT_PLAYER_BODY_VERSION &&
            service.size == sizeof(ShipOotPlayerBodyV1))
            return static_cast<const ShipOotPlayerBodyV1*>(service.table);
    }
    return nullptr;
}
} // namespace HostSlimBody

int main(int argc, char** argv) {
    for (int bit = 0; bit < 32; ++bit)
        gBitFlags[bit] = uint32_t{ 1 } << bit;
    ShipLuaHost::SetOotNativeGamepadBridge({ HasGamepad, GetGamepadButtons, ClearButton, BindButton, ReloadMappings,
                                             GetSettingInt, SetSettingInt, GetGamepadAxis, BindAxis });
    ShipLuaHost::SetOotNativeResourceBridge({ HasResourceFile, ReadResourceFile, ListResourceFiles, DirtyResources,
                                              UnloadResource, MountArchive, UnmountArchive, GetGameVersions,
                                              ReadResourceFileLayers });
    auto policy = ShipLuaHost::CreateOotNativePolicy();
    {
        Check(policy.publicSymbols && policy.publicSymbols->layoutId == LINKSPAN_OOT_LAYOUT_ID,
              "catálogo público com o layout do host");
        uintptr_t address = 0xDEAD;
        Check(policy.publicSymbols->resolve("Math_SinS", &address) == SHIP_NATIVE_OK &&
                  address == reinterpret_cast<uintptr_t>(&PublicProbeSinS),
              "função pública resolve pelo catálogo");
        address = 0xDEAD;
        Check(policy.publicSymbols->resolve("gItemIcons", &address) == SHIP_NATIVE_OK &&
                  address == reinterpret_cast<uintptr_t>(&gPublicProbeIcons),
              "variável pública dá o endereço do armazenamento");
        for (const char* name : { "Math_Sin", "Math_SinSX", "", "z_lib.c!Math_SinS", "gitemicons" }) {
            address = 0xDEAD;
            Check(policy.publicSymbols->resolve(name, &address) == SHIP_NATIVE_UNSUPPORTED && address == 0,
                  "nome fora do catálogo: UNSUPPORTED e saída zero");
        }
        address = 0xDEAD;
        Check(policy.publicSymbols->resolve(nullptr, &address) == SHIP_NATIVE_INVALID_ARGUMENT, "nome nulo recusado");
        Check(policy.publicSymbols->resolve("Math_SinS", nullptr) == SHIP_NATIVE_INVALID_ARGUMENT,
              "saída nula recusada");
        static const LinkSpanPublicSymbol unsorted[] = { { "b", &failures, 1 }, { "a", &failures, 1 } };
        Check(ShipLuaHost::FindOotPublicSymbol(unsorted, 2, "a") == nullptr, "tabela fora de ordem não é usada");
        static const LinkSpanPublicSymbol sorted[] = { { "A", &failures, 1 },
                                                       { "B", &failures, 1 },
                                                       { "_c", &failures, 1 } };
        Check(ShipLuaHost::FindOotPublicSymbol(sorted, 3, "_c") == &sorted[2] &&
                  ShipLuaHost::FindOotPublicSymbol(sorted, 3, "A") == &sorted[0],
              "busca binária na ordem de strcmp");
    }
    {
        using namespace HostSlimArchives;
        auto* registry = ShipLuaHost::GetOotHookRegistry();
        const auto point = ShipLuaHost::GetOotHookPoints().modsArchives;
        Check(point && registry->FindPoint(LINKSPAN_OOT_HOOK_MODS_ARCHIVES, 1) == point, "oot.mods.archives v1");
        const ShipNativeHookSpec replace{ sizeof(replace),
                                          LINKSPAN_OOT_HOOK_MODS_ARCHIVES,
                                          1,
                                          sizeof(ShipOotModArchivesHookV1),
                                          SHIP_NATIVE_HOOK_REPLACE,
                                          0,
                                          0,
                                          Observe,
                                          nullptr };
        uint64_t handle = 0;
        Check(registry->Register("archives.test", replace, &handle) == SHIP_NATIVE_UNSUPPORTED, "só OBSERVE");
        const std::vector<ShipLuaHost::OotModArchive> archives{
            { "C:/jogo/mods/camada.o2r", "", "mod/camada/" },
            { "C:/jogo/mods/fases/assets/fase.o2r", "exemplo.fases", "" },
        };
        ShipLuaHost::DispatchOotModArchives(archives); // sem hook: nada a fazer
        Check(calls == 0, "sem observer, nenhuma chamada");
        const ShipNativeHookSpec observe{ sizeof(observe),
                                          LINKSPAN_OOT_HOOK_MODS_ARCHIVES,
                                          1,
                                          sizeof(ShipOotModArchivesHookV1),
                                          SHIP_NATIVE_HOOK_OBSERVE,
                                          SHIP_NATIVE_HOOK_AFTER,
                                          0,
                                          Observe,
                                          nullptr };
        Check(registry->Register("archives.test", observe, &handle) == SHIP_NATIVE_OK, "observer registrado");
        ShipLuaHost::DispatchOotModArchives(archives);
        Check(stride == sizeof(ShipOotModArchiveV1) && seen.size() == 2, "duas entradas, stride do host");
        Check(seen.size() == 2 && seen[0].order == 0 && seen[0].owner.empty() && seen[0].prefix == "mod/camada/" &&
                  seen[1].order == 1 && seen[1].owner == "exemplo.fases" && seen[1].prefix.empty() &&
                  seen[1].path == "C:/jogo/mods/fases/assets/fase.o2r",
              "entradas na ordem dada, com dono e prefixo");
        seen.clear();
        calls = 0;
        ShipLuaHost::DispatchOotModArchives({});
        Check(calls == 1 && seen.empty(), "lista vazia também é entregue, sem entradas");
        registry->RemoveOwner("archives.test");
    }
    {
        using namespace HostSlimAudio;
        auto* registry = ShipLuaHost::GetOotHookRegistry();
        int16_t pcm[4] = { 100, -100, 32000, -32000 }; // 2 quadros estéreo
        Check(!ShipLuaHost::OotAudioMixActive() && !ShipLuaHost::MixOotAudio(pcm, 2) && pcm[0] == 100,
              "sem hook nada é publicado e o buffer fica intacto");
        Check(!Hook(SHIP_NATIVE_HOOK_REPLACE, 0, 0, Transform, nullptr), "oot.audio.mix não aceita REPLACE");
        Add big{ 1000 }, small{ 10 };
        const auto bigHandle = Hook(SHIP_NATIVE_HOOK_TRANSFORM, 0, 5, Transform, &big);
        Hook(SHIP_NATIVE_HOOK_TRANSFORM, 0, 1, Transform, &small);
        Hook(SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_BEFORE, 0, Observe, nullptr);
        Hook(SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_AFTER, 0, Observe, nullptr);
        Check(ShipLuaHost::OotAudioMixActive(), "o registro publica a lista");
        seen.clear();
        Check(ShipLuaHost::MixOotAudio(pcm, 2) && pcm[0] == 1110 && pcm[1] == 910 && pcm[2] == 32767 &&
                  pcm[3] == -30990,
              "transforms por prioridade, com saturação");
        Check(seen.size() == 2 && seen[0] == std::vector<int16_t>{ 100, -100, 32000, -32000 } &&
                  seen[1] == std::vector<int16_t>{ 1110, 910, 32767, -30990 },
              "BEFORE vê o jogo e AFTER vê a mistura");

        big.status = SHIP_NATIVE_FAILURE;
        int16_t again[4]{};
        for (int round = 0; round < 2; ++round) {
            std::fill(std::begin(again), std::end(again), int16_t{ 0 });
            ShipLuaHost::MixOotAudio(again, 2);
        }
        // Reconstruir conserva as duas falhas. A próxima chamada é a terceira.
        const auto extra = Hook(SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_AFTER, 0, Observe, nullptr);
        std::fill(std::begin(again), std::end(again), int16_t{ 0 });
        ShipLuaHost::MixOotAudio(again, 2);
        Check(again[0] == 10 && again[3] == 10, "transform que falha é desfeito e o seguinte roda");
        // Reconstruir antes da bomba conserva também disabled.
        Check(registry->Unregister("audio.test", extra) == SHIP_NATIVE_OK, "reconstrução antes da bomba");
        big.calls = 0;
        ShipLuaHost::MixOotAudio(again, 2);
        Check(big.calls == 0, "três falhas desligam o hook na thread de áudio");
        ShipLuaHost::PumpOotAudioMix();
        const auto described = registry->Describe();
        Check(
            std::any_of(described.begin(), described.end(),
                        [&](const ShipLua::NativeHookInfo& info) { return info.handle == bigHandle && !info.enabled; }),
            "a bomba desativa o hook no registro");
        big.status = SHIP_NATIVE_OK;
        ShipLuaHost::MixOotAudio(again, 2);
        Check(big.calls == 0, "a lista nova não tem o hook desativado");

        std::vector<int16_t> huge((LINKSPAN_OOT_AUDIO_MAX_FRAMES + 1) * LINKSPAN_OOT_AUDIO_CHANNELS);
        Check(!ShipLuaHost::MixOotAudio(huge.data(), LINKSPAN_OOT_AUDIO_MAX_FRAMES + 1),
              "acima de 1680 quadros é recusado");

        const auto slow = Hook(SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_AFTER, 0, Slow, nullptr);
        entered.store(false);
        release.store(false);
        exited.store(false);
        unregisterStarted.store(false);
        std::thread audio([] {
            int16_t buffer[4]{};
            ShipLuaHost::MixOotAudio(buffer, 2);
        });
        while (!entered.load())
            std::this_thread::yield();
        sawWait.store(false);
        std::thread releaser([] {
            while (!unregisterStarted.load())
                std::this_thread::yield();
            // Só solta o callback depois de ver o Unregister esperando a thread de áudio (com prazo, para uma
            // implementação sem espera reprovar em vez de travar).
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!ShipLuaHost::OotAudioMixWaitingForReaders() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            sawWait.store(ShipLuaHost::OotAudioMixWaitingForReaders());
            release.store(true);
        });
        unregisterStarted.store(true); // começo da tentativa, com Slow já dentro do callback
        const auto status = registry->Unregister("audio.test", slow);
        Check(status == SHIP_NATIVE_OK && sawWait.load() && exited.load() &&
                  !ShipLuaHost::OotAudioMixWaitingForReaders(),
              "unregister só volta depois que a thread de áudio saiu do callback");
        audio.join();
        releaser.join();
        registry->RemoveOwner("audio.test");
        Check(!ShipLuaHost::OotAudioMixActive(), "sem hooks, nada publicado");
    }
    {
        using namespace HostSlimHooks;
        const auto& points = ShipLuaHost::GetOotHookPoints();
        auto* registry = ShipLuaHost::GetOotHookRegistry();
        Check(points.playerVoiceSfx && points.playerRollAllowed && points.playerLedgeGrabAllowed &&
                  points.playerAction && points.playerUpdateEnd && points.playerMaskDraw && points.playDestroy,
              "7 pontos novos declarados");
        Check(registry->FindPoint(LINKSPAN_OOT_HOOK_PLAYER_ACTION, 1) == points.playerAction,
              "oot.player.action v1 pelo nome");
        Check(!Hook(LINKSPAN_OOT_HOOK_PLAYER_ACTION, sizeof(ShipOotPlayerActionHookV1), SHIP_NATIVE_HOOK_REPLACE, 0,
                    Action) &&
                  !Hook(LINKSPAN_OOT_HOOK_PLAYER_ROLL_ALLOWED, sizeof(ShipOotPlayerGateHookV1),
                        SHIP_NATIVE_HOOK_REPLACE, 0, Veto) &&
                  !Hook(LINKSPAN_OOT_HOOK_PLAY_DESTROY, sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_TRANSFORM, 0, Seen),
              "modos fora do contrato são recusados");

        uint16_t sfx = 0x6800;
        Check(ShipLuaHost::DispatchOotPlayerVoice(&play, &player, &sfx) == 0 && sfx == 0x6800,
              "sem hook a voz passa intacta");
        Hook(LINKSPAN_OOT_HOOK_PLAYER_VOICE_SFX, sizeof(ShipOotPlayerVoiceHookV1), SHIP_NATIVE_HOOK_TRANSFORM, 0,
             Voice);
        Check(ShipLuaHost::DispatchOotPlayerVoice(&play, &player, &sfx) == 0 && sfx == 0x6810, "voz trocada");
        sfx = 0x6801;
        Check(ShipLuaHost::DispatchOotPlayerVoice(&play, &player, &sfx) == 1, "voz consumida");
        sfx = 0x6802;
        Check(ShipLuaHost::DispatchOotPlayerVoice(&play, &player, &sfx) == 0 && sfx == 0x6802,
              "transform que falha não troca a voz");

        uint8_t answer = 0;
        Check(ShipLuaHost::DispatchOotPlayerGate(points.playerRollAllowed, &play, &player, 1) == 1,
              "sem hook o rolamento segue o jogo");
        Hook(LINKSPAN_OOT_HOOK_PLAYER_ROLL_ALLOWED, sizeof(ShipOotPlayerGateHookV1), SHIP_NATIVE_HOOK_TRANSFORM, 0,
             Veto, &answer);
        Check(ShipLuaHost::DispatchOotPlayerGate(points.playerRollAllowed, &play, &player, 1) == 0, "hook veta");
        answer = 1;
        Check(ShipLuaHost::DispatchOotPlayerGate(points.playerRollAllowed, &play, &player, 0) == 0,
              "hook não libera o que o jogo vetou");
        Check(ShipLuaHost::DispatchOotPlayerGate(points.playerLedgeGrabAllowed, &play, &player, 1) == 1,
              "o veto de um ponto não vaza para o outro");

        trace.clear();
        Check(ShipLuaHost::DispatchOotPlayerAction(&play, &player, nullptr, 1, Run, (void*)"action") == 1 &&
                  trace == std::vector<std::string>{ "action" },
              "sem hook a ação roda");
        Hook(LINKSPAN_OOT_HOOK_PLAYER_ACTION, sizeof(ShipOotPlayerActionHookV1), SHIP_NATIVE_HOOK_OBSERVE,
             SHIP_NATIVE_HOOK_BEFORE, Action);
        Hook(LINKSPAN_OOT_HOOK_PLAYER_ACTION, sizeof(ShipOotPlayerActionHookV1), SHIP_NATIVE_HOOK_OBSERVE,
             SHIP_NATIVE_HOOK_AFTER, Action);
        trace.clear();
        Check(ShipLuaHost::DispatchOotPlayerAction(&play, &player, nullptr, 1, Run, (void*)"action") == 1,
              "ação roda com observers");
        Check(ShipLuaHost::DispatchOotPlayerAction(&play, &player, nullptr, 0, Run, (void*)"action") == 0,
              "PAUSE/GameInteractor segura a ação");
        Check(trace == std::vector<std::string>{ "before:10", "action", "after:11", "before:00", "after:00" },
              "BEFORE e AFTER em volta da ação, com executed");

        trace.clear();
        ShipLuaHost::DispatchOotPlayerMaskDraw(&play, &player, 2, 1, Run, (void*)"vanilla");
        Check(trace == std::vector<std::string>{ "vanilla" }, "sem hook desenha a máscara do jogo");
        Hook(LINKSPAN_OOT_HOOK_PLAYER_MASK_DRAW, sizeof(ShipOotPlayerMaskDrawHookV1), SHIP_NATIVE_HOOK_REPLACE, 0,
             Mask);
        Check(!Hook(LINKSPAN_OOT_HOOK_PLAYER_MASK_DRAW, sizeof(ShipOotPlayerMaskDrawHookV1), SHIP_NATIVE_HOOK_REPLACE,
                    0, Mask),
              "REPLACE da máscara é exclusivo");
        trace.clear();
        ShipLuaHost::DispatchOotPlayerMaskDraw(&play, &player, 2, 1, Run, (void*)"vanilla");
        ShipLuaHost::DispatchOotPlayerMaskDraw(&play, &player, 1, 1, Run, (void*)"vanilla");
        ShipLuaHost::DispatchOotPlayerMaskDraw(&play, &player, 1, 0, Run, (void*)"vanilla");
        Check(trace == std::vector<std::string>{ "own:2", "vanilla" },
              "REPLACE troca a máscara; call_original desenha só se o jogo permitir");

        trace.clear();
        Hook(LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END, sizeof(ShipOotPlayerTickHookV1), SHIP_NATIVE_HOOK_OBSERVE,
             SHIP_NATIVE_HOOK_AFTER, Seen);
        ShipLuaHost::DispatchOotPlayerTick(points.playerUpdateEnd, &play, &player);
        Check(seenPlay == &play && seenPlayer == &player, "update_end leva PlayState e Player");
        Hook(LINKSPAN_OOT_HOOK_PLAY_DESTROY, sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE,
             SHIP_NATIVE_HOOK_BEFORE, Seen);
        ShipLuaHost::DispatchOotPlayDestroy(&play);
        Check(trace ==
                      std::vector<std::string>{ LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END, LINKSPAN_OOT_HOOK_PLAY_DESTROY } &&
                  seenPlay == &play && seenPlayer == nullptr,
              "play.destroy com o PlayState");
        registry->RemoveOwner("hooks.test");
        Check(!ShipLuaHost::HasOotHooks(points.playerAction) && !ShipLuaHost::HasOotHooks(points.playerMaskDraw),
              "hooks do dono saem");
    }
    {
        using namespace HostSlimMovement;
        static_assert(offsetof(ShipOotMovementV3, get_action_state) == sizeof(ShipOotMovementV2));
        static_assert(sizeof(ShipOotMovementV3) == sizeof(ShipOotMovementV2) + 4 * sizeof(void*));
        static_assert(sizeof(ShipOotPlayerActionStateV1) == 40 && sizeof(ShipOotPlayerActionTickV1) == 88 &&
                      sizeof(ShipOotPlayerActionSpecV1) == 24);
        const auto* movement = Find(policy);
        Check(movement && movement->size == sizeof(ShipOotMovementV3) && movement->layout_id &&
                  std::strcmp(movement->layout_id, LINKSPAN_OOT_LAYOUT_ID) == 0,
              "movement V3 publicado uma vez, com layout");
        if (movement) {
            ShipLuaHost::SetOotActionBridge({ Snapshot, Restore });
            ShipLuaHost::AddOotActionRevokeListener(Revoked);
            ShipLuaHost::NotifyOotPlayerCreated(&playerTag);
            Owner owner;
            owner.movement = movement;
            const ShipOotPlayerActionSpecV1 spec{ sizeof(spec), 0, Tick, &owner };
            auto shortSpec = spec;
            shortSpec.size = sizeof(spec) - 1;
            auto noCallback = spec;
            noCallback.callback = nullptr;
            const std::string longOwner(129, 'x');
            uint64_t token = 7, other = 7;
            ShipOotPlayerActionStateV1 state{};
            state.size = sizeof(state) - 1;
            Check(movement->get_action_state(nullptr) == SHIP_NATIVE_INVALID_ARGUMENT &&
                      movement->get_action_state(&state) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "estado nulo ou curto");
            Check(movement->acquire_action("move.a", &shortSpec, &token) == SHIP_NATIVE_INVALID_ARGUMENT && token == 0,
                  "spec curta zera o token");
            for (const char* bad : { "", longOwner.c_str(), "a\nb", "a\x7f" })
                Check(movement->acquire_action(bad, &spec, &token) == SHIP_NATIVE_INVALID_ARGUMENT && token == 0,
                      "dono inválido");
            Check(movement->acquire_action(nullptr, &spec, &token) == SHIP_NATIVE_INVALID_ARGUMENT &&
                      movement->acquire_action("move.a", nullptr, &token) == SHIP_NATIVE_INVALID_ARGUMENT &&
                      movement->acquire_action("move.a", &noCallback, &token) == SHIP_NATIVE_INVALID_ARGUMENT &&
                      movement->acquire_action("move.a", &spec, nullptr) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "argumentos obrigatórios");
            Check(movement->release_action(0) == SHIP_NATIVE_INVALID_ARGUMENT &&
                      movement->release_action(1) == SHIP_NATIVE_INVALID_ARGUMENT &&
                      movement->restore_player_animation(0) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "token zero ou de outro domínio");

            state.size = sizeof(state);
            gameplay = false;
            Check(movement->get_action_state(&state) == SHIP_NATIVE_UNSUPPORTED &&
                      (state.flags & SHIP_OOT_ACTION_STATE_BLOCKED) &&
                      movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_UNSUPPORTED,
                  "sem Player");
            gameplay = true;
            eligible = false;
            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_UNSUPPORTED, "Link ocupado");
            eligible = true;

            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK && token, "primeiro dono");
            owner.token = token;
            Check(movement->acquire_action("move.b", &spec, &other) == SHIP_NATIVE_LIMIT && other == 0 &&
                      movement->acquire_action("move.a", &spec, &other) == SHIP_NATIVE_LIMIT,
                  "outro dono e o mesmo dono recebem LIMIT");
            Check(movement->get_action_state(&state) == SHIP_NATIVE_OK && state.owned &&
                      state.scene_epoch == ShipLuaHost::OotSceneEpoch() &&
                      state.player_generation == ShipLuaHost::OotPlayerGeneration() && state.tick == 10 &&
                      (state.flags & SHIP_OOT_ACTION_STATE_IDLE),
                  "estado do lease");

            Check(Dispatch() == 1 && owner.calls == 1 && vanillaRuns == 1, "PASS roda a ação do jogo");
            owner.disposition = SHIP_OOT_ACTION_SUPPRESS;
            Check(Dispatch() == 0 && owner.calls == 2 && vanillaRuns == 1, "SUPPRESS segura a ação do jogo");
            owner.disposition = SHIP_OOT_ACTION_PASS;
            owner.expectedAllowed = 0;
            Check(Dispatch(0) == 0 && owner.calls == 3 && vanillaRuns == 1, "PASS respeita o gate do jogo");
            owner.expectedAllowed = 1;

            Check(movement->restore_player_animation(token) == SHIP_NATIVE_OK && restores == 1 && idleRestores == 0 &&
                      movement->get_action_state(&state) == SHIP_NATIVE_OK && state.owned,
                  "animação de Idle sem soltar o lease");
            neutral = false;
            Check(movement->restore_player_animation(token) == SHIP_NATIVE_UNSUPPORTED, "fora do chão não restaura");
            neutral = true;
            Check(movement->release_action(token) == SHIP_NATIVE_OK &&
                      movement->release_action(token) == SHIP_NATIVE_OK && idleRestores == 1 && revokes == 1 &&
                      lastRevoked == token,
                  "release idempotente, Idle uma vez, dependentes avisados");

            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK,
                  "lease para release no callback");
            owner.token = token;
            owner.releaseInside = true;
            owner.disposition = SHIP_OOT_ACTION_SUPPRESS;
            Check(Dispatch() == 1 && idleRestores == 2 && vanillaRuns == 2,
                  "release no callback: Idle depois do callback e a disposição velha não vale");
            owner.releaseInside = false;

            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK, "lease antes de troca de ação");
            owner.token = token;
            const int callsBefore = owner.calls;
            action = &walkTag;
            ShipLuaHost::NotifyOotPlayerActionChanged(&playerTag);
            action = &idleTag;
            ShipLuaHost::NotifyOotPlayerActionChanged(&playerTag);
            Check(revokes == 3 && lastRevoked == token &&
                      movement->restore_player_animation(token) == SHIP_NATIVE_UNSUPPORTED && Dispatch() == 1 &&
                      owner.calls == callsBefore && idleRestores == 2,
                  "troca de ação (A-B-A) derruba o lease sem impor Idle");

            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK, "lease antes do Link ocupado");
            owner.token = token;
            eligible = false;
            Check(Dispatch() == 1 && owner.calls == callsBefore && idleRestores == 2 && revokes == 4,
                  "Link ocupado derruba o lease");
            eligible = true;

            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK, "lease para falha");
            owner.token = token;
            owner.status = SHIP_NATIVE_FAILURE;
            const int idleAtFailure = idleRestores;
            Check(Dispatch() == 1 && movement->restore_player_animation(token) == SHIP_NATIVE_UNSUPPORTED &&
                      idleRestores == idleAtFailure,
                  "callback que falha perde o lease sem Idle e o jogo segue");
            owner.status = SHIP_NATIVE_OK;
            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK, "lease para exceção");
            owner.token = token;
            owner.throwInside = true;
            const int idleAtException = idleRestores;
            Check(Dispatch() == 1 && movement->get_action_state(&state) == SHIP_NATIVE_OK && !state.owned &&
                      idleRestores == idleAtException,
                  "exceção do callback não cruza a fronteira, revoga o lease e mantém vanilla");
            owner.throwInside = false;
            owner.disposition = SHIP_OOT_ACTION_SUPPRESS;
            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK,
                  "nova aquisição depois da exceção");
            owner.token = token;
            Check(Dispatch() == 0 && movement->release_action(token) == SHIP_NATIVE_OK,
                  "guard do callback recuperado depois da exceção");
            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK, "lease para disposição ruim");
            owner.token = token;
            owner.disposition = 42;
            const int idleAtBadDisposition = idleRestores;
            Check(Dispatch() == 1 && movement->restore_player_animation(token) == SHIP_NATIVE_UNSUPPORTED &&
                      idleRestores == idleAtBadDisposition,
                  "disposição desconhecida perde o lease sem Idle");

            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK, "lease para troca no callback");
            owner.token = token;
            owner.disposition = SHIP_OOT_ACTION_SUPPRESS;
            owner.changeActionInside = true;
            Check(Dispatch() == 1, "ação trocada no callback: vale a regra do jogo");
            owner.changeActionInside = false;
            action = &idleTag;
            ShipLuaHost::NotifyOotPlayerActionChanged(&playerTag);

            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK, "lease antes do fim da cena");
            const uint64_t epoch = ShipLuaHost::OotSceneEpoch();
            const int idleBefore = idleRestores;
            ShipLuaHost::DispatchOotPlayDestroy(&playTag);
            Check(ShipLuaHost::OotSceneEpoch() == epoch + 1 &&
                      movement->restore_player_animation(token) == SHIP_NATIVE_UNSUPPORTED &&
                      movement->release_action(token) == SHIP_NATIVE_OK && idleRestores == idleBefore,
                  "Play_Destroy derruba o lease sem tocar no jogo");
            ShipLuaHost::NotifyOotPlayerCreated(&playerTag);
            Check(movement->acquire_action("move.a", &spec, &other) == SHIP_NATIVE_OK && other != token,
                  "cena nova, mesmo endereço, token novo");
            const uint64_t generation = ShipLuaHost::OotPlayerGeneration();
            ShipLuaHost::NotifyOotPlayerCreated(&playerTag);
            Check(ShipLuaHost::OotPlayerGeneration() != generation &&
                      movement->restore_player_animation(other) == SHIP_NATIVE_UNSUPPORTED,
                  "Player recriado no mesmo endereço não herda o lease");

            owner.disposition = SHIP_OOT_ACTION_PASS;
            Check(movement->acquire_action("move.unload", &spec, &token) == SHIP_NATIVE_OK, "lease do dono que sai");
            owner.token = token;
            const int callsAtUnload = owner.calls, idleAtUnload = idleRestores;
            policy.onProviderUnload("move.unload");
            Check(owner.calls == callsAtUnload && idleRestores == idleAtUnload + 1 && Dispatch() == 1 &&
                      owner.calls == callsAtUnload,
                  "unload solta o lease sem chamar a DLL");
            Check(movement->acquire_action("move.unload", &spec, &other) == SHIP_NATIVE_UNSUPPORTED &&
                      movement->release_action(token) == SHIP_NATIVE_OK,
                  "dono descarregado não volta");

            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK,
                  "lease antes de perder o Player");
            const int revokesBeforeMissing = revokes;
            gameplay = false;
            Check(movement->get_action_state(&state) == SHIP_NATIVE_UNSUPPORTED && state.owned == 0 &&
                      (state.flags & SHIP_OOT_ACTION_STATE_BLOCKED) && revokes == revokesBeforeMissing + 1 &&
                      lastRevoked == token,
                  "snapshot sem Player derruba o lease e avisa uma vez");
            Check(movement->get_action_state(&state) == SHIP_NATIVE_UNSUPPORTED && state.owned == 0 &&
                      revokes == revokesBeforeMissing + 1,
                  "consulta repetida sem Player não repete a revogação");
            gameplay = true;

            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK,
                  "lease para ouvinte que tenta adquirir");
            revokeMovement = movement;
            revokeSpec = spec;
            acquireOnRevoke = true;
            addOnRevoke = true;
            const int lateBefore = lateRevokes;
            Check(movement->release_action(token) == SHIP_NATIVE_OK && acquireDuringRevoke == SHIP_NATIVE_LIMIT &&
                      tokenDuringRevoke == 0 && lateRevokes == lateBefore,
                  "aviso bloqueia aquisição e adia o ouvinte novo");
            acquireOnRevoke = false;
            addOnRevoke = false;
            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK &&
                      movement->release_action(token) == SHIP_NATIVE_OK && lateRevokes == lateBefore + 1,
                  "ouvinte acrescentado recebe só a próxima revogação");

            const int idleAtDirectChange = idleRestores;
            Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK,
                  "lease antes de escrita direta de outra ação elegível");
            action = &walkTag;
            Check(movement->release_action(token) == SHIP_NATIVE_OK && idleRestores == idleAtDirectChange &&
                      action == &walkTag,
                  "release não impõe Idle sobre outra ação");
            action = &idleTag;
            ShipLuaHost::NotifyOotPlayerActionChanged(&playerTag);

            owner.disposition = SHIP_OOT_ACTION_SUPPRESS;
            for (int mutation = 0; mutation < 6; ++mutation) {
                Check(movement->acquire_action("move.a", &spec, &token) == SHIP_NATIVE_OK,
                      "lease para revalidar o snapshot depois do callback");
                owner.token = token;
                owner.directActionInside = mutation == 0 || mutation == 5;
                owner.loseContextInside = mutation == 1;
                owner.changePlayInside = mutation == 2;
                owner.changePlayerInside = mutation == 3;
                owner.recreatePlayerInside = mutation == 4;
                owner.releaseInside = mutation == 5;
                const int idleAtMutation = idleRestores, revokesAtMutation = revokes;
                Check(Dispatch() == 1 && idleRestores == idleAtMutation && revokes == revokesAtMutation + 1,
                      "ação ou contexto mudou: sem SUPPRESS velho e sem Idle");
                owner.directActionInside = false;
                owner.loseContextInside = false;
                owner.changePlayInside = false;
                owner.changePlayerInside = false;
                owner.recreatePlayerInside = false;
                owner.releaseInside = false;
                gameplay = true;
                snapshotPlay = &playTag;
                snapshotPlayer = &playerTag;
                action = &idleTag;
                ShipLuaHost::NotifyOotPlayerCreated(&playerTag);
            }

            ShipNativeStatus fromWorker = SHIP_NATIVE_OK;
            std::thread worker([&] {
                ShipOotPlayerActionStateV1 workerState{};
                workerState.size = sizeof(workerState);
                fromWorker = movement->get_action_state(&workerState);
            });
            worker.join();
            Check(fromWorker == SHIP_NATIVE_INVALID_ARGUMENT, "fora da thread do jogo");
            Check(owner.ok, "payload de cada tick completo");
            ShipLuaHost::SetOotActionBridge({});
        }
    }
    {
        const auto& query = ShipLuaHost::GetOotNativeRandomizerServiceV2();
        ShipOotRandomizerItemInfoV2 info{ sizeof(info) };
        ShipOotRandomizerHintV2 hint{ sizeof(hint) };
        Check(query.item_count() == 0 && query.item_info(1, &info) == SHIP_NATIVE_UNSUPPORTED &&
                  query.find_uncollected(1, &hint) == SHIP_NATIVE_UNSUPPORTED,
              "randomizer queries refuse an absent backend");
        ShipLuaHost::BindOotRandoQueryBackend(
            { []() -> uint32_t { return 3; },
              [](uint32_t item, ShipOotRandomizerItemInfoV2* out) -> ShipNativeStatus {
                  if (item != 1)
                      return SHIP_NATIVE_INVALID_ARGUMENT;
                  out->item = item;
                  std::strcpy(out->name, "Test Item");
                  return SHIP_NATIVE_OK;
              },
              [](uint32_t item, ShipOotRandomizerHintV2* out) -> ShipNativeStatus {
                  if (item != 1)
                      return SHIP_NATIVE_FAILURE;
                  out->item = item;
                  out->check = 42;
                  return SHIP_NATIVE_OK;
              } });
        Check(query.item_count() == 3 && query.item_info(1, &info) == SHIP_NATIVE_OK && info.item == 1 &&
                  std::string(info.name) == "Test Item" && query.find_uncollected(1, &hint) == SHIP_NATIVE_OK &&
                  hint.check == 42 && query.find_uncollected(2, &hint) == SHIP_NATIVE_FAILURE,
              "randomizer queries return copied item and location data");
        info.size = 0;
        Check(query.item_info(1, &info) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  query.find_uncollected(1, nullptr) == SHIP_NATIVE_INVALID_ARGUMENT,
              "randomizer query sizes and null arguments are checked");
        bool refused = false;
        std::thread queryThread([&] {
            refused = query.item_count() == 0 && query.find_uncollected(1, &hint) == SHIP_NATIVE_INVALID_ARGUMENT;
        });
        queryThread.join();
        Check(refused, "randomizer queries run only on the owner thread");
        ShipLuaHost::BindOotRandoQueryBackend({});
    }
    Check(policy.services.size() == 34 && policy.services[0].version == LINKSPAN_OOT_ENGINE_VERSION &&
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
              policy.services[8].size == sizeof(ShipOotSaveV1) &&
              std::string(policy.services[9].name) == LINKSPAN_OOT_ITEMS_SERVICE &&
              policy.services[9].version == LINKSPAN_OOT_ITEMS_VERSION &&
              policy.services[9].size == sizeof(ShipOotItemsV1) &&
              std::string(policy.services[10].name) == LINKSPAN_OOT_ACTORS_SERVICE &&
              policy.services[10].version == LINKSPAN_OOT_ACTORS_VERSION &&
              policy.services[10].size == sizeof(ShipOotActorsV1) &&
              std::string(policy.services[11].name) == LINKSPAN_OOT_CAMERA_SERVICE &&
              policy.services[11].version == LINKSPAN_OOT_CAMERA_VERSION &&
              policy.services[11].size == sizeof(ShipOotCameraV1) &&
              std::string(policy.services[12].name) == LINKSPAN_OOT_RENDER_SERVICE &&
              policy.services[12].version == LINKSPAN_OOT_RENDER_VERSION &&
              policy.services[12].size == sizeof(ShipOotRenderV1) &&
              std::string(policy.services[13].name) == LINKSPAN_OOT_WORLD_SERVICE &&
              policy.services[13].version == LINKSPAN_OOT_WORLD_VERSION &&
              policy.services[13].size == sizeof(ShipOotWorldV1) &&
              std::string(policy.services[14].name) == LINKSPAN_OOT_COLLIDERS_SERVICE &&
              policy.services[14].version == LINKSPAN_OOT_COLLIDERS_VERSION &&
              policy.services[14].size == sizeof(ShipOotCollidersV1) &&
              std::string(policy.services[15].name) == LINKSPAN_OOT_ITEMS_SERVICE &&
              policy.services[15].version == LINKSPAN_OOT_ITEMS_VERSION_2 &&
              policy.services[15].size == sizeof(ShipOotItemsV2) &&
              std::string(policy.services[16].name) == LINKSPAN_OOT_SKELETONS_SERVICE &&
              policy.services[16].version == LINKSPAN_OOT_SKELETONS_VERSION &&
              policy.services[16].size == sizeof(ShipOotSkeletonsV1) &&
              std::string(policy.services[17].name) == LINKSPAN_OOT_ITEMS_SERVICE &&
              policy.services[17].version == LINKSPAN_OOT_ITEMS_VERSION_3 &&
              policy.services[17].size == sizeof(ShipOotItemsV3) &&
              std::string(policy.services[18].name) == LINKSPAN_OOT_RESOURCES_SERVICE &&
              policy.services[18].version == LINKSPAN_OOT_RESOURCES_VERSION_3 &&
              policy.services[18].size == sizeof(ShipOotResourcesV3) &&
              std::string(policy.services[19].name) == LINKSPAN_OOT_SCENES_SERVICE &&
              policy.services[19].version == LINKSPAN_OOT_SCENES_VERSION_2 &&
              policy.services[19].size == sizeof(ShipOotScenesV2) &&
              std::string(policy.services[20].name) == LINKSPAN_OOT_TEXT_SERVICE &&
              policy.services[20].version == LINKSPAN_OOT_TEXT_VERSION &&
              policy.services[20].size == sizeof(ShipOotTextV1) &&
              std::string(policy.services[21].name) == LINKSPAN_OOT_RENDER_SERVICE &&
              policy.services[21].version == LINKSPAN_OOT_RENDER_VERSION_2 &&
              policy.services[21].size == sizeof(ShipOotRenderV2) &&
              std::string(policy.services[22].name) == LINKSPAN_OOT_SCENES_SERVICE &&
              policy.services[22].version == LINKSPAN_OOT_SCENES_VERSION_3 &&
              policy.services[22].size == sizeof(ShipOotScenesV3) &&
              std::string(policy.services[23].name) == LINKSPAN_OOT_RANDOMIZER_SERVICE &&
              policy.services[23].version == LINKSPAN_OOT_RANDOMIZER_VERSION &&
              policy.services[23].size == sizeof(ShipOotRandomizerV1) &&
              std::string(policy.services[24].name) == LINKSPAN_OOT_ANCHOR_SERVICE &&
              policy.services[24].version == LINKSPAN_OOT_ANCHOR_VERSION &&
              policy.services[24].size == sizeof(ShipOotAnchorV1) &&
              std::string(policy.services[25].name) == LINKSPAN_OOT_RENDER_SERVICE &&
              policy.services[25].version == LINKSPAN_OOT_RENDER_VERSION_3 &&
              policy.services[25].size == sizeof(ShipOotRenderV3) &&
              std::string(policy.services[26].name) == LINKSPAN_OOT_LIGHTS_SERVICE &&
              policy.services[26].version == LINKSPAN_OOT_LIGHTS_VERSION &&
              policy.services[26].size == sizeof(ShipOotLightsV1) &&
              std::string(policy.services[27].name) == LINKSPAN_OOT_REGISTRY_SERVICE &&
              policy.services[27].version == LINKSPAN_OOT_REGISTRY_VERSION_2 &&
              policy.services[27].size == sizeof(ShipOotRegistryV2) &&
              std::string(policy.services[28].name) == LINKSPAN_OOT_ACTOR_MODELS_SERVICE &&
              policy.services[28].version == LINKSPAN_OOT_ACTOR_MODELS_VERSION &&
              policy.services[28].size == sizeof(ShipOotActorModelsV1) && policy.onProviderUnload,
          "host deve publicar engine, movement V1/V2, resources V1/V2/V3, registry V1/V2, ocarina V1, scenes V1/V2/V3, "
          "save V1, items V1/V2/V3, actors V1, camera V1, render V1/V2, world V1, colliders V1, skeletons V1, "
          "text V1, randomizer V1 e anchor V1, com as versões novas no fim");
    {
        using namespace FakeSkeletons;
        const auto* skeletons = static_cast<const ShipOotSkeletonsV1*>(policy.services[16].table);
        uint64_t skel = 0;
        Check(skeletons->create("objects/object_firefly/gKeeseSkeleton", "objects/object_firefly/gKeeseFlyAnim",
                                &skel) == SHIP_NATIVE_UNSUPPORTED &&
                  skel == 0,
              "sem ponte não há esqueleto");
        ShipLuaHost::OotSkeletonsBridge bridge;
        bridge.create = Create;
        bridge.destroy = Destroy;
        bridge.play = Play;
        bridge.update = Update;
        bridge.frame = Frame;
        bridge.draw = Draw;
        ShipLuaHost::SetOotSkeletonsBridge(bridge);
        Check(skeletons->create("", "a", &skel) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  skeletons->create("__OTR__objects/x", "a", &skel) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  skeletons->create("objects/x", nullptr, &skel) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  skeletons->create("objects/x", "a", nullptr) == SHIP_NATIVE_INVALID_ARGUMENT,
              "caminhos inválidos");
        const std::string longPath(LINKSPAN_OOT_SKELETONS_MAX_PATH, 'a');
        Check(skeletons->create(longPath.c_str(), "a", &skel) == SHIP_NATIVE_INVALID_ARGUMENT, "caminho longo");
        Check(skeletons->create("objects/curva", "objects/anim", &skel) == SHIP_NATIVE_FAILURE && skel == 0,
              "falha da ponte");
        Check(skeletons->create("objects/object_firefly/gKeeseSkeleton", "objects/object_firefly/gKeeseFlyAnim",
                                &skel) == SHIP_NATIVE_OK &&
                  skel != 0 && ShipLuaHost::OotSkeletonCount() == 1,
              "create");
        Check(skeletons->play_animation(skel, "objects/outra", 2.0f, 7, 0.0f) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  skeletons->play_animation(skel, "objects/outra", NAN, 0, 0.0f) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  skeletons->play_animation(skel, "objects/outra", 1.0f, 0, -1.0f) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  skeletons->play_animation(skel + 99, "objects/outra", 1.0f, 0, 0.0f) == SHIP_NATIVE_INVALID_ARGUMENT,
              "play_animation inválido");
        Check(skeletons->play_animation(skel, "objects/outra", 2.0f, LINKSPAN_OOT_ANIM_ONCE, 3.0f) == SHIP_NATIVE_OK,
              "play_animation");
        uint8_t finished = 9;
        Check(skeletons->update(skel, &finished) == SHIP_NATIVE_OK && finished == 0 &&
                  skeletons->update(skel, nullptr) == SHIP_NATIVE_OK &&
                  skeletons->update(skel, &finished) == SHIP_NATIVE_OK && finished == 1,
              "update informa o fim da animação ONCE");
        float frame = 0.0f, last = 0.0f;
        Check(skeletons->get_frame(skel, &frame, &last) == SHIP_NATIVE_OK && frame == 3.0f && last == 9.0f &&
                  skeletons->get_frame(skel, nullptr, &last) == SHIP_NATIVE_INVALID_ARGUMENT,
              "get_frame");
        int play = 0;
        Check(skeletons->draw(&play, skel) == SHIP_NATIVE_UNSUPPORTED, "draw fora de escopo");
        ShipLuaHost::EnterOotRenderScope();
        Check(skeletons->draw(&play, skel) == SHIP_NATIVE_OK &&
                  skeletons->draw(nullptr, skel) == SHIP_NATIVE_INVALID_ARGUMENT,
              "draw no escopo");
        ShipLuaHost::LeaveOotRenderScope();
        std::thread([&] {
            Check(skeletons->update(skel, &finished) == SHIP_NATIVE_INVALID_ARGUMENT, "só na thread do jogo");
        }).join();
        uint64_t second = 0;
        Check(skeletons->create("objects/b", "objects/c", &second) == SHIP_NATIVE_OK && second != skel, "segundo");
        Check(skeletons->destroy(skel) == SHIP_NATIVE_OK && destroyed == 1 &&
                  skeletons->destroy(skel) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  skeletons->update(skel, &finished) == SHIP_NATIVE_INVALID_ARGUMENT,
              "destroy invalida o handle");
        ShipLuaHost::ResetOotNativeSkeletons();
        Check(destroyed == 2 && ShipLuaHost::OotSkeletonCount() == 0, "reset libera o que sobrou");
        ShipLuaHost::SetOotSkeletonsBridge({});
    }
    {
        using namespace FakeGetItem;
        const auto* items = static_cast<const ShipOotItemsV2*>(policy.services[15].table);
        Check(offsetof(ShipOotItemsV2, set_button_item) == offsetof(ShipOotItemsV1, set_button_item),
              "items V2 é prefixo da V1");
        ShipLuaHost::OotItemsBridge bridge;
        bridge.giveItem = Give;
        ShipLuaHost::SetOotItemsBridge(bridge);
        uint8_t item = 0;
        const ShipOotItemSpecV1 spec{
            sizeof(spec), "autor.gancho", "textures/icon_item_static/gItemIconHookshotTex", LINKSPAN_OOT_ITEM_AGE_ANY,
            nullptr,      nullptr
        };
        Check(items->register_item(&spec, &item) == SHIP_NATIVE_OK, "registro para get-item");
        Check(items->give_item(item) == SHIP_NATIVE_INVALID_ARGUMENT, "give sem set_get_item");
        ShipOotGetItemSpecV1 get{ sizeof(get),
                                  "objects/gameplay_keep/gHeartPieceInteriorDL",
                                  LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT,
                                  2.5f,
                                  "Voce ganhou o %rGancho%w!",
                                  Receive,
                                  &received };
        ShipOotGetItemSpecV1 bad = get;
        bad.model_scale = 0.0f;
        Check(items->set_get_item(item, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "escala inválida");
        bad = get;
        bad.model_layer = 2;
        Check(items->set_get_item(item, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "camada inválida");
        bad = get;
        bad.message = "";
        Check(items->set_get_item(item, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "mensagem vazia");
        bad.message = "Voc\xC3\xAA";
        Check(items->set_get_item(item, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "mensagem fora do ASCII");
        const std::string longText(LINKSPAN_OOT_ITEMS_MAX_MESSAGE + 1, 'a');
        bad.message = longText.c_str();
        Check(items->set_get_item(item, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "mensagem longa");
        Check(items->set_get_item(0x05, &get) == SHIP_NATIVE_INVALID_ARGUMENT, "set_get_item de item não registrado");
        Check(items->set_get_item(item, &get) == SHIP_NATIVE_OK, "set_get_item");
        const auto* record = ShipLuaHost::FindOotItem(item);
        Check(record && record->hasGetItem && record->message == "Voce ganhou o %rGancho%w!" &&
                  std::string(record->modelPath) == "__OTR__objects/gameplay_keep/gHeartPieceInteriorDL" &&
                  record->modelScale == 2.5f && record->receive(record->receiveUser, item) == SHIP_NATIVE_OK &&
                  received == 1,
              "registro guarda modelo, mensagem e receive");
        Check(items->give_item(item) == SHIP_NATIVE_OK && given.size() == 1 && given[0] == item, "give_item");
        playerBusy = true;
        Check(items->give_item(item) == SHIP_NATIVE_LIMIT, "Player ocupado");
        Check(items->unregister_item(item) == SHIP_NATIVE_OK && items->give_item(item) == SHIP_NATIVE_INVALID_ARGUMENT,
              "give após unregister");
        uint8_t again = 0;
        Check(items->register_item(&spec, &again) == SHIP_NATIVE_OK && again == item &&
                  !ShipLuaHost::FindOotItem(again)->hasGetItem,
              "id reaproveitado sem get-item antigo");
        Check(items->unregister_item(again) == SHIP_NATIVE_OK, "limpeza");
        ShipLuaHost::SetOotItemsBridge({});
    }
    {
        using namespace FakeGetItem;
        const auto* items = static_cast<const ShipOotItemsV2*>(policy.services[15].table);
        const auto* rando = static_cast<const ShipOotRandomizerV1*>(policy.services[23].table);
        uint8_t plain = 0;
        uint8_t zeta = 0;
        uint8_t alfa = 0;
        const ShipOotItemSpecV1 plainSpec{
            sizeof(plainSpec),         "autor.pedra", "textures/icon_item_static/gItemIconBowTex",
            LINKSPAN_OOT_ITEM_AGE_ANY, nullptr,       nullptr
        };
        ShipOotItemSpecV1 zetaSpec = plainSpec;
        zetaSpec.name = "autor.zeta";
        ShipOotItemSpecV1 alfaSpec = plainSpec;
        alfaSpec.name = "autor.alfa";
        const ShipOotGetItemSpecV1 get{ sizeof(get),
                                        "objects/gameplay_keep/gHeartPieceInteriorDL",
                                        LINKSPAN_OOT_ITEMS_LAYER_OPAQUE,
                                        1.0f,
                                        "Voce ganhou!",
                                        Receive,
                                        &received };
        Check(items->register_item(&plainSpec, &plain) == SHIP_NATIVE_OK &&
                  items->register_item(&zetaSpec, &zeta) == SHIP_NATIVE_OK &&
                  items->register_item(&alfaSpec, &alfa) == SHIP_NATIVE_OK &&
                  items->set_get_item(zeta, &get) == SHIP_NATIVE_OK &&
                  items->set_get_item(alfa, &get) == SHIP_NATIVE_OK,
              "itens para o randomizer");
        ShipOotRandomizerItemSpecV1 offer{ sizeof(offer), plain, 1, {} };
        Check(rando->add_item(&offer) == SHIP_NATIVE_INVALID_ARGUMENT, "oferta sem get-item");
        offer.item = zeta;
        offer.copies = 0;
        Check(rando->add_item(&offer) == SHIP_NATIVE_INVALID_ARGUMENT, "zero cópias");
        offer.copies = LINKSPAN_OOT_RANDOMIZER_MAX_COPIES + 1;
        Check(rando->add_item(&offer) == SHIP_NATIVE_INVALID_ARGUMENT, "cópias demais");
        offer.copies = 2;
        Check(rando->add_item(&offer) == SHIP_NATIVE_OK, "oferta");
        offer.item = alfa;
        offer.copies = 1;
        Check(rando->add_item(&offer) == SHIP_NATIVE_OK, "segunda oferta");
        auto offers = ShipLuaHost::GetOotRandoOffers();
        Check(offers.size() == 2 && offers[0].name == "autor.alfa" && offers[0].copies == 1 &&
                  offers[1].name == "autor.zeta" && offers[1].copies == 2,
              "ofertas em ordem de nome");
        Check(rando->remove_item(plain) == SHIP_NATIVE_FAILURE, "remove sem oferta");
        Check(items->unregister_item(alfa) == SHIP_NATIVE_OK && ShipLuaHost::GetOotRandoOffers().size() == 1,
              "item removido do registro sai do pool");
        Check(rando->remove_item(zeta) == SHIP_NATIVE_OK && ShipLuaHost::GetOotRandoOffers().empty(), "remove");

        Check(rando->seed_item_count() == 0, "sem seed");
        ShipLuaHost::SetOotRandoSeedItems({ "autor.zeta", "outro.item" });
        Check(ShipLuaHost::AddOotRandoSeedItem("outro.item") == 1 && ShipLuaHost::AddOotRandoSeedItem("mais.um") == 2,
              "nome da seed tem índice fixo");
        char name[16] = {};
        char tiny[4] = {};
        Check(rando->seed_item_count() == 3 && rando->seed_item_name(0, name, sizeof(name)) == SHIP_NATIVE_OK &&
                  std::string(name) == "autor.zeta" &&
                  rando->seed_item_name(1, tiny, sizeof(tiny)) == SHIP_NATIVE_LIMIT &&
                  rando->seed_item_name(3, name, sizeof(name)) == SHIP_NATIVE_INVALID_ARGUMENT,
              "itens da seed");
        std::thread worker([&] {
            ShipOotRandomizerItemSpecV1 other{ sizeof(other), zeta, 1, {} };
            Check(rando->add_item(&other) == SHIP_NATIVE_INVALID_ARGUMENT && rando->seed_item_count() == 0,
                  "outra thread");
        });
        worker.join();
        ShipLuaHost::ResetOotNativeRando();
        Check(rando->seed_item_count() == 0, "reset limpa a seed");
        Check(items->unregister_item(plain) == SHIP_NATIVE_OK && items->unregister_item(zeta) == SHIP_NATIVE_OK,
              "limpeza");
    }
    {
        using namespace FakeItems;
        const auto* items = static_cast<const ShipOotItemsV3*>(policy.services[17].table);
        Check(offsetof(ShipOotItemsV3, give_item) == offsetof(ShipOotItemsV2, give_item), "items V3 é prefixo da V2");
        ShipLuaHost::OotItemsBridge bridge;
        bridge.setItemVisual = SetItemVisual;
        bridge.getButtonItem = GetButton;
        bridge.setButtonItem = SetButton;
        ShipLuaHost::SetOotItemsBridge(bridge);
        Check(items->get_language() == LINKSPAN_OOT_LANGUAGE_ENGLISH, "idioma sem ponte = inglês");
        bridge.getLanguage = [] { return static_cast<uint8_t>(LINKSPAN_OOT_LANGUAGE_FRENCH); };
        ShipLuaHost::SetOotItemsBridge(bridge);
        Check(items->get_language() == LINKSPAN_OOT_LANGUAGE_FRENCH, "idioma da ponte");
        uint8_t item = 0;
        const ShipOotItemSpecV1 spec{
            sizeof(spec), "autor.aljava", "textures/icon_item_static/gItemIconBowTex", LINKSPAN_OOT_ITEM_AGE_ANY,
            nullptr,      nullptr
        };
        Check(items->register_item(&spec, &item) == SHIP_NATIVE_OK, "registro v3");
        const auto* record = ShipLuaHost::FindOotItem(item);
        Check(record && record->ammo == LINKSPAN_OOT_ITEMS_NO_AMMO, "sem número por padrão");
        Check(items->set_item_ammo(item, 100, 0) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  items->set_item_ammo(item, 5, 100) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  items->set_item_ammo(0x05, 5, 0) == SHIP_NATIVE_INVALID_ARGUMENT,
              "munição fora da faixa ou de item não registrado");
        Check(items->set_item_ammo(item, 30, 30) == SHIP_NATIVE_OK && record->ammo == 30 && record->ammoFull == 30,
              "set_item_ammo");
        Check(items->set_item_ammo(item, LINKSPAN_OOT_ITEMS_NO_AMMO, 0) == SHIP_NATIVE_OK &&
                  record->ammo == LINKSPAN_OOT_ITEMS_NO_AMMO,
              "tirar o número");
        Check(items->set_item_icon(item, "__OTR__textures/x") == SHIP_NATIVE_INVALID_ARGUMENT &&
                  items->set_item_icon(0x05, "textures/x") == SHIP_NATIVE_INVALID_ARGUMENT,
              "ícone inválido ou item não registrado");
        Check(items->set_item_icon(item, "textures/icon_item_static/gItemIconBowFireTex") == SHIP_NATIVE_OK &&
                  icons[item] == "__OTR__textures/icon_item_static/gItemIconBowFireTex",
              "set_item_icon troca o ícone");
        std::thread([&] {
            Check(items->set_item_ammo(item, 1, 0) == SHIP_NATIVE_INVALID_ARGUMENT, "munição só na thread do jogo");
        }).join();
        Check(items->unregister_item(item) == SHIP_NATIVE_OK, "limpeza v3");
        ShipLuaHost::SetOotItemsBridge({});
    }
    {
        using namespace FakeText;
        const auto* text = static_cast<const ShipOotTextV1*>(policy.services[20].table);
        const uint8_t hello[] = { 'o', 'i' };
        Check(text->set_message(0, 0x10, 0, 0, hello, 2) == SHIP_NATIVE_UNSUPPORTED, "texto sem ponte");
        ShipLuaHost::OotTextBridge bridge{ Set, Remove, Clear, Reset, ForEach };
        ShipLuaHost::SetOotTextBridge(bridge);
        Check(text->set_message(LINKSPAN_OOT_TEXT_LANGUAGES, 0x10, 0, 0, hello, 2) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  text->set_message(0, 0xFFFF, 0, 0, hello, 2) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  text->set_message(0, 0x10, 16, 0, hello, 2) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  text->set_message(0, 0x10, 0, 0, nullptr, 2) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  text->set_message(0, 0x10, 0, 0, hello, LINKSPAN_OOT_TEXT_MAX_MESSAGE) ==
                      SHIP_NATIVE_INVALID_ARGUMENT,
              "argumentos de texto inválidos");
        Check(text->set_message(0, 0x10, 2, 3, hello, 2) == SHIP_NATIVE_OK && table[0x10].typePos == 0x23 &&
                  table[0x10].bytes == "oi",
              "set_message junta caixa e posição");
        Check(text->set_message(1, 0x10, 0, 0, hello, 2) == SHIP_NATIVE_FAILURE, "idioma sem tabela");
        Check(text->remove_message(0, 0x10) == SHIP_NATIVE_OK && text->remove_message(0, 0x10) == SHIP_NATIVE_FAILURE,
              "remove_message");
        Check(text->reset_language(0) == SHIP_NATIVE_OK && resets == 1 && table.size() == 1, "reset_language");
        text->set_message(0, 0x20, 0, 1, hello, 1);
        text->set_message(0, 0x30, 0, 0, hello, 2);
        std::vector<std::string> seen;
        Check(text->list_messages(0, Collect, &seen) == SHIP_NATIVE_LIMIT && seen.size() == 2 && seen[0] == "1:0/0:a" &&
                  seen[1] == "32:0/1:o",
              "list_messages em ordem de id, parando no status do callback");
        Check(text->clear_language(0) == SHIP_NATIVE_OK && table.empty(), "clear_language");
        std::thread([&] {
            Check(text->remove_message(0, 1) == SHIP_NATIVE_INVALID_ARGUMENT, "texto só na thread do jogo");
        }).join();
        ShipLuaHost::SetOotTextBridge({});
    }
    {
        using namespace FakeJson;
        const auto* resources = static_cast<const ShipOotResourcesV3*>(policy.services[18].table);
        Check(offsetof(ShipOotResourcesV3, read_file_layers) == offsetof(ShipOotResourcesV2, read_file_layers),
              "resources V3 é prefixo da V2");
        ShipOotJsonTypeSpecV1 spec{ sizeof(spec), "linkspan.test/doc", 1, 2, Transcode, nullptr };
        uint64_t handle = 0;
        Check(resources->register_json_type(&spec, &handle) == SHIP_NATIVE_UNSUPPORTED && handle == 0,
              "tipo JSON sem ponte");
        ShipLuaHost::OotJsonTypesBridge bridge;
        bridge.declareType = Declare;
        ShipLuaHost::SetOotJsonTypesBridge(bridge);
        auto bad = spec;
        bad.type = "tipo com espaço";
        Check(resources->register_json_type(&bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "nome inválido");
        bad = spec;
        bad.min_version = 3;
        Check(resources->register_json_type(&bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "faixa invertida");
        bad = spec;
        bad.max_version = 1000;
        Check(resources->register_json_type(&bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "faixa larga demais");
        bad = spec;
        bad.transcode = nullptr;
        Check(resources->register_json_type(&bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "sem transcodificador");
        bad = spec;
        bad.type = "Scene";
        Check(resources->register_json_type(&bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "tipo do host");
        Check(resources->register_json_type(&spec, &handle) == SHIP_NATIVE_OK && handle != 0 &&
                  declared.back() == "linkspan.test/doc/1-2",
              "register_json_type declara o tipo e as versões");
        uint64_t other = 0;
        Check(resources->register_json_type(&spec, &other) == SHIP_NATIVE_INVALID_ARGUMENT, "um dono por tipo");
        std::string xml;
        Check(ShipLuaHost::TranscodeOotJson("linkspan.test/doc", "docs/a.json", 2, xml) == SHIP_NATIVE_OK &&
                  xml == "<Path Version=\"0\"/>" && lastPath == "docs/a.json" && lastVersion == 2,
              "o transcodificador escreve o XML em pedaços");
        Check(ShipLuaHost::TranscodeOotJson("linkspan.test/doc", "docs/a.json", 3, xml) == SHIP_NATIVE_INVALID_ARGUMENT,
              "versão fora da faixa");
        Check(ShipLuaHost::TranscodeOotJson("outro/tipo", "docs/a.json", 1, xml) == SHIP_NATIVE_UNSUPPORTED,
              "tipo sem dono");
        std::thread([&] {
            Check(resources->unregister_json_type(handle) == SHIP_NATIVE_INVALID_ARGUMENT, "só na thread do jogo");
        }).join();
        Check(resources->unregister_json_type(handle) == SHIP_NATIVE_OK &&
                  resources->unregister_json_type(handle) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  ShipLuaHost::TranscodeOotJson("linkspan.test/doc", "docs/a.json", 1, xml) == SHIP_NATIVE_UNSUPPORTED,
              "unregister tira o dono");
        Check(resources->register_json_type(&spec, &other) == SHIP_NATIVE_OK && other != handle &&
                  resources->unregister_json_type(other) == SHIP_NATIVE_OK,
              "o tipo pode ganhar outro dono");
        ShipLuaHost::SetOotJsonTypesBridge({});
    }
    {
        using namespace FakeWorld;
        const auto* world = static_cast<const ShipOotWorldV1*>(policy.services[13].table);
        const auto* colliders = static_cast<const ShipOotCollidersV1*>(policy.services[14].table);
        ShipOotWorldHitV1 hit{ sizeof(hit) };
        Check(world->raycast_floor(0.0f, 10.0f, 0.0f, &hit) == SHIP_NATIVE_UNSUPPORTED && hit.hit == 0,
              "sem ponte não há consulta");

        ShipLuaHost::OotWorldBridge bridge;
        bridge.gameplay = Gameplay;
        bridge.raycastFloor = Floor;
        bridge.lineTest = Line;
        bridge.wallCheck = Wall;
        bridge.waterSurface = Water;
        bridge.colliderCreate = Create;
        bridge.colliderFree = Free;
        bridge.colliderSubmit = Submit;
        bridge.colliderRead = Read;
        bridge.knockback = Knockback;
        ShipLuaHost::SetOotWorldBridge(bridge);

        Check(world->raycast_floor(1.0f, 10.0f, 2.0f, &hit) == SHIP_NATIVE_OK && hit.hit == 1 && hit.pos[1] == -5.0f &&
                  hit.size == sizeof(hit),
              "raycast_floor");
        Check(world->raycast_floor(std::nan(""), 0.0f, 0.0f, &hit) == SHIP_NATIVE_INVALID_ARGUMENT, "NaN recusado");
        ShipOotWorldHitV1 small{ sizeof(uint32_t) };
        Check(world->raycast_floor(0.0f, 0.0f, 0.0f, &small) == SHIP_NATIVE_INVALID_ARGUMENT, "hit sem tamanho");
        const float from[3]{ 0.0f, 0.0f, 0.0f };
        const float to[3]{ 100.0f, 0.0f, 0.0f };
        Check(world->line_test(from, to, 0, &hit) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  world->line_test(from, to, 8, &hit) == SHIP_NATIVE_INVALID_ARGUMENT,
              "superfícies inválidas");
        Check(world->line_test(from, to, LINKSPAN_OOT_WORLD_LINE_WALL, &hit) == SHIP_NATIVE_OK && hit.hit == 1 &&
                  hit.pos[0] == 50.0f,
              "line_test com parede");
        Check(world->line_test(from, to, LINKSPAN_OOT_WORLD_LINE_FLOOR, &hit) == SHIP_NATIVE_OK && hit.hit == 0 &&
                  hit.pos[0] == 0.0f,
              "line_test limpa o resultado anterior");
        Check(world->wall_check(from, to, 0.0f, 20.0f, &hit) == SHIP_NATIVE_INVALID_ARGUMENT, "raio inválido");
        Check(world->wall_check(from, to, 15.0f, 20.0f, &hit) == SHIP_NATIVE_OK && hit.hit == 0 && hit.pos[0] == 100.0f,
              "wall_check sem parede");
        float water = 0.0f;
        Check(world->water_surface(5.0f, 5.0f, &water) == SHIP_NATIVE_OK && water == 12.0f, "water_surface");
        Check(world->water_surface(-5.0f, 5.0f, &water) == SHIP_NATIVE_FAILURE, "fora da água");

        int actor = 0;
        ShipOotCylinderSpecV1 spec{};
        spec.size = sizeof(spec);
        spec.actor = &actor;
        spec.radius = 20;
        spec.height = 40;
        uint64_t handle = 0;
        ShipOotCylinderSpecV1 bad = spec;
        bad.actor = nullptr;
        Check(colliders->create_cylinder(&bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "collider sem ator");
        bad = spec;
        bad.radius = 0;
        Check(colliders->create_cylinder(&bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "raio zero");
        inGameplay = false;
        Check(colliders->create_cylinder(&spec, &handle) == SHIP_NATIVE_UNSUPPORTED && handle == 0,
              "collider fora de gameplay");
        inGameplay = true;
        Check(colliders->create_cylinder(&spec, &handle) == SHIP_NATIVE_OK && handle && created == 1, "create");
        uint64_t second = 0;
        Check(colliders->create_cylinder(&spec, &second) == SHIP_NATIVE_OK && second != handle, "segundo collider");
        Check(colliders->submit(handle) == SHIP_NATIVE_OK && submitted == 1, "submit");
        ShipOotColliderHitsV1 hits{ sizeof(hits) };
        Check(colliders->read_hits(handle, &hits) == SHIP_NATIVE_OK && hits.ac_hit == 1 && hits.ac_dmg_flags == 0x100,
              "read_hits");
        Check(colliders->submit(999) == SHIP_NATIVE_UNSUPPORTED, "handle desconhecido");

        // destroy vale na hora para o mod, mas a memória só sai no fim do frame seguinte.
        Check(colliders->destroy(handle) == SHIP_NATIVE_OK && colliders->destroy(handle) == SHIP_NATIVE_FAILURE,
              "destroy e destroy repetido");
        Check(colliders->submit(handle) == SHIP_NATIVE_UNSUPPORTED &&
                  colliders->read_hits(handle, &hits) == SHIP_NATIVE_UNSUPPORTED,
              "collider destruído não submete nem lê");
        ShipLuaHost::FlushOotColliders();
        Check(freed.empty() && ShipLuaHost::OotColliderCount() == 2, "sem liberar no fim do mesmo frame");
        ShipLuaHost::FlushOotColliders();
        Check(freed.size() == 1 && freed[0].first == 1 && freed[0].second && ShipLuaHost::OotColliderCount() == 1,
              "libera no fim do frame seguinte");

        Check(colliders->knockback_player(nullptr, 5.0f, 0, 3.0f, 0, 0) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  colliders->knockback_player(&actor, 5.0f, 0, 3.0f, 256, 0) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  colliders->knockback_player(&actor, 5.0f, 0, 3.0f, 0, 2) == SHIP_NATIVE_INVALID_ARGUMENT,
              "knockback inválido");
        Check(colliders->knockback_player(&actor, 5.0f, 0x4000, 3.0f, 4, 1) == SHIP_NATIVE_OK && knockbacks == 1 &&
                  lastLarge == 1,
              "knockback");

        ShipLuaHost::ReleaseOotSceneColliders();
        Check(freed.size() == 2 && freed[1].first == 2 && ShipLuaHost::OotColliderCount() == 0,
              "fim da cena libera o restante");
        std::thread worker([&] {
            float y = 0.0f;
            Check(world->water_surface(1.0f, 1.0f, &y) == SHIP_NATIVE_INVALID_ARGUMENT, "outra thread");
        });
        worker.join();
        ShipLuaHost::SetOotWorldBridge({});
    }
    {
        using namespace FakeView;
        Check(policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_PLAYER_LIMB_DRAW, LINKSPAN_OOT_HOOKS_VERSION) != 0,
              "ponto oot.player.limb_draw");
        const auto* camera = static_cast<const ShipOotCameraV1*>(policy.services[11].table);
        const auto* render = static_cast<const ShipOotRenderV1*>(policy.services[12].table);
        uint64_t token = 0;
        Check(camera->acquire("autor.mod", &token) == SHIP_NATIVE_UNSUPPORTED && token == 0, "sem ponte não há câmera");

        ShipLuaHost::OotViewBridge bridge;
        bridge.cameraCreate = Create;
        bridge.cameraValid = Valid;
        bridge.cameraDestroy = Destroy;
        bridge.cameraApply = Apply;
        bridge.cameraRead = Read;
        bridge.drawDisplayList = Draw;
        bridge.matrixPush = Push;
        bridge.matrixPop = Pop;
        bridge.matrixTranslate = Translate;
        bridge.matrixScale = Scale;
        bridge.matrixRotateZYX = Rotate;
        ShipLuaHost::SetOotViewBridge(bridge);

        gameplay = false;
        Check(camera->acquire("autor.mod", &token) == SHIP_NATIVE_LIMIT, "fora de gameplay");
        gameplay = true;
        Check(camera->acquire("", &token) == SHIP_NATIVE_INVALID_ARGUMENT, "dono vazio");
        Check(camera->acquire("autor.mod", &token) == SHIP_NATIVE_OK && token && created == 1 &&
                  camera->is_owned(token) == 1,
              "acquire");
        uint64_t second = 0;
        Check(camera->acquire("outro.mod", &second) == SHIP_NATIVE_LIMIT && second == 0, "câmera exclusiva");
        ShipOotCameraViewV1 view{ sizeof(view), { 0.0f, 100.0f, -200.0f }, { 0.0f, 40.0f, 0.0f }, 60.0f };
        Check(camera->set_view(second, &view) == SHIP_NATIVE_UNSUPPORTED, "set_view sem posse");
        ShipOotCameraViewV1 bad = view;
        bad.fov = 0.0f;
        Check(camera->set_view(token, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "fov inválido");
        bad = view;
        bad.at[0] = bad.eye[0];
        bad.at[1] = bad.eye[1];
        bad.at[2] = bad.eye[2];
        Check(camera->set_view(token, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "eye igual a at");
        Check(camera->set_view(token, &view) == SHIP_NATIVE_OK && applied == 1 && lastApplied.eye[2] == -200.0f,
              "set_view aplica na hora");
        ShipLuaHost::UpdateOotCamera();
        Check(applied == 2, "vista reaplicada a cada frame");
        ShipOotCameraViewV1 read{ sizeof(read) };
        Check(camera->get_view(&read) == SHIP_NATIVE_OK && read.fov == 60.0f, "get_view");

        // Cutscene ou troca de cena: a câmera deixa de ser a ativa e a posse cai.
        active = false;
        ShipLuaHost::UpdateOotCamera();
        Check(applied == 2 && camera->is_owned(token) == 0 && destroyed == 1, "posse perdida limpa a subcâmera");
        Check(camera->release(token) == SHIP_NATIVE_OK && destroyed == 1, "release de token perdido");
        Check(camera->release(token + 100) == SHIP_NATIVE_INVALID_ARGUMENT, "release de token inexistente");
        Check(camera->acquire("outro.mod", &second) == SHIP_NATIVE_OK && second != token, "novo dono");
        Check(camera->release(second) == SHIP_NATIVE_OK && destroyed == 2 && camera->is_owned(second) == 0,
              "release devolve a câmera");

        int play = 0;
        Check(render->draw_display_list(&play, "objects/gameplay_keep/gHeartPieceInteriorDL", 0) ==
                      SHIP_NATIVE_UNSUPPORTED &&
                  render->matrix_push() == SHIP_NATIVE_UNSUPPORTED &&
                  render->matrix_translate(1.0f, 0.0f, 0.0f) == SHIP_NATIVE_UNSUPPORTED,
              "render fora de escopo de draw");
        ShipLuaHost::EnterOotRenderScope();
        Check(render->matrix_pop() == SHIP_NATIVE_INVALID_ARGUMENT, "pop além do escopo");
        Check(render->matrix_push() == SHIP_NATIVE_OK && render->matrix_translate(1.0f, 2.0f, 3.0f) == SHIP_NATIVE_OK &&
                  render->matrix_scale(0.5f, 0.5f, 0.5f) == SHIP_NATIVE_OK &&
                  render->matrix_rotate_zyx(0, 0x4000, 0) == SHIP_NATIVE_OK && depth == 1,
              "operações de matriz no escopo");
        Check(render->matrix_translate(std::nan(""), 0.0f, 0.0f) == SHIP_NATIVE_INVALID_ARGUMENT, "NaN recusado");
        Check(render->draw_display_list(&play, "objects/gameplay_keep/gHeartPieceInteriorDL", 1) == SHIP_NATIVE_OK &&
                  ops.back() == "draw:__OTR__objects/gameplay_keep/gHeartPieceInteriorDL:1",
              "draw no escopo");
        Check(render->draw_display_list(&play, "__OTR__x", 0) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  render->draw_display_list(&play, "a/../b", 0) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  render->draw_display_list(&play, "a", 2) == SHIP_NATIVE_INVALID_ARGUMENT,
              "caminho ou camada inválidos");
        ShipLuaHost::EnterOotRenderScope();
        Check(render->matrix_push() == SHIP_NATIVE_OK && render->matrix_push() == SHIP_NATIVE_OK && depth == 3,
              "escopo aninhado");
        ShipLuaHost::LeaveOotRenderScope();
        Check(depth == 1, "leave desfaz só os push do escopo interno");
        uint32_t pushes = 0;
        while (render->matrix_push() == SHIP_NATIVE_OK) {
            ++pushes;
        }
        Check(pushes == LINKSPAN_OOT_RENDER_MAX_DEPTH - 1, "limite de profundidade");
        ShipLuaHost::LeaveOotRenderScope();
        Check(depth == 0 && render->matrix_push() == SHIP_NATIVE_UNSUPPORTED, "leave devolve a pilha do host");

        Check(camera->acquire("autor.mod", &token) == SHIP_NATIVE_OK, "acquire antes do reset");
        ShipLuaHost::ResetOotNativeView();
        Check(camera->is_owned(token) == 0 && destroyed == 3, "reset libera a câmera");
        ShipLuaHost::SetOotViewBridge({});
    }
    {
        using namespace FakeView;
        static_assert(offsetof(ShipOotCameraV2, layout_id) == sizeof(ShipOotCameraV1));
        static_assert(offsetof(ShipOotCameraViewV2, roll) == sizeof(ShipOotCameraViewV1));
        static_assert(sizeof(ShipOotCameraViewV2) == 36 && sizeof(ShipOotCameraLeaseV2) == 32);
        const ShipOotCameraV1* v1 = nullptr;
        const ShipOotCameraV2* v2 = nullptr;
        for (const auto& service : policy.services) {
            if (service.name != LINKSPAN_OOT_CAMERA_SERVICE)
                continue;
            if (service.version == LINKSPAN_OOT_CAMERA_VERSION && service.size == sizeof(ShipOotCameraV1))
                v1 = static_cast<const ShipOotCameraV1*>(service.table);
            if (service.version == LINKSPAN_OOT_CAMERA_VERSION_2 && service.size == sizeof(ShipOotCameraV2))
                v2 = static_cast<const ShipOotCameraV2*>(service.table);
        }
        Check(v1 && v2 && v2->layout_id && std::strcmp(v2->layout_id, LINKSPAN_OOT_LAYOUT_ID) == 0 &&
                  v2->acquire == v1->acquire && v2->set_view == v1->set_view,
              "camera V2 publicada com o prefixo da V1");
        if (v1 && v2) {
            ShipLuaHost::OotViewBridge bridge;
            bridge.cameraCreate = Create;
            bridge.cameraValid = Valid;
            bridge.cameraDestroy = Destroy;
            bridge.cameraApply = Apply;
            bridge.cameraRead = Read;
            ShipLuaHost::SetOotViewBridge(bridge);
            gameplay = true;
            active = false;
            lastRoll = 0;
            uint64_t token = 7, other = 7;
            ShipOotCameraLeaseV2 lease{};
            lease.size = sizeof(lease);
            ShipOotCameraLeaseV2 shortLease{};
            shortLease.size = sizeof(shortLease) - 1;
            ShipOotCameraViewV2 view{ sizeof(view), { 0.0f, 30.0f, -90.0f }, { 0.0f, 20.0f, 0.0f }, 70.0f, 0x4000, 0 };
            Check(v2->acquire_ex("cam.v2", &token, nullptr) == SHIP_NATIVE_INVALID_ARGUMENT && token == 0 &&
                      v2->acquire_ex("cam.v2", &token, &shortLease) == SHIP_NATIVE_INVALID_ARGUMENT &&
                      v2->set_view_ex(999999, &view) == SHIP_NATIVE_INVALID_ARGUMENT &&
                      v2->get_lease(0, &lease) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "lease curto ou nulo, token inventado");

            Check(v1->acquire("cam.v1", &other) == SHIP_NATIVE_OK &&
                      v2->acquire_ex("cam.v2", &token, &lease) == SHIP_NATIVE_LIMIT && token == 0,
                  "V1 segura a câmera contra a V2");
            Check(v1->release(other) == SHIP_NATIVE_OK, "V1 solta");
            const uint64_t epoch = ShipLuaHost::OotSceneEpoch();
            Check(v2->acquire_ex("cam.v2", &token, &lease) == SHIP_NATIVE_OK && token && lease.owned &&
                      lease.scene_epoch == epoch && lease.camera_generation != 0,
                  "V2 com epoch e geração");
            Check(v1->acquire("cam.v1", &other) == SHIP_NATIVE_LIMIT && other == 0, "V2 segura a câmera contra a V1");

            Check(v2->set_view_ex(token, &view) == SHIP_NATIVE_OK && rollApplied && lastRoll == 0x4000,
                  "roll aplicado");
            int16_t roll = 0;
            Check(ShipLuaHost::GetOotCameraRoll(nextCamera, &playTag, &roll) && roll == 0x4000,
                  "roll para o Camera_Update");
            const int destroyedAtRoll = destroyed;
            active = false;
            Check(!ShipLuaHost::GetOotCameraRoll(nextCamera, &playTag, &roll) && destroyed == destroyedAtRoll,
                  "roll não usa câmera inválida nem a destrói no Camera_Update");
            active = true;
            bridge.cameraValid = nullptr;
            ShipLuaHost::SetOotViewBridge(bridge);
            Check(!ShipLuaHost::GetOotCameraRoll(nextCamera, &playTag, &roll) && destroyed == destroyedAtRoll,
                  "roll exige a ponte de validade");
            bridge.cameraValid = Valid;
            ShipLuaHost::SetOotViewBridge(bridge);
            ShipOotCameraViewV2 read{};
            read.size = sizeof(read);
            Check(v2->get_view_ex(&read) == SHIP_NATIVE_OK && read.roll == 0x4000 && read.fov == 60.0f,
                  "roll ida e volta");
            const ShipOotCameraViewV1 legacy{ sizeof(legacy), { 1.0f, 30.0f, -90.0f }, { 1.0f, 20.0f, 0.0f }, 65.0f };
            Check(v1->set_view(token, &legacy) == SHIP_NATIVE_OK && rollApplied && lastRoll == 0x4000 &&
                      lastApplied.fov == 65.0f,
                  "set_view da V1 conserva o roll da V2");
            auto bad = view;
            bad.fov = std::numeric_limits<float>::quiet_NaN();
            Check(v2->set_view_ex(token, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "fov NaN");
            bad = view;
            bad.size = sizeof(bad) - 1;
            Check(v2->set_view_ex(token, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "vista curta");
            struct LegacyTail {
                ShipOotCameraViewV1 view;
                uint32_t tail;
            } legacyTail{ {}, 0x12345678u };
            legacyTail.view.size = sizeof(legacyTail.view);
            Check(v1->get_view(&legacyTail.view) == SHIP_NATIVE_OK && legacyTail.tail == 0x12345678u,
                  "get_view da V1 não escreve além da V1");

            const int destroyedBefore = destroyed;
            ShipLuaHost::DispatchOotPlayDestroy(&playTag);
            Check(v2->get_lease(token, &lease) == SHIP_NATIVE_UNSUPPORTED && !lease.owned &&
                      lease.scene_epoch == epoch && destroyed == destroyedBefore &&
                      !ShipLuaHost::GetOotCameraRoll(nextCamera, &playTag, &roll) &&
                      v2->set_view_ex(token, &view) == SHIP_NATIVE_UNSUPPORTED && v2->release(token) == SHIP_NATIVE_OK,
                  "fim da cena derruba o lease sem tocar no jogo; release continua OK");
            active = false;

            Check(v2->acquire_ex("cam.release.after.destroy", &token, &lease) == SHIP_NATIVE_OK,
                  "lease para release sem consulta depois de Play_Destroy");
            const int destroyedAtDirectRelease = destroyed;
            ShipLuaHost::DispatchOotPlayDestroy(&playTag);
            Check(v2->release(token) == SHIP_NATIVE_OK && destroyed == destroyedAtDirectRelease,
                  "release imediatamente após Play_Destroy não destrói câmera de outra epoch");
            active = false;

            Check(v2->acquire_ex("cam.cleared", &token, &lease) == SHIP_NATIVE_OK &&
                      v2->set_view_ex(token, &view) == SHIP_NATIVE_OK,
                  "lease antes de limpar e reutilizar o mesmo slot");
            const uint64_t clearedGeneration = lease.camera_generation;
            const int destroyedAtClear = destroyed;
            ShipLuaHost::NotifyOotCameraCleared(nullptr, nextCamera);
            ShipLuaHost::NotifyOotCameraCleared(&playTag, int16_t(nextCamera + 1));
            Check(v2->get_lease(token, &lease) == SHIP_NATIVE_OK && lease.owned,
                  "aviso de outro play ou slot conserva o lease");
            ShipLuaHost::NotifyOotCameraCleared(&playTag, nextCamera);
            active = false; // Play_ClearCamera terminou; Lua recria o slot antes da próxima consulta
            int16_t reusedCamera = -1;
            void* reusedPlay = nullptr;
            Check(Create(&reusedCamera, &reusedPlay) == SHIP_NATIVE_OK && reusedCamera == nextCamera &&
                      reusedPlay == &playTag,
                  "câmera externa reutiliza o mesmo slot e endereço");
            Check(v2->get_lease(token, &lease) == SHIP_NATIVE_UNSUPPORTED && !lease.owned &&
                      lease.camera_generation == clearedGeneration &&
                      !ShipLuaHost::GetOotCameraRoll(nextCamera, &playTag, &roll) &&
                      v2->set_view_ex(token, &view) == SHIP_NATIVE_UNSUPPORTED &&
                      v2->release(token) == SHIP_NATIVE_OK && active && destroyed == destroyedAtClear,
                  "lease antigo não controla nem destrói a câmera recriada");
            active = false;

            Check(v2->acquire_ex("cam.unload", &token, &lease) == SHIP_NATIVE_OK &&
                      lease.scene_epoch == ShipLuaHost::OotSceneEpoch() &&
                      lease.camera_generation != clearedGeneration && v2->set_view_ex(token, &view) == SHIP_NATIVE_OK,
                  "lease novo com outra geração");
            const int destroyedAtUnload = destroyed;
            ShipLuaHost::BlockOotServiceOwner("cam.unload");
            Check(!ShipLuaHost::GetOotCameraRoll(nextCamera, &playTag, &roll) && destroyed == destroyedAtUnload,
                  "dono bloqueado não fornece roll e a consulta não destrói");
            policy.onProviderUnload("cam.unload");
            Check(destroyed == destroyedAtUnload + 1 && v2->get_lease(token, &lease) == SHIP_NATIVE_UNSUPPORTED &&
                      v2->acquire_ex("cam.unload", &other, &lease) == SHIP_NATIVE_UNSUPPORTED &&
                      v1->acquire("cam.unload", &other) == SHIP_NATIVE_UNSUPPORTED,
                  "unload devolve a câmera e bloqueia o dono");

            ShipNativeStatus fromWorker = SHIP_NATIVE_OK;
            std::thread worker([&] {
                ShipOotCameraViewV2 workerView{};
                workerView.size = sizeof(workerView);
                fromWorker = v2->get_view_ex(&workerView);
            });
            worker.join();
            Check(fromWorker == SHIP_NATIVE_INVALID_ARGUMENT, "câmera fora da thread do jogo");
            ShipLuaHost::SetOotViewBridge({});
        }
    }
    {
        using namespace HostSlimSkeletons;
        static_assert(offsetof(ShipOotSkeletonsV2, layout_id) == sizeof(ShipOotSkeletonsV1));
        static_assert(offsetof(ShipOotSkeletonsV2, create_ex) == sizeof(ShipOotSkeletonsV1) + sizeof(void*));
        const auto* skeletons = Find(policy);
        Check(skeletons && skeletons->layout_id && std::strcmp(skeletons->layout_id, LINKSPAN_OOT_LAYOUT_ID) == 0,
              "skeletons V2 publicado");
        if (skeletons) {
            Install();
            const ShipOotSkeletonSpecV2 link{ sizeof(link),           SHIP_OOT_SKELETON_LINK, "objects/corpo/skel",
                                              "objects/corpo/parado", { -57, 3377, 0 },       0 };
            auto bad = link;
            uint64_t handle = 7;
            Check(skeletons->create_ex("skel.mod", nullptr, &handle) == SHIP_NATIVE_INVALID_ARGUMENT && handle == 0,
                  "spec nula");
            bad.size = sizeof(bad) - 1;
            Check(skeletons->create_ex("skel.mod", &bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "spec curta");
            bad = link;
            bad.format = 2;
            Check(skeletons->create_ex("skel.mod", &bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "formato");
            bad = link;
            bad.skeleton_path = "__OTR__objects/corpo/skel";
            Check(skeletons->create_ex("skel.mod", &bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "caminho já prefixado");
            Check(skeletons->create_ex("", &link, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "dono vazio");

            Check(skeletons->create_ex("skel.mod", &link, &handle) == SHIP_NATIVE_OK && handle && created == 1,
                  "esqueleto LINK com dono");
            ShipOotAnimationStateV2 state{};
            state.size = sizeof(state);
            Check(skeletons->get_state(handle, &state) == SHIP_NATIVE_OK && state.asset_last_frame == 9.0f &&
                      state.animation_serial == 1,
                  "estado inicial");
            const ShipOotAnimationPlayV2 reverse{ sizeof(reverse), 2.0f, 0.0f, LINKSPAN_OOT_ANIM_ONCE, 1, { 0, 0 } };
            auto badPlay = reverse;
            badPlay.speed = -1.0f;
            Check(skeletons->play_ex(handle, "objects/corpo/soco", &badPlay) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "velocidade é magnitude");
            badPlay = reverse;
            badPlay.mode = 1;
            Check(skeletons->play_ex(handle, "objects/corpo/soco", &badPlay) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "modo desconhecido");
            Check(skeletons->play_ex(handle, "objects/corpo/soco", &reverse) == SHIP_NATIVE_OK &&
                      playLog.back() == "__OTR__objects/corpo/soco",
                  "play_ex com reverse");
            uint8_t finished = 0;
            Check(skeletons->update(handle, &finished) == SHIP_NATIVE_OK && finished == 0 &&
                      skeletons->get_state(handle, &state) == SHIP_NATIVE_OK && state.tick == 77 &&
                      state.animation_serial == 2 && state.speed == -2.0f && state.target_end_frame == 0.0f &&
                      state.current_frame == 7.0f && state.scene_epoch == ShipLuaHost::OotSceneEpoch(),
                  "estado depois do update");
            for (int i = 0; i < 4; ++i)
                skeletons->update(handle, &finished);
            Check(skeletons->get_state(handle, &state) == SHIP_NATIVE_OK && state.finished_previous_update == 1,
                  "ONCE terminou");

            ShipOotAnimationInfoV2 info{};
            info.size = sizeof(info);
            notLoaded.insert("__OTR__objects/corpo/errada");
            Check(skeletons->animation_info(handle, "objects/corpo/errada", &info) == SHIP_NATIVE_OK && info.present &&
                      !info.loaded,
                  "animação de outro formato");
            Check(skeletons->animation_info(handle, "objects/corpo/soco", &info) == SHIP_NATIVE_OK && info.loaded &&
                      info.asset_last_frame == 9.0f,
                  "animation_info");

            uint64_t legacy = 0;
            Check(skeletons->create("objects/v1/skel", "objects/v1/anim", &legacy) == SHIP_NATIVE_UNSUPPORTED,
                  "create V1 sem ponte V1 continua UNSUPPORTED");
            Limbs limbs;
            limbs.skeletons = skeletons;
            limbs.skeleton = handle;
            Check(skeletons->draw_ex(&playTag, &actorTag, handle) == SHIP_NATIVE_UNSUPPORTED, "fora do escopo de draw");
            Check(skeletons->set_post_limb(handle, nullptr, &limbs) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "user sem callback");
            Check(skeletons->set_post_limb(handle, Limb, &limbs) == SHIP_NATIVE_OK, "pós-membro");
            ShipLuaHost::EnterOotRenderScope();
            Check(skeletons->draw_ex(&playTag, &actorTag, handle) == SHIP_NATIVE_OK &&
                      limbs.seen == std::vector<int32_t>{ 0, 1, 2 },
                  "callback em cada membro");
            limbs.seen.clear();
            limbs.status = SHIP_NATIVE_FAILURE;
            skeletons->draw_ex(&playTag, &actorTag, handle);
            Check(limbs.seen.size() == 3, "três falhas no mesmo draw");
            limbs.seen.clear();
            skeletons->draw_ex(&playTag, &actorTag, handle);
            Check(limbs.seen.empty(), "callback que falha três vezes sai");
            // V-SKELETON-DIRECT-SAFETY: callback publico direto nao atravessa a ABI C com excecao.
            const auto drawWithoutException = [&](uint64_t target, const char* message) {
                ShipNativeStatus result = SHIP_NATIVE_OK;
                bool escaped = false;
                try {
                    result = skeletons->draw_ex(&playTag, &actorTag, target);
                } catch (...) { escaped = true; }
                Check(!escaped, message);
                return result;
            };
            limbs.status = SHIP_NATIVE_OK;
            limbs.throwInside = true;
            limbs.seen.clear();
            Check(skeletons->set_post_limb(handle, Limb, &limbs) == SHIP_NATIVE_OK,
                  "V-SKELETON-DIRECT-SAFETY: instala callback direto que lanca");
            Check(drawWithoutException(handle, "V-SKELETON-DIRECT-SAFETY: excecao direta contida") ==
                          SHIP_NATIVE_FAILURE &&
                      limbs.seen == std::vector<int32_t>{ 0, 1, 2 } && ShipLuaHost::InOotRenderScope(),
                  "V-SKELETON-DIRECT-SAFETY: falha observavel, draw continua e escopo externo permanece");
            limbs.seen.clear();
            Check(drawWithoutException(handle, "V-SKELETON-DIRECT-SAFETY: draw depois de tres throws") ==
                          SHIP_NATIVE_OK &&
                      limbs.seen.empty(),
                  "V-SKELETON-DIRECT-SAFETY: tres excecoes desativam o callback");
            limbs.throwInside = false;
            Check(skeletons->set_post_limb(handle, Limb, &limbs) == SHIP_NATIVE_OK &&
                      drawWithoutException(handle, "V-SKELETON-DIRECT-SAFETY: recuperacao saudavel") ==
                          SHIP_NATIVE_OK &&
                      limbs.seen == std::vector<int32_t>{ 0, 1, 2 },
                  "V-SKELETON-DIRECT-SAFETY: reregistro restaura callback saudavel");

            limbs.seen.clear();
            limbs.recovered.clear();
            limbs.replaceInside = limbs.reenterInside = limbs.throwInside = true;
            Check(drawWithoutException(handle, "V-SKELETON-DIRECT-SAFETY: troca e draw reentrante antes do throw") ==
                          SHIP_NATIVE_FAILURE &&
                      limbs.seen.size() == 1 && limbs.recovered == std::vector<int32_t>{ 0, 1, 2, 1, 2 },
                  "V-SKELETON-DIRECT-SAFETY: troca no callback revalida registro sem culpar o novo");
            limbs.throwInside = limbs.reenterInside = false;
            limbs.recovered.clear();
            Check(drawWithoutException(handle, "V-SKELETON-DIRECT-SAFETY: draw com substituto") == SHIP_NATIVE_OK &&
                      limbs.recovered == std::vector<int32_t>{ 0, 1, 2 },
                  "V-SKELETON-DIRECT-SAFETY: novo callback segue utilizavel apos reentrada");

            limbs.seen.clear();
            skeletons->set_post_limb(handle, Limb, &limbs);
            limbs.destroyInside = true;
            const int destroyedBefore = destroyed;
            Check(skeletons->draw_ex(&playTag, &actorTag, handle) == SHIP_NATIVE_OK && limbs.seen.size() == 1 &&
                      destroyed == destroyedBefore,
                  "destruir no próprio callback para os callbacks e adia a instância");
            ShipLuaHost::LeaveOotRenderScope();
            Check(limbs.ok, "payloads e destruição no pós-membro");
            Check(skeletons->get_state(handle, &state) == SHIP_NATIVE_INVALID_ARGUMENT, "handle destruído");
            ShipLuaHost::ReleaseRetiredOotSkeletons();
            Check(destroyed == destroyedBefore + 1, "instância solta no frame seguinte");

            uint64_t throwingHandle = 0;
            Check(skeletons->create_ex("skel.direct_throw", &link, &throwingHandle) == SHIP_NATIVE_OK,
                  "V-SKELETON-DIRECT-SAFETY: esqueleto para destruir e lancar");
            Limbs destroyThrow;
            destroyThrow.skeletons = skeletons;
            destroyThrow.skeleton = throwingHandle;
            destroyThrow.destroyInside = destroyThrow.throwInside = true;
            Check(skeletons->set_post_limb(throwingHandle, Limb, &destroyThrow) == SHIP_NATIVE_OK,
                  "V-SKELETON-DIRECT-SAFETY: instala destruicao reentrante");
            ShipLuaHost::EnterOotRenderScope();
            const int destroyedAtThrow = destroyed;
            Check(drawWithoutException(throwingHandle, "V-SKELETON-DIRECT-SAFETY: destruir e lancar no pos-membro") ==
                          SHIP_NATIVE_FAILURE &&
                      destroyThrow.seen.size() == 1 && destroyThrow.ok && destroyed == destroyedAtThrow,
                  "V-SKELETON-DIRECT-SAFETY: registro some, instancia permanece viva durante draw");
            ShipLuaHost::LeaveOotRenderScope();
            Check(!ShipLuaHost::InOotRenderScope() &&
                      skeletons->get_state(throwingHandle, &state) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "V-SKELETON-DIRECT-SAFETY: escopo encerrado e handle antigo invalido");
            ShipLuaHost::ReleaseRetiredOotSkeletons();
            Check(destroyed == destroyedAtThrow + 1,
                  "V-SKELETON-DIRECT-SAFETY: instancia aposentada liberada uma unica vez");

            Check(skeletons->create_ex("skel.unload", &link, &handle) == SHIP_NATIVE_OK, "esqueleto do dono que sai");
            const int destroyedAtUnload = destroyed;
            policy.onProviderUnload("skel.unload");
            ShipLuaHost::ReleaseRetiredOotSkeletons();
            Check(destroyed == destroyedAtUnload + 1 &&
                      skeletons->get_state(handle, &state) == SHIP_NATIVE_INVALID_ARGUMENT &&
                      skeletons->create_ex("skel.unload", &link, &handle) == SHIP_NATIVE_UNSUPPORTED,
                  "unload destrói os esqueletos do dono e bloqueia o dono");

            ShipNativeStatus fromWorker = SHIP_NATIVE_OK;
            std::thread worker([&] {
                uint64_t ignored = 0;
                fromWorker = skeletons->create_ex("skel.mod", &link, &ignored);
            });
            worker.join();
            Check(fromWorker == SHIP_NATIVE_INVALID_ARGUMENT, "esqueleto fora da thread do jogo");
        }
    }
    {
        using namespace HostSlimBody;
        const auto* body = Find(policy);
        const auto* movement = HostSlimMovement::Find(policy);
        Check(body && movement && body->layout_id && std::strcmp(body->layout_id, LINKSPAN_OOT_LAYOUT_ID) == 0,
              "player-body V1 publicado");
        if (body && movement) {
            HostSlimSkeletons::Install();
            ShipLuaHost::SetOotPlayerBodyBridge({ LuaBody, Activate, Deactivate, Segments, Root, RootStop });
            // Link parado em gameplay, como no começo dos testes da F2d.
            HostSlimMovement::gameplay = HostSlimMovement::eligible = HostSlimMovement::neutral = true;
            HostSlimMovement::action = &HostSlimMovement::idleTag;
            ShipLuaHost::SetOotActionBridge({ HostSlimMovement::Snapshot, HostSlimMovement::Restore });
            ShipLuaHost::NotifyOotPlayerCreated(&HostSlimMovement::playerTag);
            const ShipOotBodyAnimationV1 animations[] = {
                { sizeof(ShipOotBodyAnimationV1), 0, "parado", "objects/corpo/parado", 0, {} },
                { sizeof(ShipOotBodyAnimationV1), 0, "andar", "objects/corpo/andar", 0, {} },
                { sizeof(ShipOotBodyAnimationV1), 0, "soco", "objects/corpo/soco", 0, {} },
            };
            const ShipOotBodySegmentV1 bodySegments[] = { { sizeof(ShipOotBodySegmentV1), 6, "objects/corpo" } };
            ShipOotBodySpecV1 spec{};
            spec.size = sizeof(spec);
            spec.skeleton = { sizeof(ShipOotSkeletonSpecV2), SHIP_OOT_SKELETON_LINK, "objects/corpo/skel",
                              "objects/corpo/parado",        { -57, 3377, 0 },       0 };
            spec.default_animation = "parado";
            spec.animations = animations;
            spec.animation_count = 3;
            spec.segments = bodySegments;
            spec.segment_count = 1;

            auto bad = spec;
            bad.size = sizeof(bad) - 1;
            Check(body->set_body("body.mod", &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "spec curta");
            bad = spec;
            bad.default_animation = "voar";
            Check(body->set_body("body.mod", &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "padrão fora da lista");
            const ShipOotBodyAnimationV1 twice[] = { animations[0], animations[0] };
            bad = spec;
            bad.animations = twice;
            bad.animation_count = 2;
            Check(body->set_body("body.mod", &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "nome repetido");
            const ShipOotBodySegmentV1 segmentZero[] = { { sizeof(ShipOotBodySegmentV1), 0, "objects/corpo" } };
            bad = spec;
            bad.segments = segmentZero;
            Check(body->set_body("body.mod", &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "segmento 0");

            HostSlimSkeletons::notLoaded.insert("__OTR__objects/corpo/soco");
            const int createdBefore = HostSlimSkeletons::created;
            Check(body->set_body("body.mod", &spec) == SHIP_NATIVE_FAILURE && activated == 0, "animação inválida");
            ShipLuaHost::ReleaseRetiredOotSkeletons();
            // As duas contagens incluem os esqueletos da tarefa 22, todos destruídos lá.
            Check(HostSlimSkeletons::created == createdBefore + 1 &&
                      HostSlimSkeletons::destroyed == HostSlimSkeletons::created && !ShipLuaHost::OotPlayerBodyInUse(),
                  "falha não deixa corpo nem esqueleto");
            HostSlimSkeletons::notLoaded.clear();
            luaBody = true;
            Check(body->set_body("body.mod", &spec) == SHIP_NATIVE_LIMIT, "corpo Lua ativo");
            luaBody = false;

            Check(body->set_body("body.mod", &spec) == SHIP_NATIVE_OK && activated == 1 &&
                      segments.at(6) == "objects/corpo" && ShipLuaHost::OotPlayerBodyInUse(),
                  "corpo ativo");
            Check(body->set_body("outro.mod", &spec) == SHIP_NATIVE_LIMIT, "corpo de outro dono");
            ShipOotBodyStateV1 state{};
            state.size = sizeof(state);
            Check(body->get_state(&state) == SHIP_NATIVE_OK && state.active && !state.controlled &&
                      state.body_generation != 0 && state.scene_epoch == ShipLuaHost::OotSceneEpoch(),
                  "estado do corpo");

            Controller controller;
            controller.body = body;
            ShipOotBodyControllerV1 callbacks{ sizeof(callbacks), 0, Tick, PostLimb, Draw, &controller };
            uint64_t control = 7;
            Check(body->acquire_control("outro.mod", state.body_generation, &callbacks, &control) ==
                          SHIP_NATIVE_LIMIT &&
                      control == 0,
                  "controle só do dono do corpo");
            Check(body->acquire_control("body.mod", state.body_generation + 1, &callbacks, &control) ==
                      SHIP_NATIVE_UNSUPPORTED,
                  "geração errada");
            Check(body->acquire_control("body.mod", state.body_generation, &callbacks, &control) == SHIP_NATIVE_OK &&
                      control,
                  "controle");
            controller.control = control;
            uint64_t second = 0;
            Check(body->acquire_control("body.mod", state.body_generation, &callbacks, &second) == SHIP_NATIVE_LIMIT,
                  "um controle por vez");
            Check(body->set_root_motion(control, 0, SHIP_OOT_ROOT_DISABLED) == SHIP_NATIVE_UNSUPPORTED,
                  "raiz só dentro do tick");

            const size_t playsBefore = HostSlimSkeletons::playLog.size();
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(controller.ticks == 1 && HostSlimSkeletons::playLog.size() == playsBefore + 1 &&
                      HostSlimSkeletons::playLog.back() == "__OTR__objects/corpo/parado" && roots == 1 &&
                      rootMoves == 0,
                  "primeiro tick toca o padrão e neutraliza a raiz");
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(HostSlimSkeletons::playLog.size() == playsBefore + 1, "padrão não reinicia");

            controller.fallbackWalk = true;
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(HostSlimSkeletons::playLog.back() == "__OTR__objects/corpo/andar", "FALLBACK no mesmo tick");
            const size_t walking = HostSlimSkeletons::playLog.size();
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(HostSlimSkeletons::playLog.size() == walking, "FALLBACK repetido continua a animação");
            controller.fallbackWalk = false;
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(HostSlimSkeletons::playLog.back() == "__OTR__objects/corpo/parado", "FALLBACK vale um tick");

            const ShipOotAnimationPlayV2 once{ sizeof(once), 3.0f, 0.0f, LINKSPAN_OOT_ANIM_ONCE, 0, { 0, 0 } };
            Check(body->start_animation(control, "voar", &once, SHIP_OOT_ANIM_OVERRIDE) == SHIP_NATIVE_INVALID_ARGUMENT,
                  "nome desconhecido");
            Check(body->start_animation(control, "soco", &once, SHIP_OOT_ANIM_OVERRIDE) == SHIP_NATIVE_OK, "OVERRIDE");
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(HostSlimSkeletons::playLog.back() == "__OTR__objects/corpo/soco", "OVERRIDE aplicado");
            for (int i = 0; i < 4; ++i)
                ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(HostSlimSkeletons::playLog.back() == "__OTR__objects/corpo/parado", "ONCE terminado volta ao padrão");

            uint64_t action = 0;
            HostSlimMovement::Owner actionOwner;
            const ShipOotPlayerActionSpecV1 actionSpec{ sizeof(actionSpec), 0, HostSlimMovement::Tick, &actionOwner };
            Check(movement->acquire_action("body.mod", &actionSpec, &action) == SHIP_NATIVE_OK, "ação do dono");
            controller.action = action;
            controller.rootMode = SHIP_OOT_ROOT_XZ;
            const int movesBefore = rootMoves, restartsBefore = rootRestarts;
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(rootMoves == movesBefore + 2 && rootRestarts == restartsBefore + 1,
                  "raiz XZ com a ação do dono, primeira amostra neutra");
            const int stopsBefore = rootStops;
            movement->release_action(action);
            Check(rootStops == stopsBefore + 1, "ação solta depois do tick neutraliza o MOVEACTOR do frame");
            controller.rootMode = SHIP_OOT_ROOT_DISABLED;
            controller.ok =
                controller.ok && body->set_root_motion(control, action, SHIP_OOT_ROOT_XZ) == SHIP_NATIVE_UNSUPPORTED;

            Check(body->set_segment(control, 16, "objects/x") == SHIP_NATIVE_INVALID_ARGUMENT &&
                      body->set_segment(control, 8, "objects/extra") == SHIP_NATIVE_OK &&
                      segments.at(8) == "objects/extra" && body->set_segment(control, 8, nullptr) == SHIP_NATIVE_OK &&
                      !segments.count(8),
                  "segmentos");
            failSegments = true;
            Check(body->set_segment(control, 9, "objects/extra") == SHIP_NATIVE_FAILURE && !segments.count(9),
                  "segmento que falha não entra");
            failSegments = false;

            ShipLuaHost::EnterOotRenderScope();
            controller.drawDefault = 0;
            Check(!ShipLuaHost::OotPlayerBodyDraw(&playTag, &playerTag, &actorTag) && controller.draws == 1,
                  "draw do dono no lugar do esqueleto");
            controller.drawDefault = 1;
            Check(ShipLuaHost::OotPlayerBodyDraw(&playTag, &playerTag, &actorTag), "draw padrão");
            const auto* skeletons = HostSlimSkeletons::Find(policy);
            Check(skeletons &&
                      skeletons->draw_ex(&playTag, &actorTag, ShipLuaHost::OotPlayerBodySkeleton()) == SHIP_NATIVE_OK &&
                      controller.posts == 3,
                  "pós-membro do corpo chega ao dono");
            ShipLuaHost::LeaveOotRenderScope();

            const uint64_t generation = state.body_generation;
            ShipLuaHost::DispatchOotPlayDestroy(&playTag);
            Check(body->get_state(&state) == SHIP_NATIVE_OK && !state.active && deactivated == 1 && shownLink == 0 &&
                      ShipLuaHost::OotPlayerBodyInUse(),
                  "fim da cena guarda só a definição");
            ShipLuaHost::OotPlayerBodyPlayerUpdate(&playTag, &playerTag);
            Check(body->get_state(&state) == SHIP_NATIVE_OK && state.active && state.body_generation > generation &&
                      activated == 2,
                  "cena nova recria o corpo com geração nova");
            const int ticksBefore = controller.ticks;
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(controller.ticks == ticksBefore &&
                      body->start_animation(control, "soco", &once, SHIP_OOT_ANIM_OVERRIDE) == SHIP_NATIVE_UNSUPPORTED,
                  "controle da geração anterior caiu");

            Check(body->acquire_control("body.mod", state.body_generation, &callbacks, &control) == SHIP_NATIVE_OK,
                  "controle na geração nova");
            controller.control = control;
            controller.tickStatus = SHIP_NATIVE_FAILURE;
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(body->get_state(&state) == SHIP_NATIVE_OK && !state.controlled, "tick que falha perde o controle");
            controller.tickStatus = SHIP_NATIVE_OK;

            // V-BODY-CALLBACK-SAFETY: excecao nao cruza o host e o proximo controle continua utilizavel.
            const auto withoutException = [&](auto&& operation, const char* message) {
                bool escaped = false;
                try {
                    operation();
                } catch (...) { escaped = true; }
                Check(!escaped, message);
                return !escaped;
            };
            const auto recoverControl = [&] {
                state.size = sizeof(state);
                const bool acquired =
                    body->get_state(&state) == SHIP_NATIVE_OK && state.active &&
                    body->acquire_control("body.mod", state.body_generation, &callbacks, &control) == SHIP_NATIVE_OK;
                Check(acquired, "V-BODY-CALLBACK-SAFETY: callbackDepth permite readquirir controle");
                controller.control = control;
                return acquired;
            };

            if (recoverControl()) {
                const int ticksAtException = controller.ticks;
                controller.throwTick = true;
                withoutException([&] { ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag); },
                                 "V-BODY-CALLBACK-SAFETY: excecao do tick contida");
                controller.throwTick = false;
                Check(controller.ticks == ticksAtException + 1 && body->get_state(&state) == SHIP_NATIVE_OK &&
                          state.active && !state.controlled,
                      "V-BODY-CALLBACK-SAFETY: tick com excecao cancela controle e conserva corpo");
                if (recoverControl()) {
                    Check(body->set_root_motion(control, 0, SHIP_OOT_ROOT_DISABLED) == SHIP_NATIVE_UNSUPPORTED,
                          "V-BODY-CALLBACK-SAFETY: ticking restaurado fora do callback");
                    const int ticksBeforeRecovery = controller.ticks;
                    withoutException([&] { ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag); },
                                     "V-BODY-CALLBACK-SAFETY: tick apos recuperacao");
                    Check(controller.ticks == ticksBeforeRecovery + 1 && body->get_state(&state) == SHIP_NATIVE_OK &&
                              state.controlled,
                          "V-BODY-CALLBACK-SAFETY: controlador novo recebe tick");
                    body->release_control(control);
                }
            }

            if (recoverControl()) {
                ShipLuaHost::EnterOotRenderScope();
                const int drawsAtException = controller.draws;
                controller.drawDefault = 0;
                controller.throwDraw = true;
                for (int attempt = 0; attempt < 4; ++attempt) {
                    withoutException(
                        [&] {
                            Check(ShipLuaHost::OotPlayerBodyDraw(&playTag, &playerTag, &actorTag),
                                  "V-BODY-CALLBACK-SAFETY: draw com excecao usa desenho padrao");
                        },
                        "V-BODY-CALLBACK-SAFETY: excecao do draw contida");
                }
                controller.throwDraw = false;
                Check(controller.draws == drawsAtException + 3 && body->get_state(&state) == SHIP_NATIVE_OK &&
                          state.controlled,
                      "V-BODY-CALLBACK-SAFETY: tres falhas desativam so o callback draw");
                body->release_control(control);
                if (recoverControl()) {
                    const int drawsBeforeRecovery = controller.draws;
                    withoutException(
                        [&] {
                            Check(!ShipLuaHost::OotPlayerBodyDraw(&playTag, &playerTag, &actorTag),
                                  "V-BODY-CALLBACK-SAFETY: draw novo volta a escolher desenho");
                        },
                        "V-BODY-CALLBACK-SAFETY: draw apos recuperacao");
                    Check(controller.draws == drawsBeforeRecovery + 1,
                          "V-BODY-CALLBACK-SAFETY: novo controle restaura callback draw");
                    body->release_control(control);
                }
                controller.drawDefault = 1;
                ShipLuaHost::LeaveOotRenderScope();
            }

            if (recoverControl()) {
                ShipLuaHost::EnterOotRenderScope();
                const int postsAtException = controller.posts;
                controller.throwPost = true;
                if (skeletons) {
                    for (int attempt = 0; attempt < 2; ++attempt) {
                        withoutException(
                            [&] {
                                Check(skeletons->draw_ex(&playTag, &actorTag, ShipLuaHost::OotPlayerBodySkeleton()) ==
                                          SHIP_NATIVE_OK,
                                      "V-BODY-CALLBACK-SAFETY: draw sobrevive ao post_limb com excecao");
                            },
                            "V-BODY-CALLBACK-SAFETY: excecao do post_limb contida");
                    }
                }
                controller.throwPost = false;
                Check(skeletons && controller.posts == postsAtException + 3 &&
                          body->get_state(&state) == SHIP_NATIVE_OK && state.controlled,
                      "V-BODY-CALLBACK-SAFETY: tres falhas desativam so o post_limb");
                body->release_control(control);
                if (recoverControl() && skeletons) {
                    const int postsBeforeRecovery = controller.posts;
                    withoutException(
                        [&] {
                            Check(skeletons->draw_ex(&playTag, &actorTag, ShipLuaHost::OotPlayerBodySkeleton()) ==
                                      SHIP_NATIVE_OK,
                                  "V-BODY-CALLBACK-SAFETY: draw depois de recuperar post_limb");
                        },
                        "V-BODY-CALLBACK-SAFETY: post_limb apos recuperacao");
                    Check(controller.posts == postsBeforeRecovery + 3,
                          "V-BODY-CALLBACK-SAFETY: novo controle restaura post_limb");
                    body->release_control(control);
                }
                ShipLuaHost::LeaveOotRenderScope();
            }

            // V-BODY-LIFETIME-REENTRY: teardown no callback nao reutiliza controle ou esqueleto invalidados.
            // Mais forte que engine.kill_actor: Actor_Kill so marca; Actor_Delete avisa depois do callback.
            for (int source = 0; source < 3; ++source) {
                ShipLuaHost::ReleaseRetiredOotSkeletons();
                state.size = sizeof(state);
                Check(body->get_state(&state) == SHIP_NATIVE_OK && state.active,
                      "V-BODY-LIFETIME-REENTRY: corpo ativo antes da reentrada");
                const uint64_t epochBefore = state.scene_epoch, generationBefore = state.body_generation;
                if (!recoverControl())
                    continue;
                const uint64_t oldControl = control;
                const int postsBefore = controller.posts, destroyedBefore = HostSlimSkeletons::destroyed;
                controller.endSceneTick = source == 0;
                controller.endSceneDraw = source == 1;
                controller.endScenePost = source == 2;
                if (source == 0) {
                    withoutException([&] { ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag); },
                                     "V-BODY-LIFETIME-REENTRY: teardown durante tick");
                } else {
                    ShipLuaHost::EnterOotRenderScope();
                    if (source == 1) {
                        withoutException(
                            [&] {
                                Check(!ShipLuaHost::OotPlayerBodyDraw(&playTag, &playerTag, &actorTag),
                                      "V-BODY-LIFETIME-REENTRY: draw invalido nao pede esqueleto padrao");
                            },
                            "V-BODY-LIFETIME-REENTRY: teardown durante draw");
                    } else if (skeletons) {
                        const uint64_t oldSkeleton = ShipLuaHost::OotPlayerBodySkeleton();
                        withoutException(
                            [&] {
                                Check(skeletons->draw_ex(&playTag, &actorTag, oldSkeleton) == SHIP_NATIVE_OK,
                                      "V-BODY-LIFETIME-REENTRY: draw em curso pode terminar");
                            },
                            "V-BODY-LIFETIME-REENTRY: teardown durante post_limb");
                        Check(controller.posts == postsBefore + 1 && HostSlimSkeletons::destroyed == destroyedBefore,
                              "V-BODY-LIFETIME-REENTRY: proximos membros param e instancia fica aposentada");
                    }
                    ShipLuaHost::LeaveOotRenderScope();
                }
                controller.endSceneTick = controller.endSceneDraw = controller.endScenePost = false;
                Check(body->get_state(&state) == SHIP_NATIVE_OK && !state.active && !state.controlled &&
                          state.scene_epoch == epochBefore && ShipLuaHost::OotPlayerBodyInUse(),
                      "V-BODY-LIFETIME-REENTRY: definicao preservada sem inventar epoch de cena");
                Check(body->start_animation(oldControl, "soco", &once, SHIP_OOT_ANIM_OVERRIDE) ==
                          SHIP_NATIVE_UNSUPPORTED,
                      "V-BODY-LIFETIME-REENTRY: controle antigo invalidado");
                ShipLuaHost::ReleaseRetiredOotSkeletons();
                Check(HostSlimSkeletons::destroyed == destroyedBefore + 1,
                      "V-BODY-LIFETIME-REENTRY: esqueleto liberado uma vez fora do callback");
                ShipLuaHost::OotPlayerBodyPlayerUpdate(&playTag, &playerTag);
                Check(body->get_state(&state) == SHIP_NATIVE_OK && state.active && !state.controlled &&
                          state.body_generation > generationBefore && state.scene_epoch == epochBefore,
                      "V-BODY-LIFETIME-REENTRY: reativacao troca geracao, conserva epoch");
                if (recoverControl()) {
                    Check(control != oldControl &&
                              body->set_root_motion(control, 0, SHIP_OOT_ROOT_DISABLED) == SHIP_NATIVE_UNSUPPORTED,
                          "V-BODY-LIFETIME-REENTRY: token novo e guarda de ticking recuperada");
                    const int ticksBeforeRecovery = controller.ticks;
                    withoutException([&] { ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag); },
                                     "V-BODY-LIFETIME-REENTRY: tick depois da reativacao");
                    Check(controller.ticks == ticksBeforeRecovery + 1,
                          "V-BODY-LIFETIME-REENTRY: callback novo recebe tick");
                    body->release_control(control);
                }
            }

            const uint64_t generationBeforeRecovery = state.body_generation;
            Check(body->set_body("body.mod", &spec) == SHIP_NATIVE_OK && body->get_state(&state) == SHIP_NATIVE_OK &&
                      state.active && !state.controlled && state.body_generation > generationBeforeRecovery,
                  "V-BODY-CALLBACK-SAFETY: set_body funciona depois das tres excecoes");

            Check(body->acquire_control("body.mod", state.body_generation, &callbacks, &control) == SHIP_NATIVE_OK,
                  "controle antes do unload");
            const int callsAtUnload = controller.ticks + controller.draws + controller.posts;
            policy.onProviderUnload("body.mod");
            ShipLuaHost::OotPlayerBodyTick(&playTag, &playerTag);
            Check(controller.ticks + controller.draws + controller.posts == callsAtUnload && shownLink == 1 &&
                      !ShipLuaHost::OotPlayerBodyInUse() &&
                      body->set_body("body.mod", &spec) == SHIP_NATIVE_UNSUPPORTED,
                  "unload devolve o Link sem chamar a DLL");
            Check(controller.ok, "payloads do corpo");
            ShipLuaHost::ReleaseRetiredOotSkeletons();
            ShipLuaHost::SetOotPlayerBodyBridge({});
            ShipLuaHost::SetOotActionBridge({});
        }
    }
    {
        // CEL-004: render V3.
        using namespace FakeRenderV3;
        Check(policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR,
                                      LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR_VERSION) != 0 &&
                  policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_LIGHT_POINT_COLOR, 1) == 0 &&
                  policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_LIGHT_FAIRY, LINKSPAN_OOT_HOOK_LIGHT_FAIRY_VERSION) != 0,
              "pontos oot.light.point_color v2 e oot.light.fairy");
        const auto* render = static_cast<const ShipOotRenderV3*>(policy.services[25].table);
        Check(render->matrix_push == static_cast<const ShipOotRenderV2*>(policy.services[21].table)->matrix_push &&
                  render->set_toon_shadow_params ==
                      static_cast<const ShipOotRenderV2*>(policy.services[21].table)->set_toon_shadow_params,
              "V3 conserva V2 no início");
        ShipLuaHost::OotViewBridge bridge;
        bridge.matrixPush = Push;
        bridge.matrixPop = Pop;
        bridge.matrixTranslateNew = TranslateNew;
        bridge.matrixRotateAxis = RotateAxis;
        bridge.exportMatrix = Export;
        bridge.drawNative = DrawNative;
        bridge.setToon = SetToon;
        bridge.frameInfo = Frame;
        bridge.interpolationOpen = Open;
        bridge.interpolationClose = Close;
        ShipLuaHost::SetOotViewBridge(bridge);

        int dl = 0;
        const void* mtx = &dl;
        Check(render->set_actor_toon_enabled(0) == SHIP_NATIVE_UNSUPPORTED &&
                  render->matrix_translate_new(1.0f, 0.0f, 0.0f) == SHIP_NATIVE_UNSUPPORTED &&
                  render->matrix_rotate_axis(1.0f, 0.0f, 1.0f, 0.0f) == SHIP_NATIVE_UNSUPPORTED &&
                  render->export_current_matrix(&mtx) == SHIP_NATIVE_UNSUPPORTED && mtx == nullptr &&
                  render->draw_native_display_list(&dl, 0) == SHIP_NATIVE_UNSUPPORTED &&
                  render->interpolation_begin(&dl, 0) == SHIP_NATIVE_UNSUPPORTED && ops.empty(),
              "V3 fora de escopo");
        ShipOotRenderFrameInfoV1 info{ sizeof(info) };
        Check(render->get_frame_info(&info) == SHIP_NATIVE_OK && info.frame == 42 && info.camera_epoch == 3 &&
                  info.delta_seconds == 0.05f && info.aspect_ratio == 2.0f && info.size == sizeof(info),
              "get_frame_info fora de escopo");
        ShipOotRenderFrameInfoV1 small{ sizeof(small) - 1 };
        Check(render->get_frame_info(&small) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  render->get_frame_info(nullptr) == SHIP_NATIVE_INVALID_ARGUMENT,
              "frame info inválido");

        ShipLuaHost::EnterOotRenderScope(&gfxTag);
        Check(render->matrix_translate_new(1.0f, 0.0f, 0.0f) == SHIP_NATIVE_INVALID_ARGUMENT && ops.empty(),
              "translate_new exige push do mod");
        Check(render->matrix_push() == SHIP_NATIVE_OK &&
                  render->matrix_translate_new(7.0f, 0.0f, 0.0f) == SHIP_NATIVE_OK && ops.back() == "new:7" &&
                  render->matrix_translate_new(std::nan(""), 0.0f, 0.0f) == SHIP_NATIVE_INVALID_ARGUMENT,
              "translate_new depois do push");
        Check(render->matrix_rotate_axis(1.0f, 0.0f, 2.0f, 0.0f) == SHIP_NATIVE_OK && lastAxis[0] == 0.0f &&
                  lastAxis[1] == 1.0f && lastAxis[2] == 0.0f,
              "rotate_axis normaliza o eixo");
        Check(render->matrix_rotate_axis(1.0f, 0.0f, 0.0f, 0.0f) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  render->matrix_rotate_axis(std::nan(""), 0.0f, 1.0f, 0.0f) == SHIP_NATIVE_INVALID_ARGUMENT,
              "eixo nulo ou ângulo inválido");
        Check(render->export_current_matrix(&mtx) == SHIP_NATIVE_OK && mtx == &mtxTag &&
                  render->export_current_matrix(nullptr) == SHIP_NATIVE_INVALID_ARGUMENT,
              "export_current_matrix no escopo");
        Check(render->draw_native_display_list(&dl, 1) == SHIP_NATIVE_OK && ops.back() == "native:1" &&
                  render->draw_native_display_list(&dl, 2) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  render->draw_native_display_list(nullptr, 0) == SHIP_NATIVE_INVALID_ARGUMENT,
              "draw_native_display_list no escopo do GraphicsContext");
        Check(render->set_actor_toon_enabled(0) == SHIP_NATIVE_UNSUPPORTED, "toon por ator só no hook de ator");
        Check(render->interpolation_end() == SHIP_NATIVE_INVALID_ARGUMENT &&
                  render->interpolation_begin(nullptr, 3) == SHIP_NATIVE_OK &&
                  render->interpolation_begin(&dl, 0) == SHIP_NATIVE_OK && children == 2 &&
                  render->interpolation_end() == SHIP_NATIVE_OK && children == 1,
              "filhos de interpolação");
        ShipLuaHost::LeaveOotRenderScope();
        Check(children == 0 && depth == 0, "fim do escopo fecha filhos e desfaz push");

        ShipLuaHost::EnterOotRenderScope();
        Check(render->export_current_matrix(&mtx) == SHIP_NATIVE_UNSUPPORTED &&
                  render->draw_native_display_list(&dl, 0) == SHIP_NATIVE_UNSUPPORTED,
              "sem GraphicsContext não há desenho próprio");
        uint32_t opened = 0;
        while (render->interpolation_begin(&dl, static_cast<int32_t>(opened)) == SHIP_NATIVE_OK)
            ++opened;
        Check(opened == LINKSPAN_OOT_RENDER_MAX_INTERPOLATION, "limite de filhos por escopo");
        ShipLuaHost::LeaveOotRenderScope();
        Check(children == 0, "limite também fecha no fim do escopo");

        // Colchete toon por ator: desliga no hook, religa antes do próximo ator.
        ops.clear();
        ShipLuaHost::BeginOotActorDrawFrame(false);
        ShipLuaHost::EnterOotRenderScope(&gfxTag, ShipLuaHost::OotRenderScopeKind::ActorDraw);
        Check(render->set_actor_toon_enabled(0) == SHIP_NATIVE_OK && ops.empty(), "sem colchete no frame, nada muda");
        ShipLuaHost::LeaveOotRenderScope();
        ShipLuaHost::RestoreOotActorToon(&gfxTag);
        Check(ops.empty(), "sem colchete não religa");
        ShipLuaHost::BeginOotActorDrawFrame(true);
        ShipLuaHost::EnterOotRenderScope(&gfxTag, ShipLuaHost::OotRenderScopeKind::ActorDraw);
        Check(render->set_actor_toon_enabled(0) == SHIP_NATIVE_OK &&
                  render->set_actor_toon_enabled(0) == SHIP_NATIVE_OK && ops.size() == 1 && ops.back() == "toon:0",
              "desliga o colchete uma vez");
        ShipLuaHost::LeaveOotRenderScope();
        ShipLuaHost::RestoreOotActorToon(&gfxTag);
        ShipLuaHost::RestoreOotActorToon(&gfxTag);
        Check(ops.size() == 2 && ops.back() == "toon:1", "religa antes do próximo ator, uma vez");
        ShipLuaHost::EnterOotRenderScope(&gfxTag, ShipLuaHost::OotRenderScopeKind::ActorDraw);
        Check(render->set_actor_toon_enabled(0) == SHIP_NATIVE_OK &&
                  render->set_actor_toon_enabled(1) == SHIP_NATIVE_OK && ops.size() == 4 && ops.back() == "toon:1",
              "o próprio hook pode desfazer");
        ShipLuaHost::LeaveOotRenderScope();
        ShipLuaHost::RestoreOotActorToon(&gfxTag);
        Check(ops.size() == 4, "nada a religar depois de desfazer");
        ShipLuaHost::EnterOotRenderScope(&gfxTag, ShipLuaHost::OotRenderScopeKind::ActorDraw);
        Check(render->set_actor_toon_enabled(0) == SHIP_NATIVE_OK && ops.size() == 5, "último ator do frame desliga");
        ShipLuaHost::LeaveOotRenderScope();
        ShipLuaHost::BeginOotActorDrawFrame(true);
        ShipLuaHost::RestoreOotActorToon(&gfxTag);
        Check(ops.size() == 5 && Count("toon:0") == 3 && Count("toon:1") == 2,
              "frame novo começa com o colchete aberto pelo laço");
        ShipLuaHost::ResetOotNativeView();
        ShipLuaHost::SetOotViewBridge({});
    }
    {
        // CEL-004: linkspan.oot.lights.
        using namespace FakeLights;
        const auto* lights = static_cast<const ShipOotLightsV1*>(policy.services[26].table);
        ShipOotPointLightV1 light{ sizeof(light), { 10.0f, 20.0f, 30.0f }, 200, { 255, 200, 0 }, 0 };
        uint64_t handle = 99;
        Check(lights->create_point_light("autor.mod", &light, &handle) == SHIP_NATIVE_UNSUPPORTED && handle == 0,
              "sem ponte não há luz");
        ShipLuaHost::OotLightsBridge bridge;
        bridge.gameplay = Gameplay;
        bridge.insert = Insert;
        bridge.update = Update;
        bridge.remove = Remove;
        bridge.info = Info;
        ShipLuaHost::SetOotLightsBridge(bridge);
        ShipOotPointLightV1 bad = light;
        bad.size = sizeof(bad) - 1;
        Check(lights->create_point_light("autor.mod", &bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "size curto");
        bad = light;
        bad.position[1] = std::nan("");
        Check(lights->create_point_light("autor.mod", &bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "posição NaN");
        bad = light;
        bad.radius = -1;
        Check(lights->create_point_light("autor.mod", &bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT, "raio negativo");
        bad = light;
        bad.glow = 2;
        Check(lights->create_point_light("autor.mod", &bad, &handle) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  lights->create_point_light("", &light, &handle) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  lights->create_point_light("autor.mod", &light, nullptr) == SHIP_NATIVE_INVALID_ARGUMENT,
              "glow, dono e handle inválidos");
        current = nullptr;
        Check(lights->create_point_light("autor.mod", &light, &handle) == SHIP_NATIVE_UNSUPPORTED, "fora de gameplay");
        current = &playTag;
        Check(lights->create_point_light("autor.mod", &light, &handle) == SHIP_NATIVE_OK && handle &&
                  ops.back() == "insert:0" && lastLight.radius == 200,
              "create");
        const void* identity = nullptr;
        Check(lights->get_point_light_info(handle, &identity) == SHIP_NATIVE_OK && identity == &infos[0], "identidade");
        light.radius = 150;
        Check(lights->update_point_light(handle, &light) == SHIP_NATIVE_OK && ops.back() == "update:0" &&
                  lastLight.radius == 150,
              "update");
        Check(lights->destroy_point_light(handle) == SHIP_NATIVE_OK && ops.back() == "remove:0:a", "destroy");
        Check(lights->update_point_light(handle, &light) == SHIP_NATIVE_UNSUPPORTED &&
                  lights->get_point_light_info(handle, &identity) == SHIP_NATIVE_UNSUPPORTED && identity == nullptr &&
                  lights->destroy_point_light(handle) == SHIP_NATIVE_OK,
              "handle apagado");
        Check(lights->update_point_light(uint64_t{ 1 } << 40, &light) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  lights->destroy_point_light(uint64_t{ 1 } << 40) == SHIP_NATIVE_INVALID_ARGUMENT,
              "handle que nunca existiu");
        uint64_t handles[LINKSPAN_OOT_LIGHTS_MAX]{};
        bool created = true;
        for (uint32_t i = 0; i < LINKSPAN_OOT_LIGHTS_MAX; ++i) {
            created =
                created && lights->create_point_light(i % 2 ? "b.mod" : "a.mod", &light, &handles[i]) == SHIP_NATIVE_OK;
        }
        uint64_t extra = 0;
        Check(created && ShipLuaHost::OotLightCount() == LINKSPAN_OOT_LIGHTS_MAX &&
                  lights->create_point_light("a.mod", &light, &extra) == SHIP_NATIVE_LIMIT && extra == 0,
              "teto do host");
        Check(handles[0] != handle, "vaga reusada tem handle novo");
        ShipLuaHost::ReleaseOotLightOwner("a.mod");
        Check(ShipLuaHost::OotLightCount() == LINKSPAN_OOT_LIGHTS_MAX / 2 &&
                  lights->update_point_light(handles[0], &light) == SHIP_NATIVE_UNSUPPORTED &&
                  lights->update_point_light(handles[1], &light) == SHIP_NATIVE_OK,
              "unload tira só as luzes do dono");
        full = true;
        Check(lights->create_point_light("a.mod", &light, &extra) == SHIP_NATIVE_LIMIT, "buffer do jogo cheio");
        full = false;
        ops.clear();
        ShipLuaHost::ReleaseOotSceneLights();
        Check(ShipLuaHost::OotLightCount() == 0 && ops.size() == LINKSPAN_OOT_LIGHTS_MAX / 2 &&
                  std::all_of(ops.begin(), ops.end(), [](const std::string& op) { return op.back() == 'a'; }),
              "fim da cena tira as luzes da lista dela");
        Check(lights->create_point_light("a.mod", &light, &handle) == SHIP_NATIVE_OK, "luz da cena nova");
        ops.clear();
        current = &otherPlay;
        Check(lights->update_point_light(handle, &light) == SHIP_NATIVE_UNSUPPORTED && ops.empty() &&
                  ShipLuaHost::OotLightCount() == 0,
              "luz de outra PlayState cai sem mexer na lista nova");
        current = &playTag;
        std::thread([&] {
            uint64_t other = 0;
            Check(lights->create_point_light("a.mod", &light, &other) == SHIP_NATIVE_INVALID_ARGUMENT, "outra thread");
        }).join();
        // Reentrada pela bridge: create/update/destroy recusam sem tocar em vaga, e a luz externa termina inteira.
        ShipLuaHost::ResetOotNativeLights();
        ops.clear();
        uint64_t outer = 0;
        reenter = lights;
        reenterHandle = 0;
        reentered.clear();
        Check(lights->create_point_light("a.mod", &light, &outer) == SHIP_NATIVE_OK && outer &&
                  ShipLuaHost::OotLightCount() == 1 && ops.size() == 1 && ops[0] == "insert:0" && reentered.size() == 3,
              "create reentrante cria só a luz externa");
        reenterHandle = outer;
        reentered.clear();
        Check(lights->update_point_light(outer, &light) == SHIP_NATIVE_OK && ShipLuaHost::OotLightCount() == 1 &&
                  lights->get_point_light_info(outer, &identity) == SHIP_NATIVE_OK && identity == &infos[0],
              "update reentrante não apaga a própria luz");
        Check(reentered.size() == 3 &&
                  std::all_of(reentered.begin(), reentered.end(),
                              [](ShipNativeStatus status) { return status == SHIP_NATIVE_UNSUPPORTED; }),
              "reentrada responde UNSUPPORTED");
        reenter = nullptr;
        Check(lights->destroy_point_light(outer) == SHIP_NATIVE_OK && ShipLuaHost::OotLightCount() == 0,
              "fora da bridge o serviço volta a aceitar");
        ShipLuaHost::ResetOotNativeLights();
        ShipLuaHost::SetOotLightsBridge({});
    }
    {
        using namespace FakeItems;
        const auto* items = static_cast<const ShipOotItemsV1*>(policy.services[9].table);
        const auto* actors = static_cast<const ShipOotActorsV1*>(policy.services[10].table);
        uint8_t item = 0;
        ShipOotItemSpecV1 spec{ sizeof(spec),
                                "autor.lanterna",
                                "textures/icon_item_static/gItemIconLensOfTruthTex",
                                LINKSPAN_OOT_ITEM_AGE_ANY,
                                Use,
                                &uses };
        Check(items->register_item(&spec, &item) == SHIP_NATIVE_OK && item == LINKSPAN_OOT_ITEMS_FIRST_ID,
              "sem ponte o registro ainda aloca o id");
        Check(items->set_button_item(1, item) == SHIP_NATIVE_UNSUPPORTED, "sem ponte não há botão");
        Check(items->unregister_item(item) == SHIP_NATIVE_OK, "remove o item sem ponte");

        ShipLuaHost::OotItemsBridge bridge;
        bridge.setItemVisual = SetItemVisual;
        bridge.getButtonItem = GetButton;
        bridge.setButtonItem = SetButton;
        bridge.addActorType = AddActor;
        bridge.killActors = Kill;
        bridge.drawDisplayList = Draw;
        ShipLuaHost::SetOotItemsBridge(bridge);

        Check(items->register_item(&spec, &item) == SHIP_NATIVE_OK && item == LINKSPAN_OOT_ITEMS_FIRST_ID &&
                  icons[item] == "__OTR__textures/icon_item_static/gItemIconLensOfTruthTex",
              "registro entrega o ícone com __OTR__ ao jogo");
        uint8_t other = 0;
        Check(items->register_item(&spec, &other) == SHIP_NATIVE_INVALID_ARGUMENT && other == 0xFF, "nome repetido");
        ShipOotItemSpecV1 bad = spec;
        bad.name = "semponto";
        Check(items->register_item(&bad, &other) == SHIP_NATIVE_INVALID_ARGUMENT, "nome sem namespace");
        bad = spec;
        bad.name = "autor.outro";
        bad.icon_path = "__OTR__textures/x";
        Check(items->register_item(&bad, &other) == SHIP_NATIVE_INVALID_ARGUMENT, "ícone com __OTR__");
        bad.icon_path = "../fora";
        Check(items->register_item(&bad, &other) == SHIP_NATIVE_INVALID_ARGUMENT, "ícone com ..");
        bad.icon_path = spec.icon_path;
        bad.age = 3;
        Check(items->register_item(&bad, &other) == SHIP_NATIVE_INVALID_ARGUMENT, "idade inválida");
        bad.age = LINKSPAN_OOT_ITEM_AGE_CHILD;
        Check(items->register_item(&bad, &other) == SHIP_NATIVE_OK && other == LINKSPAN_OOT_ITEMS_FIRST_ID + 1,
              "segundo item no id seguinte");

        uint8_t found = 0;
        Check(items->find_item("autor.lanterna", &found) == SHIP_NATIVE_OK && found == item, "find_item");
        Check(items->find_item("autor.nada", &found) == SHIP_NATIVE_FAILURE && found == 0xFF, "find_item ausente");
        Check(items->set_button_item(0, item) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  items->set_button_item(4, item) == SHIP_NATIVE_INVALID_ARGUMENT,
              "só botões C");
        Check(items->set_button_item(1, 0x05) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  items->set_button_item(1, LINKSPAN_OOT_ITEMS_LAST_ID) == SHIP_NATIVE_INVALID_ARGUMENT,
              "só itens sintéticos registrados");
        buttons[2] = 0x05;
        Check(items->set_button_item(1, item) == SHIP_NATIVE_OK && items->set_button_item(3, other) == SHIP_NATIVE_OK &&
                  buttons[1] == item && buttons[3] == other,
              "set_button_item");
        uint8_t onButton = 0;
        Check(items->get_button_item(2, &onButton) == SHIP_NATIVE_OK && onButton == 0x05,
              "get_button_item devolve item vanilla");
        const auto* record = ShipLuaHost::FindOotItem(item);
        Check(record && record->use(record->user, item, 1) == SHIP_NATIVE_OK && uses == 1, "callback de uso");

        auto saved = ShipLuaHost::ExportOotItemButtons();
        Check(saved.size() == 2 && saved[1] == "autor.lanterna" && saved[3] == "autor.outro",
              "save grava só botões sintéticos, pelo nome");
        Check(items->unregister_item(other) == SHIP_NATIVE_OK && buttons[3] == 0xFF && !icons.count(other),
              "unregister esvazia o botão e o ícone");
        Check(items->unregister_item(other) == SHIP_NATIVE_FAILURE &&
                  items->unregister_item(0x05) == SHIP_NATIVE_INVALID_ARGUMENT,
              "unregister repetido ou fora da faixa");

        // Carga: o nome volta a id mesmo que o id mude; nome sem registro esvazia.
        Check(items->unregister_item(item) == SHIP_NATIVE_OK && buttons[1] == 0xFF, "remove a lanterna");
        spec.name = "autor.bomba";
        uint8_t bomb = 0;
        Check(items->register_item(&spec, &bomb) == SHIP_NATIVE_OK && bomb == LINKSPAN_OOT_ITEMS_FIRST_ID,
              "id livre reaproveitado");
        spec.name = "autor.lanterna";
        Check(items->register_item(&spec, &item) == SHIP_NATIVE_OK && item == LINKSPAN_OOT_ITEMS_FIRST_ID + 1,
              "lanterna volta com outro id");
        buttons = { 0xFF, LINKSPAN_OOT_ITEMS_FIRST_ID, 0x05, 0xEE };
        ShipLuaHost::RestoreOotItemButtons(saved);
        Check(buttons[1] == item && buttons[2] == 0x05 && buttons[3] == 0xFF,
              "restore mapeia nomes, mantém vanilla e esvazia nome desconhecido");
        buttons = { 0xFF, 0xFF, 0xEE, 0xFF };
        ShipLuaHost::RestoreOotItemButtons({});
        Check(buttons[2] == 0xFF, "id sintético sem nome salvo sai do botão");

        int16_t actorId = -1;
        ShipOotActorTypeSpecV1 actorSpec{ sizeof(actorSpec), "autor.orbe", 7,       0,       1,      0x150,
                                          nullptr,           nullptr,      nullptr, nullptr, nullptr };
        Check(actors->register_actor_type(&actorSpec, &actorId) == SHIP_NATIVE_OK && actorId == 0x1D7,
              "register_actor_type");
        Check(actors->register_actor_type(&actorSpec, &actorId) == SHIP_NATIVE_INVALID_ARGUMENT && actorId == -1,
              "tipo ativo repetido");
        int16_t lookup = 0;
        Check(actors->find_actor_type("autor.orbe", &lookup) == SHIP_NATIVE_OK && lookup == 0x1D7, "find_actor_type");
        Check(actors->draw_display_list(&lookup, "objects/gameplay_keep/gHeartPieceInteriorDL", 1) ==
                  SHIP_NATIVE_UNSUPPORTED,
              "draw fora do draw do ator");
        ShipLuaHost::SetOotActorDrawActive(true);
        Check(actors->draw_display_list(&lookup, "objects/gameplay_keep/gHeartPieceInteriorDL", 1) == SHIP_NATIVE_OK &&
                  lastDraw == "__OTR__objects/gameplay_keep/gHeartPieceInteriorDL",
              "draw dentro do draw do ator");
        Check(actors->draw_display_list(&lookup, "objects/x", 2) == SHIP_NATIVE_INVALID_ARGUMENT, "modo inválido");
        ShipLuaHost::SetOotActorDrawActive(false);
        ShipLuaHost::EnterOotActorCallback();
        ShipOotActorTypeSpecV1 nested = actorSpec;
        nested.name = "autor.dentro";
        Check(actors->register_actor_type(&nested, &lookup) == SHIP_NATIVE_UNSUPPORTED,
              "registro dentro de callback de ator");
        ShipLuaHost::LeaveOotActorCallback();
        Check(actors->unregister_actor_type(0x1D7) == SHIP_NATIVE_OK && killedTypes.size() == 1 &&
                  killedTypes[0].first == 0x1D7 && killedTypes[0].second == 7,
              "unregister mata as instâncias");
        const auto* type = ShipLuaHost::FindOotActorType(0x1D7);
        Check(type && !type->active, "tipo removido fica inativo");
        Check(actors->find_actor_type("autor.orbe", &lookup) == SHIP_NATIVE_FAILURE, "tipo inativo não é achado");
        Check(actors->register_actor_type(&actorSpec, &actorId) == SHIP_NATIVE_OK && actorId == 0x1D7,
              "mesmo nome reaproveita o id");

        std::thread worker([&] {
            uint8_t id = 0;
            Check(items->find_item("autor.lanterna", &id) == SHIP_NATIVE_INVALID_ARGUMENT, "outra thread");
        });
        worker.join();
        ShipLuaHost::ResetOotNativeItems();
        Check(icons.empty() && !ShipLuaHost::FindOotItem(item) &&
                  actors->find_actor_type("autor.orbe", &lookup) == SHIP_NATIVE_FAILURE,
              "reset limpa itens e desativa tipos");
        ShipLuaHost::SetOotItemsBridge({});
    }
    {
        Check(!policy.escapeHatch, "sem binding do jogo não há escape hatch");
        const std::string sha(64, 'c');
        const std::string text = "linkspan-symbols 1\nsha256 " + sha + "\n" +
                                 "10a0\t-\tz_player.c\tPlayer_Action_Roll\n" + "20b0\t-\tz_en_a.c\tInit\n" +
                                 "30c0\t-\tz_en_b.c\tInit\n" + "40d0\tfolded\tz_lib.c\tMath_Nop\n" +
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
        Check(!table.Parse("linkspan-symbols 1\nsha256 " + sha + "\n10\tquente\ta.c\tX\n", error), "flag inválida");
    }
    {
        for (const char* point :
             { LINKSPAN_OOT_HOOK_SAVE_LOADED, LINKSPAN_OOT_HOOK_SAVE_SAVING, LINKSPAN_OOT_HOOK_SAVE_DELETED,
               LINKSPAN_OOT_HOOK_SAVE_COPIED, LINKSPAN_OOT_HOOK_ANCHOR_STATE }) {
            Check(policy.hooks->FindPoint(point, LINKSPAN_OOT_HOOKS_VERSION) != 0, point);
        }
        const auto* save = static_cast<const ShipOotSaveV1*>(policy.services[8].table);
        ShipLuaHost::ClearOotSaveData();
        uint64_t mod = 0, again = 0, bad = 0;
        Check(save->open_namespace("autor.mod", 2, &mod) == SHIP_NATIVE_OK && mod, "abre namespace");
        Check(save->open_namespace("autor.mod", 2, &again) == SHIP_NATIVE_OK && again == mod,
              "mesmo namespace e versão devolvem o mesmo handle");
        Check(save->open_namespace("autor.mod", 3, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, "versão divergente");
        for (const char* name : { "semponto", "linkspan.host", ".a.b", "a.b.", "a b.c", "" }) {
            Check(save->open_namespace(name, 1, &bad) == SHIP_NATIVE_INVALID_ARGUMENT, name);
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
        ShipLuaHost::SetOotHostSaveBlock("linkspan.scenes", 1, nlohmann::json{ { "x.y", { 1, 2, 3, 4, 5, 6, 7 } } });

        auto exported = ShipLuaHost::ExportOotSaveSection();
        exported["namespaces"]["outro.mod"] = { { "version", 4 }, { "required", true }, { "data", { { "k", "v" } } } };
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

        // NEI-HOST-003: namespaces no estado do time do Anchor.
        const auto* anchor = static_cast<const ShipOotAnchorV1*>(policy.services[24].table);
        ShipLuaHost::ClearOotSaveData();
        Check(anchor->connected() == 0, "sem ponte o Anchor não está conectado");
        ShipLuaHost::SetOotAnchorBridge({ [] { return true; } });
        Check(anchor->connected() == 1, "conectado pela ponte");
        Check(anchor->share_namespace(0) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  anchor->share_namespace(mod + 999) == SHIP_NATIVE_INVALID_ARGUMENT,
              "share de handle inválido");
        Check(anchor->share_namespace(mod) == SHIP_NATIVE_OK && anchor->share_namespace(mod) == SHIP_NATIVE_OK,
              "share idempotente");
        Check(ShipLuaHost::ExportOotAnchorTeamState().is_null(), "namespace sem dados não vai no estado");
        Check(save->write(mod, "{\"n\":1}", 7) == SHIP_NATIVE_OK, "dados do mod");
        auto team = ShipLuaHost::ExportOotAnchorTeamState();
        Check(team["version"] == 1 && team["namespaces"]["autor.mod"]["version"] == 2 &&
                  team["namespaces"]["autor.mod"]["data"]["n"] == 1,
              "estado do time leva o namespace compartilhado");
        team["namespaces"]["autor.mod"]["data"]["n"] = 5;
        team["namespaces"]["outro.mod"] = { { "version", 1 }, { "data", 3 } };
        auto imported = ShipLuaHost::ImportOotAnchorTeamState(team);
        Check(imported.replaced.size() == 1 && imported.refused.empty() &&
                  save->read(mod, text.data(), static_cast<uint32_t>(text.size()), &size) == SHIP_NATIVE_OK &&
                  nlohmann::json::parse(text.substr(0, size))["n"] == 5,
              "import substitui só o que este cliente compartilha");
        uint64_t other = 0;
        Check(save->open_namespace("outro.mod", 1, &other) == SHIP_NATIVE_OK &&
                  save->read(other, nullptr, 0, &size) == SHIP_NATIVE_UNSUPPORTED,
              "namespace não compartilhado fica de fora");
        team["namespaces"]["autor.mod"]["version"] = 3;
        imported = ShipLuaHost::ImportOotAnchorTeamState(team);
        Check(imported.replaced.empty() && imported.refused.size() == 1 && imported.refused[0] == "autor.mod" &&
                  save->read(mod, text.data(), static_cast<uint32_t>(text.size()), &size) == SHIP_NATIVE_OK &&
                  nlohmann::json::parse(text.substr(0, size))["n"] == 5,
              "versão de schema diferente é recusada sem mexer no local");
        team["version"] = 2;
        Check(ShipLuaHost::ImportOotAnchorTeamState(team).replaced.empty(), "estado de outra versão ignorado");
        Check(anchor->unshare_namespace(other) == SHIP_NATIVE_FAILURE &&
                  anchor->unshare_namespace(mod) == SHIP_NATIVE_OK && ShipLuaHost::ExportOotAnchorTeamState().is_null(),
              "unshare");
        for (uint32_t i = 0; i < LINKSPAN_OOT_ANCHOR_MAX_SHARED; ++i) {
            uint64_t handle = 0;
            const std::string name = "limite.mod" + std::to_string(i);
            Check(save->open_namespace(name.c_str(), 1, &handle) == SHIP_NATIVE_OK &&
                      anchor->share_namespace(handle) == SHIP_NATIVE_OK,
                  "share até o limite");
        }
        Check(anchor->share_namespace(other) == SHIP_NATIVE_LIMIT, "LIMIT além do máximo");
        std::thread([&] {
            Check(anchor->share_namespace(mod) == SHIP_NATIVE_INVALID_ARGUMENT && anchor->connected() == 0,
                  "anchor só na thread do jogo");
        }).join();
        ShipLuaHost::ResetOotNativeAnchor();
        Check(anchor->share_namespace(other) == SHIP_NATIVE_OK, "reset esquece os compartilhados");
        ShipLuaHost::ResetOotNativeAnchor();
        ShipLuaHost::SetOotAnchorBridge({});
        ShipLuaHost::ClearOotSaveData();
    }
    Check(policy.hooks && policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_PLAY_UPDATE, LINKSPAN_OOT_HOOKS_VERSION) &&
              policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_ACTOR_UPDATE, LINKSPAN_OOT_HOOKS_VERSION) &&
              policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_ACTOR_DRAW, LINKSPAN_OOT_HOOKS_VERSION) &&
              policy.hooks->HookCount() == 0,
          "host deve declarar oot.play.update, oot.actor.update e oot.actor.draw v1 sem hooks");
    {
        Check(policy.hooks->FindPoint(LINKSPAN_OOT_HOOK_ROOM_ACTORS, LINKSPAN_OOT_HOOK_ROOM_ACTORS_VERSION) != 0,
              "ponto oot.room.actors");
        std::array<ShipOotActorEntryV2, 8> entries{};
        entries[0] = { 0x15, { 1, 2, 3 }, { 0, 0, 0 }, 3 };
        entries[1] = { 0x16, { 4, 5, 6 }, { 0, 0x4000, 0 }, 7 };
        Check(ShipLuaHost::DispatchOotRoomActors(nullptr, 81, 0, 0, "scenes/x", entries.data(), 2, 8) == 2,
              "sem hooks a lista não muda");
        struct Seen {
            std::string path;
            int32_t scene = 0;
        } seen;
        ShipNativeHookSpec spec{ sizeof(ShipNativeHookSpec),
                                 LINKSPAN_OOT_HOOK_ROOM_ACTORS,
                                 LINKSPAN_OOT_HOOK_ROOM_ACTORS_VERSION,
                                 sizeof(ShipOotRoomActorsHookV2),
                                 SHIP_NATIVE_HOOK_TRANSFORM,
                                 0,
                                 0,
                                 [](void* user, const ShipNativeHookCall* call) -> ShipNativeStatus {
                                     auto* data = static_cast<Seen*>(user);
                                     auto* payload = static_cast<ShipOotRoomActorsHookV2*>(call->payload);
                                     data->path = payload->room_path;
                                     data->scene = payload->scene_id;
                                     payload->entries[0].params = 0x42;
                                     payload->entries[payload->count] = { 0x77, { 9, 9, 9 }, { 0, 0, 0 }, 1 };
                                     ++payload->count;
                                     return SHIP_NATIVE_OK;
                                 },
                                 &seen };
        uint64_t handle = 0;
        Check(policy.hooks->Register("test", spec, &handle) == SHIP_NATIVE_OK, "transform em oot.room.actors");
        Check(ShipLuaHost::DispatchOotRoomActors(nullptr, 81, 0, 0, "scenes/x", entries.data(), 2, 8) == 3 &&
                  entries[0].params == 0x42 && entries[2].id == 0x77 && seen.path == "scenes/x" && seen.scene == 81,
              "transform edita e acrescenta atores");
        Check(ShipLuaHost::DispatchOotRoomActors(nullptr, 81, 0, 0, "scenes/x", entries.data(), 8, 8) == 8,
              "contagem acima da capacidade mantém a lista");
        Check(policy.hooks->Unregister("test", handle) == SHIP_NATIVE_OK, "remove transform");
        spec.callback = [](void*, const ShipNativeHookCall* call) -> ShipNativeStatus {
            static_cast<ShipOotRoomActorsHookV2*>(call->payload)->count = 0;
            return SHIP_NATIVE_FAILURE;
        };
        Check(policy.hooks->Register("test", spec, &handle) == SHIP_NATIVE_OK &&
                  ShipLuaHost::DispatchOotRoomActors(nullptr, 81, 0, 0, "scenes/x", entries.data(), 3, 8) == 3 &&
                  policy.hooks->Unregister("test", handle) == SHIP_NATIVE_OK,
              "transform que falha tem a contagem restaurada");
        spec.mode = SHIP_NATIVE_HOOK_REPLACE;
        Check(policy.hooks->Register("test", spec, &handle) == SHIP_NATIVE_UNSUPPORTED,
              "oot.room.actors recusa replace");
    }
    {
        struct Seen {
            int calls = 0;
            int16_t id = 0;
        } seen;
        const ShipNativeHookSpec spec{ sizeof(ShipNativeHookSpec),
                                       LINKSPAN_OOT_HOOK_ACTOR_UPDATE,
                                       LINKSPAN_OOT_HOOKS_VERSION,
                                       sizeof(ShipOotActorHookV1),
                                       SHIP_NATIVE_HOOK_OBSERVE,
                                       SHIP_NATIVE_HOOK_BEFORE,
                                       0,
                                       [](void* user, const ShipNativeHookCall* call) -> ShipNativeStatus {
                                           auto* data = static_cast<Seen*>(user);
                                           ++data->calls;
                                           data->id = static_cast<const ShipOotActorHookV1*>(call->payload)->actor_id;
                                           return SHIP_NATIVE_OK;
                                       },
                                       &seen };
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
        ShipOotActorHookV1 payload{ sizeof(ShipOotActorHookV1), nullptr, nullptr, 7, 0 };
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
        if (!layer || layer->size < sizeof(ShipOotResourceLayerV2) || layer->layer_index != capture.archives.size() ||
            layer->layer_count != 2) {
            return SHIP_NATIVE_FAILURE;
        }
        capture.archives.emplace_back(archive, layer->archive_path_length);
        capture.contents.emplace_back(reinterpret_cast<const char*>(data), layer->data_size);
        capture.hashes.push_back(layer->content_hash);
        return SHIP_NATIVE_OK;
    };
    Check(resourcesV2->read_file_layers("test/layers.json", collectLayer, &layers) == SHIP_NATIVE_OK &&
              layers.archives == std::vector<std::string>{ "base.o2r", "override.shipmod" } &&
              layers.contents == std::vector<std::string>{ "{\"base\":1}", "{\"mod\":2}" } &&
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
              dirtiedResources.size() == 1 && unloadedResources.size() == 1 &&
              unmountedArchives == std::vector<uint64_t>{ 10 },
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
    gamepadButtons = uint32_t{ 1 } << 3;
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
              movementV2->bind_gamepad_axis && movementV2->player_use_item_shortcut && movementV2->get_item_button_rect,
          "movement V2 deve preservar o prefixo da V1 e publicar as funções novas");
    gamepadAxes[5] = 32000;
    Check(movementV2->get_gamepad_axis(0, 5) == 32000 && movementV2->get_gamepad_axis(4, 5) == 0,
          "movement V2 deve expor eixos físicos SDL por porta");
    Check(movementV2->bind_gamepad_axis(0, BTN_CLEFT, 5, 0) == SHIP_NATIVE_INVALID_ARGUMENT &&
              movementV2->bind_gamepad_axis(0, BTN_CLEFT, 5, 1) == SHIP_NATIVE_OK && boundAxes.size() == 1 &&
              boundAxes[0] == std::tuple<uint16_t, uint8_t, int8_t>{ BTN_CLEFT, 5, 1 },
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
        Check(movementV2->player_use_item_shortcut(ITEM_LENS) == SHIP_NATIVE_UNSUPPORTED && usedItems.size() == before,
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
    Check(shortcut(ITEM_BOOTS_IRON) == SHIP_NATIVE_OK && CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) == EQUIP_VALUE_BOOTS_IRON &&
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

    gOcarinaSongButtons[OCARINA_SONG_SARIAS] = { 6, { 1, 2, 3, 1, 2, 3 } };
    // Estado inicial de code_800EC960.c: o Espantalho já declara 8 botões, mas sem gravação a nota [1]
    // mantém volume 0xFF, e isso sozinho não pode anunciar a música.
    gOcarinaSongButtons[OCARINA_SONG_SCARECROW_SPAWN] = { 8, { 0 } };
    sOcarinaSongNotes[OCARINA_SONG_SCARECROW_SPAWN][1].volume = 0xFF;
    std::array<uint8_t, LINKSPAN_OOT_OCARINA_MAX_NOTES> songNotes{};
    uint32_t songNoteCount = 0;
    Check(ocarina && ocarina->size == sizeof(ShipOotOcarinaV1) && !ocarina->is_active() &&
              ocarina->get_available_song_flags() == 0 && ocarina->get_song_count() == OCARINA_SONG_SCARECROW_SPAWN,
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
    gOcarinaSongButtons[OCARINA_SONG_SCARECROW_SPAWN] = { 8, { 0, 1, 2, 3, 4, 3, 2, 1 } };
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
    Check(!ocarina->is_active() && ocarina->get_available_song_flags() == 0 && OotNative_TakePendingOcarinaSong() == -1,
          "fechar a ocarina deve descartar a música pendente");
    OotNative_PublishOcarinaState(1, uint16_t(1u << OCARINA_SONG_SARIAS));
    {
        // O teste usa um bridge simulado: cobre o roteamento, não ShipInit no host.
        const int writesBefore = settingWrites;
        const int32_t previousFreeLook = settingValue;
        const int32_t nextFreeLook = previousFreeLook == 0 ? 1 : 0;
        Check(movementV2->set_setting_int("gSettings.FreeLook.Enabled", nextFreeLook) == SHIP_NATIVE_OK &&
                  settingWrites == writesBefore + 1 && settingValue == nextFreeLook &&
                  movementV2->set_setting_int("gSettings.FreeLook.Enabled", previousFreeLook) == SHIP_NATIVE_OK &&
                  settingWrites == writesBefore + 2 && settingValue == previousFreeLook,
              "setting do SoH deve chegar ao bridge ao alternar e restaurar");
        const char* transientSettings[] = {
            "linkspan.hud.hide_item_button.c_left",
            "linkspan.hud.hide_item_button.c_down",
            "linkspan.hud.hide_item_button.c_right",
            "linkspan.input.sword_over_shield",
            "linkspan.hud.dpad",
            "linkspan.pause.equip_selector",
        };
        for (const char* name : transientSettings) {
            Check(movementV2->set_setting_int(name, 1) == SHIP_NATIVE_OK && movementV2->get_setting_int(name, 0) == 1 &&
                      movementV2->set_setting_int(name, 0) == SHIP_NATIVE_OK &&
                      movementV2->get_setting_int(name, 1) == 0,
                  "setting transitório deve alternar e restaurar somente no host");
        }
        Check(movementV2->set_setting_int("linkspan.transient_settings", 0) == SHIP_NATIVE_INVALID_ARGUMENT &&
                  settingWrites == writesBefore + 2,
              "settings transitórios e probe não devem chamar o bridge nem ShipInit");
    }
    Check(movementV2->get_setting_int("linkspan.transient_settings", 0) == 15 &&
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
                  filter(shieldAndSword, 0) == Buttons{ BTN_B, BTN_B } &&
                  filter(shieldAndSword, 0) == Buttons{ BTN_B, 0 } && filter(BTN_R, 0) == Buttons{ BTN_R, 0 },
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
    Check(movementV2->set_setting_int("linkspan.hud.dpad", 1) == SHIP_NATIVE_OK &&
              LinkSpan_PauseEquipSelectorEnabled() == 0 &&
              movementV2->set_setting_int("linkspan.pause.equip_selector", 7) == SHIP_NATIVE_OK &&
              movementV2->get_setting_int("linkspan.pause.equip_selector", 0) == 1 &&
              LinkSpan_PauseEquipSelectorEnabled() == 1 &&
              otherSettings.find("linkspan.pause.equip_selector") == otherSettings.end() &&
              movementV2->set_setting_int("linkspan.hud.dpad", 0) == SHIP_NATIVE_OK &&
              LinkSpan_PauseEquipSelectorEnabled() == 1 &&
              movementV2->set_setting_int("linkspan.pause.equip_selector", 0) == SHIP_NATIVE_OK &&
              LinkSpan_PauseEquipSelectorEnabled() == 0,
          "seletor do pause (RFC 0028) deve ter setting próprio, só em memória e independente do D-pad do HUD");
    std::thread worker([&] {
        Check(!engine->get_player() && !engine->get_save_context() && !movement->get_input_current(0),
              "thread externa deve ser recusada");
        uint32_t size = 0;
        Check(!resources->has_file("test/core.json") &&
                  resources->read_file("test/core.json", nullptr, 0, &size) == SHIP_NATIVE_INVALID_ARGUMENT,
              "resources V1 deve recusar thread externa");
        Check(resourcesV2->read_file_layers("test/layers.json", collectLayer, &layers) == SHIP_NATIVE_INVALID_ARGUMENT,
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
                  movementV2->set_setting_int("linkspan.pause.equip_selector", 1) == SHIP_NATIVE_UNSUPPORTED &&
                  movementV2->get_setting_int("linkspan.transient_settings", 7) == 7,
              "settings transitórios devem recusar thread externa");
    });
    OotNative_PublishOcarinaState(0, 0);
    worker.join();
    gamepadAxes[5] = 0;
    boundAxes.clear();
    Check(engine->spawn_actor(42, 1, 2, 3, 4, 5, 6, 123) == &spawned && spawned.id == 42 && spawned.params == 123 &&
              spawned.world.pos.y == 2 && spawned.world.rot.z == 6,
          "spawn deve encaminhar argumentos sem catálogo por mod");
    Check(engine->kill_actor(&spawned) == SHIP_NATIVE_INVALID_ARGUMENT, "ator fora da lista");
    play.actorCtx.actorLists[ACTORCAT_PROP].head = &spawned;
    Check(engine->kill_actor(&spawned) == SHIP_NATIVE_OK && killed == &spawned, "kill do ator presente");

    if (argc == 3 && std::string(argv[2]) == "public") {
        const auto manifest = ShipLua::ParseManifestFile((std::filesystem::path(argv[1]) / "manifest.toml").string());
        if (!manifest.isOk()) {
            std::cerr << manifest.message;
            return 1;
        }
        auto report = [&](const ShipLua::Manifest& source) -> std::string {
            auto loaded = ShipLua::NativeProvider::Load(source, argv[1], policy);
            if (!loaded.isOk())
                return "load: " + loaded.message;
            std::array<char, 512> response{};
            const auto called = (*loaded.value)->Call("report", "", 0, response.data(), uint32_t(response.size()));
            return called.code == ShipLua::ErrorCode::Ok ? std::string(response.data(), called.size) : "call falhou";
        }; // o provider descarrega no fim de cada chamada
        const auto same = report(*manifest.value);
        std::cout << same << '\n';
        Check(same == "public-probe sin=ok:1.000 icons=ok:1 extra=unsupported:0 qual=unsupported:0 miss=unsupported:0",
              "probe 1.4 com layout igual resolve só o catálogo");
        auto other = *manifest.value;
        other.provider->hostLayoutId = std::string(64, '0');
        Check(report(other).starts_with("public-probe sin=unsupported"),
              "layout diferente: carrega, mas o caminho público fica UNSUPPORTED");
        return failures ? 1 : 0;
    }
    if (argc == 3 && std::string(argv[2]) != "public") {
        const auto manifest = ShipLua::ParseManifestFile((std::filesystem::path(argv[1]) / "manifest.toml").string());
        if (!manifest.isOk()) {
            std::cerr << manifest.message;
            return 1;
        }
        auto loaded = ShipLua::NativeProvider::Load(*manifest.value, argv[1], policy);
        if (!loaded.isOk()) {
            std::cerr << loaded.message;
            return 1;
        }
        std::array<char, 1024> response{};
        player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
        auto called = (*loaded.value)->Call("jump", "", 0, response.data(), uint32_t(response.size()));
        const float expected = std::strtof(argv[2], nullptr);
        Check(called.code == ShipLua::ErrorCode::Ok && std::isfinite(expected) &&
                  std::abs(player.actor.velocity.y - expected) < 0.001f,
              "DLL independente deve modificar Player real");
        std::cout << std::string(response.data(), called.size) << '\n';
        response.fill(0);
        const auto resourceProbe =
            (*loaded.value)->Call("resource_probe", "", 0, response.data(), uint32_t(response.size()));
        Check(resourceProbe.code == ShipLua::ErrorCode::Ok &&
                  std::string(response.data(), resourceProbe.size) == "{\"ok\":1}",
              "DLL independente deve ler o VFS pelo serviço resources V1");
        response.fill(0);
        const auto layerProbe = (*loaded.value)->Call("layer_probe", "", 0, response.data(), uint32_t(response.size()));
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
        // DMR 0.2.14+: o configure deixa a câmera livre desligada; ela só liga quando o analógico direito
        // assume uma vista de gameplay válida.
        Check(configured.code == ShipLua::ErrorCode::Ok && settingValue == 0 &&
                  otherSettings["gEnhancements.PersistentMasks"] == 1 &&
                  otherSettings["gSettings.FreeLook.InvertYAxis"] == 0,
              "configure deve ligar PersistentMasks sem inverter o eixo vertical e esperar o analógico direito");
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
        gamepadButtons = uint32_t{ 1 } << 3;
        Check(callUpdate() == "jump" && !movement->is_player_grounded(), "X Nintendo deve iniciar pulo");
        gamepadButtons = 0;
        Check(callUpdate() == "airborne", "pulo deve permanecer aéreo após soltar X");
        player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
        Check(callUpdate() == "landed", "pulo deve reconhecer a aterrissagem");
        gamepadButtons = uint32_t{ 1 } << 1;
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
        // Câmera principal ativa (CameraCanResume). Sem o soh.symbols deste processo, o DMR liga a câmera
        // livre sem sincronizar pela vista, como antes da 0.2.14.
        static Camera mainCamera{};
        mainCamera.status = CAM_STAT_ACTIVE;
        play.cameraPtrs[CAM_ID_MAIN] = &mainCamera;
        play.activeCamera = CAM_ID_MAIN;
        play.state.input[0].rel.right_stick_x = 30;
        play.state.input[0].rel.right_stick_y = 0;
        Check(callUpdate() == "camera-free" && settingValue == 1,
              "mover o analógico direito deve ligar a câmera livre");
        play.state.input[0].rel.right_stick_x = 0;
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
            std::find(boundAxes.begin(), boundAxes.end(), AxisBinding{ BTN_R, 4, 1 }) != boundAxes.end();
        Check(clearedButtons.size() >= 9 && boundButtons.size() >= 5 &&
                  std::vector<Binding>(boundButtons.end() - 5, boundButtons.end()) ==
                      std::vector<Binding>{ { BTN_A, 1 }, { BTN_B, 2 }, { BTN_B, 0 }, { BTN_CUP, 14 }, { BTN_L, 4 } } &&
                  shieldOnZl && boundAxes.back() == AxisBinding{ BTN_CLEFT, 5, 1 },
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
        Check(LinkSpan_PauseEquipSelectorEnabled() == 1, "perfil Nintendo deve ligar o seletor do pause (RFC 0028)");
        const auto itemMenuState = [&] {
            response.fill(0);
            const auto result =
                (*loaded.value)->Call("hud_item_menu", "", 0, response.data(), uint32_t(response.size()));
            return result.code == ShipLua::ErrorCode::Ok ? std::string(response.data(), result.size)
                                                         : std::string("erro");
        };
        gSaveContext.equips.buttonItems[1] = ITEM_BOW;
        gSaveContext.equips.buttonItems[2] = ITEM_NONE;
        gSaveContext.equips.buttonItems[3] = ITEM_HOOKSHOT;
        Check(itemMenuState() == "none", "menu de itens deve ficar fechado sem segurar o R");
        gamepadButtons = uint32_t{ 1 } << 10;
        // Formato do seletor (DMR 0.2.14+): os três C ficam no lugar, com o caminho do ícone; sem a tabela
        // de ícones do host o caminho fica vazio.
        Check(callUpdate() == "item-menu" && itemMenuState() == "1;1:3:\t2:255:\t3:10:" && settingValue == 0,
              "segurar R deve abrir o menu no C equipado, com os três C no lugar, e pausar a câmera livre");
        play.state.input[0].rel.right_stick_x = 60;
        callUpdate();
        Check(itemMenuState() == "3;1:3:\t2:255:\t3:10:", "analógico direito para a direita deve pular o C vazio");
        callUpdate();
        Check(itemMenuState() == "3;1:3:\t2:255:\t3:10:", "analógico mantido inclinado não deve andar de novo");
        play.state.input[0].rel.right_stick_x = 0;
        gamepadButtons = 0;
        Check(callUpdate() == "item-c-right" && boundAxes.back() == AxisBinding{ BTN_CRIGHT, 5, 1 } &&
                  itemMenuState() == "none" && LinkSpan_ItemButtonHidden(1) == 1 && LinkSpan_ItemButtonHidden(3) == 0,
              "soltar o R deve equipar o destacado no ZR e deixar só ele no HUD");
        gamepadAxes[5] = 32000;
        gamepadButtons = uint32_t{ 1 } << 10;
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
        gamepadButtons = uint32_t{ 1 } << 8;
        callUpdate();
        gamepadButtons = 0;
        Check(callUpdate() == "lens-on" && play.actorCtx.lensActive && usedItems.back() == ITEM_LENS,
              "toque no R3 deve ligar a lente pelo host sem exigir botão C");
        gSaveContext.linkAge = LINK_AGE_CHILD;
        gSaveContext.inventory.items[SLOT_TRADE_CHILD] = ITEM_MASK_BUNNY;
        player.currentMask = PLAYER_MASK_NONE;
        gamepadButtons = uint32_t{ 1 } << 8;
        callUpdate();
        std::this_thread::sleep_for(std::chrono::milliseconds(420));
        Check(callUpdate() == "mask-on" && player.currentMask == PLAYER_MASK_BUNNY,
              "segurar o R3 deve colocar a máscara do slot infantil");
        gamepadButtons = 0;
        Check(callUpdate() != "lens-on", "soltar o R3 depois de segurar não deve alternar a lente");

        const auto pressAndRelease = [&](uint8_t sdlButton) {
            gamepadButtons = uint32_t{ 1 } << sdlButton;
            callUpdate();
            gamepadButtons = 0;
            return callUpdate();
        };
        const auto quickSwapState = [&] {
            response.fill(0);
            const auto result =
                (*loaded.value)->Call("hud_quick_swap", "", 0, response.data(), uint32_t(response.size()));
            return result.code == ShipLua::ErrorCode::Ok ? std::string(response.data(), result.size)
                                                         : std::string("erro");
        };
        gSaveContext.linkAge = LINK_AGE_ADULT;
        player.currentMask = PLAYER_MASK_NONE;
        player.actor.bgCheckFlags = BGCHECKFLAG_GROUND;
        resetPlayerAction();
        gamepadButtons = uint32_t{ 1 } << 13;
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
        gamepadButtons = uint32_t{ 1 } << 11;
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
        Check(LinkSpan_PauseEquipSelectorEnabled() == 0, "unload deve desligar o seletor do pause");
        LinkSpan_CaptureDpadBackground(&play, 271, 55, 255, 200, 100, 180);
        Check(LinkSpan_OwnedDpadBackground(&play, &backgroundX, &backgroundY, &backgroundR, &backgroundG, &backgroundB,
                                           &backgroundAlpha) == 0,
              "sem D-pad tomado o fundo fica com o Interface_Draw");

        ShipOotEngineV1 incompatible = *engine;
        incompatible.layout_id = "incompatible";
        policy.services[0].table = &incompatible;
        Check(!ShipLua::NativeProvider::Load(*manifest.value, argv[1], policy).isOk(),
              "layout divergente deve ser recusado");
    }
    gPlayState = nullptr;
    Check(!engine->get_player(), "troca de cena invalida acesso ao Player");
    return failures ? 1 : 0;
}
