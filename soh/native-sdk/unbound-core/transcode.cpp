#include "transcode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace LinkSpanUnbound {
namespace {

// Valores do formato gravados na largura do campo do jogo (§2: fora da faixa "embrulha").
int32_t S8(int64_t v) {
    return static_cast<int8_t>(v);
}
int32_t U8(int64_t v) {
    return static_cast<uint8_t>(v);
}
int32_t S16(int64_t v) {
    return static_cast<int16_t>(v);
}
int32_t U16(int64_t v) {
    return static_cast<uint16_t>(v);
}
int32_t S32(int64_t v) {
    return static_cast<int32_t>(v);
}
int32_t Integral(double v) {
    return static_cast<int32_t>(std::llround(v));
}

class Xml {
  public:
    Xml& Open(const char* name) {
        CloseTag();
        mText += '<';
        mText += name;
        mStack.push_back(name);
        mTagOpen = true;
        return *this;
    }
    Xml& Attr(const char* name, const std::string& value) {
        mText += ' ';
        mText += name;
        mText += "=\"";
        mText += EscapeXml(value);
        mText += '"';
        return *this;
    }
    Xml& Attr(const char* name, int64_t value) {
        return Attr(name, std::to_string(value));
    }
    Xml& Attr(const char* name, int32_t value) {
        return Attr(name, std::to_string(value));
    }
    Xml& Attr(const char* name, uint32_t value) {
        return Attr(name, std::to_string(value));
    }
    Xml& Float(const char* name, double value) {
        char text[40];
        std::snprintf(text, sizeof(text), "%.9g", std::isfinite(value) ? value : 0.0);
        return Attr(name, std::string(text));
    }
    Xml& Close() {
        if (mTagOpen) {
            mText += "/>";
            mTagOpen = false;
        } else {
            mText += "</";
            mText += mStack.back();
            mText += '>';
        }
        mStack.pop_back();
        return *this;
    }
    // Elemento sem filhos numa linha: Leaf("X").Attr(...).Close().
    Xml& Leaf(const char* name) {
        return Open(name);
    }
    // XML já pronto como filho do elemento aberto.
    Xml& Raw(const std::string& text) {
        CloseTag();
        mText += text;
        return *this;
    }
    std::string Finish() {
        while (!mStack.empty()) {
            Close();
        }
        return std::move(mText);
    }

