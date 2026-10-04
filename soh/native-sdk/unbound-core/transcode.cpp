#include "transcode.h"
#include "actor_registry.h"

#include <algorithm>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <set>

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

// O XML do jogo é sempre lido com ponto decimal. O snprintf segue o LC_NUMERIC de quem carregou o mod, que num
// Windows pt-BR escreveria "0,25", então o separador do locale é trocado por ponto aqui.
std::string DecimalText(double value) {
    char text[40];
    std::snprintf(text, sizeof(text), "%.9g", std::isfinite(value) ? value : 0.0);
    std::string number(text);
    const char* point = std::localeconv()->decimal_point;
    if (point != nullptr && *point != '\0' && *point != '.') {
        const size_t at = number.find(point);
        if (at != std::string::npos) {
            number.replace(at, std::strlen(point), ".");
        }
    }
    return number;
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
        return Attr(name, DecimalText(value));
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

// Maior índice de setup transcodificado (os alternativos viram um <AlternateHeader> por índice, vazios no meio).
constexpr int64_t kMaxSetupIndex = 255;

// Tetos do runtime que o XML não carrega (z_scene_otr.cpp e z64object.h): o jogo corta com erro ao entrar na
// sala; aqui a nota sai antes, já no game.ready (UNBOUND-019) para documento de mod.
constexpr size_t kMaxRooms = 32768;      // índice de sala s16
constexpr size_t kMaxRoomActors = 65535; // numSetupActors u16
// OBJECT_EXCHANGE_BANK_MAX (1024) menos as vagas permanentes antes da lista da sala: gameplay_keep, o objeto do Link
// e o keep da cena (Object_Spawn em z_scene.c/z_scene_otr.cpp), mais o cavalo que o host acrescenta à lista nas
// cenas com cavalo.
constexpr size_t kMaxObjects = 1020;
// SHAPE_SORT_MAX (z_room.c): a malha tipo 2 só considera as primeiras entradas, antes do teste de distância.
constexpr size_t kShapeSortMax = 1024;
// BGCHECK_XYZ_ABSMAX. Além dele o jogo apaga os efeitos EffectSs (poeira, faíscas, respingos) em vez de desenhá-los
// (z_effect_soft_sprite.c, |c| > 1 048 576); a colisão continua, e o BgCheck_PosErrorCheck só registra (osSyncPrintf,
// desligado no build).
constexpr double kWorldLimit = 1048576.0;

// Posições além de kWorldLimit numa lista: quantas e a primeira, para uma nota só. Compara o valor que o jogo
// recebe: nas posições, o f32 que o FloatAttribute das fábricas tira do texto do Xml::Float (%.9g); nos bounds, o
// inteiro do Integral. O cast direto do double erra perto do limite: 1048576.063 vira o f32 1048576.125, mas o XML
// leva "1048576.06", que a fábrica lê como 1048576.
struct WorldLimitCheck {
    size_t count = 0;
    std::string first;

    static double FromXml(double value) {
        const std::string text = DecimalText(value);
        float parsed = 0.0f;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec == std::errc::result_out_of_range) {
            // Fora do alcance do f32 o FloatAttribute lê ±inf (estouro) ou ±0 (abaixo do menor subnormal). O valor de
            // `parsed` nesse caso depende da biblioteca (o MSVC grava inf, o libstdc++ deixa 0).
            return std::fabs(value) > 1.0 ? std::copysign(HUGE_VAL, value) : std::copysign(0.0, value);
        }
        return parsed;
    }

    void Add(const std::string& key, const Vec3& pos, bool integral = false) {
        const auto effective = [integral](double value) {
            return integral ? static_cast<double>(Integral(value)) : FromXml(value);
        };
        const double x = effective(pos.x);
        const double y = effective(pos.y);
        const double z = effective(pos.z);
        if (std::fabs(x) <= kWorldLimit && std::fabs(y) <= kWorldLimit && std::fabs(z) <= kWorldLimit) {
            return;
        }
        if (count++ == 0) {
            // O exemplo mostra o valor recebido; o infinito (que o DecimalText escreveria como 0) mostra o do JSON.
            const auto shown = [](double received, double written) {
                return DecimalText(std::isfinite(received) ? received : written);
            };
            first = key + "=" + shown(x, pos.x) + "," + shown(y, pos.y) + "," + shown(z, pos.z);
        }
    }

    void Note(TranscodeContext& context, const std::string& where) const {
        if (count) {
            context.notes.push_back(where + ": " + std::to_string(count) +
                                    " posição(ões) além de ±1048576 (ex.: " + first +
                                    "); lá o jogo apaga os efeitos de partícula (EffectSs) em vez de desenhá-los");
        }
    }
};

std::string Where(const TranscodeContext& context, const std::string& what) {
    return context.path + " " + what;
}

