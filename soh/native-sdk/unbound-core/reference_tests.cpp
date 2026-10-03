// Testes da coleta de referências de recurso dos documentos de mod (UNBOUND-019, plano §10.5/§14.3).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
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

// Lista posicional {"0": item, ..., "n-1": item} como texto.
std::string PositionalText(size_t n, const std::string& item) {
    std::string text = "{";
    for (size_t i = 0; i < n; ++i) {
        text += (i ? ",\"" : "\"") + std::to_string(i) + "\":" + item;
    }
    return text + "}";
}

// UNBOUND-025: grafo cena -> colisão e salas. Cena com três exits, uma sala, colisão com três câmeras.
struct GraphFixture {
    std::map<std::string, ReferenceReport> reports;
    std::map<std::string, int> reads;

    GraphFixture() {
        Put("scene.json", R"({"$schema":"unbound/scene/1","collision":"collision.json","rooms":{"0":"room.json"},
            "setups":{"0":{"exits":{"0":0,"1":0,"2":0}}}})");
        Put("room.json", R"({"$schema":"unbound/room/1","setups":{"0":{}}})");
        Put("collision.json", R"({"$schema":"unbound/collision/3","bulk":{"file":"x.bin"},
            "cameras":{"0":{},"1":{},"2":{}},"surfaceTypes":{"0":{"exit":0}}})");
    }
    void Put(const std::string& path, const std::string& json) {
        reports[path] = Collect({ json }, path);
        CHECK(reports[path].accepted);
    }
    ReferenceReport& Scene() { return reports.at("scene.json"); }
    Json& SceneDoc() { return reports.at("scene.json").document; }
    Json& Collision() { return reports.at("collision.json").document; }
    SceneGraph Graph() {
        reads.clear();
        auto graph = CollectSceneGraph(Scene(), [this](const std::string& path) -> const ReferenceReport* {
            ++reads[path];
            const auto found = reports.find(path);
            return found == reports.end() ? nullptr : &found->second;
        });
        // O cache do game.ready guarda salas e colisões compactadas: o grafo tem de sair igual em todo caso.
        std::map<std::string, ReferenceReport> compact;
        for (const auto& [path, report] : reports) {
            compact[path] = report;
            compact[path].document = CompactForGraph(report.document, report.kind);
        }
        const auto again = CollectSceneGraph(Scene(), [&](const std::string& path) -> const ReferenceReport* {
            const auto found = compact.find(path);
            return found == compact.end() ? nullptr : &found->second;
        });
        CHECK(again.notes == graph.notes && again.gaps == graph.gaps);
        return graph;
    }
};

bool AnyText(const std::vector<std::string>& texts, const std::string& fragment) {
    return std::any_of(texts.begin(), texts.end(),
                       [&](const std::string& text) { return text.find(fragment) != std::string::npos; });
}

void TestGraphExits() {
    // Exit é 1-based: 3 cabe em três saídas, 4 lê fora; 0 é sem saída; negativo e o valor embrulhado em s32 também.
    for (const int64_t value : { 0LL, 1LL, 3LL, 4LL, -1LL, -2147483648LL, 4294967296LL, 4294967299LL }) {
        GraphFixture f;
        f.Collision()["surfaceTypes"]["0"]["exit"] = value;
        const auto graph = f.Graph();
        const int32_t exit = static_cast<int32_t>(value);
        const bool outside = exit != 0 && (exit < 0 || exit > 3);
        CHECK(AnyText(graph.notes, "superfície(s) com exit fora de 1..3") == outside);
        CHECK(!outside || AnyText(graph.notes, "(ex.: 0.exit=" + std::to_string(exit) + ")"));
        CHECK(graph.gaps.empty());
    }
    // Cada header emitido tem a sua lista; um {} não herda a do 0. Setup ausente cai num já conferido.
    GraphFixture f;
    f.SceneDoc()["setups"]["1"] = Json::object();
    f.SceneDoc()["setups"]["2"] = ParseJson(R"({"exits":{"0":0,"1":0}})");
    f.SceneDoc()["setups"]["256"] = Json::object();
    f.Collision()["surfaceTypes"]["0"]["exit"] = 3;
    const auto graph = f.Graph();
    CHECK(graph.notes.size() == 2);
    CHECK(AnyText(graph.notes, "setups.1): 1 superfície(s) com exit fora de 1..0"));
    CHECK(AnyText(graph.notes, "setups.2): 1 superfície(s) com exit fora de 1..2"));
    // Sala que traz exits (mesmo vazio) troca a lista: exits não conferidos, sem nota falsa.
    for (const bool empty : { false, true }) {
        GraphFixture g;
        g.reports["room.json"].document["setups"]["0"]["exits"] = empty ? Json::object() : ParseJson(R"({"0":0})");
        g.Collision()["surfaceTypes"]["0"]["exit"] = 4;
        const auto roomExits = g.Graph();
        CHECK(!AnyText(roomExits.notes, "superfície(s)"));
        CHECK(AnyText(roomExits.gaps, "room.json setups.0 troca os exits"));
    }
}