  private:
    void CloseTag() {
        if (mTagOpen) {
            mText += '>';
            mTagOpen = false;
        }
    }
    std::string mText;
    std::vector<std::string> mStack;
    bool mTagOpen = false;
};

struct Shared {
    Json rooms;
    std::string collision;
};

struct EntryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void Rgb(Xml& xml, const Json& value, const char* r, const char* g, const char* b) {
    int64_t c[3] = { 0, 0, 0 };
    if (value.is_array() && value.size() >= 3) {
        for (int i = 0; i < 3; ++i) {
            c[i] = ToInt(value[i]);
        }
    }
    xml.Attr(r, U8(c[0])).Attr(g, U8(c[1])).Attr(b, U8(c[2]));
}

void Dir(Xml& xml, const Json& value, const char* x, const char* y, const char* z) {
    int64_t c[3] = { 0, 0, 0 };
    if (value.is_array() && value.size() >= 3) {
        for (int i = 0; i < 3; ++i) {
            c[i] = ToInt(value[i]);
        }
    }
    xml.Attr(x, S8(c[0])).Attr(y, S8(c[1])).Attr(z, S8(c[2]));
}

void ActorAttrs(Xml& xml, const Json& actor) {
    const Vec3 pos = ReadVec3(SubArray(actor, "pos"));
    const Json& rotation = SubArray(actor, "rot");
    int64_t rot[3] = { 0, 0, 0 };
    if (rotation.size() >= 3) {
        for (int i = 0; i < 3; ++i) {
            rot[i] = ToInt(rotation[i]);
        }
    }
    xml.Attr("Id", S16(Field(actor, "id")))
        .Float("PosX", pos.x)
        .Float("PosY", pos.y)
        .Float("PosZ", pos.z)
        .Attr("RotX", S16(rot[0]))
        .Attr("RotY", S16(rot[1]))
        .Attr("RotZ", S16(rot[2]))
        .Attr("Params", S16(Field(actor, "params")));
}

std::string Where(const TranscodeContext& context, const std::string& what) {
    return context.path + " " + what;
}

void Lighting(Xml& xml, const Json& list, TranscodeContext& context) {
    xml.Open("SetLightingSettings");
    const auto keys = PositionalKeys(list, Where(context, "lighting"));
    if (keys.size() > 255) {
        context.notes.push_back(Where(context, "lighting") + ": " + std::to_string(keys.size()) +
                                " entradas; o índice de luz é um byte (§9)");
    }
    for (const auto& key : keys) {
        const Json& entry = list[key];
        xml.Leaf("LightingSetting");
        Rgb(xml, SubArray(entry, "ambient"), "AmbientColorR", "AmbientColorG", "AmbientColorB");
        Dir(xml, SubArray(entry, "light1Dir"), "Light1DirX", "Light1DirY", "Light1DirZ");
        Rgb(xml, SubArray(entry, "light1Color"), "Light1ColorR", "Light1ColorG", "Light1ColorB");
        Dir(xml, SubArray(entry, "light2Dir"), "Light2DirX", "Light2DirY", "Light2DirZ");
        Rgb(xml, SubArray(entry, "light2Color"), "Light2ColorR", "Light2ColorG", "Light2ColorB");
        Rgb(xml, SubArray(entry, "fogColor"), "FogColorR", "FogColorG", "FogColorB");
        // A palavra vanilla: 10 bits de fog near e 6 de blend rate (z_kankyo.c).
        const int64_t fogNear = ((Field(entry, "fogBlendRate") & 0x3F) << 10) | (Field(entry, "fogNear") & 0x3FF);
        xml.Attr("FogNear", S16(fogNear)).Attr("FogFar", S16(Field(entry, "fogFar")));
        // Neblina em unidades do mundo: a fábrica XML aplica os padrões do SPEC aos que faltam.
        static const std::pair<const char*, const char*> kWorldFog[] = {
            { "fogStart", "FogStart" }, { "fogEnd", "FogEnd" }, { "drawDistance", "DrawDistance" },
            { "nearPlane", "NearPlane" } };
        if (HasNumber(entry, "fogStart") || HasNumber(entry, "fogEnd") || HasNumber(entry, "drawDistance")) {
            for (const auto& [json, attr] : kWorldFog) {
                if (HasNumber(entry, json)) {
                    xml.Float(attr, NumberField(entry, json));
                }
            }
        }
        xml.Close();
    }
    xml.Close();
}

uint32_t PassOf(const Json& entry) {
    if (!entry.contains("pass")) {
        return 3;
    }
    const std::string pass = PathField(entry, "pass");
    if (pass == "opa") {
        return 1;
    }
    if (pass == "xlu") {
        return 2;
    }
    if (pass == "both") {
        return 3;
    }
    throw EntryError("pass '" + pass + "' não é opa, xlu nem both");
}

void MaterialAnim(Xml& xml, const Json& entry) {
    if (!entry.is_object()) {
        throw EntryError("a entrada não é um objeto");
    }
    // Faixas (segmento, passo, comprimento, índices) a fábrica XML confere; aqui só o que o XML não
    // consegue expressar: nomes, contagens casadas e tipos dos elementos.
    const uint32_t pass = PassOf(entry);
    const int64_t segment = Field(entry, "segment", -1);
    const std::string type = PathField(entry, "type");
    if (type == "texScroll" || type == "twoTexScroll") {
        const Json& layers = SubArray(entry, "layers");
        xml.Open("TexScroll")
            .Attr("Segment", segment)
            .Attr("Pass", pass)
            .Attr("Type", type == "texScroll" ? 0 : 1);
        for (const auto& layer : layers) {
            if (!layer.is_object()) {
                throw EntryError("layers tem um elemento que não é objeto");
            }
            xml.Leaf("Layer")
                .Attr("XStep", S8(Field(layer, "xStep")))
                .Attr("YStep", S8(Field(layer, "yStep")))
                .Attr("Width", U8(Field(layer, "width")))
                .Attr("Height", U8(Field(layer, "height")))
                .Close();
        }
        xml.Close();
        return;
    }
    if (type == "color" || type == "colorLerp" || type == "colorNonLinear") {
        const Json& keyFrames = SubArray(entry, "keyFrames");
        const Json& prim = SubArray(entry, "primColors");
        const Json& env = SubArray(entry, "envColors");
        if (prim.size() != keyFrames.size() || (!env.empty() && env.size() != keyFrames.size())) {
            throw EntryError("primColors / envColors precisam de uma entrada por key frame");
        }
        const int64_t length = Field(entry, "length");
        xml.Open("Color")
            .Attr("Segment", segment)
            .Attr("Pass", pass)
            .Attr("Type", type == "color" ? 2 : type == "colorLerp" ? 3 : 4)
            .Attr("Length", length);
        for (size_t i = 0; i < keyFrames.size(); ++i) {
            if (!prim[i].is_array() || prim[i].size() < 5 || (!env.empty() && (!env[i].is_array() || env[i].size() < 4))) {
                throw EntryError("cor do key frame " + std::to_string(i) + " com menos componentes");
            }
            xml.Leaf("KeyFrame")
                .Attr("Frame", U16(ToInt(keyFrames[i])))
                .Attr("PrimR", U8(ToInt(prim[i][0])))
                .Attr("PrimG", U8(ToInt(prim[i][1])))
                .Attr("PrimB", U8(ToInt(prim[i][2])))
                .Attr("PrimA", U8(ToInt(prim[i][3])))
                .Attr("LodFrac", U8(ToInt(prim[i][4])));
            if (!env.empty()) {
                xml.Attr("EnvR", U8(ToInt(env[i][0])))
                    .Attr("EnvG", U8(ToInt(env[i][1])))
                    .Attr("EnvB", U8(ToInt(env[i][2])))
                    .Attr("EnvA", U8(ToInt(env[i][3])));
            }
            xml.Close();
        }
        xml.Close();
        return;
    }
    if (type == "texCycle") {
        xml.Open("TexCycle").Attr("Segment", segment).Attr("Pass", pass);
        for (const auto& texture : SubArray(entry, "textures")) {
            if (!texture.is_string()) {
                throw EntryError("textures tem um valor que não é caminho");
            }
            xml.Leaf("Texture").Attr("Path", texture.get<std::string>()).Close();
        }
        for (const auto& frame : SubArray(entry, "frames")) {
            xml.Leaf("Frame").Attr("Index", ToInt(frame, -1)).Close();
        }
        xml.Close();
        return;
    }
    throw EntryError("type '" + type + "' não é um tipo de material animado");
}

void MaterialAnims(Xml& xml, const Json& list, TranscodeContext& context) {
    // Uma entrada ruim é descartada com erro e o resto do documento carrega (§4.2). Gera cada entrada
    // num XML à parte para não deixar uma entrada pela metade no documento.
    std::string entries;
    for (const auto& key : PositionalKeys(list, Where(context, "materialAnims"))) {
        try {
            Xml one;
            MaterialAnim(one, list[key]);
            entries += one.Finish();
        } catch (const EntryError& error) {
            context.notes.push_back(Where(context, "materialAnims/" + key) + ": " + error.what() +
                                    "; entrada descartada");
        }
    }
    xml.Open("SetAnimatedMaterialList");
    xml.Raw(entries);
    xml.Close();
}

int32_t ResolveExit(const Json& value, const std::string& key, TranscodeContext& context) {
    if (value.is_number_integer() && !value.is_number_float()) {
        const int64_t index = ToInt(value, -1);
        if (index >= 0) {
            return U16(index);
        }
    } else if (value.is_string()) {
        const std::string name = value.get<std::string>();
        const int32_t index = context.resolveEntrance ? context.resolveEntrance(name) : -1;
        if (index >= 0) {
            return U16(index);
        }
        int64_t parsed = 0;
        if (ParseIntString(name, parsed) && parsed >= 0) {
            return U16(parsed);
        }
        throw DocumentError(Where(context, "exits/" + key) + ": entrada desconhecida '" + name + "'");
    }
    throw DocumentError(Where(context, "exits/" + key) + ": uma saída é um índice ou um nome de entrada");
}

void Mesh(Xml& xml, const Json& mesh, TranscodeContext& context) {
    const int64_t type = Field(mesh, "type");
    if (type != 0 && type != 1 && type != 2) {
        throw DocumentError(Where(context, "mesh") + ": type " + std::to_string(type) + " não é 0, 1 nem 2");
    }
    xml.Open("SetMesh").Attr("Data", 0).Attr("MeshHeaderType", type);
    if (type == 1) {
        const int64_t format = Field(mesh, "format", 1);
        if (format != 1 && format != 2) {
            throw DocumentError(Where(context, "mesh") + ": format " + std::to_string(format) + " não é 1 nem 2");
        }
        std::vector<const Json*> images;
        const Json& images2 = Sub(mesh, "images");
        std::vector<std::string> keys;
        if (format == 1) {
            images.push_back(&Sub(mesh, "image"));
        } else {
            keys = PositionalKeys(images2, Where(context, "mesh/images"));
            if (keys.size() > 255) {
                throw DocumentError(Where(context, "mesh") + ": " + std::to_string(keys.size()) +
                                    " imagens (no máximo 255)");
            }
            for (const auto& key : keys) {
                images.push_back(&images2[key]);
            }
        }
        xml.Open("Polygon")
            .Attr("PolyType", 1)
            .Attr("Format", format)
            .Attr("BgImageCount", static_cast<uint32_t>(images.size()))
            .Attr("MeshOpa", PathField(mesh, "opa"))
            .Attr("MeshXlu", PathField(mesh, "xlu"));
        for (const Json* image : images) {
            xml.Leaf("BgImage")
                .Attr("Unknown_00", U16(Field(*image, "unk00")))
                .Attr("Id", U8(Field(*image, "id")))
                .Attr("ImagePath", PathField(*image, "source"))
                .Attr("Unknown_0C", S32(Field(*image, "unk0C")))
                .Attr("TLUT", S32(Field(*image, "tlut")))
                .Attr("Width", U16(Field(*image, "width")))
                .Attr("Height", U16(Field(*image, "height")))
                .Attr("Fmt", U8(Field(*image, "fmt")))
                .Attr("Siz", U8(Field(*image, "siz")))
                .Attr("Mode0", U16(Field(*image, "mode0")))
                .Attr("TLUTCount", U16(Field(*image, "tlutCount")))
                .Close();
        }
        xml.Close().Close();
        return;
    }
    const Json& entries = Sub(mesh, "entries");
    const auto keys = PositionalKeys(entries, Where(context, "mesh/entries"));
    xml.Attr("PolyNum", static_cast<uint32_t>(keys.size()));
    for (const auto& key : keys) {
        const Json& entry = entries[key];
        xml.Leaf("Polygon").Attr("PolyType", type);
        if (type == 2) {
            const Vec3 pos = ReadVec3(SubArray(entry, "pos"));
            xml.Float("PosX", pos.x).Float("PosY", pos.y).Float("PosZ", pos.z).Float("Unknown",
                                                                                  NumberField(entry, "radius"));
        }
        xml.Attr("MeshOpa", PathField(entry, "opa")).Attr("MeshXlu", PathField(entry, "xlu")).Close();
    }
    xml.Close();
}

void Setup(Xml& xml, const Json& setup, const Shared& shared, TranscodeContext& context) {
    const auto has = [&](const char* key) { return setup.contains(key) && !setup[key].is_null(); };
    if (has("specialObjects")) {
        const Json& s = setup["specialObjects"];
        xml.Leaf("SetSpecialObjects")
            .Attr("ElfMessage", S8(Field(s, "elfMessage")))
            .Attr("GlobalObject", S16(Field(s, "globalObject")))
            .Close();
    }
    if (!shared.collision.empty()) {
        xml.Leaf("SetCollisionHeader").Attr("FileName", shared.collision).Close();
    }
    if (shared.rooms.is_object() && !shared.rooms.empty()) {
        xml.Open("SetRoomList");
        for (const auto& key : PositionalKeys(shared.rooms, Where(context, "rooms"))) {
            const Json& room = shared.rooms[key];
            xml.Leaf("RoomEntry")
                .Attr("Path", room.is_string() ? room.get<std::string>() : std::string())
                .Attr("VromStart", 0)
                .Attr("VromEnd", 0)
                .Close();
        }
        xml.Close();
    }
    if (has("behavior")) {
        const Json& s = setup["behavior"];
        xml.Leaf("SetRoomBehavior")
            .Attr("GameplayFlags1", S8(Field(s, "gameplayFlags")))
            .Attr("GameplayFlags2", S32(Field(s, "gameplayFlags2")))
            .Close();
    }
    if (has("echo")) {
        xml.Leaf("SetEchoSettings").Attr("Echo", S8(ToInt(setup["echo"]))).Close();
    }
    if (has("time")) {
        const Json& s = setup["time"];
        xml.Leaf("SetTimeSettings")
            .Attr("Hour", U8(Field(s, "hour", 255)))
            .Attr("Minute", U8(Field(s, "minute", 255)))
            .Attr("TimeIncrement", U8(Field(s, "increment", 255)))
            .Close();
    }
    if (has("wind")) {
        const Json& s = setup["wind"];
        xml.Leaf("SetWind")
            .Attr("WindWest", S8(Field(s, "west")))
            .Attr("WindVertical", S8(Field(s, "vertical")))
            .Attr("WindSouth", S8(Field(s, "south")))
            .Attr("WindSpeed", U8(Field(s, "speed")))
            .Close();
    }
    if (has("skyboxModifier")) {
        const Json& s = setup["skyboxModifier"];
        xml.Leaf("SetSkyboxModifier")
            .Attr("SkyboxDisabled", U8(Field(s, "skyboxDisabled")))
            .Attr("SunMoonDisabled", U8(Field(s, "sunMoonDisabled")))
            .Close();
    }
    if (has("skybox")) {
        const Json& s = setup["skybox"];
        xml.Leaf("SetSkyboxSettings")
            .Attr("Unknown", U8(Field(s, "unk")))
            .Attr("SkyboxId", U8(Field(s, "id")))
            .Attr("Weather", U8(Field(s, "weather")))
            .Attr("Indoors", U8(Field(s, "indoors")))
            .Close();
    }
    if (has("sound")) {
        const Json& s = setup["sound"];
        xml.Leaf("SetSoundSettings")
            .Attr("Reverb", U8(Field(s, "reverb")))
            .Attr("NatureAmbienceId", U8(Field(s, "natureAmbience")))
            .Attr("SeqId", U8(Field(s, "seq")));
        const std::string song = PathField(s, "song");
        if (!song.empty()) {
            xml.Attr("Song", song);
        }
        xml.Close();
    }
    if (has("cameraSettings")) {
        const Json& s = setup["cameraSettings"];
        xml.Leaf("SetCameraSettings")
            .Attr("CameraMovement", S8(Field(s, "cameraMovement")))
            .Attr("WorldMapArea", S32(Field(s, "worldMapArea")))
            .Close();
    }
    if (has("lighting")) {
        Lighting(xml, setup["lighting"], context);
    }
    if (has("materialAnims")) {
        MaterialAnims(xml, setup["materialAnims"], context);
    }
    if (has("paths") && setup["paths"].is_array()) {
        xml.Open("SetPathways");
        for (const auto& file : setup["paths"]) {
            if (file.is_string()) {
                xml.Leaf("Pathway").Attr("FilePath", file.get<std::string>()).Close();
            }
        }
        xml.Close();
    }
    if (has("entrances")) {
        const Json& list = setup["entrances"];
        xml.Open("SetEntranceList");
        for (const auto& key : PositionalKeys(list, Where(context, "entrances"))) {
            xml.Leaf("EntranceEntry")
                .Attr("Spawn", U8(Field(list[key], "spawn")))
                .Attr("Room", S16(Field(list[key], "room")))
                .Close();
        }
        xml.Close();
    }
    if (has("spawns")) {
        const Json& list = setup["spawns"];
        xml.Open("SetStartPositionList");
        for (const auto& key : PositionalKeys(list, Where(context, "spawns"))) {
            xml.Leaf("StartPositionEntry");
            ActorAttrs(xml, list[key]);
            xml.Close();
        }
        xml.Close();
    }
    if (has("transitionActors")) {
        const Json& list = setup["transitionActors"];
        xml.Open("SetTransitionActorList");
        for (const auto& key : PositionalKeys(list, Where(context, "transitionActors"))) {
            const Json& t = list[key];
            const Vec3 pos = ReadVec3(SubArray(t, "pos"));
            xml.Leaf("TransitionActorEntry")
                .Attr("FrontSideRoom", S16(Field(Sub(t, "front"), "room")))
                .Attr("FrontSideEffects", S8(Field(Sub(t, "front"), "effects")))
                .Attr("BackSideRoom", S16(Field(Sub(t, "back"), "room")))
                .Attr("BackSideEffects", S8(Field(Sub(t, "back"), "effects")))
                .Attr("Id", S16(Field(t, "id")))
                .Float("PosX", pos.x)
                .Float("PosY", pos.y)
                .Float("PosZ", pos.z)
                .Attr("RotY", S16(Field(t, "rotY")))
                .Attr("Params", S16(Field(t, "params")))
                .Close();
        }
        xml.Close();
    }
    if (has("objects")) {
        const Json& list = setup["objects"];
        xml.Open("SetObjectList");
        for (const auto& key : PositionalKeys(list, Where(context, "objects"))) {
            xml.Leaf("ObjectEntry").Attr("Id", S16(ToInt(list[key]))).Close();
        }
        xml.Close();
    }
    if (has("lights")) {
        const Json& list = setup["lights"];
        xml.Open("SetLightList");
        for (const auto& key : PositionalKeys(list, Where(context, "lights"))) {
            const Json& light = list[key];
            const int64_t type = Field(light, "type");
            if (type < 0 || type > 2) {
                throw DocumentError(Where(context, "lights/" + key) + ": type " + std::to_string(type) +
                                    " não é 0, 1 nem 2");
            }
            xml.Leaf("LightInfo").Attr("Type", type);
            if (type == 1) {
                Dir(xml, SubArray(light, "dir"), "DirX", "DirY", "DirZ");
                Rgb(xml, SubArray(light, "color"), "ColorR", "ColorG", "ColorB");
            } else {
                const Vec3 pos = ReadVec3(SubArray(light, "pos"));
                xml.Float("X", pos.x).Float("Y", pos.y).Float("Z", pos.z);
                Rgb(xml, SubArray(light, "color"), "ColorR", "ColorG", "ColorB");
                xml.Attr("DrawGlow", U8(Field(light, "glow"))).Attr("Radius", S16(Field(light, "radius")));
            }
            xml.Close();
        }
        xml.Close();
    }
    if (has("actors")) {
        const Json& list = setup["actors"];
        xml.Open("SetActorList");
        for (const auto& key : ListKeys(list)) {
            if (list[key].is_object()) {
                xml.Leaf("ActorEntry");
                ActorAttrs(xml, list[key]);
                xml.Close();
            }
        }
        xml.Close();
    }
    if (has("exits")) {
        const Json& list = setup["exits"];
        xml.Open("SetExitList");
        for (const auto& key : PositionalKeys(list, Where(context, "exits"))) {
            xml.Leaf("ExitEntry").Attr("Id", S16(ResolveExit(list[key], key, context))).Close();
        }
        xml.Close();
    }
    if (has("mesh")) {
        Mesh(xml, setup["mesh"], context);
    }
    if (has("cutscene") && setup["cutscene"].is_string()) {
        xml.Leaf("SetCutscenes").Attr("FileName", setup["cutscene"].get<std::string>()).Close();
    }
    xml.Leaf("EndMarker").Close();
}

} // namespace

