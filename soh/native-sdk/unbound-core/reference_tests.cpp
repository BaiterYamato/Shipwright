// Testes da coleta de referências de recurso dos documentos de mod (UNBOUND-019, plano §10.5/§14.3).
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "scene_references.h"
#include "unbound_format.h"

namespace {

using namespace LinkSpanUnbound;
using Refs = std::vector<std::pair<std::string, std::string>>;

int gFailures = 0;

#define CHECK(expr)                                                                  \
    do {                                                                             \
        if (!(expr)) {                                                               \
            std::fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #expr);  \
            ++gFailures;                                                             \
        }                                                                            \
    } while (0)

ReferenceReport Collect(const std::vector<std::string>& jsons, const std::string& path = "scenes/x/scene.json") {
    std::vector<LayerDocument> layers;
    char name = 'A';
    for (const auto& json : jsons) {
        layers.push_back({ std::string("D:/Jogo/mods/") + name++ + ".o2r", json, 0 });
    }
    TranscodeContext context;
    context.path = path;
    context.resolveEntrance = [](const std::string&) { return -1; };
    context.resolveActor = [](const std::string&) { return -1; };
    // Restos de outro documento no contexto recebido não podem vazar para o relatório.
    context.references.push_back({ "lixo", "lixo" });
    context.notes.push_back("lixo");
    return CollectReferences(layers, context);
}

bool Has(const ReferenceReport& report, const std::string& field, const std::string& path) {
    for (const auto& reference : report.references) {
        if (reference.first == field && reference.second == path) {
            return true;
        }
    }
    return false;
}

void TestScene() {
    // Salas e colisão saem uma vez por setup no XML; no relatório, uma vez só. Entrada de materialAnims descartada
    // (tipo inválido) não conta, mesmo com textures.
    const auto r = Collect({ R"({"$schema":"unbound/scene/1",
      "collision": "scenes/x/collision.json",
      "rooms": {"0": "scenes/x/rooms/0.json", "1": "__OTR__scenes/x/rooms/1.json"},
      "setups": {
        "0": {"sound": {"seq": 2, "natureAmbience": 19, "reverb": 0, "song": "custom/music/Tema"},
              "materialAnims": {"0": {"segment": 8, "type": "texCycle", "textures": ["t/a", "t/b", "t/a"], "frames": [0, 1]},
                                "1": {"segment": 9, "type": "bogus", "textures": ["t/descartada"]}},
              "paths": ["scenes/x/paths/a.json"],
              "cutscene": "scenes/x/cs/abertura"},
        "1": {"skybox": {"id": 29}}
      }})" });
    CHECK(r.typed && r.accepted && r.error.empty());
    CHECK(r.schema == "unbound/scene/1");
    const Refs expected = {
        { "materialAnims.textures", "t/a" },   { "materialAnims.textures", "t/b" },
        { "sound.song", "custom/music/Tema" }, { "paths", "scenes/x/paths/a.json" },
        { "collision", "scenes/x/collision.json" }, { "rooms", "scenes/x/rooms/0.json" },
        { "rooms", "__OTR__scenes/x/rooms/1.json" }, { "cutscene", "scenes/x/cs/abertura" },
    };
    for (const auto& [field, path] : expected) {
        CHECK(Has(r, field, path));
    }
    CHECK(r.references.size() == expected.size());
    CHECK(!Has(r, "materialAnims.textures", "t/descartada") && !Has(r, "lixo", "lixo"));
    // A entrada descartada vira nota, como no jogo; a nota velha do contexto some.
    bool discarded = false;
    for (const auto& note : r.notes) {
        CHECK(note != "lixo");
        discarded = discarded || note.find("materialAnims/1") != std::string::npos;
    }
    CHECK(discarded);
}

void TestRoom() {
    // Mesh tipo 2: opa referenciado, xlu null fica de fora.
    auto r = Collect({ R"({"$schema":"unbound/room/1","setups":{"0":{
        "mesh": {"type": 2, "entries": {"0": {"pos": [0, 0, 0], "radius": 50, "opa": "dl/a", "xlu": null},
                                         "1": {"pos": [0, 0, 0], "radius": 50, "opa": "dl/b", "xlu": "dl/b_xlu"}}}}}})" },
                     "scenes/x/rooms/0.json");
    CHECK(r.typed && r.accepted);
    CHECK((r.references == Refs{ { "mesh.entries.opa", "dl/a" }, { "mesh.entries.opa", "dl/b" },
                                 { "mesh.entries.xlu", "dl/b_xlu" } }));
    // Mesh tipo 1 formato 2: display list e cada imagem de fundo.
    r = Collect({ R"({"$schema":"unbound/room/1","setups":{"0":{"mesh":{"type":1,"format":2,"opa":"dl/bg",
        "images":{"0":{"source":"tex/bg0","width":320,"height":240,"id":1},"1":{"source":"tex/bg1"}}}}}})" });
    CHECK(r.accepted);
    CHECK((r.references == Refs{ { "mesh.opa", "dl/bg" }, { "mesh.image.source", "tex/bg0" },
                                 { "mesh.image.source", "tex/bg1" } }));
    // Formato 1: uma imagem só, em "image".
    r = Collect({ R"({"$schema":"unbound/room/1","setups":{"0":{"mesh":{"type":1,"format":1,"opa":"dl/um",
        "image":{"source":"tex/um"}}}}})" });
    CHECK(r.accepted && Has(r, "mesh.image.source", "tex/um") && Has(r, "mesh.opa", "dl/um"));
}

