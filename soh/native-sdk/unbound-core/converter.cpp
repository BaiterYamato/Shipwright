#include "converter.h"

#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace LinkSpanUnbound {
namespace {

using Json = nlohmann::ordered_json;

constexpr size_t kOtrHeaderSize = 0x40;

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Leitor do corpo de um recurso binário do SoH (depois do cabeçalho OTR), na ordem de bytes do cabeçalho.
class Reader {
  public:
    Reader(const std::string& bytes, const std::string& path) : mBytes(bytes), mPath(path) {
        if (bytes.size() < kOtrHeaderSize) {
            throw FormatError(path + ": recurso menor que o cabeçalho OTR");
        }
        mBig = bytes[0] == 1;
        mPos = kOtrHeaderSize;
    }
    uint64_t Unsigned(size_t size) {
        if (mPos + size > mBytes.size()) {
            throw FormatError(mPath + ": recurso truncado");
        }
        uint64_t value = 0;
        for (size_t i = 0; i < size; ++i) {
            const uint64_t byte = static_cast<uint8_t>(mBytes[mPos + i]);
            value |= mBig ? byte << (8 * (size - 1 - i)) : byte << (8 * i);
        }
        mPos += size;
        return value;
    }
    int8_t S8() {
        return static_cast<int8_t>(Unsigned(1));
    }
    uint8_t U8() {
        return static_cast<uint8_t>(Unsigned(1));
    }
    int16_t S16() {
        return static_cast<int16_t>(Unsigned(2));
    }
    uint16_t U16() {
        return static_cast<uint16_t>(Unsigned(2));
    }
    int32_t S32() {
        return static_cast<int32_t>(Unsigned(4));
    }
    uint32_t U32() {
        return static_cast<uint32_t>(Unsigned(4));
    }
    std::string String() {
        const int32_t length = S32();
        if (length < 0 || mPos + static_cast<size_t>(length) > mBytes.size()) {
            throw FormatError(mPath + ": string fora do recurso");
        }
        std::string text = mBytes.substr(mPos, static_cast<size_t>(length));
        mPos += static_cast<size_t>(length);
        return text;
    }
    uint32_t Count(size_t minEntryBytes) {
        const uint32_t count = U32();
        if (minEntryBytes && count > (mBytes.size() - mPos) / minEntryBytes) {
            throw FormatError(mPath + ": contagem " + std::to_string(count) + " maior que o recurso");
        }
        return count;
    }