// Itens de uma lista que o jogo lê fora do lugar: quantos e o primeiro, para uma nota só.
struct ListNote {
    size_t count = 0;
    std::string first;

    void Add(const std::string& example) {
        if (count++ == 0) {
            first = example;
        }
    }

    void Note(TranscodeContext& context, const std::string& where, const std::string& what) const {
        if (count) {
            context.notes.push_back(where + ": " + std::to_string(count) + " " + what + " (ex.: " + first + ")");
        }
    }
};

// Estado da câmera do jogo (camDataIdx, nextCamDataIdx em z64camera.h) é s16.
constexpr int64_t kMaxCameraState = 32767;

using References = std::vector<std::pair<std::string, std::string>>;

void Reference(References& references, const char* field, const std::string& path) {
    if (!path.empty()) {
        references.emplace_back(field, path);
    }
}

// Faixas do `sound` (SPEC §4.2, Unbound 0.8). O motor indexa tabelas com esses bytes sem conferir, e o 255
// que exportadores usavam como "nenhum" derrubava a thread de áudio no primeiro pôr do sol. Aqui o §2 não
// embrulha: fora da faixa vira o "nenhum" do próprio jogo, com nota, e o resto do `sound` continua valendo.
constexpr int64_t kLastVanillaSeq = 0x6D; // NA_BGM_VARIOUS_SFX
constexpr int64_t kNoMusicSeq = 0x7F;     // NA_BGM_NO_MUSIC
constexpr int64_t kNoNatureAmbience = 0x13; // NATURE_ID_NONE, último da tabela de 20

// Saídas (SPEC §4.2, Unbound 0.8). Só vale como número uma entrada com o mesmo número para todo jogador: as
// vanilla abaixo de ENTR_MAX e as de retorno dinâmico (z64scene.h, ReturnEntranceIndex), que o z_player.c
// resolve antes de indexar a tabela. O intervalo entre elas é onde o jogo numera as entradas de mod, e o
// número de cada uma depende dos mods montados: essas só pelo nome.
constexpr int64_t kVanillaEntranceCount = 1556; // ENTR_MAX do SoH 9.2.3
constexpr int64_t kFirstReturnEntrance = 0x7FF9;
const char* const kReturnEntranceNames[] = {
    "ENTR_RETURN_YOUSEI_IZUMI_YOKO", "ENTR_RETURN_SYATEKIJYOU",      "ENTR_RETURN_2",      "ENTR_RETURN_SHOP1",
    "ENTR_RETURN_4",                 "ENTR_RETURN_DAIYOUSEI_IZUMI", "ENTR_RETURN_GROTTO",
};
constexpr int64_t kLastReturnEntrance = kFirstReturnEntrance + std::size(kReturnEntranceNames) - 1;

bool IsStableEntranceIndex(int64_t index) {
    return index < kVanillaEntranceCount || (index >= kFirstReturnEntrance && index <= kLastReturnEntrance);
}