void TestDelta() {
    // Delta de mod sobre a sala da base: vale a referência mesclada (a de cima), não a de baixo.
    const auto r = Collect({ R"({"$schema":"unbound/room/1","setups":{"0":{"mesh":{"type":0,
        "entries":{"0":{"opa":"dl/vanilla_ntsc","xlu":null}}}}}})",
                             R"({"$schema":"unbound/room/1","setups":{"0":{"mesh":{"entries":{"0":{"opa":"dl/mod"}}}}}})" });
    CHECK(r.accepted);
    CHECK((r.references == Refs{ { "mesh.entries.opa", "dl/mod" } }));
}

void TestCollision() {
    const auto r = Collect({ R"({"$schema":"unbound/collision/3",
      "bounds": {"min": [0, 0, 0], "max": [1, 1, 1]},
      "bulk": {"file": "scenes/x/collision.bin", "vertices": 4, "polys": 2},
      "surfaceTypes": {"0": {"camera": 0}}})" },
                           "scenes/x/collision.json");
    CHECK(r.typed && r.accepted);
    CHECK((r.references == Refs{ { "bulk.file", "scenes/x/collision.bin" } }));
}

void TestUntyped() {
    // Registro, texto e tipo desconhecido: o host não passa pelo framework.
    CHECK(!Collect({ R"({"scenes":{"a":{}}})" }).typed);
    CHECK(!Collect({ R"({"$schema":"unbound/text/1","0":"oi"})" }).typed);
    // Tipo do framework com versão não registrada: o host não acha fábrica, então é recusa explicada, não silêncio.
    for (const char* doc : { R"({"$schema":"unbound/collision/1","bulk":{"file":"x"}})",
                             R"({"$schema":"unbound/scene/2","setups":{"0":{}}})" }) {
        const auto r = Collect({ doc });
        CHECK(r.typed && !r.accepted && r.references.empty());
        CHECK(r.error.find("o framework registra") != std::string::npos);
    }
    CHECK(Collect({ R"({"$schema":"unbound/collision/1","bulk":{"file":"x"}})" }).error.find("collision/3") !=
          std::string::npos);
    CHECK(RegisteredVersion("unbound/room") == 1 && RegisteredVersion("unbound/collision") == 3 &&
          RegisteredVersion("unbound/text") == 0);
    // Sem '{' no primeiro byte o jogo pula a camada (mescla estrita); nenhuma serve = sem tipo.
    CHECK(!Collect({ R"(  {"$schema":"unbound/scene/1","setups":{"0":{}}})" }).typed);
    CHECK(!Collect({ "não é json" }).typed);
    DocumentKind kind = DocumentKind::Paths;
    CHECK(DocumentKindFor("unbound/scene", 1, kind) && kind == DocumentKind::Scene);
    CHECK(DocumentKindFor("unbound/collision", 3, kind) && kind == DocumentKind::Collision);
    CHECK(!DocumentKindFor("unbound/room", 2, kind));
}

void TestRefused() {
    // Cena sem setup "0": o jogo recusaria ao entrar; aqui sai antes, com o motivo e sem referências.
    const auto r = Collect({ R"({"$schema":"unbound/scene/1","collision":"c","setups":{"1":{}}})" });
    CHECK(r.typed && !r.accepted && r.references.empty());
    CHECK(r.error.find("setup") != std::string::npos);
    // Colisão sem bulk.file.
    const auto c = Collect({ R"({"$schema":"unbound/collision/3","bounds":{"min":[0,0,0],"max":[1,1,1]},"bulk":{}})" });
    CHECK(c.typed && !c.accepted && c.error.find("bulk.file") != std::string::npos);
}

