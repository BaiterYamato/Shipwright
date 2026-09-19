// Liga o registro de cenas dos mods (OotNativeScenes.cpp) ao jogo: sementes vanilla,
// ponteiro gEntranceTable, flags salvas por id de cena e viagem para uma entrada.
#include "OotNativeScenes.h"

#include <iterator>
#include <map>
#include <set>
#include <fast/resource/type/Texture.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <spdlog/spdlog.h>

#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
extern EntranceInfo gEntranceTableVanilla[];
}

namespace {

#define DEFINE_SCENE(name, title, enumValue, config, unk_10, unk_12) #enumValue,
const char* const kSceneNames[] = {
#include "tables/scene_table.h"
};
#undef DEFINE_SCENE

#define DEFINE_ENTRANCE(enumValue, sceneId, spawn, continueBgm, displayTitleCard, endTransType, startTransType) \
    #enumValue,
const char* const kEntranceNames[] = {
#include "tables/entrance_table.h"
};
#undef DEFINE_ENTRANCE

static_assert(std::size(kSceneNames) == SCENE_ID_MAX);
static_assert(std::size(kEntranceNames) == ENTR_MAX);
constexpr int32_t kHorseScenes[] = { SCENE_HYRULE_FIELD, SCENE_LAKE_HYLIA, SCENE_GERUDO_VALLEY,
                                     SCENE_GERUDOS_FORTRESS, SCENE_LON_LON_RANCH };

void ApplyEntranceTable(EntranceInfo* table, int32_t) {
    gEntranceTable = table ? table : gEntranceTableVanilla;
}

// Mesmo efeito do GameInteractor::RawAction::TeleportPlayer, sem o som, e só com o
// jogador em cena e nenhuma transição em andamento.
ShipNativeStatus TravelToEntrance(int32_t entranceIndex) {
    PlayState* play = gPlayState;
    if (!play || gSaveContext.gameMode != GAMEMODE_NORMAL || gSaveContext.fileNum == 0xFF || !GET_PLAYER(play) ||
        play->transitionTrigger != TRANS_TRIGGER_OFF || play->transitionMode != TRANS_MODE_OFF) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    play->nextEntranceIndex = static_cast<s16>(entranceIndex);
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    return SHIP_NATIVE_OK;
}

const char* SceneFileName(int32_t sceneId) {
    return gSceneTable[sceneId].sceneFile.fileName;
}

uint8_t SceneDrawConfig(int32_t sceneId) {
    return gSceneTable[sceneId].config;
}

// Mesma regra de OTRPlay_SpawnScene (z_play_otr.cpp).
bool SceneHasMasterQuest(int32_t sceneId) {
    return (sceneId >= SCENE_DEKU_TREE && sceneId <= SCENE_ICE_CAVERN) || sceneId == SCENE_GERUDO_TRAINING_GROUND ||
           sceneId == SCENE_INSIDE_GANONS_CASTLE;
}

[[maybe_unused]] const bool kScenesBound = [] {
    ShipLuaHost::OotVanillaScenes vanilla;
    vanilla.sceneNames = kSceneNames;
    vanilla.sceneCount = SCENE_ID_MAX;
    vanilla.entrances = gEntranceTableVanilla;
    vanilla.entranceNames = kEntranceNames;
    vanilla.entranceCount = ENTR_MAX;
    vanilla.drawConfigCount = SDC_MAX;
    vanilla.sceneFileName = SceneFileName;
    vanilla.sceneDrawConfig = SceneDrawConfig;
    vanilla.sceneHasMasterQuest = SceneHasMasterQuest;
    vanilla.horseScenes = kHorseScenes;
    vanilla.horseSceneCount = std::size(kHorseScenes);
    ShipLuaHost::SetOotVanillaScenes(vanilla);
    ShipLuaHost::SetOotEntranceTableListener(ApplyEntranceTable);
    ShipLuaHost::SetOotSceneTravel(TravelToEntrance);
    return true;
}();

} // namespace