std::string EscapeXml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

std::string TranscodeScene(const Json& doc, bool room, TranscodeContext& context) {
    Shared shared;
    if (!room) {
        shared.rooms = Sub(doc, "rooms");
        shared.collision = PathField(doc, "collision");
    }
    const Json& setups = Sub(doc, "setups");
    if (!setups.contains("0") || !setups["0"].is_object()) {
        throw DocumentError(Where(context, "setups") + ": falta o setup \"0\"");
    }
    int64_t maxSetup = 0;
    std::vector<std::pair<int64_t, std::string>> alternates;
    for (const auto& key : ListKeys(setups)) {
        int64_t index = -1;
        if (!ParseIntString(key, index) || index < 0 || key.find_first_not_of("0123456789") != std::string::npos) {
            continue; // outras chaves são ignoradas (§4.2)
        }
        maxSetup = std::max(maxSetup, index);
        if (index > 0 && setups[key].is_object()) {
            alternates.push_back({ index, key });
        }
    }
    Xml xml;
    // Cena e sala têm a mesma raiz: o loader acha o tipo pelo nome da raiz e o SoH registra a fábrica
    // XML de cena só como "Room" (OTRGlobals.cpp).
    xml.Open("Room").Attr("Version", 0);
    if (maxSetup > 0) {
        // Um <AlternateHeader> por setup 1..N; vazio = o jogo cai no setup 0 (§4.2).
        xml.Open("SetAlternateHeaders");
        for (int64_t index = 1; index <= maxSetup; ++index) {
            xml.Open("AlternateHeader");
            const auto found = std::find_if(alternates.begin(), alternates.end(),
                                            [index](const auto& entry) { return entry.first == index; });
            if (found != alternates.end()) {
                Setup(xml, setups[found->second], shared, context);
            }
            xml.Close();
        }
        xml.Close();
    }
    Setup(xml, setups["0"], shared, context);
    return xml.Finish();
}

