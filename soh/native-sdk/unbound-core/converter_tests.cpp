// Testes do conversor binário -> formato 2 com recursos sintéticos no layout do SoH.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

#include "converter.h"
#include "transcode.h"
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

class Bin {
  public:
    Bin() : mBytes(0x40, '\0') {} // cabeçalho OTR, little-endian
    Bin& U8(uint32_t v) {
        mBytes.push_back(static_cast<char>(v & 0xFF));
        return *this;
    }
    Bin& U16(uint32_t v) {
        return U8(v).U8(v >> 8);
    }
    Bin& U32(uint32_t v) {
        return U16(v & 0xFFFF).U16(v >> 16);
    }
    Bin& Str(const std::string& s) {
        U32(static_cast<uint32_t>(s.size()));
        mBytes += s;
        return *this;
    }
    const std::string& Bytes() const {
        return mBytes;
    }

  private:
    std::string mBytes;
};

std::map<std::string, std::string> Resources() {
    std::map<std::string, std::string> r;
    Bin scene;
    scene.U32(7);
    scene.U32(0x03).Str("__OTR__scenes/shared/test_scene/testCol");
    scene.U32(0x04).U32(1).Str("scenes/shared/test_scene/test_room_0").U32(0).U32(0);
    scene.U32(0x07).U8(2).U16(3);
    scene.U32(0x0F).U32(1);
    for (int i = 0; i < 18; ++i) {
        scene.U8(i == 3 ? 0xB7 : 200); // light1Dir x = -73
    }
    scene.U16((1 << 10) | 993).U16(12800);
    scene.U32(0x18).U32(2).Str("").Str("scenes/shared/test_scene/testSet2");
    scene.U32(0x0D).U32(1).Str("scenes/shared/test_scene/testPath");
    scene.U32(0x14);
    r["scenes/shared/test_scene/test_scene"] = scene.Bytes();

    Bin alt;
    alt.U32(2);
    alt.U32(0x11).U8(0).U8(29).U8(1).U8(0);
    alt.U32(0x14);
    r["scenes/shared/test_scene/testSet2"] = alt.Bytes();

    Bin room;
    room.U32(5);
    room.U32(0x01).U32(1).U16(16).U16(static_cast<uint16_t>(-100)).U16(0).U16(50).U16(0).U16(0xC000).U16(0).U16(0xFFFF);
    room.U32(0x0B).U32(2).U16(1).U16(0x15);
    room.U32(0x0A).U8(0).U8(0).U8(2).U8(0).Str("__OTR__scenes/shared/test_scene/test_room_0DL_1").Str("").U8(0).Str("").Str(
        "scenes/shared/test_scene/test_room_0DL_2");
    room.U32(0x10).U8(0xFF).U8(0xFF).U8(0);
    room.U32(0x14);
    r["scenes/shared/test_scene/test_room_0"] = room.Bytes();

    Bin col;
    col.U16(static_cast<uint16_t>(-10)).U16(0).U16(static_cast<uint16_t>(-10)).U16(10).U16(5).U16(10);
    col.U32(3);
    col.U16(0).U16(0).U16(0).U16(10).U16(0).U16(0).U16(0).U16(0).U16(10);
    col.U32(1);
    col.U16(0).U16((1u << 13) | 0).U16((1u << 13) | 1).U16(2).U16(0).U16(0x7FFF).U16(0).U16(static_cast<uint16_t>(-3));
    col.U32(1).U32((5u << 6) | 3).U32((2u << 8) | 7); // data1, data0
    col.U32(1).U16(1).U16(3).U32(0);
    col.U32(3).U16(1).U16(2).U16(3).U16(4).U16(5).U16(6).U16(7).U16(8).U16(9);
    col.U32(1).U16(static_cast<uint16_t>(-50)).U16(0).U16(0).U16(100).U16(100).U32((0x3Fu << 13) | (2u << 8) | 1 | (1u << 19));
    r["scenes/shared/test_scene/testCol"] = col.Bytes();

    Bin path;
    path.U32(1).U32(2).U16(1).U16(2).U16(3).U16(4).U16(5).U16(6);
    r["scenes/shared/test_scene/testPath"] = path.Bytes();
    return r;
}

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void TestConvert() {
    const auto resources = Resources();
    const ReadResourceFn read = [&](const std::string& path, std::string& bytes) {
        const auto found = resources.find(path);
        if (found == resources.end()) {
            return false;
        }
        bytes = found->second;
        return true;
    };
    CHECK(SceneDirName("test_scene", false) == "scenes/test" && SceneDirName("test_scene", true) == "scenes/test_mq");
    std::vector<ConvertedFile> files;
    ConvertReport report;
    CHECK(ConvertScene(read, "scenes/shared/test_scene/test_scene", "scenes/test", files, report));
    CHECK(report.scenes == 1 && report.rooms == 1 && report.collisions == 1 && report.paths == 1 && report.failures == 0);
    std::map<std::string, std::string> byPath;
    for (const auto& file : files) {
        byPath[file.path] = file.bytes;
    }
    CHECK(byPath.size() == 5);
    const Json scene = Json::parse(byPath["scenes/test/scene.json"]);
    CHECK(scene["$schema"] == "unbound/scene/1" && scene["collision"] == "scenes/test/collision.json");
    CHECK(scene["rooms"]["0"] == "scenes/test/rooms/0.json");
    CHECK(scene["setups"]["0"]["specialObjects"]["globalObject"] == 3);
    CHECK(scene["setups"]["0"]["lighting"]["0"]["fogNear"] == 993 && scene["setups"]["0"]["lighting"]["0"]["fogBlendRate"] == 1);
    CHECK(scene["setups"]["0"]["lighting"]["0"]["light1Dir"][0] == -73);
    CHECK(scene["setups"]["0"]["paths"][0] == "scenes/test/paths/testPath.json");
    CHECK(!scene["setups"].contains("1") && scene["setups"]["2"]["skybox"]["id"] == 29);

    const Json room = Json::parse(byPath["scenes/test/rooms/0.json"]);
    const Json& actor = room["setups"]["0"]["actors"]["0"];
    CHECK(actor["id"] == 16 && actor["pos"] == Json::parse("[-100,0,50]") && actor["rot"][1] == -16384 &&
          actor["params"] == 65535);
    CHECK(room["setups"]["0"]["objects"]["1"] == 0x15);
    CHECK(room["setups"]["0"]["mesh"]["entries"]["0"]["opa"] == "scenes/shared/test_scene/test_room_0DL_1");
    CHECK(room["setups"]["0"]["mesh"]["entries"]["0"]["xlu"].is_null());
    CHECK(room["setups"]["0"]["time"]["hour"] == 255);

    const Json col = Json::parse(byPath["scenes/test/collision.json"]);
    CHECK(col["$schema"] == "unbound/collision/3" && col["bulk"]["vertices"] == 3 && col["bulk"]["polys"] == 1);
    CHECK(col["surfaceTypes"]["0"]["camera"] == 7 && col["surfaceTypes"]["0"]["exit"] == 2 &&
          col["surfaceTypes"]["0"]["material"] == 3 && col["surfaceTypes"]["0"]["lightSetting"] == 5);
    CHECK(col["cameras"]["0"]["positionIndex"] == 0 && col["cameraPositions"]["2"] == Json::parse("[7,8,9]"));
    CHECK(col["waterBoxes"]["0"]["room"] == -1 && col["waterBoxes"]["0"]["lightSetting"] == 2 &&
          col["waterBoxes"]["0"]["notSwimmable"] == 1 && col["waterBoxes"]["0"]["xMin"] == -50);
    const std::string& bin = byPath["scenes/test/collision.bin"];
    CHECK(bin.size() == 3 * 12 + 28);
    // vA do polígono: índice 0 com xpFlags 1 no formato de 29 bits.
    CHECK(static_cast<uint8_t>(bin[36 + 7]) == 0x20 && static_cast<uint8_t>(bin[36 + 24]) == 0xFD);

    // Volta completa: o que o conversor escreve o transcodificador aceita.
    TranscodeContext context;
    context.path = "scenes/test/scene.json";
    const std::string sceneXml = TranscodeScene(Json::parse(byPath["scenes/test/scene.json"]), false, context);
    CHECK(Contains(sceneXml, "<SetAlternateHeaders><AlternateHeader/><AlternateHeader><SetCollisionHeader"));
    CHECK(Contains(sceneXml, "FogNear=\"2017\" FogFar=\"12800\""));
    MergedDocument merged;
    CHECK(MergeLayers({ { "base", byPath["scenes/test/collision.json"], 0 } }, true, merged));
    const std::string colXml = TranscodeCollision(merged.doc, context);
    CHECK(Contains(colXml, "<CameraData SType=\"1\" NumData=\"3\" CameraPosDataSeg=\"0\"/>"));
    CHECK(MergeLayers({ { "base", byPath["scenes/test/rooms/0.json"], 0 } }, true, merged));
    const std::string roomXml = TranscodeScene(merged.doc, true, context);
    CHECK(Contains(roomXml, "<ActorEntry Id=\"16\" PosX=\"-100\" PosY=\"0\" PosZ=\"50\" RotX=\"0\" RotY=\"-16384\" RotZ=\"0\" "
                            "Params=\"-1\"/>"));
    CHECK(Contains(roomXml, "<Polygon PolyType=\"0\" MeshOpa=\"scenes/shared/test_scene/test_room_0DL_1\" MeshXlu=\"\"/>"));

    // Recurso que falta ou truncado: a cena fica de fora e nada é emitido.
    auto broken = resources;
    broken["scenes/shared/test_scene/test_room_0"].resize(0x48);
    const ReadResourceFn readBroken = [&](const std::string& path, std::string& bytes) {
        const auto found = broken.find(path);
        if (found == broken.end()) {
            return false;
        }
        bytes = found->second;
        return true;
    };
    std::vector<ConvertedFile> none;
    ConvertReport failed;
    CHECK(!ConvertScene(readBroken, "scenes/shared/test_scene/test_scene", "scenes/test", none, failed));
    CHECK(none.empty() && failed.failures == 1 && failed.scenes == 0 && failed.rooms == 0 && failed.errors.size() == 1);

    // ZIP: o arquivo gerado deve abrir com qualquer leitor (verificado pelo script de teste em Python).
    const std::string zip = BuildStoredZip(files);
    CHECK(zip.size() > 22 && zip.compare(0, 4, "PK\x03\x04") == 0);
    if (const char* out = std::getenv("UNBOUND_TEST_ZIP")) {
        std::ofstream(out, std::ios::binary) << zip;
    }
}

} // namespace

int main() {
    TestConvert();
    if (gFailures) {
        std::fprintf(stderr, "unbound converter: %d falhas\n", gFailures);
        return EXIT_FAILURE;
    }
    std::printf("unbound converter: ok\n");
    return EXIT_SUCCESS;
}