  private:
    const std::string& mBytes;
    std::string mPath;
    size_t mPos = 0;
    bool mBig = false;
};

std::string StripOtr(std::string path) {
    constexpr const char* prefix = "__OTR__";
    if (path.rfind(prefix, 0) == 0) {
        path.erase(0, std::strlen(prefix));
    }
    return path;
}

std::string Leaf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Key(size_t index) {
    return std::to_string(index);
}

Json Vec(int64_t x, int64_t y, int64_t z) {
    return Json::array({ x, y, z });
}

Json Rgb(int v0, int v1, int v2) {
    return Json::array({ v0 & 0xFF, v1 & 0xFF, v2 & 0xFF });
}

Json OptionalPath(const std::string& path) {
    const std::string stripped = StripOtr(path);
    return stripped.empty() ? Json(nullptr) : Json(stripped);
}

struct Refs {
    std::string collision;
    std::vector<std::string> rooms;
    std::vector<std::string> alternates;
    std::vector<std::string> pathways;
};

Json ReadActor(Reader& r) {
    const int64_t id = r.U16();
    const int64_t x = r.S16();
    const int64_t y = r.S16();
    const int64_t z = r.S16();
    const int64_t rx = r.S16();
    const int64_t ry = r.S16();
    const int64_t rz = r.S16();
    const int64_t params = r.U16();
    return Json{ { "id", id }, { "pos", Vec(x, y, z) }, { "rot", Vec(rx, ry, rz) }, { "params", params } };
}

Json ReadMesh(Reader& r) {
    Json mesh = Json::object();
    r.S8(); // data
    const int type = r.S8();
    mesh["type"] = type;
    if (type == 1) {
        const int format = r.U8();
        r.String();
        r.String();
        const uint32_t count = r.Count(24);
        Json images = Json::object();
        Json first;
        for (uint32_t i = 0; i < count; ++i) {
            Json image;
            image["unk00"] = r.U16();
            image["id"] = r.U8();
            image["source"] = StripOtr(r.String());
            image["unk0C"] = r.U32();
            image["tlut"] = r.U32();
            image["width"] = r.U16();
            image["height"] = r.U16();
            image["fmt"] = r.U8();
            image["siz"] = r.U8();
            image["mode0"] = r.U16();
            image["tlutCount"] = r.U16();
            if (i == 0) {
                first = image;
            }
            images[Key(i)] = image;
        }
        r.S8(); // polyType
        mesh["format"] = format;
        mesh["opa"] = OptionalPath(r.String());
        mesh["xlu"] = OptionalPath(r.String());
        if (format == 1) {
            // O formato 1 guarda uma imagem só, sem id nem unk00 (SPEC.md §4.3).
            if (!first.is_null()) {
                first.erase("id");
                first.erase("unk00");
            }
            mesh["image"] = first.is_null() ? Json::object() : first;
        } else {
            mesh["images"] = images;
        }
        return mesh;
    }
    const uint32_t count = r.U8();
    Json entries = Json::object();
    for (uint32_t i = 0; i < count; ++i) {
        r.S8(); // polyType
        Json entry = Json::object();
        if (type == 2) {
            const int64_t x = r.S16();
            const int64_t y = r.S16();
            const int64_t z = r.S16();
            entry["pos"] = Vec(x, y, z);
            entry["radius"] = r.S16();
        }
        entry["opa"] = OptionalPath(r.String());
        entry["xlu"] = OptionalPath(r.String());
        entries[Key(i)] = entry;
    }
    mesh["entries"] = entries;
    return mesh;
}

Json ReadLight(Reader& r) {
    const int type = r.U8();
    const int16_t x = r.S16();
    const int16_t y = r.S16();
    const int16_t z = r.S16();
    const int c0 = r.U8();
    const int c1 = r.U8();
    const int c2 = r.U8();
    const int glow = r.U8();
    const int16_t radius = r.S16();
    Json light{ { "type", type } };
    if (type == 1) {
        // O loader binário grava os campos de luz pontual e o jogo lê a união como luz direcional; o JSON
        // reproduz o que fica na memória (bytes little-endian de x, y, z).
        const auto lo = [](int16_t v) { return static_cast<int>(static_cast<int8_t>(v & 0xFF)); };
        const auto hi = [](int16_t v) { return static_cast<int>(static_cast<int8_t>((v >> 8) & 0xFF)); };
        light["dir"] = Json::array({ lo(x), hi(x), lo(y) });
        light["color"] = Rgb(hi(y), lo(z), hi(z));
    } else {
        light["pos"] = Vec(x, y, z);
        light["color"] = Rgb(c0, c1, c2);
        light["glow"] = glow;
        light["radius"] = radius;
    }
    return light;
}

Json ReadLighting(Reader& r) {
    const uint32_t count = r.Count(22);
    Json list = Json::object();
    for (uint32_t i = 0; i < count; ++i) {
        int8_t c[18];
        for (auto& value : c) {
            value = r.S8();
        }
        const uint16_t fogNear = static_cast<uint16_t>(r.S16());
        const uint16_t fogFar = r.U16();
        const auto rgb = [&](int at) { return Rgb(c[at], c[at + 1], c[at + 2]); };
        const auto dir = [&](int at) { return Json::array({ c[at], c[at + 1], c[at + 2] }); };
        list[Key(i)] = Json{ { "ambient", rgb(0) },           { "light1Dir", dir(3) },
                             { "light1Color", rgb(6) },       { "light2Dir", dir(9) },
                             { "light2Color", rgb(12) },      { "fogColor", rgb(15) },
                             { "fogNear", fogNear & 0x3FF },  { "fogBlendRate", (fogNear >> 10) & 0x3F },
                             { "fogFar", fogFar } };
    }
    return list;
}

// Um cabeçalho (cena, sala ou cabeçalho alternativo) vira um setup; referências vão para `refs`.
Json ReadSetup(const std::string& bytes, const std::string& path, Refs& refs) {
    Reader r(bytes, path);
    Json setup = Json::object();
    const uint32_t commands = r.Count(4);
    for (uint32_t index = 0; index < commands; ++index) {
        const int32_t id = r.S32();
        switch (id) {
            case 0x00: { // SetStartPositionList
                const uint32_t count = r.Count(16);
                Json list = Json::object();
                for (uint32_t i = 0; i < count; ++i) {
                    list[Key(i)] = ReadActor(r);
                }
                setup["spawns"] = list;
                break;
            }
            case 0x01: { // SetActorList
                const uint32_t count = r.Count(16);
                Json list = Json::object();
                for (uint32_t i = 0; i < count; ++i) {
                    list[Key(i)] = ReadActor(r);
                }
                setup["actors"] = list;
                break;
            }
            case 0x02: // SetCsCamera: sem dados no SoH
                r.S8();
                r.S32();
                break;
            case 0x03:
                refs.collision = StripOtr(r.String());
                break;
            case 0x04: {
                const uint32_t count = r.Count(12);
                refs.rooms.clear();
                for (uint32_t i = 0; i < count; ++i) {
                    refs.rooms.push_back(StripOtr(r.String()));
                    r.S32();
                    r.S32();
                }
                break;
            }
            case 0x05: {
                const int west = r.S8();
                const int vertical = r.S8();
                const int south = r.S8();
                const int speed = r.U8();
                setup["wind"] = Json{ { "west", west }, { "vertical", vertical }, { "south", south }, { "speed", speed } };
                break;
            }
            case 0x06: {
                const uint32_t count = r.Count(2);
                Json list = Json::object();
                for (uint32_t i = 0; i < count; ++i) {
                    const int spawn = r.U8();
                    const int room = r.U8();
                    list[Key(i)] = Json{ { "spawn", spawn }, { "room", room } };
                }
                setup["entrances"] = list;
                break;
            }
            case 0x07: {
                const int elf = r.S8();
                const int global = r.S16();
                setup["specialObjects"] = Json{ { "elfMessage", elf }, { "globalObject", global } };
                break;
            }
            case 0x08: {
                const int flags = r.S8();
                const int32_t flags2 = r.S32();
                setup["behavior"] = Json{ { "gameplayFlags", flags }, { "gameplayFlags2", flags2 } };
                break;
            }
            case 0x0A:
                setup["mesh"] = ReadMesh(r);
                break;
            case 0x0B: {
                const uint32_t count = r.Count(2);
                Json list = Json::object();
                for (uint32_t i = 0; i < count; ++i) {
                    list[Key(i)] = r.U16();
                }
                setup["objects"] = list;
                break;
            }
            case 0x0C: {
                const uint32_t count = r.Count(12);
                Json list = Json::object();
                for (uint32_t i = 0; i < count; ++i) {
                    list[Key(i)] = ReadLight(r);
                }
                setup["lights"] = list;
                break;
            }
            case 0x0D: {
                const uint32_t count = r.Count(4);
                for (uint32_t i = 0; i < count; ++i) {
                    refs.pathways.push_back(StripOtr(r.String()));
                }
                break;
            }
            case 0x0E: {
                const uint32_t count = r.Count(16);
                Json list = Json::object();
                for (uint32_t i = 0; i < count; ++i) {
                    const int frontRoom = r.S8();
                    const int frontEffects = r.U8();
                    const int backRoom = r.S8();
                    const int backEffects = r.U8();
                    const int actor = r.S16();
                    const int64_t x = r.S16();
                    const int64_t y = r.S16();
                    const int64_t z = r.S16();
                    const int rotY = r.S16();
                    const int params = r.U16();
                    list[Key(i)] = Json{ { "id", actor },
                                         { "pos", Vec(x, y, z) },
                                         { "rotY", rotY },
                                         { "params", params },
                                         { "front", { { "room", frontRoom }, { "effects", frontEffects } } },
                                         { "back", { { "room", backRoom }, { "effects", backEffects } } } };
                }
                setup["transitionActors"] = list;
                break;
            }
            case 0x0F:
                setup["lighting"] = ReadLighting(r);
                break;
            case 0x10: {
                const int hour = r.U8();
                const int minute = r.U8();
                const int increment = r.U8();
                setup["time"] = Json{ { "hour", hour }, { "minute", minute }, { "increment", increment } };
                break;
            }
            case 0x11: {
                const int unk = r.U8();
                const int skybox = r.U8();
                const int weather = r.U8();
                const int indoors = r.U8();
                setup["skybox"] = Json{ { "id", skybox }, { "weather", weather }, { "indoors", indoors }, { "unk", unk } };
                break;
            }
            case 0x12: {
                const int disabled = r.U8();
                const int sunMoon = r.U8();
                setup["skyboxModifier"] = Json{ { "skyboxDisabled", disabled }, { "sunMoonDisabled", sunMoon } };
                break;
            }
            case 0x13: {
                const uint32_t count = r.Count(2);
                Json list = Json::object();
                for (uint32_t i = 0; i < count; ++i) {
                    list[Key(i)] = r.U16();
                }
                setup["exits"] = list;
                break;
            }
            case 0x14: // EndMarker
                break;
            case 0x15: {
                const int reverb = r.U8();
                const int nature = r.U8();
                const int seq = r.U8();
                setup["sound"] = Json{ { "seq", seq }, { "natureAmbience", nature }, { "reverb", reverb } };
                break;
            }
            case 0x16:
                setup["echo"] = r.S8();
                break;
            case 0x17:
                setup["cutscene"] = StripOtr(r.String());
                break;
            case 0x18: {
                const uint32_t count = r.Count(4);
                refs.alternates.clear();
                for (uint32_t i = 0; i < count; ++i) {
                    refs.alternates.push_back(StripOtr(r.String()));
                }
                break;
            }
            case 0x19: {
                const int movement = r.S8();
                const int32_t area = r.S32();
                setup["cameraSettings"] = Json{ { "cameraMovement", movement }, { "worldMapArea", area } };
                break;
            }
            default:
                throw FormatError(path + ": comando de cena " + std::to_string(id) + " desconhecido");
        }
    }
    return setup;
}

struct Context {
    const ReadResourceFn& read;
    const std::string& sceneDir;
    std::vector<ConvertedFile>& out;
    ConvertReport& report;
    std::set<std::string> written;
};

std::string Load(Context& ctx, const std::string& path) {
    std::string bytes;
    if (!ctx.read(path, bytes)) {
        throw FormatError(path + ": recurso ausente");
    }
    return bytes;
}

void Emit(Context& ctx, const std::string& path, std::string bytes) {
    if (ctx.written.insert(path).second) {
        ctx.out.push_back({ path, std::move(bytes) });
    }
}

std::string ConvertPaths(Context& ctx, const std::string& resource) {
    const std::string outPath = ctx.sceneDir + "/paths/" + Leaf(resource) + ".json";
    if (ctx.written.count(outPath)) {
        return outPath;
    }
    const std::string bytes = Load(ctx, resource);
    Reader r(bytes, resource);
    Json paths = Json::object();
    const uint32_t count = r.Count(4);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t points = r.Count(6);
        Json list = Json::array();
        for (uint32_t p = 0; p < points; ++p) {
            const int64_t x = r.S16();
            const int64_t y = r.S16();
            const int64_t z = r.S16();
            list.push_back(Vec(x, y, z));
        }
        paths[Key(i)] = Json{ { "points", list } };
    }
    Json doc{ { "$schema", "unbound/paths/1" }, { "paths", paths } };
    Emit(ctx, outPath, doc.dump(2));
    ++ctx.report.paths;
    return outPath;
}