std::string TranscodeCollision(const Json& doc, TranscodeContext& context) {
    const Json& bounds = Sub(doc, "bounds");
    const Vec3 min = ReadVec3(SubArray(bounds, "min"));
    const Vec3 max = ReadVec3(SubArray(bounds, "max"));
    const Json& bulk = Sub(doc, "bulk");
    Xml xml;
    xml.Open("CollisionHeader")
        .Attr("Version", 0)
        .Attr("MinBoundsX", Integral(min.x))
        .Attr("MinBoundsY", Integral(min.y))
        .Attr("MinBoundsZ", Integral(min.z))
        .Attr("MaxBoundsX", Integral(max.x))
        .Attr("MaxBoundsY", Integral(max.y))
        .Attr("MaxBoundsZ", Integral(max.z));
    const std::string bulkFile = PathField(bulk, "file");
    if (bulkFile.empty()) {
        throw DocumentError(Where(context, "bulk.file") + ": falta o collision.bin");
    }
    xml.Attr("BulkFile", bulkFile)
        .Attr("BulkVertices", static_cast<uint32_t>(Field(bulk, "vertices")))
        .Attr("BulkPolys", static_cast<uint32_t>(Field(bulk, "polys")));

    const Json& surfaces = Sub(doc, "surfaceTypes");
    const auto surfaceKeys = PositionalKeys(surfaces, Where(context, "surfaceTypes"));
    if (surfaceKeys.size() > 65535) {
        throw DocumentError(Where(context, "surfaceTypes") + ": mais de 65535 tipos de superfície");
    }
    for (const auto& key : surfaceKeys) {
        const Json& s = surfaces[key];
        xml.Leaf("SurfaceType")
            .Attr("Camera", S32(Field(s, "camera")))
            .Attr("Exit", S32(Field(s, "exit")))
            .Attr("LightSetting", S32(Field(s, "lightSetting")))
            .Attr("FloorType", U8(Field(s, "floorType")))
            .Attr("WallFlags", U8(Field(s, "wallFlags")))
            .Attr("WallType", U8(Field(s, "wallType")))
            .Attr("FloorProperty", U8(Field(s, "floorProperty")))
            .Attr("IsSoft", U8(Field(s, "isSoft")))
            .Attr("IsHorseBlocked", U8(Field(s, "isHorseBlocked")))
            .Attr("Material", U8(Field(s, "material")))
            .Attr("FloorEffect", U8(Field(s, "floorEffect")))
            .Attr("Echo", U8(Field(s, "echo")))
            .Attr("CanHookshot", U8(Field(s, "canHookshot")))
            .Attr("ConveyorSpeed", U8(Field(s, "conveyorSpeed")))
            .Attr("ConveyorDirection", U8(Field(s, "conveyorDirection")))
            .Attr("IsWallDamage", U8(Field(s, "isWallDamage")))
            .Close();
    }

    const Json& cameras = Sub(doc, "cameras");
    for (const auto& key : PositionalKeys(cameras, Where(context, "cameras"))) {
        const Json& c = cameras[key];
        const auto index = c.is_object() ? c.find("positionIndex") : c.end();
        const int64_t position = index != c.end() && !index->is_null() ? ToInt(*index, -1) : -1;
        xml.Leaf("CameraData")
            .Attr("SType", U16(Field(c, "sType")))
            .Attr("NumData", S16(Field(c, "count")))
            .Attr("CameraPosDataSeg", S32(position < 0 ? -1 : position))
            .Close();
    }
    // O XML agrupa as posições de câmera de 3 em 3 (posição, rotação, fov); o índice de cada vetor
    // continua o mesmo, e o último grupo é completado com zeros.
    const Json& positions = Sub(doc, "cameraPositions");
    const auto positionKeys = PositionalKeys(positions, Where(context, "cameraPositions"));
    for (size_t i = 0; i < positionKeys.size(); i += 3) {
        int64_t v[9] = {};
        for (size_t j = 0; j < 3 && i + j < positionKeys.size(); ++j) {
            const Json& vector = positions[positionKeys[i + j]];
            if (vector.is_array() && vector.size() >= 3) {
                for (int k = 0; k < 3; ++k) {
                    v[j * 3 + k] = static_cast<int64_t>(std::llround(ToNumber(vector[k])));
                }
            }
        }
        xml.Leaf("CameraPositionData")
            .Attr("PosX", S16(v[0]))
            .Attr("PosY", S16(v[1]))
            .Attr("PosZ", S16(v[2]))
            .Attr("RotX", S16(v[3]))
            .Attr("RotY", S16(v[4]))
            .Attr("RotZ", S16(v[5]))
            .Attr("FOV", S16(v[6]))
            .Attr("JfifID", S16(v[7]))
            .Attr("Unknown", S16(v[8]))
            .Close();
    }

    const Json& water = Sub(doc, "waterBoxes");
    const auto waterKeys = PositionalKeys(water, Where(context, "waterBoxes"));
    if (waterKeys.size() > 65535) {
        throw DocumentError(Where(context, "waterBoxes") + ": " + std::to_string(waterKeys.size()) +
                            " water boxes (no máximo 65535)");
    }
    for (const auto& key : waterKeys) {
        const Json& w = water[key];
        xml.Leaf("WaterBox")
            .Attr("XMin", Integral(NumberField(w, "xMin")))
            .Attr("Ysurface", Integral(NumberField(w, "ySurface")))
            .Attr("ZMin", Integral(NumberField(w, "zMin")))
            .Attr("XLength", Integral(NumberField(w, "xLength")))
            .Attr("ZLength", Integral(NumberField(w, "zLength")))
            .Attr("Camera", S32(Field(w, "camera")))
            .Attr("LightSetting", S32(Field(w, "lightSetting")))
            .Attr("Room", S32(Field(w, "room", -1)))
            .Attr("NotSwimmable", Field(w, "notSwimmable") ? 1 : 0)
            .Close();
    }
    return xml.Finish();
}

std::string TranscodePaths(const Json& doc, TranscodeContext& context) {
    const Json& paths = Sub(doc, "paths");
    Xml xml;
    xml.Open("Path").Attr("Version", 0);
    for (const auto& key : PositionalKeys(paths, Where(context, "paths"))) {
        const Json& points = SubArray(Sub(paths, key.c_str()), "points");
        if (points.size() > 255) {
            context.notes.push_back(Where(context, "paths/" + key) + ": " + std::to_string(points.size()) +
                                    " pontos; cortado em 255 (§9)");
        }
        xml.Open("PathData");
        for (size_t i = 0; i < points.size() && i < 255; ++i) {
            const Vec3 point = ReadVec3(points[i]);
            xml.Leaf("PathPoint").Float("X", point.x).Float("Y", point.y).Float("Z", point.z).Close();
        }
        xml.Close();
    }
    return xml.Finish();
}

} // namespace LinkSpanUnbound