void TestGraphWater() {
    // Três salas: -1 é todas; 0..2 valem; 3, 63 e negativos não ligam em sala nenhuma.
    for (const int64_t value : { -2LL, -1LL, 0LL, 2LL, 3LL, 63LL, 4294967295LL }) {
        GraphFixture f;
        f.SceneDoc()["rooms"] = ParseJson(R"({"0":"room.json","1":"room.json","2":"room.json"})");
        f.Collision()["waterBoxes"] = Json{ { "0", Json{ { "room", value } } } };
        const auto graph = f.Graph();
        const int32_t room = static_cast<int32_t>(value);
        CHECK(AnyText(graph.notes, "water box(es) com room fora das 3 salas") == (room != -1 && (room < 0 || room >= 3)));
        CHECK(f.reads["room.json"] == 1); // três slots, uma sala
    }
    // Room.num é s16: com 32 769 slots, 32 767 é a última sala alcançável.
    GraphFixture f;
    f.SceneDoc()["rooms"] = ParseJson(PositionalText(32769, R"("room.json")"));
    f.Collision()["waterBoxes"] = ParseJson(R"({"0":{"room":32767},"1":{"room":32768}})");
    const auto graph = f.Graph();
    CHECK(graph.notes.size() == 1 && AnyText(graph.notes, "1 water box(es) com room fora das 32768 salas"));
}

void TestGraphDoors() {
    // Câmera da porta (s8): -1 (0xFF) e -99 (157) não leem a tabela; 3 de 3 lê fora; 128 vira -128.
    for (const int64_t value : { 2LL, 3LL, -1LL, 255LL, -99LL, 157LL, -2LL, 128LL }) {
        GraphFixture f;
        f.SceneDoc()["setups"]["0"]["transitionActors"] =
            Json{ { "0", Json{ { "id", 9 }, { "front", Json{ { "effects", value } } },
                               { "back", Json{ { "effects", -1 } } } } } };
        const auto graph = f.Graph();
        const int32_t camera = static_cast<int8_t>(value);
        const bool outside = camera != -1 && camera != -99 && (camera < 0 || camera >= 3);
        CHECK(AnyText(graph.notes, "lado(s) de porta com câmera fora das 3") == outside);
        CHECK(!outside || AnyText(graph.notes, "0.front.effects=" + std::to_string(camera)));
    }
    // Id inválido ou por nome vira -1 no XML: não é porta.
    for (const char* id : { "-1", "\"ator/inexistente\"" }) {
        GraphFixture f;
        f.SceneDoc()["setups"]["0"]["transitionActors"] =
            ParseJson(std::string(R"({"0":{"id":)") + id + R"(,"front":{"effects":7},"back":{"effects":7}}})");
        CHECK(!AnyText(f.Graph().notes, "câmera fora"));
    }
    // Porta de uma sala usa a câmera da colisão da cena, inclusive num setup alternativo.
    GraphFixture f;
    f.reports["room.json"].document["setups"]["2"] =
        ParseJson(R"({"transitionActors":{"0":{"id":9,"front":{"effects":3},"back":{"effects":4}}}})");
    CHECK(AnyText(f.Graph().notes, "room.json setups.2.transitionActors: 2 lado(s) de porta com câmera fora das 3"));
    // Room do lado (s16): positiva além das salas lê fora ao montar a cena, mesmo sem ator válido; negativa não.
    for (const int64_t value : { 0LL, 2LL, 3LL, -1LL, -2LL, 32768LL, 65539LL }) {
        GraphFixture g;
        g.SceneDoc()["rooms"] = ParseJson(R"({"0":"room.json","1":"room.json","2":"room.json"})");
        g.SceneDoc()["setups"]["0"]["transitionActors"] =
            Json{ { "0", Json{ { "id", -1 }, { "front", Json{ { "room", value } } }, { "back", Json{ { "room", -1 } } } } } };
        const int32_t room = static_cast<int16_t>(value);
        CHECK(AnyText(g.Graph().notes, "com room além das 3 salas da cena") == (room >= 3));
    }
}