void TestLookupPath() {
    CHECK(ResourceLookupPath("mesh.opa", "__OTR__scenes/x/a") == "scenes/x/a");
    CHECK(ResourceLookupPath("rooms", "scenes/x/a") == "scenes/x/a");
    CHECK(ResourceLookupPath("cutscene", "x__OTR__y") == "x__OTR__y");
    CHECK(ResourceLookupPath("paths", "__OTR__").empty());
    // bulk.file a fábrica de colisão lê cru (ArchiveManager::LoadFile), com o nome literal.
    CHECK(IsRawFileReference("bulk.file") && !IsRawFileReference("collision"));
    CHECK(ResourceLookupPath("bulk.file", "__OTR__custom/c.bin") == "__OTR__custom/c.bin");
}

void TestDiscardedCycles() {
    // Entradas que o AddCycle da fábrica descartaria (frames vazio, índice fora de textures, textura que não é
    // caminho depois de uma válida) não deixam referência, e cada uma vira nota.
    for (const char* cycle : { R"("textures":["t/sumida"],"frames":[])", R"("textures":["t/sumida"],"frames":[1])",
                               R"("textures":["t/sumida"],"frames":[-1])", R"("textures":["t/sumida",4],"frames":[0])",
                               R"("textures":[],"frames":[0])" }) {
        const auto r = Collect({ std::string(R"({"$schema":"unbound/scene/1","setups":{"0":{"materialAnims":{"0":{)"
                                             R"("segment":8,"type":"texCycle",)") + cycle + "}}}}}" });
        CHECK(r.typed && r.accepted && r.references.empty());
        bool discarded = false;
        for (const auto& note : r.notes) {
            discarded = discarded || note.find("entrada descartada") != std::string::npos;
        }
        CHECK(discarded);
    }
    // Frame no último índice válido continua aceito.
    const auto ok = Collect({ R"({"$schema":"unbound/scene/1","setups":{"0":{"materialAnims":{"0":{"segment":8,)"
                              R"("type":"texCycle","textures":["t/a","t/b"],"frames":[1,0,1]}}}}})" });
    CHECK(ok.accepted && ok.references.size() == 2);
}

void TestSparseSetups() {
    // Setup 1e9 num JSON de 60 bytes virava um <AlternateHeader> por índice; acima de 255 é ignorado com nota.
    const auto r = Collect({ R"({"$schema":"unbound/scene/1","collision":"c","setups":{"0":{},"1000000000":{}}})" });
    CHECK(r.typed && r.accepted && r.references.size() == 1);
    bool ignored = false;
    for (const auto& note : r.notes) {
        ignored = ignored || note.find("setups/1000000000: índice acima de 255 ignorado") != std::string::npos;
    }
    CHECK(ignored);
    MergedDocument merged;
    CHECK(MergeLayers({ { "m", R"({"setups":{"0":{},"255":{"skybox":{"id":3}},"256":{}}})", 0 } }, true, merged));
    TranscodeContext context;
    context.path = "x";
    const std::string xml = TranscodeScene(merged.doc, false, context);
    size_t headers = 0;
    for (size_t at = xml.find("<AlternateHeader"); at != std::string::npos; at = xml.find("<AlternateHeader", at + 1)) {
        ++headers;
    }
    CHECK(headers == 255 && xml.size() < 16 * 1024);
}

void TestVersionNames() {
    CHECK(std::string(GameVersionName(0xD43DA81F)) == "NTSC-US 1.1");
    CHECK(std::string(GameVersionName(0x09465AC3)) == "PAL GC");
    CHECK(std::string(GameVersionName(0x917D18F6)) == "PAL GC MQ debug");
    CHECK(std::string(GameVersionName(0xFFFFFFFF)).empty());
}

} // namespace

int main() {
    TestScene();
    TestRoom();
    TestDelta();
    TestCollision();
    TestUntyped();
    TestRefused();
    TestLookupPath();
    TestDiscardedCycles();
    TestSparseSetups();
    TestVersionNames();
    if (gFailures) {
        std::fprintf(stderr, "%d falha(s)\n", gFailures);
        return 1;
    }
    std::puts("reference_tests: ok");
    return 0;
}