void PutU16(std::string& out, uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void PutU32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

uint32_t UnpackLegacyVtxWord(uint16_t packed) {
    return static_cast<uint32_t>(packed & 0x1FFF) | (static_cast<uint32_t>(packed >> 13) << 29);
}

std::string ConvertCollision(Context& ctx, const std::string& resource) {
    const std::string jsonPath = ctx.sceneDir + "/collision.json";
    if (ctx.written.count(jsonPath)) {
        return jsonPath;
    }
    const std::string binPath = ctx.sceneDir + "/collision.bin";
    const std::string bytes = Load(ctx, resource);
    Reader r(bytes, resource);
    int64_t bounds[6];
    for (auto& value : bounds) {
        value = r.S16();
    }
    std::string bin;
    const uint32_t vertices = r.Count(6);
    bin.reserve(static_cast<size_t>(vertices) * 12);
    for (uint32_t i = 0; i < vertices; ++i) {
        for (int k = 0; k < 3; ++k) {
            PutU32(bin, static_cast<uint32_t>(static_cast<int32_t>(r.S16())));
        }
    }
    const uint32_t polys = r.Count(16);
    for (uint32_t i = 0; i < polys; ++i) {
        const uint16_t type = r.U16();
        const uint32_t a = UnpackLegacyVtxWord(r.U16());
        const uint32_t b = UnpackLegacyVtxWord(r.U16());
        const uint32_t c = UnpackLegacyVtxWord(r.U16());
        const uint16_t nx = r.U16();
        const uint16_t ny = r.U16();
        const uint16_t nz = r.U16();
        const int32_t dist = r.S16();
        PutU16(bin, type);
        PutU16(bin, 0);
        PutU32(bin, a);
        PutU32(bin, b);
        PutU32(bin, c);
        PutU16(bin, nx);
        PutU16(bin, ny);
        PutU16(bin, nz);
        PutU16(bin, 0);
        PutU32(bin, static_cast<uint32_t>(dist));
    }
    Json surfaces = Json::object();
    const uint32_t surfaceCount = r.Count(8);
    for (uint32_t i = 0; i < surfaceCount; ++i) {
        // Mesma ordem do loader binário do host: a primeira palavra é data1, a segunda data0.
        const uint32_t data1 = r.U32();
        const uint32_t data0 = r.U32();
        surfaces[Key(i)] = Json{ { "camera", data0 & 0xFF },
                                 { "exit", (data0 >> 8) & 0x1F },
                                 { "floorType", (data0 >> 13) & 0x1F },
                                 { "wallFlags", (data0 >> 18) & 7 },
                                 { "wallType", (data0 >> 21) & 0x1F },
                                 { "floorProperty", (data0 >> 26) & 0xF },
                                 { "isSoft", (data0 >> 30) & 1 },
                                 { "isHorseBlocked", (data0 >> 31) & 1 },
                                 { "material", data1 & 0xF },
                                 { "floorEffect", (data1 >> 4) & 3 },
                                 { "lightSetting", (data1 >> 6) & 0x1F },
                                 { "echo", (data1 >> 11) & 0x3F },
                                 { "canHookshot", (data1 >> 17) & 1 },
                                 { "conveyorSpeed", (data1 >> 18) & 7 },
                                 { "conveyorDirection", (data1 >> 21) & 0x3F },
                                 { "isWallDamage", (data1 >> 27) & 1 } };
    }
    const uint32_t cameraCount = r.Count(8);
    std::vector<Json> cameras;
    std::vector<int32_t> cameraIndices;
    for (uint32_t i = 0; i < cameraCount; ++i) {
        const int sType = r.U16();
        const int count = r.S16();
        cameraIndices.push_back(r.S32());
        cameras.push_back(Json{ { "sType", sType }, { "count", count } });
    }
    const int32_t positionCount = r.S32();
    if (positionCount < 0) {
        throw FormatError(resource + ": contagem de posições de câmera negativa");
    }
    Json positions = Json::object();
    for (int32_t i = 0; i < positionCount; ++i) {
        const int64_t x = r.S16();
        const int64_t y = r.S16();
        const int64_t z = r.S16();
        positions[Key(static_cast<size_t>(i))] = Vec(x, y, z);
    }
    Json cameraList = Json::object();
    for (size_t i = 0; i < cameras.size(); ++i) {
        // O loader binário aponta para a posição mesmo com índice fora da lista quando há posições; o
        // formato só aceita índice válido ou nenhum.
        const int32_t index = cameraIndices[i];
        cameras[i]["positionIndex"] =
            positionCount > 0 && index >= 0 && index < positionCount ? Json(index) : Json(nullptr);
        cameraList[Key(i)] = cameras[i];
    }
    const int32_t waterCount = r.S32();
    Json water = Json::object();
    for (int32_t i = 0; i < waterCount; ++i) {
        const int xMin = r.S16();
        const int ySurface = r.S16();
        const int zMin = r.S16();
        const int xLength = r.S16();
        const int zLength = r.S16();
        const uint32_t properties = r.U32();
        const uint32_t room = (properties >> 13) & 0x3F;
        water[Key(static_cast<size_t>(i))] = Json{ { "xMin", xMin },
                                                   { "ySurface", ySurface },
                                                   { "zMin", zMin },
                                                   { "xLength", xLength },
                                                   { "zLength", zLength },
                                                   { "camera", properties & 0xFF },
                                                   { "lightSetting", (properties >> 8) & 0x1F },
                                                   { "room", room == 0x3F ? -1 : static_cast<int>(room) },
                                                   { "notSwimmable", (properties >> 19) & 1 } };
    }
    Json doc{ { "$schema", "unbound/collision/3" },
              { "bounds", { { "min", Vec(bounds[0], bounds[1], bounds[2]) },
                            { "max", Vec(bounds[3], bounds[4], bounds[5]) } } },
              { "bulk", { { "file", binPath }, { "vertices", vertices }, { "polys", polys } } },
              { "surfaceTypes", surfaces },
              { "cameras", cameraList },
              { "cameraPositions", positions },
              { "waterBoxes", water } };
    Emit(ctx, binPath, std::move(bin));
    Emit(ctx, jsonPath, doc.dump(2));
    ++ctx.report.collisions;
    return jsonPath;
}

// Setup 0 do cabeçalho principal mais um setup por cabeçalho alternativo (vazio = sem setup).
Json ReadSetups(Context& ctx, const std::string& resource, Refs& refs) {
    Json setups = Json::object();
    Json first = ReadSetup(Load(ctx, resource), resource, refs);
    const std::vector<std::string> alternates = refs.alternates;
    std::vector<std::string> pathways = refs.pathways;
    const auto finishPaths = [&](Json& setup, const std::vector<std::string>& files) {
        if (files.empty()) {
            return;
        }
        Json list = Json::array();
        for (const auto& file : files) {
            list.push_back(ConvertPaths(ctx, file));
        }
        setup["paths"] = list;
    };
    finishPaths(first, pathways);
    setups["0"] = first;
    for (size_t i = 0; i < alternates.size(); ++i) {
        if (alternates[i].empty()) {
            continue;
        }
        Refs altRefs;
        Json alternate = ReadSetup(Load(ctx, alternates[i]), alternates[i], altRefs);
        if (!altRefs.collision.empty() && altRefs.collision != refs.collision) {
            ctx.report.errors.push_back(ctx.sceneDir + ": setup " + Key(i + 1) +
                                        " usa outra colisão; o formato guarda uma só");
        }
        finishPaths(alternate, altRefs.pathways);
        setups[Key(i + 1)] = alternate;
    }
    return setups;
}

} // namespace

