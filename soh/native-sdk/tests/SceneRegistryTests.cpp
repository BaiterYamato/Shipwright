#include "OotNativeScenes.h"

#include <iostream>
#include <string>
#include <thread>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr int32_t kSceneCount = 3;
const int32_t kHorseScenes[] = { 0, 2 };
const char* const kSceneNames[kSceneCount] = { "SCENE_A", "SCENE_B", "SCENE_C" };
constexpr int32_t kEntranceCount = 6;
const char* const kEntranceNames[kEntranceCount] = { "ENTR_A_0", "ENTR_A_1", "ENTR_A_2",
                                                     "ENTR_A_3", "ENTR_B_0", "ENTR_C_0" };
const EntranceInfo kEntrances[kEntranceCount] = { { 0, 0, 1 }, { 0, 1, 2 }, { 0, 2, 3 },
                                                  { 0, 3, 4 }, { 1, 0, 5 }, { 2, 0, 6 } };

EntranceInfo* publishedTable = nullptr;
int32_t publishedCount = -1;
int32_t travelledTo = -1;

void ListenTable(EntranceInfo* table, int32_t count) {
    publishedTable = table;
    publishedCount = count;
}

ShipNativeStatus Travel(int32_t index) {
    travelledTo = index;
    return SHIP_NATIVE_OK;
}

ShipOotSceneDefinitionV1 Scene(const char* name, const char* path, int32_t id = LINKSPAN_OOT_SCENES_AUTO,
                               uint8_t drawConfig = 0, const char* displayName = nullptr) {
    ShipOotSceneDefinitionV1 scene{};
    scene.size = sizeof(scene);
    scene.name = name;
    scene.display_name = displayName;
    scene.scene_path = path;
    scene.requested_id = id;
    scene.draw_config = drawConfig;
    return scene;
}

ShipOotEntranceDefinitionV1 Entrance(const char* key, int32_t index = LINKSPAN_OOT_SCENES_AUTO) {
    ShipOotEntranceDefinitionV1 entrance{};
    entrance.size = sizeof(entrance);
    entrance.key = key;
    entrance.requested_index = index;
    entrance.end_transition = 2;
    entrance.start_transition = 2;
    return entrance;
}

const char* FileName(int32_t sceneId) {
    static const char* const files[kSceneCount] = { "a_scene", "b_scene", "c_scene" };
    return files[sceneId];
}

uint8_t DrawConfig(int32_t sceneId) {
    return static_cast<uint8_t>(sceneId + 1);
}

bool HasMasterQuest(int32_t sceneId) {
    return sceneId == 1;
}

bool SameEntrance(const EntranceInfo& left, const EntranceInfo& right) {
    return left.scene == right.scene && left.spawn == right.spawn && left.field == right.field;
}

} // namespace

