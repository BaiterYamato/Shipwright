#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "json_merge.h"
#include "room_actors.h"
#include "scene_registry.h"

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    constexpr const char* schema = "linkspan.unbound.actor-patch/v1";
    const std::vector<LinkSpanUnbound::LayerDocument> layers{
        { "base.zip",
          R"({"$schema":"linkspan.unbound.actor-patch/v1","actor":{"name":"guard","stats":{"health":3,"speed":1},"drops":["rupee"]},"enabled":true})",
          1 },
        { "override.zip",
          R"({"$schema":"linkspan.unbound.actor-patch/v1","actor":{"stats":{"health":8},"drops":["heart"]},"enabled":false})",
          2 },
    };
    LinkSpanUnbound::MergeResult result;
    std::string error;
    Check(LinkSpanUnbound::MergeDocuments(schema, layers, result, error), "merge válido deve funcionar");
    Check(result.layerCount == 2 && result.hash != 0, "resultado deve registrar camadas e hash");
    Check(
        result.json ==
            R"({"$schema":"linkspan.unbound.actor-patch/v1","actor":{"name":"guard","stats":{"health":8,"speed":1},"drops":["heart"]},"enabled":false})",
        "objetos devem mesclar recursivamente e arrays devem ser substituídos");

    auto wrong = layers;
    wrong[1].json = R"({"$schema":"outro/v1","actor":{"name":"wrong"}})";
    Check(!LinkSpanUnbound::MergeDocuments(schema, wrong, result, error) && error.starts_with("schema mismatch"),
          "schema divergente deve ser recusado");
    wrong[1].json = "{";
    Check(!LinkSpanUnbound::MergeDocuments(schema, wrong, result, error) && !error.empty(),
          "JSON inválido deve ser recusado sem exceção externa");

    const std::vector<LinkSpanUnbound::LayerDocument> registryLayers{
        { "base.o2r",
          R"({"mod/b":{"scene":"scenes/b","entrances":{"main":{"spawn":1}}},"mod/a":{"scene":"scenes/a","sceneId":200,"drawConfig":3,"titleCardTexture":"textures/a_title"},"mod/gone":{"scene":"scenes/gone"}})",
          1 },
        { "patch.o2r",
          R"({"mod/gone":null,"mod/a":{"name":"Cena A","entrances":{"north":{"index":1560,"showTitleCard":true,"endTransition":5,"layers":{}}}},"mod/bad":{"scene":"scenes/bad","sceneId":10},"mod/nopath":{"name":"x"},"$order":["mod/a"]})",
          2 },
    };
    Check(LinkSpanUnbound::MergeSchemaFreeDocuments(registryLayers, result, error) && result.layerCount == 2,
          "registro sem $schema deve mesclar");
    Check(result.json.find("mod/gone") == std::string::npos && result.json.find(R"("sceneId":200)") != std::string::npos,
          "null deve remover a cena e a camada de baixo deve sobreviver ao patch");
    LinkSpanUnbound::SceneRegistryDocument registry;
    Check(LinkSpanUnbound::ParseSceneRegistry(result.json, registry, error), "registro mesclado deve ser lido");
    Check(registry.scenes.size() == 2 && registry.scenes[0].name == "mod/a" && registry.scenes[1].name == "mod/b",
          "cenas válidas devem sair em ordem de chave");
    const auto& sceneA = registry.scenes[0];
    Check(sceneA.displayName == "Cena A" && sceneA.path == "scenes/a" && sceneA.sceneId == 200 &&
              sceneA.drawConfig == 3 && sceneA.entrances.size() == 1 && sceneA.entrances[0].key == "north" &&
              sceneA.entrances[0].index == 1560 && sceneA.entrances[0].showTitleCard &&
              sceneA.entrances[0].endTransition == 5 && sceneA.entrances[0].startTransition == 2 &&
              sceneA.titleCard == "textures/a_title",
          "campos da cena e da entrada devem vir do documento com os padrões do SPEC");
    const auto& sceneB = registry.scenes[1];
    Check(sceneB.displayName == "mod/b" && sceneB.sceneId == -1 && sceneB.entrances.size() == 1 &&
              sceneB.entrances[0].spawn == 1 && sceneB.entrances[0].index == -1,
          "sem id e sem índice a cena e a entrada devem pedir o próximo livre");
    Check(registry.notes.size() == 3, "sceneId fora do intervalo, cena sem caminho e layers devem virar notas");
    Check(!LinkSpanUnbound::ParseSceneRegistry("[]", registry, error) && !error.empty(),
          "raiz que não é objeto deve ser recusada");
    Check(LinkSpanUnbound::ParseSceneRegistry(
              R"({"z":{"scene":"s"},"10":{"scene":"s"},"9":{"scene":"s"},"a":{"scene":"s"},"$order":["z"]})", registry,
              error) &&
              registry.scenes.size() == 4 && registry.scenes[0].name == "z" && registry.scenes[1].name == "9" &&
              registry.scenes[2].name == "10" && registry.scenes[3].name == "a",
          "o registro deve seguir $order, depois inteiros em ordem numérica, depois bytes (§3.5)");

    using LinkSpanUnbound::RoomActor;
    Check(LinkSpanUnbound::RoomDocumentPath("scenes/shared/spot00_scene/spot00_room_0", 0) ==
                  "scenes/spot00/rooms/0.json" &&
              LinkSpanUnbound::RoomDocumentPath("scenes/mq/ydan_scene/ydan_room_3", 3) ==
                  "scenes/ydan_mq/rooms/3.json" &&
              LinkSpanUnbound::RoomDocumentPath("scenes/nonmq/ydan_scene/ydan_room_1", 1) ==
                  "scenes/ydan/rooms/1.json" &&
              LinkSpanUnbound::RoomDocumentPath("rooms/qualquer", 0).empty() &&
              LinkSpanUnbound::RoomDocumentPath("scenes/shared/spot00_scene/x", -1).empty(),
          "caminho do documento da sala");
    const std::vector<RoomActor> vanilla{
        { 0x0015, { 10, 20, 30 }, { 0, 0x4000, 0 }, 3 },
        { 0x0095, { -5, 0, 5 }, { 0, 0, 0 }, 0x0100 },
        { 0x0125, { 1, 1, 1 }, { 0, 0, 0 }, 7 },
    };
    LinkSpanUnbound::RoomActorsResult room;
    Check(LinkSpanUnbound::ApplyRoomActorLayers(vanilla, 0, {}, room, error) && room.actors.size() == 3 &&
              room.actors[1].id == 0x0095 && room.actors[1].pos[0] == -5 && room.actors[1].params == 0x0100 &&
              room.actors[0].rot[1] == 0x4000,
          "sem camadas a lista vanilla volta igual");
    const std::vector<LinkSpanUnbound::LayerDocument> roomLayers{
        { "mod-a.zip",
          "{ // comentário aceito\n \"setups\": { \"0\": { \"actors\": {"
          " \"1\": { \"pos\": [100.9, \"0x10\", -3], \"params\": \"0x0200\" },"
          " \"2\": null,"
          " \"novo\": { \"id\": 21, \"pos\": [7, 8, 9], \"rot\": [0, true, 0], \"params\": 3 },"
          " \"10\": { \"id\": 22, \"pos\": [1, 2] } } } } }",
          1 },
        { "mod-b.zip", "{\"setups\":{\"0\":{\"actors\":{\"$order\":[\"novo\",\"zz\",\"novo\"]}}}}", 2 },
        { "quebrado.zip", "  {\"setups\":{}}", 3 },
    };
    Check(LinkSpanUnbound::ApplyRoomActorLayers(vanilla, 0, roomLayers, room, error), "camadas de sala mescladas");
    Check(room.layersUsed == 2 && room.notes.size() == 1 && room.notes[0].starts_with("quebrado.zip"),
          "camada inválida é pulada com nota e as outras seguem");
    Check(room.actors.size() == 4, "null remove, chave nova acrescenta");
    Check(room.actors[0].id == 21 && room.actors[0].pos[2] == 9 && room.actors[0].rot[1] == 1,
          "$order põe a chave listada primeiro; booleano vira inteiro");
    Check(room.actors[1].id == 0x0015 && room.actors[1].params == 3, "depois vem a ordem numérica das chaves");
    Check(room.actors[2].id == 0x0095 && room.actors[2].pos[0] == 100.9f && room.actors[2].pos[1] == 16 &&
              room.actors[2].pos[2] == -3 && room.actors[2].params == 0x0200 && room.actors[2].rot[1] == 0,
          "patch parcial mantém campos vanilla; posição fracionária, string hex aceita");
    Check(room.actors[3].id == 22 && room.actors[3].pos[0] == 0 && room.actors[3].pos[1] == 0,
          "vetor com menos de 3 elementos vira [0,0,0]");
    const std::vector<LinkSpanUnbound::LayerDocument> replaceLayers{
        { "troca.zip",
          R"({"setups":{"0":{"actors":{"$replace":true,"a":{"id":70000,"pos":[40000,0,0],"params":-1}}}}})", 1 },
    };
    Check(LinkSpanUnbound::ApplyRoomActorLayers(vanilla, 0, replaceLayers, room, error) && room.actors.size() == 1 &&
              room.actors[0].id == static_cast<int16_t>(70000 & 0xFFFF) &&
              room.actors[0].pos[0] == 40000.0f && room.actors[0].params == -1,
          "$replace descarta a lista de baixo; inteiros fora da largura fazem wrap, posição é f32");
    Check(LinkSpanUnbound::ApplyRoomActorLayers(vanilla, 2, replaceLayers, room, error) && room.actors.size() == 3,
          "patch de outra camada de cena não mexe na lista atual");
    const std::vector<LinkSpanUnbound::LayerDocument> wrongSchema{
        { "v2.zip", R"({"$schema":"unbound/room/2","setups":{}})", 1 },
    };
    Check(!LinkSpanUnbound::ApplyRoomActorLayers(vanilla, 0, wrongSchema, room, error) && !error.empty(),
          "$schema de outra versão é rejeitado");

    std::cout << "unbound json merge: ok\n";
    return 0;
}