std::string SceneDirName(const std::string& sceneFileName, bool mq) {
    std::string name = sceneFileName;
    const std::string suffix = "_scene";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.resize(name.size() - suffix.size());
    }
    return "scenes/" + name + (mq ? "_mq" : "");
}

bool ConvertScene(const ReadResourceFn& read, const std::string& scenePath, const std::string& sceneDir,
                  std::vector<ConvertedFile>& out, ConvertReport& report) {
    std::vector<ConvertedFile> files;
    Context ctx{ read, sceneDir, files, report, {} };
    const ConvertReport before = report;
    try {
        Refs refs;
        Json setups = ReadSetups(ctx, scenePath, refs);
        Json doc{ { "$schema", "unbound/scene/1" } };
        if (!refs.collision.empty()) {
            doc["collision"] = ConvertCollision(ctx, refs.collision);
        }
        Json rooms = Json::object();
        for (size_t i = 0; i < refs.rooms.size(); ++i) {
            Refs roomRefs;
            Json room{ { "$schema", "unbound/room/1" }, { "setups", ReadSetups(ctx, refs.rooms[i], roomRefs) } };
            const std::string roomPath = sceneDir + "/rooms/" + Key(i) + ".json";
            Emit(ctx, roomPath, room.dump(2));
            rooms[Key(i)] = roomPath;
            ++report.rooms;
        }
        doc["rooms"] = rooms;
        doc["setups"] = setups;
        Emit(ctx, sceneDir + "/scene.json", doc.dump(2));
    } catch (const std::exception& error) {
        // Cena que não converte inteira fica de fora; os contadores voltam ao que eram.
        const auto errors = report.errors;
        report = before;
        report.errors = errors;
        report.errors.push_back(sceneDir + ": " + error.what());
        ++report.failures;
        return false;
    }
    ++report.scenes;
    for (auto& file : files) {
        out.push_back(std::move(file));
    }
    return true;
}

