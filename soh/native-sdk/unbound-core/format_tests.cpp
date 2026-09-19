// Testes das regras da SPEC.md §2–§4 do Unbound: merge por camada e JSON -> XML do host.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "transcode.h"
#include "unbound_docs.h"
#include "unbound_format.h"

namespace {

using namespace LinkSpanUnbound;

int gFailures = 0;

#define CHECK(expr)                                                                  \
    do {                                                                             \
        if (!(expr)) {                                                               \
            std::fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #expr);  \
            ++gFailures;                                                             \
        }                                                                            \
    } while (0)

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

MergedDocument Merge(std::vector<std::string> jsons, bool strict = true) {
    std::vector<LayerDocument> layers;
    int n = 0;
    for (auto& json : jsons) {
        layers.push_back({ "camada" + std::to_string(n++), std::move(json), 0 });
    }
    MergedDocument out;
    MergeLayers(layers, strict, out);
    return out;
}

template <typename F> bool Rejects(F&& call) {
    try {
        call();
        return false;
    } catch (const DocumentError&) { return true; }
}

void TestValues() {
    int64_t v = 0;
    CHECK(ParseIntString("0x0F12", v) && v == 0x0F12);
    CHECK(ParseIntString("-5", v) && v == -5);
    CHECK(!ParseIntString(" 5", v) && !ParseIntString("5x", v) && !ParseIntString("0x", v));
    double d = 0;
    CHECK(ParseNumberString("1.5e2", d) && d == 150.0);
    CHECK(!ParseNumberString("inf", d) && !ParseNumberString("nan", d) && !ParseNumberString("0x1p3", d));
    CHECK(ToInt(Json(3.9)) == 3 && ToInt(Json(-3.9)) == -3 && ToInt(Json(true)) == 1);
    CHECK(ToInt(Json("0x10")) == 16 && ToInt(Json("abc"), 7) == 7);
    CHECK(ToNumber(Json(true), 4.0) == 4.0); // booleano não é número
    const Vec3 short2 = ReadVec3(Json::parse("[1,2]"));
    CHECK(short2.x == 0 && short2.y == 0 && short2.z == 0);
    const Vec3 extra = ReadVec3(Json::parse("[1,\"2.5\",3,9]"));
    CHECK(extra.x == 1 && extra.y == 2.5 && extra.z == 3);
}

void TestMerge() {
    // Objetos mesclam por chave; arrays trocam inteiros; null apaga; comentário é aceito.
    auto m = Merge({ R"({"$schema":"unbound/room/1","a":{"x":1,"y":2},"list":[1,2,3]})",
                     R"({/* comentario */ "a":{"y":5,"z":6},"list":[9], "gone":null})",
                     R"({"a":{"x":null}} // fim)" });
    CHECK(m.layersUsed == 3 && m.schema == "unbound/room/1" && m.type == "unbound/room" && m.version == 1);
    CHECK(m.doc["a"] == Json::parse(R"({"y":5,"z":6})"));
    CHECK(m.doc["list"] == Json::parse("[9]"));
    CHECK(!m.doc.contains("gone") && !m.doc.contains("$schema"));

    // $replace descarta as camadas de baixo e some do resultado.
    m = Merge({ R"({"$schema":"unbound/scene/1","s":{"a":1,"b":2}})", R"({"s":{"$replace":true,"c":3}})" });
    CHECK(m.doc["s"] == Json::parse(R"({"c":3})"));

    // Aninhamento acima do limite pula a camada antes do parse; colchete dentro de string não conta.
    const std::string deep = std::string(kMaxJsonDepth, '[') + std::string(kMaxJsonDepth, ']');
    CHECK(JsonDepthWithin(deep) && !JsonDepthWithin("[" + deep + "]"));
    CHECK(JsonDepthWithin(R"({"s":"[[[[[[[[\"{{{{{"})", 2) && !JsonDepthWithin(R"({"a":{"b":[]}})", 2));
    m = Merge({ R"({"$schema":"unbound/scene/1","k":1})", "{\"x\":" + std::string(100000, '[') });
    CHECK(m.layersUsed == 1 && m.doc["k"] == 1 && m.notes.size() == 1 &&
          m.notes[0].find("aninhamento") != std::string::npos);

    // Camada inválida é pulada e as outras mesclam; primeiro byte que não é '{' também.
    m = Merge({ R"({"$schema":"unbound/scene/1","k":1})", "{quebrado", " {\"k\":2}", R"({"k":3})" });
    CHECK(m.layersUsed == 2 && m.doc["k"] == 3 && m.notes.size() == 2);
    m = Merge({ " {\"k\":2}" }, false);
    CHECK(m.layersUsed == 1);

    // O $schema de topo em string da camada mais alta vence; não string não conta.
    m = Merge({ R"({"$schema":"unbound/collision/2"})", R"({"$schema":"unbound/collision/3"})",
                R"({"$schema":5})" });
    CHECK(m.schema == "unbound/collision/3" && m.version == 3);
    m = Merge({ R"({"k":1})" });
    CHECK(m.schema.empty());
    MergedDocument none;
    CHECK(!MergeLayers({ { "x", "nao json", 0 } }, true, none));
}

void TestKeys() {
    const Json actors = Json::parse(R"({"b":{},"10":{},"2":{},"-1":{},"a":{},"$order":["a","zz","a","10"]})");
    const auto keys = ListKeys(actors);
    const std::vector<std::string> expected{ "a", "10", "-1", "2", "b" };
    CHECK(keys == expected);
    const Json list = Json::parse(R"({"0":1,"1":2,"2":3})");
    CHECK(PositionalKeys(list, "x").size() == 3);
    CHECK(Rejects([] { PositionalKeys(Json::parse(R"({"0":1,"1":2,"3":3})"), "x"); }));
    CHECK(Rejects([] { PositionalKeys(Json::parse(R"({"0":1,"$order":["0"]})"), "x"); }));
    // Buraco criado por um null de uma camada de cima (§3.2).
    auto m = Merge({ R"({"l":{"0":1,"1":2,"2":3}})", R"({"l":{"1":null}})" });
    CHECK(Rejects([&] { PositionalKeys(m.doc["l"], "l"); }));
    m = Merge({ R"({"l":{"0":1,"1":2,"2":3}})", R"({"l":{"2":null}})" });
    CHECK(PositionalKeys(m.doc["l"], "l").size() == 2);
}

TranscodeContext Context() {
    TranscodeContext context;
    context.path = "scenes/test/scene.json";
    context.resolveEntrance = [](const std::string& name) -> int32_t {
        if (name == "ENTR_HYRULE_FIELD_0") {
            return 0x00CD;
        }
        if (name == "mod/cena/main") {
            return 1560;
        }
        return -1;
    };
    return context;
}

void TestScene() {
    const Json doc = Json::parse(R"({
      "collision": "scenes/test/collision.json",
      "rooms": {"0": "scenes/test/rooms/0.json", "1": "scenes/test/rooms/1.json"},
      "setups": {
        "0": {
          "specialObjects": {"elfMessage": 1, "globalObject": 3},
          "skybox": {"id": 1, "weather": 0, "indoors": 0},
          "sound": {"seq": 2, "natureAmbience": 19, "reverb": 0, "song": "custom/music/Tema & Cia"},
          "lighting": {"0": {"ambient": [70, 45, 57], "light1Dir": [73, 73, 73], "fogNear": 993,
                              "fogBlendRate": 1, "fogFar": 12800, "drawDistance": 30000}},
          "entrances": {"0": {"spawn": 0, "room": 0}},
          "spawns": {"0": {"id": 0, "pos": [1.5, 2, 3], "rot": [0, 49152, 0], "params": "0x0FFF"}},
          "exits": {"0": "ENTR_HYRULE_FIELD_0", "1": 5, "2": "mod/cena/main", "3": "0x10"},
          "materialAnims": {"0": {"segment": 8, "type": "texScroll",
                                   "layers": [{"xStep": 0, "yStep": 1, "width": 32, "height": 32}]},
                            "1": {"segment": 9, "type": "bogus"},
                            "2": {"segment": 10, "pass": "xlu", "type": "colorLerp", "length": 64,
                                  "keyFrames": [0, 32], "primColors": [[1,2,3,4,5],[6,7,8,9,10]],
                                  "envColors": [[1,1,1,1],[2,2,2,2]]},
                            "3": {"segment": 11, "type": "texCycle", "textures": ["t/a", "t/b"],
                                  "frames": [0, 1, 1]}},
          "paths": ["scenes/test/paths/a.json"]
        },
        "2": {"skybox": {"id": 29}},
        "nome": {"skybox": {"id": 99}}
      }})");
    TranscodeContext context = Context();
    const std::string xml = TranscodeScene(doc, false, context);
    CHECK(xml.rfind("<Room Version=\"0\"><SetAlternateHeaders><AlternateHeader/><AlternateHeader>", 0) == 0);
    CHECK(Contains(xml, "<SetSkyboxSettings Unknown=\"0\" SkyboxId=\"29\""));
    CHECK(!Contains(xml, "SkyboxId=\"99\""));
    CHECK(Contains(xml, "<SetCollisionHeader FileName=\"scenes/test/collision.json\"/>"));
    CHECK(Contains(xml, "<RoomEntry Path=\"scenes/test/rooms/1.json\" VromStart=\"0\" VromEnd=\"0\"/>"));
    CHECK(Contains(xml, "Song=\"custom/music/Tema &amp; Cia\""));
    CHECK(Contains(xml, "FogNear=\"2017\" FogFar=\"12800\" DrawDistance=\"30000\""));
    CHECK(Contains(xml, "Light1DirX=\"73\""));
    CHECK(Contains(xml, "<StartPositionEntry Id=\"0\" PosX=\"1.5\" PosY=\"2\" PosZ=\"3\" RotX=\"0\" RotY=\"-16384\" "
                        "RotZ=\"0\" Params=\"4095\"/>"));
    CHECK(Contains(xml, "<ExitEntry Id=\"205\"/><ExitEntry Id=\"5\"/><ExitEntry Id=\"1560\"/><ExitEntry Id=\"16\"/>"));
    CHECK(Contains(xml, "<TexScroll Segment=\"8\" Pass=\"3\" Type=\"0\"><Layer XStep=\"0\" YStep=\"1\" Width=\"32\" "
                        "Height=\"32\"/></TexScroll>"));
    CHECK(Contains(xml, "<Color Segment=\"10\" Pass=\"2\" Type=\"3\" Length=\"64\"><KeyFrame Frame=\"0\" PrimR=\"1\""));
    CHECK(Contains(xml, "EnvR=\"2\" EnvG=\"2\" EnvB=\"2\" EnvA=\"2\"/></Color>"));
    CHECK(Contains(xml, "<TexCycle Segment=\"11\" Pass=\"3\"><Texture Path=\"t/a\"/><Texture Path=\"t/b\"/><Frame "
                        "Index=\"0\"/>"));
    CHECK(context.notes.size() == 1 && Contains(context.notes[0], "materialAnims/1"));
    CHECK(Contains(xml, "<Pathway FilePath=\"scenes/test/paths/a.json\"/>"));
    CHECK(xml.size() >= 19 && xml.compare(xml.size() - 19, 19, "<EndMarker/></Room>") == 0);

    // Saídas: nome desconhecido, fracionário e negativo recusam o documento.
    for (const char* exit : { R"("NAO_EXISTE")", "1.5", "-1", "true" }) {
        Json bad = Json::parse(R"({"setups":{"0":{"exits":{"0":0}}}})");
        bad["setups"]["0"]["exits"]["0"] = Json::parse(exit);
        TranscodeContext c = Context();
        CHECK(Rejects([&] { TranscodeScene(bad, false, c); }));
    }
    TranscodeContext c = Context();
    CHECK(Rejects([&] { TranscodeScene(Json::parse(R"({"setups":{"1":{}}})"), false, c); }));
    CHECK(Rejects([&] { TranscodeScene(Json::parse(R"({"setups":{"0":{"lights":{"0":{"type":3}}}}})"), true, c); }));
    CHECK(Rejects([&] { TranscodeScene(Json::parse(R"({"setups":{"0":{"mesh":{"type":3}}}})"), true, c); }));
}

void TestRoom() {
    const Json doc = Json::parse(R"({"setups":{"0":{
        "time": {"hour": 12},
        "wind": {"west": -10, "speed": 200},
        "objects": {"0": 1, "1": 1500},
        "lights": {"0": {"type": 1, "dir": [0, -127, 0], "color": [255, 255, 255]},
                   "1": {"type": 0, "pos": [100000.5, 0, 0], "color": [1, 2, 3], "glow": 1, "radius": 300}},
        "actors": {"b": {"id": 16, "pos": [0, 0, 0], "rot": [0, 0, 0], "params": -1},
                   "a": {"id": 17, "pos": [1, 1, 1]}, "$order": ["b"]},
        "mesh": {"type": 2, "entries": {"0": {"pos": [0, 0, 70000], "radius": 5000.5, "opa": "dl/a", "xlu": null}}}
      }}})");
    TranscodeContext context = Context();
    const std::string xml = TranscodeScene(doc, true, context);
    CHECK(xml.rfind("<Room Version=\"0\"><SetTimeSettings Hour=\"12\" Minute=\"255\" TimeIncrement=\"255\"/>", 0) == 0);
    CHECK(Contains(xml, "<SetWind WindWest=\"-10\" WindVertical=\"0\" WindSouth=\"0\" WindSpeed=\"200\"/>"));
    CHECK(Contains(xml, "<ObjectEntry Id=\"1500\"/>"));
    CHECK(Contains(xml, "<LightInfo Type=\"1\" DirX=\"0\" DirY=\"-127\" DirZ=\"0\" ColorR=\"255\""));
    CHECK(Contains(xml, "<LightInfo Type=\"0\" X=\"100000.5\""));
    const size_t b = xml.find("<ActorEntry Id=\"16\"");
    const size_t a = xml.find("<ActorEntry Id=\"17\"");
    CHECK(b != std::string::npos && a != std::string::npos && b < a);
    CHECK(Contains(xml, "<SetMesh Data=\"0\" MeshHeaderType=\"2\" PolyNum=\"1\"><Polygon PolyType=\"2\" PosX=\"0\" "
                        "PosY=\"0\" PosZ=\"70000\" Unknown=\"5000.5\" MeshOpa=\"dl/a\" MeshXlu=\"\"/></SetMesh>"));
    CHECK(!Contains(xml, "SetCollisionHeader") && !Contains(xml, "SetRoomList"));

    const Json background = Json::parse(R"({"setups":{"0":{"mesh":{"type":1,"format":2,"opa":"dl/bg",
        "images":{"0":{"source":"tex/bg0","width":320,"height":240,"id":1},"1":{"source":"tex/bg1"}}}}}})");
    const std::string bg = TranscodeScene(background, true, context);
    CHECK(Contains(bg, "<Polygon PolyType=\"1\" Format=\"2\" BgImageCount=\"2\" MeshOpa=\"dl/bg\" MeshXlu=\"\"><BgImage "
                       "Unknown_00=\"0\" Id=\"1\" ImagePath=\"tex/bg0\""));
}

void TestCollision() {
    const Json doc = Json::parse(R"({
      "bounds": {"min": [-100000, -10, -5.4], "max": [100000, 500, 5.6]},
      "bulk": {"file": "scenes/test/collision.bin", "vertices": 4, "polys": 2},
      "surfaceTypes": {"0": {"camera": 300, "exit": 40, "floorType": 5, "canHookshot": 1}},
      "cameras": {"0": {"sType": 1, "count": 3, "positionIndex": 1}, "1": {"sType": 2, "positionIndex": null}},
      "cameraPositions": {"0": [1, 2, 3], "1": [4, 5, 6], "2": [7, 8, 9], "3": [10, 11, 12]},
      "waterBoxes": {"0": {"xMin": -70000.4, "ySurface": 10, "zMin": 0, "xLength": 140000, "zLength": 50,
                           "lightSetting": 200, "notSwimmable": 1}}
    })");
    TranscodeContext context = Context();
    const std::string xml = TranscodeCollision(doc, context);
    CHECK(xml.rfind("<CollisionHeader Version=\"0\" MinBoundsX=\"-100000\" MinBoundsY=\"-10\" MinBoundsZ=\"-5\" "
                    "MaxBoundsX=\"100000\" MaxBoundsY=\"500\" MaxBoundsZ=\"6\" BulkFile=\"scenes/test/collision.bin\" "
                    "BulkVertices=\"4\" BulkPolys=\"2\">",
                    0) == 0);
    CHECK(Contains(xml, "<SurfaceType Camera=\"300\" Exit=\"40\" LightSetting=\"0\" FloorType=\"5\""));
    CHECK(Contains(xml, "CanHookshot=\"1\""));
    CHECK(Contains(xml, "<CameraData SType=\"1\" NumData=\"3\" CameraPosDataSeg=\"1\"/><CameraData SType=\"2\" "
                        "NumData=\"0\" CameraPosDataSeg=\"-1\"/>"));
    CHECK(Contains(xml, "<CameraPositionData PosX=\"1\" PosY=\"2\" PosZ=\"3\" RotX=\"4\" RotY=\"5\" RotZ=\"6\" FOV=\"7\" "
                        "JfifID=\"8\" Unknown=\"9\"/><CameraPositionData PosX=\"10\" PosY=\"11\" PosZ=\"12\" RotX=\"0\""));
    CHECK(Contains(xml, "<WaterBox XMin=\"-70000\" Ysurface=\"10\" ZMin=\"0\" XLength=\"140000\" ZLength=\"50\" "
                        "Camera=\"0\" LightSetting=\"200\" Room=\"-1\" NotSwimmable=\"1\"/>"));
    CHECK(Rejects([&] { TranscodeCollision(Json::parse(R"({"bulk":{}})"), context); }));
}