void TestGraphGaps() {
    // Colisão desconhecida, recusada ou de outro tipo nunca vira tabela vazia: só lacuna, sem nota.
    for (const int mode : { 0, 1, 2 }) {
        GraphFixture f;
        f.Collision()["surfaceTypes"]["0"]["exit"] = 9;
        if (mode == 0) f.reports.erase("collision.json");
        if (mode == 1) f.reports["collision.json"].accepted = false;
        if (mode == 2) f.reports["collision.json"].kind = DocumentKind::Room;
        const auto graph = f.Graph();
        CHECK(graph.notes.empty());
        CHECK(graph.gaps.size() == 1 && AnyText(graph.gaps, "colisão collision.json sem documento Unbound aceito"));
    }
    // Sala desconhecida: exits não conferidos.
    GraphFixture f;
    f.reports.erase("room.json");
    f.Collision()["surfaceTypes"]["0"]["exit"] = 9;
    const auto graph = f.Graph();
    CHECK(!AnyText(graph.notes, "superfície(s)"));
    CHECK(AnyText(graph.gaps, "sala room.json sem documento Unbound aceito"));
    // __OTR__ sai antes da busca; a colisão mesclada é a que vale ($replace muda a quantidade de câmeras).
    GraphFixture m;
    m.reports["scene.json"] = Collect({ R"({"$schema":"unbound/scene/1","collision":"__OTR__collision.json",
        "rooms":{"0":"__OTR__room.json"},"setups":{"0":{"exits":{"0":0},
        "transitionActors":{"0":{"id":9,"pos":[0,0,0],"front":{"effects":1},"back":{"effects":-1}}}}}})" }, "scene.json");
    CHECK(m.Scene().accepted);
    m.reports["collision.json"] = Collect({ R"({"$schema":"unbound/collision/3","bulk":{"file":"x.bin"},
        "cameras":{"0":{},"1":{},"2":{}},"surfaceTypes":{"0":{"exit":0}}})",
        R"({"cameras":{"$replace":true,"0":{}},"surfaceTypes":{"0":{"exit":2}}})" }, "collision.json");
    const auto merged = m.Graph();
    CHECK(m.reads["collision.json"] == 1 && m.reads["room.json"] == 1);
    CHECK(AnyText(merged.notes, "câmera fora das 1"));
    CHECK(AnyText(merged.notes, "exit fora de 1..1"));
    // O grafo não muda o documento.
    const std::string before = m.Scene().document.dump();
    m.Graph();
    CHECK(before == m.Scene().document.dump());
}

void TestGraphCompact() {
    // A versão do cache tira atores, geometria e campos que o grafo não lê; chaves e $order das listas ficam.
    const auto room = Collect({ R"({"$schema":"unbound/room/1","setups":{"0":{"actors":{"0":{"id":9,"pos":[0,0,0]}},
        "transitionActors":{"0":{"id":9,"pos":[1,2,3],"rotY":0,"params":5,"front":{"room":0,"effects":2},
        "back":{"room":1,"effects":-1}}},"exits":{"0":0}},"1":{"echo":3}}})" }, "room.json");
    CHECK(room.accepted);
    const Json small = CompactForGraph(room.document, DocumentKind::Room);
    CHECK(!small["setups"]["0"].contains("actors"));
    CHECK(small["setups"]["0"]["transitionActors"]["0"].dump() ==
          R"({"id":9,"front":{"room":0,"effects":2},"back":{"room":1,"effects":-1}})");
    CHECK(small["setups"]["0"]["exits"] == Json::object() && small["setups"]["1"] == Json::object());
    CHECK(!small.contains("$schema"));
    const Json collision = ParseJson(R"({"bulk":{"file":"x.bin"},"cameras":{"$order":["1","0"],"1":{"sType":3},
        "0":{"sType":1}},"waterBoxes":[{"xMin":0,"room":2},{"camera":1}],"surfaceTypes":{"0":{"exit":4,"camera":2}}})");
    CHECK(CompactForGraph(collision, DocumentKind::Collision).dump() ==
          R"({"cameras":{"$order":["1","0"],"1":{},"0":{}},"waterBoxes":[{"room":2},{}],"surfaceTypes":{"0":{"exit":4}}})");
    CHECK(CompactForGraph(collision, DocumentKind::Paths).empty());
    CHECK(CompactForGraph(collision, DocumentKind::Scene) == collision);
    // Listas no teto (a UB024-A tem 32 770 câmeras): linear, sem a busca por chave do ordered_json.
    const Json big = ParseJson(R"({"cameras":)" + PositionalText(32770, R"({"sType":1})") + R"(,"surfaceTypes":)" +
                               PositionalText(65535, R"({"exit":1,"camera":0,"floor":2})") + "}");
    const auto started = std::chrono::steady_clock::now();
    const Json compact = CompactForGraph(big, DocumentKind::Collision);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    CHECK(compact["cameras"].size() == 32770 && compact["surfaceTypes"].size() == 65535);
    CHECK(compact["surfaceTypes"]["65534"].dump() == R"({"exit":1})");
    CHECK(ms.count() < 2000); // quadrático passa de vários segundos
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
    TestGraphExits();
    TestGraphWater();
    TestGraphDoors();
    TestGraphGaps();
    TestGraphCompact();
    if (gFailures) {
        std::fprintf(stderr, "%d falha(s)\n", gFailures);
        return 1;
    }
    std::puts("reference_tests: ok");
    return 0;
}
