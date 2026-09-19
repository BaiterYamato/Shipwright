#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <thread>

#include "oot_scenes.h"
#include "z64.h"

namespace ShipLuaHost {

// Dados vanilla que o binding do jogo entrega ao registro. Os ponteiros precisam
// viver até o fim do processo (tabelas estáticas do jogo).
struct OotVanillaScenes {
    const char* const* sceneNames = nullptr; // enum SCENE_*, índice = id
    int32_t sceneCount = 0;
    const EntranceInfo* entrances = nullptr;
    const char* const* entranceNames = nullptr; // enum ENTR_*, índice = posição na tabela
    int32_t entranceCount = 0;
    int32_t drawConfigCount = 0;
    // gSceneTable: arquivo ("spot00_scene"), draw config e se a cena tem pastas nonmq/mq.
    const char* (*sceneFileName)(int32_t sceneId) = nullptr;
    uint8_t (*sceneDrawConfig)(int32_t sceneId) = nullptr;
    bool (*sceneHasMasterQuest)(int32_t sceneId) = nullptr;
};

// Recebe a tabela de entradas vigente sempre que ela muda: nullptr quando só há
// entradas vanilla, senão a tabela combinada, válida até a próxima chamada.
using OotEntranceTableListener = void (*)(EntranceInfo* table, int32_t count);
// Viagem para uma entrada já validada; roda na thread do jogo.
using OotSceneTravelFn = ShipNativeStatus (*)(int32_t entranceIndex);

void SetOotVanillaScenes(const OotVanillaScenes& vanilla);
void SetOotEntranceTableListener(OotEntranceTableListener listener);
void SetOotSceneTravel(OotSceneTravelFn travel);

void InitializeOotNativeScenes(std::thread::id ownerThread = std::this_thread::get_id());
void ResetOotNativeScenes();
const ShipOotScenesV1& GetOotNativeScenesService();
const ShipOotScenesV2& GetOotNativeScenesServiceV2();

struct OotCustomScene {
    std::string path;
    uint8_t drawConfig = 0;
};

// Consultas do jogo, na thread do jogo.
int32_t OotEntranceCount();
bool FindOotCustomScene(int32_t sceneId, OotCustomScene& scene);
const std::string* OotCustomSceneDisplayName(int32_t sceneId);
// Textura do título de uma cena registrada com register_scene_v2; nullptr sem título.
const std::string* OotCustomSceneTitleCard(int32_t sceneId);
// Caminho que substitui uma cena vanilla (override_scene), para a variante pedida.
bool OotSceneOverridePath(int32_t sceneId, bool masterQuest, std::string& path);
// Nome estável da cena: enum SCENE_* para vanilla, nome do registro para cenas de mod.
bool OotSceneStableName(int32_t sceneId, std::string& name);
bool OotSceneIdFromStableName(const std::string& name, int32_t& sceneId);
// Flags de uma cena registrada, guardadas pelo nome da cena durante a sessão.
// Id sem registro recebe um armazenamento zerado a cada chamada.
SavedSceneFlags* OotCustomSceneFlags(int32_t sceneId);
// Flags das cenas de mod por nome, para gravar e restaurar com o save.
std::map<std::string, SavedSceneFlags, std::less<>> ExportOotCustomSceneFlags();
void ReplaceOotCustomSceneFlags(std::map<std::string, SavedSceneFlags, std::less<>> flags);

} // namespace ShipLuaHost