// Chamadas pelo código do jogo, sempre na thread do jogo.
extern "C" SavedSceneFlags* LinkSpan_SceneFlags(s32 sceneNum) {
    if (sceneNum >= 0 && sceneNum < static_cast<s32>(std::size(gSaveContext.sceneFlags))) {
        return &gSaveContext.sceneFlags[sceneNum];
    }
    return ShipLuaHost::OotCustomSceneFlags(sceneNum);
}

// TitleCard_InitPlaceName (z_actor.c): textura do título da cena de mod, ou NULL. O TitleCard_Draw carrega um
// bloco I8 de 144x24 com esses números fixos; textura menor seria lida além do fim, então ela só vale se o
// recurso tiver pelo menos esse tamanho (conferido uma vez por caminho).
extern "C" const char* LinkSpan_CustomSceneTitleCard(s32 sceneNum) {
    static std::map<std::string, bool, std::less<>> checked;
    static std::set<std::string, std::less<>> paths;
    const std::string* texture = ShipLuaHost::OotCustomSceneTitleCard(sceneNum);
    if (!texture) {
        return nullptr;
    }
    try {
        auto found = checked.find(*texture);
        if (found == checked.end()) {
            const auto resource = std::dynamic_pointer_cast<Fast::Texture>(
                Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(*texture));
            const bool fits = resource && resource->Width == 144 && resource->Height == 24 &&
                              resource->ImageDataSize >= 144u * 24u;
            if (!fits) {
                SPDLOG_WARN("Link-Span: título de cena {} não é uma textura de 144x24 com 3456 bytes; ignorado",
                            *texture);
            }
            found = checked.emplace(*texture, fits).first;
        }
        return found->second ? paths.emplace("__OTR__" + *texture).first->c_str() : nullptr;
    } catch (...) {
        return nullptr;
    }
}

extern "C" s32 LinkSpan_EntranceCount(void) {
    return ShipLuaHost::OotEntranceCount();
}

extern "C" void LinkSpan_ReportInvalidEntrance(s32 entranceIndex, s32 sceneLayer) {
    SPDLOG_ERROR("Link-Span: entrada {:#x} (camada {}) fora da tabela de {} entradas", entranceIndex, sceneLayer,
                 ShipLuaHost::OotEntranceCount());
}

extern "C" s32 LinkSpan_HorseAllowed(s32 sceneNum) {
    return ShipLuaHost::OotSceneHorseAllowed(sceneNum);
}

extern "C" s32 LinkSpan_HorseSpawn(s32 sceneNum, Vec3f* pos, s16* angle) {
    return pos && angle && ShipLuaHost::OotSceneHorseSpawn(sceneNum, *pos, *angle);
}

extern "C" s32 LinkSpan_UsesGeneratedHorseCall(s32 sceneNum) {
    return ShipLuaHost::OotSceneUsesGeneratedHorseCall(sceneNum);
}

extern "C" s32 LinkSpan_ResolveHorseScene(s16* sceneNum) {
    int32_t resolved = 0;
    if (!sceneNum || !ShipLuaHost::ResolveOotHorseSceneSnapshotName(resolved)) {
        return false;
    }
    *sceneNum = static_cast<s16>(resolved);
    return true;
}

extern "C" BetterSceneSelectEntry* LinkSpan_BuildBetterWarpScenes(BetterSceneSelectEntry* vanilla, s32 vanillaCount,
                                                                     void (*loadFunc)(SelectContext*, s32), s32* count) {
    if (!count) {
        return vanilla;
    }
    int32_t builtCount = vanillaCount;
    BetterSceneSelectEntry* entries =
        ShipLuaHost::OotBuildBetterWarpScenes(vanilla, vanillaCount, loadFunc, builtCount);
    *count = builtCount;
    return entries;
}