void TestPaths() {
    Json doc = Json::parse(R"({"paths":{"0":{"points":[[1,2,3],[4.5,5,6]]},"1":{"points":[]}}})");
    for (int i = 0; i < 300; ++i) {
        doc["paths"]["1"]["points"].push_back(Json::array({ i, 0, 0 }));
    }
    TranscodeContext context = Context();
    const std::string xml = TranscodePaths(doc, context);
    CHECK(xml.rfind("<Path Version=\"0\"><PathData><PathPoint X=\"1\" Y=\"2\" Z=\"3\"/><PathPoint X=\"4.5\"", 0) == 0);
    CHECK(Contains(xml, "<PathPoint X=\"254\" Y=\"0\" Z=\"0\"/></PathData></Path>"));
    CHECK(!Contains(xml, "X=\"255\"") && context.notes.size() == 1);
}

void TestManifest() {
    CHECK(CheckManifest(R"({"format":"unbound","formatVersion":2,"features":["scenes","paths"]})").base);
    CHECK(CheckManifest(R"({"features":["scenes"]})").base); // formatVersion ausente = 2
    const auto modLayer = CheckManifest(R"({"formatVersion":2,"features":["text"]})");
    CHECK(modLayer.valid && !modLayer.base);
    CHECK(!CheckManifest(R"({"formatVersion":1,"features":["scenes"]})").valid);
    CHECK(!CheckManifest(R"({"formatVersion":2,"requires":{"formatVersion":3},"features":["scenes"]})").valid);
    CHECK(!CheckManifest("[1,2]").valid && !CheckManifest("nada").valid);
}