void Lighting(Xml& xml, const Json& list, TranscodeContext& context) {
    xml.Open("SetLightingSettings");
    const auto items = PositionalItems(list, Where(context, "lighting"));
    if (items.size() > 255) {
        context.notes.push_back(Where(context, "lighting") + ": " + std::to_string(items.size()) +
                                " entradas; o índice de luz é um byte, cortado em 255 (§9)");
    }
    for (size_t i = 0; i < items.size() && i < 255; ++i) {
        const Json& entry = *items[i].second;
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

void MaterialAnim(Xml& xml, const Json& entry, References& references) {
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
            if ((layer.contains("xSpeed") && !layer["xSpeed"].is_number()) ||
                (layer.contains("ySpeed") && !layer["ySpeed"].is_number())) {
                throw EntryError("xSpeed e ySpeed precisam ser números");
            }
            xml.Leaf("Layer")
                .Attr("XStep", S8(Field(layer, "xStep")))
                .Attr("YStep", S8(Field(layer, "yStep")))
                .Float("XSpeed", ToNumber(layer.value("xSpeed", Json(0.0))))
                .Float("YSpeed", ToNumber(layer.value("ySpeed", Json(0.0))))
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
            Reference(references, "materialAnims.textures", texture.get<std::string>());
        }
        // As mesmas regras do AddCycle da fábrica: entrada que ela descartaria sai descartada aqui, sem referência.
        const size_t textureCount = SubArray(entry, "textures").size();
        if (textureCount > 65536) {
            throw EntryError("mais de 65536 texturas");
        }
        const Json& frames = SubArray(entry, "frames");
        if (frames.empty() || frames.size() > 65535) {
            throw EntryError("frames precisa de 1 a 65535 entradas");
        }
        for (const auto& frame : frames) {
            const int64_t index = ToInt(frame, -1);
            if (index < 0 || index >= static_cast<int64_t>(textureCount)) {
                throw EntryError("frame " + std::to_string(index) + " não é índice de textures");
            }
            xml.Leaf("Frame").Attr("Index", index).Close();
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
    for (const auto& [key, entry] : PositionalItems(list, Where(context, "materialAnims"))) {
        try {
            Xml one;
            References references;
            MaterialAnim(one, *entry, references);
            entries += one.Finish();
            context.references.insert(context.references.end(), references.begin(), references.end());
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
    const std::string where = Where(context, "exits/" + key);
    const auto fromNumber = [&](int64_t index) {
        if (!IsStableEntranceIndex(index)) {
            throw DocumentError(where + ": a saída " + std::to_string(index) +
                                " está no intervalo que o jogo usa para entradas de mod; use o nome "
                                "\"<cena>/<entrada>\"");
        }
        return U16(index);
    };
    if (value.is_number_integer() && !value.is_number_float()) {
        const int64_t index = ToInt(value, -1);
        if (index >= 0) {
            return fromNumber(index);
        }
    } else if (value.is_string()) {
        const std::string name = value.get<std::string>();
        for (size_t i = 0; i < std::size(kReturnEntranceNames); ++i) {
            if (name == kReturnEntranceNames[i]) {
                return U16(kFirstReturnEntrance + static_cast<int64_t>(i));
            }
        }
        const int32_t index = context.resolveEntrance ? context.resolveEntrance(name) : -1;
        if (index >= 0) {
            return U16(index);
        }
        int64_t parsed = 0;
        if (ParseIntString(name, parsed) && parsed >= 0) { // "0x0211" ainda é índice, não nome
            return fromNumber(parsed);
        }
        throw DocumentError(where + ": entrada desconhecida '" + name + "'");
    }
    throw DocumentError(where + ": uma saída é um índice ou um nome de entrada");
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
        if (format == 1) {
            images.push_back(&Sub(mesh, "image"));
        } else {
            const auto items = PositionalItems(images2, Where(context, "mesh/images"));
            if (items.size() > 255) {
                throw DocumentError(Where(context, "mesh") + ": " + std::to_string(items.size()) +
                                    " imagens (no máximo 255)");
            }
            for (const auto& item : items) {
                images.push_back(item.second);
            }
        }
        xml.Open("Polygon")
            .Attr("PolyType", 1)
            .Attr("Format", format)
            .Attr("BgImageCount", static_cast<uint32_t>(images.size()))
            .Attr("MeshOpa", PathField(mesh, "opa"))
            .Attr("MeshXlu", PathField(mesh, "xlu"));
        Reference(context.references, "mesh.opa", PathField(mesh, "opa"));
        Reference(context.references, "mesh.xlu", PathField(mesh, "xlu"));
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
            Reference(context.references, "mesh.image.source", PathField(*image, "source"));
        }
        xml.Close().Close();
        return;
    }
    const Json& entries = Sub(mesh, "entries");
    const auto items = PositionalItems(entries, Where(context, "mesh/entries"));
    if (type == 2 && items.size() > kShapeSortMax) {
        context.notes.push_back(Where(context, "mesh/entries") + ": " + std::to_string(items.size()) +
                                " entradas na malha tipo 2; o jogo só considera as primeiras " +
                                std::to_string(kShapeSortMax) + " (SHAPE_SORT_MAX) e não desenha as demais");
    }
    xml.Attr("PolyNum", static_cast<uint32_t>(items.size()));
    for (const auto& item : items) {
        const Json& entry = *item.second;
        xml.Leaf("Polygon").Attr("PolyType", type);
        if (type == 2) {
            const Vec3 pos = ReadVec3(SubArray(entry, "pos"));
            xml.Float("PosX", pos.x).Float("PosY", pos.y).Float("PosZ", pos.z).Float("Unknown",
                                                                                  NumberField(entry, "radius"));
        }
        xml.Attr("MeshOpa", PathField(entry, "opa")).Attr("MeshXlu", PathField(entry, "xlu")).Close();
        Reference(context.references, "mesh.entries.opa", PathField(entry, "opa"));
        Reference(context.references, "mesh.entries.xlu", PathField(entry, "xlu"));
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
        Reference(context.references, "collision", shared.collision);
    }
    if (shared.rooms.is_object() && !shared.rooms.empty()) {
        xml.Open("SetRoomList");
        const auto rooms = PositionalItems(shared.rooms, Where(context, "rooms"));
        if (rooms.size() > kMaxRooms) {
            // Sala é s16 nas portas, nas saídas e no índice da sala atual: as de cima não são alcançáveis.
            context.notes.push_back(Where(context, "rooms") + ": " + std::to_string(rooms.size()) +
                                    " salas; o índice de sala vai até " + std::to_string(kMaxRooms - 1) +
                                    " (s16), as de cima não são alcançáveis");
        }
        for (const auto& item : rooms) {
            const Json& room = *item.second;
            xml.Leaf("RoomEntry")
                .Attr("Path", room.is_string() ? room.get<std::string>() : std::string())
                .Attr("VromStart", 0)
                .Attr("VromEnd", 0)
                .Close();
            Reference(context.references, "rooms", room.is_string() ? room.get<std::string>() : std::string());
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
        int64_t seq = Field(s, "seq");
        if (seq < 0 || (seq > kLastVanillaSeq && seq != kNoMusicSeq)) {
            context.notes.push_back(Where(context, "sound") + ": seq " + std::to_string(seq) +
                                    " não é uma sequência vanilla; usando sem música (" + std::to_string(kNoMusicSeq) +
                                    ")");
            seq = kNoMusicSeq;
        }
        int64_t nature = Field(s, "natureAmbience");
        if (nature < 0 || nature > kNoNatureAmbience) {
            context.notes.push_back(Where(context, "sound") + ": natureAmbience " + std::to_string(nature) +
                                    " não é uma ambiência; usando nenhuma (" + std::to_string(kNoNatureAmbience) +
                                    ")");
            nature = kNoNatureAmbience;
        }
        xml.Leaf("SetSoundSettings")
            .Attr("Reverb", U8(Field(s, "reverb")))
            .Attr("NatureAmbienceId", U8(nature))
            .Attr("SeqId", U8(seq));
        const std::string song = PathField(s, "song");
        if (!song.empty()) {
            xml.Attr("Song", song);
            Reference(context.references, "sound.song", song);
            if (seq == kNoMusicSeq) {
                // A música toca no lugar do tema, e uma cena sem música nunca pede tema.
                context.notes.push_back(Where(context, "sound") + ": song " + song + " não toca com seq " +
                                        std::to_string(kNoMusicSeq) + " (sem música)");
            }
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
                Reference(context.references, "paths", file.get<std::string>());
            }
        }
        xml.Close();
    }
    if (has("entrances")) {
        const Json& list = setup["entrances"];
        xml.Open("SetEntranceList");
        for (const auto& item : PositionalItems(list, Where(context, "entrances"))) {
            xml.Leaf("EntranceEntry")
                .Attr("Spawn", U8(Field(*item.second, "spawn")))
                .Attr("Room", S16(Field(*item.second, "room")))
                .Close();
        }
        xml.Close();
    }
    if (has("spawns")) {
        const Json& list = setup["spawns"];
        xml.Open("SetStartPositionList");
        WorldLimitCheck outside;
        for (const auto& [key, item] : PositionalItems(list, Where(context, "spawns"))) {
            xml.Leaf("StartPositionEntry");
            ActorAttrs(xml, *item);
            xml.Close();
            outside.Add(key, ReadVec3(SubArray(*item, "pos")));
        }
        outside.Note(context, Where(context, "spawns"));
        xml.Close();
    }
    if (has("transitionActors")) {
        const Json& list = setup["transitionActors"];
        xml.Open("SetTransitionActorList");
        WorldLimitCheck outside;
        for (const auto& [key, item] : PositionalItems(list, Where(context, "transitionActors"))) {
            const Json& t = *item;
            int16_t actorId = -1;
            int64_t parsedId = 0;
            const auto idValue = t.find("id");
            const bool named = idValue != t.end() && idValue->is_string() &&
                               !ParseIntString(idValue->get<std::string>(), parsedId);
            const int64_t numericId = Field(t, "id");
            if (!named && numericId >= 0 && numericId < LINKSPAN_OOT_ACTOR_MODELS_ID_BASE)
                actorId = static_cast<int16_t>(numericId);
            else context.notes.push_back(Where(context, "transitionActors/" + key) +
                                         ": id inválido; entrada preservada sem ator");
            const Vec3 pos = ReadVec3(SubArray(t, "pos"));
            outside.Add(key, pos);
            xml.Leaf("TransitionActorEntry")
                .Attr("FrontSideRoom", S16(Field(Sub(t, "front"), "room")))
                .Attr("FrontSideEffects", S8(Field(Sub(t, "front"), "effects")))
                .Attr("BackSideRoom", S16(Field(Sub(t, "back"), "room")))
                .Attr("BackSideEffects", S8(Field(Sub(t, "back"), "effects")))
                .Attr("Id", actorId)
                .Float("PosX", pos.x)
                .Float("PosY", pos.y)
                .Float("PosZ", pos.z)
                .Attr("RotY", S16(Field(t, "rotY")))
                .Attr("Params", S16(Field(t, "params")))
                .Close();
        }
        outside.Note(context, Where(context, "transitionActors"));
        xml.Close();
    }
    if (has("objects")) {
        const Json& list = setup["objects"];
        xml.Open("SetObjectList");
        const auto objects = PositionalItems(list, Where(context, "objects"));
        if (objects.size() > kMaxObjects) {
            context.notes.push_back(Where(context, "objects") + ": " + std::to_string(objects.size()) +
                                    " objetos; o banco do jogo tem 1024 vagas, até 4 delas com objetos permanentes "
                                    "(gameplay_keep, Link, keep da cena e cavalo), e acima de " +
                                    std::to_string(kMaxObjects) + " os últimos podem ser descartados ao entrar na sala");
        }
        for (const auto& item : objects) {
            xml.Leaf("ObjectEntry").Attr("Id", S16(ToInt(*item.second))).Close();
        }
        xml.Close();
    }
    if (has("lights")) {
        const Json& list = setup["lights"];
        xml.Open("SetLightList");
        for (const auto& [key, item] : PositionalItems(list, Where(context, "lights"))) {
            const Json& light = *item;
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
        size_t written = 0;
        WorldLimitCheck outside;
        for (const auto& [key, item] : ListItems(list)) {
            if (item->is_object()) {
                int16_t actorId = -1;
                if (!ResolveRoomActorId(*item, context.resolveActor, actorId, context.notes,
                                        Where(context, "actors/" + key))) continue;
                xml.Leaf("ActorEntry");
                Json actor = *item;
                actor["id"] = actorId;
                ActorAttrs(xml, actor);
                xml.Close();
                ++written;
                outside.Add(key, ReadVec3(SubArray(*item, "pos")));
            }
        }
        if (written > kMaxRoomActors) {
            context.notes.push_back(Where(context, "actors") + ": " + std::to_string(written) +
                                    " atores; o jogo carrega os primeiros " + std::to_string(kMaxRoomActors));
        }
        outside.Note(context, Where(context, "actors"));
        xml.Close();
    }
    if (has("exits")) {
        const Json& list = setup["exits"];
        xml.Open("SetExitList");
        for (const auto& [key, item] : PositionalItems(list, Where(context, "exits"))) {
            xml.Leaf("ExitEntry").Attr("Id", S16(ResolveExit(*item, key, context))).Close();
        }
        xml.Close();
    }
    if (has("mesh")) {
        Mesh(xml, setup["mesh"], context);
    }
    if (has("cutscene") && setup["cutscene"].is_string()) {
        xml.Leaf("SetCutscenes").Attr("FileName", setup["cutscene"].get<std::string>()).Close();
        Reference(context.references, "cutscene", setup["cutscene"].get<std::string>());
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

// R08/R09 (UNBOUND-028): o minimapa das dungeons vanilla (z_map_exp.c, Map_InitData e Map_SetPaletteData) lê
// textura, bússola e paleta pela sala sem conferir. Tabelas de z_map_data.c: salas com minimapa por dungeon
// (sDgnMinimapCount), início de cada uma na lista de texturas (sDgnMinimapTexIndexOffset; 239 nomes ao todo),
// paleta em linhas de 32 (sRoomPalette[10][32]; o valor lido é índice de escrita em mapPalette, de 32 bytes:
// fora de 0..15, escreve fora), bússola em
// linhas de 44 (sRoomCompassOffsetX/Y[10][44]) e bit de visita só até a sala 31 (Map_InitRoomData). Além das salas
// de uma dungeon, a leitura cai nas dungeons seguintes (as tabelas são contíguas) até acabar a tabela inteira. Os
// chefes fazem as mesmas leituras na entrada, mas não desenham o minimapa, e o desenho depende do mapa da dungeon:
// por isso a nota diz o que o jogo lê, não o que mostra.
struct DungeonMinimap {
    const char* scene;
    size_t index;
    bool masterQuest; // tem variante MQ (scenes/<cena>_mq): só as dez dungeons (SceneHasMasterQuest)
};
constexpr DungeonMinimap kDungeonMinimaps[] = {
    { "ydan", 0, true },       { "ddan", 1, true },        { "bdan", 2, true },      { "Bmori1", 3, true },
    { "HIDAN", 4, true },      { "MIZUsin", 5, true },     { "jyasinzou", 6, true }, { "HAKAdan", 7, true },
    { "HAKAdanCH", 8, true },  { "ice_doukutu", 9, true },
    // O chefe usa a tabela da dungeon dele (mapIndex = cena - SCENE_DEKU_TREE_BOSS).
    { "ydan_boss", 0, false }, { "ddan_boss", 1, false },  { "bdan_boss", 2, false }, { "moribossroom", 3, false },
    { "FIRE_bs", 4, false },   { "MIZUsin_bs", 5, false }, { "jyasinboss", 6, false }, { "HAKAdan_bs", 7, false },
};
constexpr size_t kDungeonCount = 10;
constexpr size_t kMinimapRooms[kDungeonCount] = { 13, 19, 17, 27, 38, 44, 32, 27, 10, 12 };
constexpr size_t kMinimapTexOffset[kDungeonCount] = { 0, 13, 32, 49, 76, 114, 158, 190, 217, 227 };
constexpr size_t kMinimapTexCount = 239;
constexpr size_t kPaletteRow = 32;
constexpr size_t kCompassRow = 44;

// "na sala 13" ou "nas salas 13 a 238", cortado na última sala da cena.
std::string RoomRange(size_t first, size_t end, size_t rooms) {
    const size_t last = std::min(end, rooms) - 1;
    return first == last ? "na sala " + std::to_string(first)
                         : "nas salas " + std::to_string(first) + " a " + std::to_string(last);
}

void DungeonMinimapNote(size_t rooms, TranscodeContext& context) {
    for (const auto& dungeon : kDungeonMinimaps) {
        const std::string base = std::string("scenes/") + dungeon.scene;
        // A variante MQ (scenes/<cena>_mq, como o extrator grava) usa o mesmo id de cena e as mesmas tabelas.
        if (context.path != base + "/scene.json" && !(dungeon.masterQuest && context.path == base + "_mq/scene.json")) {
            continue;
        }
        const size_t index = dungeon.index;
        const size_t own = kMinimapRooms[index];
        if (rooms <= std::min(own, kPaletteRow)) {
            return;
        }
        // Cada tabela: das dungeons seguintes de `start` até `end` (fim da tabela inteira), além dela daí em diante.
        // `next` é onde acaba a dungeon seguinte, para o singular.
        std::vector<std::string> parts;
        auto table = [&](const std::string& what, size_t start, size_t next, size_t end, const std::string& after) {
            std::string part;
            if (start < end && rooms > start) {
                part = what + (std::min(end, rooms) > next ? " das dungeons seguintes " : " da dungeon seguinte ") +
                       RoomRange(start, end, rooms);
            }
            if (rooms > end) {
                part += (part.empty() ? what + " " : std::string(" e ")) + "além da " +
                        (what == "textura" ? "lista de 239 nomes" : "tabela") + " da sala " + std::to_string(end) +
                        " em diante" + after;
            }
            if (!part.empty()) {
                parts.push_back(part);
            }
        };
        const size_t rows = kDungeonCount - index;
        const size_t textureEnd = kMinimapTexCount - kMinimapTexOffset[index];
        const size_t textureNext = index + 1 < kDungeonCount ? own + kMinimapRooms[index + 1] : textureEnd;
        table("textura", own, textureNext, textureEnd, "");
        if (rooms > kPaletteRow) {
            parts.push_back("visita não marcada da sala 32 em diante");
        }
        table("paleta", kPaletteRow, 2 * kPaletteRow, kPaletteRow * rows, " (risco de escrita fora de mapPalette)");
        table("bússola", kCompassRow, 2 * kCompassRow, kCompassRow * rows, "");
        std::string note = Where(context, "rooms") + ": " + std::to_string(rooms) + " salas, e o minimapa de " +
                           dungeon.scene + " tem " + std::to_string(own) +
                           " (z_map_exp.c lê as tabelas pela sala sem conferir): ";
        for (size_t i = 0; i < parts.size(); i++) {
            note += (i ? "; " : "") + parts[i];
        }
        context.notes.push_back(note);
        return;
    }
}

// R08 (UNBOUND-028): o jogo guarda a área do mapa-múndi em s16 (gSaveContext.worldMapArea). 0..21 são áreas e 22 é
// "fora do mapa" (fontes de fada e grutas vanilla). Negativo passa nas guardas < 22 da pausa (z_kaleido_map_PAL.c)
// e lê textura e moldura antes das tabelas; acima de 22 lê além delas. Numa cena de exterior vanilla, o valor também
// indexa gBitFlags na entrada (z_scene_otr.cpp) e marca worldMapAreaData no save. O 22 não sai em nota, mas o host
// também o lê além das quatro tabelas de 22 da moldura (func_80823A0C, sem guarda): é do vanilla e vai para a guarda
// do host.
void WorldMapAreaNote(const Json& setups, const std::vector<std::pair<int64_t, std::string>>& alternates,
                      TranscodeContext& context) {
    ListNote outside;
    // O 0 e o primeiro alias de cada índice alternativo, como o <AlternateHeader> emitido.
    std::vector<std::string> keys{ "0" };
    std::set<int64_t> emitted;
    for (const auto& [index, key] : alternates) {
        if (emitted.insert(index).second) {
            keys.push_back(key);
        }
    }
    for (const auto& key : keys) {
        const Json& setup = setups[key];
        if (!setup.is_object() || !setup.contains("cameraSettings")) {
            continue;
        }
        const int64_t value = Field(setup["cameraSettings"], "worldMapArea");
        const int16_t stored = static_cast<int16_t>(S32(value));
        if (stored < 0 || stored > 22) {
            outside.Add("setups." + key + "=" + std::to_string(value));
        }
    }
    outside.Note(context, Where(context, "cameraSettings.worldMapArea"),
                 "setup(s) fora de 0..22 (o jogo guarda s16); na pausa, o mapa-múndi lê as tabelas de área fora "
                 "(negativo passa nas guardas < 22), e numa cena de exterior vanilla o valor indexa gBitFlags e "
                 "marca worldMapAreaData no save");
}

std::string TranscodeScene(const Json& doc, bool room, TranscodeContext& context) {
    Shared shared;
    if (!room) {
        shared.rooms = Sub(doc, "rooms");
        shared.collision = PathField(doc, "collision");
        // Só objeto vira lista de salas (como no <RoomEntry> emitido).
        if (shared.rooms.is_object()) {
            DungeonMinimapNote(PositionalItems(shared.rooms, Where(context, "rooms")).size(), context);
        }
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
        if (index > kMaxSetupIndex) {
            // Um <AlternateHeader> por índice até o maior: um setup "1000000000" num JSON de 60 bytes viraria
            // gigabytes de XML. O jogo nunca pede cabeçalho tão alto (sceneLayer de cutscene vai até 19).
            context.notes.push_back(Where(context, "setups/" + key) + ": índice acima de " +
                                    std::to_string(kMaxSetupIndex) + " ignorado");
            continue;
        }
        maxSetup = std::max(maxSetup, index);
        if (index > 0 && setups[key].is_object()) {
            alternates.push_back({ index, key });
        }
    }
    WorldMapAreaNote(setups, alternates, context);
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
    WorldLimitCheck outside;
    outside.Add("min", min, true);
    outside.Add("max", max, true);
    outside.Note(context, Where(context, "bounds"));
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
    Reference(context.references, "bulk.file", bulkFile);
    xml.Attr("BulkFile", bulkFile)
        .Attr("BulkVertices", static_cast<uint32_t>(Field(bulk, "vertices")))
        .Attr("BulkPolys", static_cast<uint32_t>(Field(bulk, "polys")));

    // Câmera de superfície e de água é índice da tabela `cameras`; o jogo não confere o tamanho
    // (BgCheck_GetBgCamSettingImpl e WaterBox_GetCameraSType, z_bgcheck.c) e lê fora dela. A da superfície é lida
    // sempre, até -1; a da água, só positiva (Camera_GetWaterBoxDataIdx, z_camera.c, trata <= 0 como sem câmera).
    const Json& cameras = Sub(doc, "cameras");
    const auto cameraItems = PositionalItems(cameras, Where(context, "cameras"));
    const auto cameraCount = static_cast<int64_t>(cameraItems.size());
    const auto checkCamera = [cameraCount](ListNote& outside, ListNote& wide, const std::string& key, int64_t camera,
                                           bool noneUpToZero) {
        if (noneUpToZero && camera <= 0) {
            return;
        }
        if (camera < 0 || camera >= cameraCount) {
            outside.Add(key + ".camera=" + std::to_string(camera));
        } else if (camera > kMaxCameraState) {
            wide.Add(key + ".camera=" + std::to_string(camera));
        }
    };
    const std::string outsideText =
        "câmera(s) fora da tabela de " + std::to_string(cameraCount) + "; lá o jogo lê fora da lista de câmeras";
    const std::string wideText = "câmera(s) acima de 32767; o estado da câmera do jogo é s16";

    const Json& surfaces = Sub(doc, "surfaceTypes");
    const auto surfaceItems = PositionalItems(surfaces, Where(context, "surfaceTypes"));
    if (surfaceItems.size() > 65535) {
        throw DocumentError(Where(context, "surfaceTypes") + ": mais de 65535 tipos de superfície");
    }
    ListNote surfaceOutside;
    ListNote surfaceWide;
    for (const auto& item : surfaceItems) {
        const Json& s = *item.second;
        checkCamera(surfaceOutside, surfaceWide, item.first, Field(s, "camera"), false);
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
    surfaceOutside.Note(context, Where(context, "surfaceTypes"), outsideText);
    surfaceWide.Note(context, Where(context, "surfaceTypes"), wideText);

    // As posições de uma câmera são um BgCamFuncData (posição, rotação e fov: três vetores) ou, no crawlspace,
    // `count` pontos: o jogo lê max(count, 3) vetores a partir de positionIndex, conforme o preset. A fábrica confere
    // só o início; fora dele, ou sem positionIndex, aponta para um único vetor zero e o jogo lê o que vem depois.
    // `count` é guardado em s16 e lido como u16 (BgCheck_GetBgCamCount).
    const auto positionCount = static_cast<int64_t>(PositionalItems(Sub(doc, "cameraPositions"),
                                                                    Where(context, "cameraPositions")).size());
    ListNote positionOutside;
    ListNote positionMissing;
    ListNote countRange;
    for (const auto& item : cameraItems) {
        const Json& c = *item.second;
        const auto index = c.is_object() ? c.find("positionIndex") : c.end();
        const int64_t position = index != c.end() && !index->is_null() ? ToInt(*index, -1) : -1;
        const int64_t count = Field(c, "count");
        const std::string example =
            item.first + ": positionIndex=" + (position < 0 ? "null" : std::to_string(position)) +
            " count=" + std::to_string(count);
        if (count < 0 || count > INT16_MAX) {
            countRange.Add(example);
        }
        // Por subtração: positionIndex + count pode transbordar.
        if (position >= 0 && (position >= positionCount || std::max<int64_t>(count, 3) > positionCount - position)) {
            positionOutside.Add(example);
        } else if (position < 0 && count > 0) {
            positionMissing.Add(example);
        }
        xml.Leaf("CameraData")
            .Attr("SType", U16(Field(c, "sType")))
            .Attr("NumData", S16(Field(c, "count")))
            .Attr("CameraPosDataSeg", S32(position < 0 ? -1 : position))
            .Close();
    }
    positionOutside.Note(context, Where(context, "cameras"),
                         "câmera(s) com posições além das " + std::to_string(positionCount) +
                             " de cameraPositions (o jogo lê max(count, 3)); lá ele usa a posição zero ou lê fora "
                             "da lista");
    positionMissing.Note(context, Where(context, "cameras"),
                         "câmera(s) com count sem positionIndex; lá o jogo lê a partir de um único vetor zero");
    countRange.Note(context, Where(context, "cameras"),
                    "câmera(s) com count fora de 0..32767; o jogo guarda s16 e lê como u16");
    // O XML agrupa as posições de câmera de 3 em 3 (posição, rotação, fov); o índice de cada vetor
    // continua o mesmo, e o último grupo é completado com zeros.
    const Json& positions = Sub(doc, "cameraPositions");
    const auto positionItems = PositionalItems(positions, Where(context, "cameraPositions"));
    size_t wrapped = 0;
    std::string firstWrapped;
    for (size_t i = 0; i < positionItems.size(); i += 3) {
        int64_t v[9] = {};
        for (size_t j = 0; j < 3 && i + j < positionItems.size(); ++j) {
            const Json& vector = *positionItems[i + j].second;
            if (vector.is_array() && vector.size() >= 3) {
                for (int k = 0; k < 3; ++k) {
                    v[j * 3 + k] = static_cast<int64_t>(std::llround(ToNumber(vector[k])));
                    if (v[j * 3 + k] < INT16_MIN || v[j * 3 + k] > INT16_MAX) {
                        if (wrapped++ == 0) {
                            firstWrapped = positionItems[i + j].first + "[" + std::to_string(k) + "]=" +
                                           std::to_string(v[j * 3 + k]);
                        }
                    }
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
    if (wrapped) {
        // A câmera fixa guarda Vec3s: o §2 embrulha o valor, aqui pelo menos o autor fica sabendo.
        context.notes.push_back(Where(context, "cameraPositions") + ": " + std::to_string(wrapped) +
                                " valor(es) fora de -32768..32767 embrulhados (ex.: " + firstWrapped + ")");
    }

    const Json& water = Sub(doc, "waterBoxes");
    const auto waterItems = PositionalItems(water, Where(context, "waterBoxes"));
    if (waterItems.size() > 65535) {
        throw DocumentError(Where(context, "waterBoxes") + ": " + std::to_string(waterItems.size()) +
                            " water boxes (no máximo 65535)");
    }
    ListNote waterOutside;
    ListNote waterWide;
    for (const auto& item : waterItems) {
        const Json& w = *item.second;
        checkCamera(waterOutside, waterWide, item.first, Field(w, "camera"), true);
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
    waterOutside.Note(context, Where(context, "waterBoxes"), outsideText);
    waterWide.Note(context, Where(context, "waterBoxes"), wideText);
    return xml.Finish();
}

std::string TranscodePaths(const Json& doc, TranscodeContext& context) {
    const Json& paths = Sub(doc, "paths");
    Xml xml;
    xml.Open("Path").Attr("Version", 0);
    for (const auto& [key, path] : PositionalItems(paths, Where(context, "paths"))) {
        const Json& points = SubArray(*path, "points");
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
