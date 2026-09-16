#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "json_merge.h"
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
          R"({"mod/b":{"scene":"scenes/b","entrances":{"main":{"spawn":1}}},"mod/a":{"scene":"scenes/a","sceneId":200,"drawConfig":3},"mod/gone":{"scene":"scenes/gone"}})",
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
              sceneA.entrances[0].endTransition == 5 && sceneA.entrances[0].startTransition == 2,
          "campos da cena e da entrada devem vir do documento com os padrões do SPEC");
    const auto& sceneB = registry.scenes[1];
    Check(sceneB.displayName == "mod/b" && sceneB.sceneId == -1 && sceneB.entrances.size() == 1 &&
              sceneB.entrances[0].spawn == 1 && sceneB.entrances[0].index == -1,
          "sem id e sem índice a cena e a entrada devem pedir o próximo livre");
    Check(registry.notes.size() == 3, "sceneId fora do intervalo, cena sem caminho e layers devem virar notas");
    Check(!LinkSpanUnbound::ParseSceneRegistry("[]", registry, error) && !error.empty(),
          "raiz que não é objeto deve ser recusada");

    std::cout << "unbound json merge: ok\n";
    return 0;
}