TextTable Text(std::vector<std::string> jsons) {
    std::vector<LayerDocument> layers;
    int n = 0;
    for (auto& json : jsons) {
        layers.push_back({ "camada" + std::to_string(n++), std::move(json), 0 });
    }
    TextTable table;
    BuildTextTable(layers, table);
    return table;
}

void TestText() {
    uint32_t replaced = 0;
    CHECK(DecodeMessageText("A\xC3\xA9\x01\xE4\xB8\x80", replaced) == std::string("A\xE9\x01?") && replaced == 1);

    // Camada de baixo acrescenta e troca; a de cima remove uma vanilla, desfaz a remoção de outra e
    // substitui um campo.
    const TextTable table = Text({
        R"({"$schema":"unbound/text/1","messages":{"0x0F12":{"box":1,"ypos":2,"text":"Oi\u0002"},
            "0x0071":null,"0x0072":null,"20":{"text":"vinte"}}})",
        R"({"messages":{"0x0072":{"text":"volta"},"0x0073":null,"0x0F12":{"box":17},
            "65535":{"text":"x"},"0x0010":"texto solto","0x0011":{"box":0}}})",
    });
    CHECK(table.layersUsed == 2 && !table.replaceTable);
    CHECK(table.messages.size() == 3);
    CHECK(table.messages[0].id == 20 && table.messages[0].bytes == "vinte");
    CHECK(table.messages[1].id == 0x72 && table.messages[1].bytes == "volta");
    CHECK(table.messages[2].id == 0x0F12 && table.messages[2].box == 1 && table.messages[2].ypos == 2 &&
          table.messages[2].bytes == "Oi");
    CHECK(table.removed == std::vector<uint32_t>({ 0x71, 0x73 }));
    CHECK(table.notes.size() == 3); // 65535, texto solto e sem "text"

    // $replace esvazia a tabela do jogo; remoções de baixo deixam de valer.
    const TextTable replaced2 = Text({ R"({"messages":{"0x0071":null}})",
                                       R"({"messages":{"$replace":true,"1":{"text":"um"}}})" });
    CHECK(replaced2.replaceTable && replaced2.removed.empty() && replaced2.messages.size() == 1);

    // Mensagem longa é truncada em 8191 bytes (+ 0x02 do jogo).
    const TextTable longText = Text({ R"({"messages":{"1":{"text":")" + std::string(9000, 'a') + R"("}}})" });
    CHECK(longText.messages.size() == 1 && longText.messages[0].bytes.size() == kMaxMessageBytes &&
          longText.notes.size() == 1);

    // Camada inválida é pulada; messages que não é objeto não aplica nada.
    const TextTable broken = Text({ "{quebrado", R"({"messages":[1,2]})" });
    CHECK(broken.layersUsed == 1 && broken.messages.empty() && broken.notes.size() == 2);
}

} // namespace

int main() {
    TestValues();
    TestMerge();
    TestKeys();
    TestScene();
    TestRoom();
    TestCollision();
    TestPaths();
    TestManifest();
    TestText();
    if (gFailures) {
        std::fprintf(stderr, "unbound format: %d falhas\n", gFailures);
        return EXIT_FAILURE;
    }
    std::printf("unbound format: ok\n");
    return EXIT_SUCCESS;
}