std::vector<ConvertedFile> BuildBase(const ReadResourceFn& read, const std::vector<SceneSource>& scenes,
                                     const std::string& sourceJson, ConvertReport& report) {
    std::vector<ConvertedFile> files;
    for (const auto& scene : scenes) {
        ConvertScene(read, scene.resource, scene.dir, files, report);
    }
    Json features = Json::array();
    if (report.failures == 0 && report.scenes > 0) {
        features.push_back("scenes");
        features.push_back("collision");
        features.push_back("paths");
    }
    Json source = Json::parse(sourceJson, nullptr, false);
    if (!source.is_object()) {
        source = Json::object();
    }
    source["converter"] = kConverterVersion;
    source["scenes"] = report.scenes;
    source["failures"] = report.failures;
    const Json manifest{ { "format", "unbound" },
                         { "formatVersion", 2 },
                         { "game", "oot" },
                         { "source", source },
                         { "features", features },
                         { "requires", { { "formatVersion", 2 } } } };
    files.insert(files.begin(), ConvertedFile{ "unbound.json", manifest.dump(2) });
    return files;
}

namespace {

uint32_t Crc32(const std::string& data) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (const char byte : data) {
        crc = table[(crc ^ static_cast<uint8_t>(byte)) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

} // namespace

std::string BuildStoredZip(const std::vector<ConvertedFile>& files) {
    std::string zip;
    std::string central;
    constexpr uint16_t kDosDate = (0 << 9) | (1 << 5) | 1; // 1980-01-01
    for (const auto& file : files) {
        const uint32_t crc = Crc32(file.bytes);
        const uint32_t offset = static_cast<uint32_t>(zip.size());
        const uint32_t size = static_cast<uint32_t>(file.bytes.size());
        PutU32(zip, 0x04034b50);
        PutU16(zip, 20);
        PutU16(zip, 0x0800); // nomes UTF-8
        PutU16(zip, 0);      // stored
        PutU16(zip, 0);
        PutU16(zip, kDosDate);
        PutU32(zip, crc);
        PutU32(zip, size);
        PutU32(zip, size);
        PutU16(zip, static_cast<uint32_t>(file.path.size()));
        PutU16(zip, 0);
        zip += file.path;
        zip += file.bytes;

        PutU32(central, 0x02014b50);
        PutU16(central, 20);
        PutU16(central, 20);
        PutU16(central, 0x0800);
        PutU16(central, 0);
        PutU16(central, 0);
        PutU16(central, kDosDate);
        PutU32(central, crc);
        PutU32(central, size);
        PutU32(central, size);
        PutU16(central, static_cast<uint32_t>(file.path.size()));
        PutU16(central, 0);
        PutU16(central, 0);
        PutU16(central, 0);
        PutU16(central, 0);
        PutU32(central, 0);
        PutU32(central, offset);
        central += file.path;
    }
    const uint32_t centralOffset = static_cast<uint32_t>(zip.size());
    zip += central;
    PutU32(zip, 0x06054b50);
    PutU16(zip, 0);
    PutU16(zip, 0);
    PutU16(zip, static_cast<uint32_t>(files.size()));
    PutU16(zip, static_cast<uint32_t>(files.size()));
    PutU32(zip, static_cast<uint32_t>(central.size()));
    PutU32(zip, centralOffset);
    PutU16(zip, 0);
    return zip;
}

} // namespace LinkSpanUnbound