int main() {
    ShipLuaHost::OotVanillaScenes vanilla;
    vanilla.sceneNames = kSceneNames;
    vanilla.sceneCount = kSceneCount;
    vanilla.entrances = kEntrances;
    vanilla.entranceNames = kEntranceNames;
    vanilla.entranceCount = kEntranceCount;
    vanilla.drawConfigCount = 4;
    vanilla.sceneFileName = FileName;
    vanilla.sceneDrawConfig = DrawConfig;
    vanilla.sceneHasMasterQuest = HasMasterQuest;
    vanilla.horseScenes = kHorseScenes;
    vanilla.horseSceneCount = static_cast<int32_t>(sizeof(kHorseScenes) / sizeof(kHorseScenes[0]));
    ShipLuaHost::SetOotVanillaScenes(vanilla);
    ShipLuaHost::SetOotEntranceTableListener(ListenTable);
    ShipLuaHost::SetOotSceneTravel(Travel);
    ShipLuaHost::InitializeOotNativeScenes();
    const auto& scenes = ShipLuaHost::GetOotNativeScenesService();
    Check(scenes.size == sizeof(ShipOotScenesV1), "tabela V1 deve declarar tamanho completo");
    Check(!publishedTable && publishedCount == kEntranceCount && ShipLuaHost::OotEntranceCount() == kEntranceCount,
          "sem mods o jogo deve usar a tabela vanilla");

    uint64_t field = 0;
    int32_t fieldId = 0;
    auto scene = Scene("demo/field", "scenes/shared/spot00_scene/spot00_scene");
    Check(scenes.register_scene(&scene, &field, &fieldId) == SHIP_NATIVE_OK && field && fieldId == 128,
          "primeira cena automática deve receber o id 128");
    uint64_t rejected = 0;
    int32_t rejectedId = 0;
    Check(scenes.register_scene(&scene, &rejected, &rejectedId) == SHIP_NATIVE_INVALID_ARGUMENT && !rejected,
          "nome de cena repetido deve ser recusado");
    scene = Scene("SCENE_B", "scenes/x");
    Check(scenes.register_scene(&scene, &rejected, &rejectedId) == SHIP_NATIVE_INVALID_ARGUMENT,
          "nome de enum vanilla deve ser recusado");
    scene = Scene("demo/low", "scenes/x", 127);
    Check(scenes.register_scene(&scene, &rejected, &rejectedId) == SHIP_NATIVE_INVALID_ARGUMENT,
          "id abaixo de 128 deve ser recusado");
    scene = Scene("demo/high", "scenes/x", 32768);
    Check(scenes.register_scene(&scene, &rejected, &rejectedId) == SHIP_NATIVE_INVALID_ARGUMENT,
          "id acima de 32767 deve ser recusado");
    scene = Scene("demo/draw", "scenes/x", LINKSPAN_OOT_SCENES_AUTO, 4);
    Check(scenes.register_scene(&scene, &rejected, &rejectedId) == SHIP_NATIVE_INVALID_ARGUMENT,
          "draw config fora da tabela deve ser recusado");
    scene = Scene("demo/nopath", "");
    Check(scenes.register_scene(&scene, &rejected, &rejectedId) == SHIP_NATIVE_INVALID_ARGUMENT,
          "cena sem caminho deve ser recusada");
    scene = Scene("demo/small", "scenes/x");
    scene.size = sizeof(scene) - 1;
    Check(scenes.register_scene(&scene, &rejected, &rejectedId) == SHIP_NATIVE_INVALID_ARGUMENT,
          "definição menor que a V1 deve ser recusada");

    uint64_t cave = 0;
    int32_t caveId = 0;
    scene = Scene("demo/cave", "scenes/demo/cave", 200, 3, "Caverna");
    Check(scenes.register_scene(&scene, &cave, &caveId) == SHIP_NATIVE_OK && caveId == 200,
          "id explícito livre deve ser aceito");
    scene = Scene("demo/cave2", "scenes/x", 200);
    Check(scenes.register_scene(&scene, &rejected, &rejectedId) == SHIP_NATIVE_INVALID_ARGUMENT,
          "id explícito ocupado deve ser recusado");
    uint64_t next = 0;
    int32_t nextId = 0;
    scene = Scene("demo/next", "scenes/demo/next");
    Check(scenes.register_scene(&scene, &next, &nextId) == SHIP_NATIVE_OK && nextId == 201,
          "id automático deve seguir o maior já registrado");

    auto entrance = Entrance("main");
    entrance.spawn = 2;
    entrance.show_title_card = 1;
    entrance.end_transition = 5;
    entrance.start_transition = 7;
    int32_t mainIndex = 0;
    Check(scenes.register_entrance(field, &entrance, &mainIndex) == SHIP_NATIVE_OK && mainIndex == 8,
          "primeira entrada automática deve ocupar o primeiro grupo depois das vanilla");
    Check(publishedTable && publishedCount == 12 && ShipLuaHost::OotEntranceCount() == 12,
          "entrada de mod deve publicar a tabela combinada");
    bool vanillaKept = publishedTable != nullptr;
    for (int32_t index = 0; vanillaKept && index < kEntranceCount; ++index) {
        vanillaKept = SameEntrance(publishedTable[index], kEntrances[index]);
    }
    Check(vanillaKept, "tabela combinada deve preservar as entradas vanilla");
    Check(publishedTable && publishedTable[6].scene == kSceneCount && publishedTable[7].scene == kSceneCount,
          "posições sem uso devem apontar para a cena vanilla sem uso");
    const uint16_t expectedField = ENTRANCE_INFO_DISPLAY_TITLE_CARD_FLAG | (5 << ENTRANCE_INFO_END_TRANS_TYPE_SHIFT) | 7;
    bool groupFilled = publishedTable != nullptr;
    for (int32_t index = 8; groupFilled && index < 12; ++index) {
        groupFilled = publishedTable[index].scene == 128 && publishedTable[index].spawn == 2 &&
                      publishedTable[index].field == expectedField;
    }
    Check(groupFilled, "as quatro camadas da entrada devem apontar para a cena, o spawn e as transições");

    int32_t rejectedIndex = 0;
    Check(scenes.register_entrance(field, &entrance, &rejectedIndex) == SHIP_NATIVE_INVALID_ARGUMENT,
          "entrada repetida na mesma cena deve ser recusada");
    entrance = Entrance("odd", 10);
    Check(scenes.register_entrance(field, &entrance, &rejectedIndex) == SHIP_NATIVE_INVALID_ARGUMENT,
          "índice que não é múltiplo de 4 deve ser recusado");
    entrance = Entrance("low", 4);
    Check(scenes.register_entrance(field, &entrance, &rejectedIndex) == SHIP_NATIVE_INVALID_ARGUMENT,
          "índice dentro das vanilla deve ser recusado");
    entrance = Entrance("taken", 8);
    Check(scenes.register_entrance(cave, &entrance, &rejectedIndex) == SHIP_NATIVE_INVALID_ARGUMENT,
          "grupo ocupado deve ser recusado");
    entrance = Entrance("spawn");
    entrance.spawn = 128;
    Check(scenes.register_entrance(field, &entrance, &rejectedIndex) == SHIP_NATIVE_INVALID_ARGUMENT,
          "spawn acima de 127 deve ser recusado");
    entrance = Entrance("transition");
    entrance.end_transition = 128;
    Check(scenes.register_entrance(field, &entrance, &rejectedIndex) == SHIP_NATIVE_INVALID_ARGUMENT,
          "transição acima de 127 deve ser recusada");
    entrance = Entrance("orphan");
    Check(scenes.register_entrance(999, &entrance, &rejectedIndex) == SHIP_NATIVE_INVALID_ARGUMENT,
          "entrada de cena desconhecida deve ser recusada");

    int32_t deepIndex = 0;
    entrance = Entrance("deep", 16);
    Check(scenes.register_entrance(cave, &entrance, &deepIndex) == SHIP_NATIVE_OK && deepIndex == 16 &&
              publishedCount == 20,
          "índice explícito livre deve crescer a tabela");
    int32_t nextIndex = 0;
    entrance = Entrance("main");
    Check(scenes.register_entrance(next, &entrance, &nextIndex) == SHIP_NATIVE_OK && nextIndex == 20,
          "índice automático deve seguir o maior grupo registrado");

    int32_t found = 0;
    Check(scenes.find_scene("SCENE_C", &found) == SHIP_NATIVE_OK && found == 2, "cena vanilla deve ser achada");
    Check(scenes.find_scene("demo/cave", &found) == SHIP_NATIVE_OK && found == 200, "cena de mod deve ser achada");
    Check(scenes.find_scene("demo/none", &found) == SHIP_NATIVE_UNSUPPORTED, "cena desconhecida não existe");
    Check(scenes.find_scene("", &found) == SHIP_NATIVE_INVALID_ARGUMENT, "nome vazio deve ser recusado");
    Check(scenes.find_entrance("ENTR_B_0", &found) == SHIP_NATIVE_OK && found == 4, "entrada vanilla deve ser achada");
    Check(scenes.find_entrance("demo/field/main", &found) == SHIP_NATIVE_OK && found == 8,
          "entrada de mod deve ser achada pelo nome completo");
    Check(scenes.find_entrance("demo/field/none", &found) == SHIP_NATIVE_UNSUPPORTED,
          "entrada desconhecida não existe");

    ShipLuaHost::OotCustomScene custom;
    Check(ShipLuaHost::FindOotCustomScene(200, custom) && custom.path == "scenes/demo/cave" && custom.drawConfig == 3,
          "jogo deve resolver caminho e draw config da cena de mod");
    Check(!ShipLuaHost::FindOotCustomScene(2, custom), "id vanilla não é cena de mod");
    const std::string* caveName = ShipLuaHost::OotCustomSceneDisplayName(200);
    const std::string* fieldName = ShipLuaHost::OotCustomSceneDisplayName(128);
    Check(caveName && *caveName == "Caverna" && fieldName && *fieldName == "demo/field",
          "nome de exibição deve vir da definição ou do nome da cena");
    SavedSceneFlags* flags = ShipLuaHost::OotCustomSceneFlags(128);
    flags->chest = 0x10;
    Check(ShipLuaHost::OotCustomSceneFlags(128) == flags && flags->chest == 0x10,
          "flags de cena de mod devem ficar no mesmo armazenamento");
    SavedSceneFlags* scratch = ShipLuaHost::OotCustomSceneFlags(300);
    Check(scratch && scratch != flags && scratch->chest == 0, "id sem registro deve receber flags zeradas");

    Check(scenes.travel_to_entrance(8) == SHIP_NATIVE_OK && travelledTo == 8, "viagem para entrada de mod");
    Check(scenes.travel_to_entrance(9) == SHIP_NATIVE_INVALID_ARGUMENT, "viagem só para o início do grupo");
    Check(scenes.travel_to_entrance(12) == SHIP_NATIVE_INVALID_ARGUMENT, "viagem para grupo livre é recusada");
    Check(scenes.travel_to_entrance(2) == SHIP_NATIVE_OK && travelledTo == 2, "viagem para entrada vanilla");

    Check(scenes.unregister_scene(field) == SHIP_NATIVE_OK, "cena de mod deve ser removida");
    Check(publishedTable && publishedTable[8].scene == kSceneCount && publishedCount == 24,
          "remoção deve liberar o grupo sem encolher a tabela");
    Check(scenes.find_entrance("demo/field/main", &found) == SHIP_NATIVE_UNSUPPORTED &&
              scenes.travel_to_entrance(8) == SHIP_NATIVE_INVALID_ARGUMENT && !ShipLuaHost::FindOotCustomScene(128, custom),
          "cena removida deve sumir com as entradas");
    Check(scenes.unregister_scene(field) == SHIP_NATIVE_INVALID_ARGUMENT, "remoção repetida deve ser recusada");
    scene = Scene("demo/field", "scenes/shared/spot00_scene/spot00_scene");
    uint64_t fieldAgain = 0;
    int32_t fieldAgainId = 0;
    Check(scenes.register_scene(&scene, &fieldAgain, &fieldAgainId) == SHIP_NATIVE_OK && fieldAgainId == 202 &&
              ShipLuaHost::OotCustomSceneFlags(202)->chest == 0x10,
          "novo registro da mesma cena deve manter as flags da sessão");

    ShipNativeStatus workerStatus = SHIP_NATIVE_OK;
    std::thread worker([&scenes, &workerStatus] {
        auto definition = Scene("demo/worker", "scenes/x");
        uint64_t handle = 0;
        int32_t id = 0;
        workerStatus = scenes.register_scene(&definition, &handle, &id);
    });
    worker.join();
    Check(workerStatus == SHIP_NATIVE_INVALID_ARGUMENT, "registro fora da thread do jogo deve ser recusado");

    ShipLuaHost::ResetOotNativeScenes();
    Check(!publishedTable && publishedCount == kEntranceCount && ShipLuaHost::OotEntranceCount() == kEntranceCount,
          "reset deve devolver o jogo à tabela vanilla");
    Check(scenes.find_scene("SCENE_A", &found) == SHIP_NATIVE_INVALID_ARGUMENT,
          "depois do shutdown o serviço não atende ninguém");
    ShipLuaHost::InitializeOotNativeScenes();
    Check(scenes.find_scene("demo/cave", &found) == SHIP_NATIVE_UNSUPPORTED, "novo init começa sem cenas de mod");
    scene = Scene("demo/field", "scenes/shared/spot00_scene/spot00_scene");
    Check(scenes.register_scene(&scene, &fieldAgain, &fieldAgainId) == SHIP_NATIVE_OK && fieldAgainId == 128,
          "novo init deve recomeçar os ids em 128");

    // V2 (OOT-CORE-008): título, info e override de cena vanilla.
    const auto& v2 = ShipLuaHost::GetOotNativeScenesServiceV2();
    Check(v2.size == sizeof(ShipOotScenesV2) && v2.get_scene_count() == kSceneCount, "V2 e contagem vanilla");
    ShipOotSceneDefinitionV2 titled{ sizeof(titled), "demo/titled", nullptr, "scenes/demo/titled", 300, 1,
                                     "textures/demo/title" };
    uint64_t titledHandle = 0;
    int32_t titledId = 0;
    Check(v2.register_scene_v2(&titled, &titledHandle, &titledId) == SHIP_NATIVE_OK && titledId == 300 &&
              ShipLuaHost::OotCustomSceneTitleCard(300) &&
              *ShipLuaHost::OotCustomSceneTitleCard(300) == "textures/demo/title" &&
              !ShipLuaHost::OotCustomSceneTitleCard(128),
          "register_scene_v2 guarda o título");
    ShipOotSceneInfoV1 info{ sizeof(info) };
    Check(v2.get_scene_info(0, 0, &info) == SHIP_NATIVE_OK && !info.is_custom && !info.has_master_quest &&
              info.draw_config == 1 && std::string(info.name) == "SCENE_A" &&
              std::string(info.file_name) == "a_scene" &&
              std::string(info.scene_path) == "scenes/shared/a_scene/a_scene",
          "info de cena vanilla compartilhada");
    Check(v2.get_scene_info(1, 1, &info) == SHIP_NATIVE_OK && info.has_master_quest &&
              std::string(info.scene_path) == "scenes/mq/b_scene/b_scene",
          "info da variante MQ");
    Check(v2.get_scene_info(300, 0, &info) == SHIP_NATIVE_OK && info.is_custom &&
              std::string(info.name) == "demo/titled" && std::string(info.scene_path) == "scenes/demo/titled",
          "info de cena de mod");
    Check(v2.get_scene_info(301, 0, &info) == SHIP_NATIVE_UNSUPPORTED, "id sem cena");
    Check(v2.override_scene(128, 0, "x") == SHIP_NATIVE_INVALID_ARGUMENT &&
              v2.override_scene(0, 1, "x") == SHIP_NATIVE_INVALID_ARGUMENT,
          "override só de cena vanilla e MQ só onde existe");
    std::string overridePath;
    Check(v2.override_scene(1, 0, "scenes/b/scene.json") == SHIP_NATIVE_OK &&
              ShipLuaHost::OotSceneOverridePath(1, false, overridePath) && overridePath == "scenes/b/scene.json" &&
              !ShipLuaHost::OotSceneOverridePath(1, true, overridePath),
          "override por variante");
    Check(v2.get_scene_info(1, 0, &info) == SHIP_NATIVE_OK && info.overridden &&
              std::string(info.scene_path) == "scenes/b/scene.json",
          "info mostra o override");
    Check(v2.override_scene(1, 0, nullptr) == SHIP_NATIVE_OK && !ShipLuaHost::OotSceneOverridePath(1, false, overridePath),
          "NULL volta ao vanilla");
    const auto& v3 = ShipLuaHost::GetOotNativeScenesServiceV3();
    ShipOotSceneDefinitionV3 horse{ sizeof(horse), "demo/horse", nullptr, "scenes/demo/horse", 301, 1, nullptr,
                                    1, 1, 1.5f, 2.0f, 3.0f, -16384 };
    uint64_t horseHandle = 0;
    int32_t horseId = 0;
    Check(v3.size == sizeof(ShipOotScenesV3) && v3.register_scene_v3(&horse, &horseHandle, &horseId) == SHIP_NATIVE_OK &&
              horseId == 301 && ShipLuaHost::OotSceneHorseAllowed(0) && ShipLuaHost::OotSceneHorseAllowed(horseId),
          "V3 deve preservar a semente vanilla e registrar a capacidade de cavalo");
    Vec3f horsePos{};
    int16_t horseAngle = 0;
    Check(ShipLuaHost::OotSceneHorseSpawn(horseId, horsePos, horseAngle) && horsePos.x == 1.5f && horseAngle == -16384 &&
              ShipLuaHost::OotSceneUsesGeneratedHorseCall(horseId),
          "V3 deve publicar a posição de espera e a chamada gerada");
    v2.override_scene(0, 0, "scenes/a/scene.json");
    ShipLuaHost::ResetOotNativeScenes();
    ShipLuaHost::InitializeOotNativeScenes();
    Check(!ShipLuaHost::OotSceneOverridePath(0, false, overridePath), "shutdown limpa os overrides");

    if (failures) {
        return 1;
    }
    std::cout << "oot native scenes: ok\n";
    return 0;
}
