// Fronteiras da matriz de limites do Prelude (plano §10.3/§14.3, UNBOUND-020): para cada teto que passa pelo
// framework, os casos limite-1, limite e limite+1, com o que o XML leva e a nota ou recusa que sai.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "room_actors.h"
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

size_t Count(const std::string& text, const std::string& needle) {
    size_t count = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

bool AnyNote(const TranscodeContext& context, const std::string& needle) {
    for (const auto& note : context.notes) {
        if (note.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

TranscodeContext Context() {
    TranscodeContext context;
    context.path = "limites";
    context.resolveEntrance = [](const std::string&) { return -1; };
    context.resolveActor = [](const std::string&) { return -1; };
    return context;
}

// Lista posicional {"0": item, ..., "n-1": item} como texto: montar por Json custaria o mesmo que o parse.
std::string PositionalText(size_t n, const std::string& item) {
    std::string text = "{";
    for (size_t i = 0; i < n; ++i) {
        text += (i ? ",\"" : "\"") + std::to_string(i) + "\":" + item;
    }
    return text + "}";
}

double MillisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// R01: 255 pontos por path (Path.count é u8).
void TestPathPoints() {
    for (const size_t n : { 254u, 255u, 256u }) {
        std::string points = "[";
        for (size_t i = 0; i < n; ++i) {
            points += (i ? ",[" : "[") + std::to_string(i) + ".5,1.25,-0.5]";
        }
        points += "]";
        TranscodeContext context = Context();
        const std::string xml = TranscodePaths(Json::parse(R"({"paths":{"0":{"points":)" + points + "}}}"), context);
        CHECK(Count(xml, "<PathPoint ") == std::min<size_t>(n, 255));
        CHECK(context.notes.size() == (n > 255 ? 1u : 0u));
        CHECK(n <= 255 || AnyNote(context, "256 pontos; cortado em 255"));
        // M03: a fração chega ao XML (ponto 254 é o último que cabe).
        CHECK(xml.find("X=\"254.5\" Y=\"1.25\" Z=\"-0.5\"") != std::string::npos || n < 255);
    }
}

// R05: 255 light settings por cena (envCtx.numLightSettings é u8).
void TestLightSettings() {
    for (const size_t n : { 254u, 255u, 256u }) {
        const std::string doc = R"({"setups":{"0":{"lighting":)" + PositionalText(n, R"({"ambient":[1,2,3]})") + "}}}";
        TranscodeContext context = Context();
        const std::string xml = TranscodeScene(Json::parse(doc), false, context);
        CHECK(Count(xml, "<LightingSetting ") == std::min<size_t>(n, 255));
        CHECK(AnyNote(context, "256 entradas") == (n > 255));
    }
}

// M23: mensagem de 8 192 bytes com o 0x02 do jogo.
void TestMessageBytes() {
    for (const size_t n : { 8190u, 8191u, 8192u, 9000u }) {
        TextTable table;
        CHECK(BuildTextTable({ { "m", R"({"messages":{"1":{"text":")" + std::string(n, 'a') + R"("}}})", 0 } }, table));
        CHECK(table.messages.size() == 1 && table.messages[0].bytes.size() == std::min<size_t>(n, kMaxMessageBytes));
        bool truncated = false;
        for (const auto& note : table.notes) {
            truncated = truncated || note.find("truncada em 8192") != std::string::npos;
        }
        CHECK(truncated == (n > kMaxMessageBytes));
    }
}

std::string CollisionText(size_t surfaces, size_t water) {
    return R"({"bounds":{"min":[0,0,0],"max":[1,1,1]},"bulk":{"file":"limites/vazio.bin","vertices":0,"polys":0},)"
           R"("surfaceTypes":)" + PositionalText(surfaces, "{}") + R"(,"waterBoxes":)" + PositionalText(water, "{}") +
           "}";
}

// R06: 65 535 surface types e water boxes por cena (índices u16); acima disso o documento é recusado.
void TestCollisionCounts() {
    for (const size_t n : { 65534u, 65535u, 65536u }) {
        for (const bool surfaces : { true, false }) {
            const auto start = std::chrono::steady_clock::now();
            const Json doc = ParseJson(surfaces ? CollisionText(n, 0) : CollisionText(1, n));
            const double parsed = MillisecondsSince(start);
            TranscodeContext context = Context();
            std::string xml;
            std::string error;
            try {
                xml = TranscodeCollision(doc, context);
            } catch (const DocumentError& e) {
                error = e.what();
            }
            std::printf("colisão %s=%zu: parse %.0f ms, total %.0f ms\n", surfaces ? "surfaceTypes" : "waterBoxes", n,
                        parsed, MillisecondsSince(start));
            if (n <= 65535) {
                CHECK(error.empty());
                CHECK(Count(xml, surfaces ? "<SurfaceType " : "<WaterBox ") == n);
            } else {
                CHECK(xml.empty());
                CHECK(error.find(surfaces ? "mais de 65535 tipos de superfície" : "water boxes (no máximo 65535)") !=
                      std::string::npos);
            }
        }
    }
}

// O parse com índice tem de dar o mesmo documento que o do nlohmann, inclusive na ordem das chaves e na chave
// repetida (primeira posição, último valor).
void TestParseEquivalence() {
    const std::vector<std::string> docs = {
        R"({"b":1,"a":[1,2.5,-3,"x",true,false,null],"c":{"z":{},"y":[]}})",
        R"({"k":1,"k":2,"j":0,"k":{"n":3}})",
        "{ // comentário\n \"a\": /* bloco */ 1 }",
        R"({"u":"é😀","big":18446744073709551615,"neg":-9223372036854775808,"f":1e308})",
        "[1,{\"a\":2},[]]",
        "\"solto\"",
        "42",
    };
    for (const auto& text : docs) {
        const Json expected = Json::parse(text, nullptr, false, true);
        const Json actual = ParseJson(text);
        CHECK(!actual.is_discarded() && actual == expected && actual.dump() == expected.dump());
    }
    std::string many = "{";
    for (int i = 0; i < 100; ++i) {
        many += "\"" + std::to_string(i % 40) + "\":" + std::to_string(i) + (i < 99 ? "," : "}");
    }
    CHECK(ParseJson(many).dump() == Json::parse(many).dump());
    for (const char* bad : { "{quebrado", "{\"a\":1} lixo", "", "{\"a\":}" }) {
        CHECK(ParseJson(bad).is_discarded());
    }
    // Sem ignorar comentários, comentário é erro, como no nlohmann.
    CHECK(ParseJson("{/*x*/\"a\":1}", false).is_discarded());
}

// Regra antiga do MergeJson (busca por chave), para comparar com o caminho indexado dos objetos grandes.
void ReferenceMerge(Json& base, const Json& overlay) {
    if (!base.is_object() || !overlay.is_object() ||
        (overlay.contains("$replace") && overlay["$replace"].is_boolean() && overlay["$replace"].get<bool>())) {
        base = overlay;
        return;
    }
    for (const auto& [key, value] : overlay.items()) {
        if (value.is_null()) {
            base.erase(key);
        } else if (value.is_object() && base.contains(key) && base[key].is_object()) {
            ReferenceMerge(base[key], value);
        } else {
            base[key] = value;
        }
    }
}

void TestMergeEquivalence() {
    Json base = Json::object();
    for (int i = 0; i < 40; ++i) {
        base[std::to_string(i)] = i % 3 == 0 ? Json{ { "v", i }, { "w", { { "x", i } } } } : Json(i);
    }
    const Json overlays[] = {
        Json::parse(R"({"0":{"v":null,"n":1},"1":null,"2":{"o":1},"5":null,"novo":7,"3":{"w":{"x":null,"y":2}}})"),
        Json::parse(R"({"10":null,"11":null,"novo":null,"39":{"$replace":true,"z":1}})"),
        Json::parse(R"({"a":1,"b":2})"),
        Json::parse(R"({"$replace":true,"so":1})"),
    };
    for (const auto& overlay : overlays) {
        Json expected = base;
        Json actual = base;
        ReferenceMerge(expected, overlay);
        MergeJson(actual, overlay);
        CHECK(actual.dump() == expected.dump());
    }
}

// Overlay com chave repetida (só montado à mão: o ParseJson junta as repetidas) e base pequena ou vazia com overlay
// grande, que também passam pelo caminho indexado.
void TestMergeEdgeCases() {
    const auto overlayOf = [](const std::vector<std::pair<std::string, Json>>& entries) {
        Json overlay = Json::object();
        for (const auto& [key, value] : entries) {
            overlay.get_ref<Json::object_t&>().emplace_back(key, value);
        }
        return overlay;
    };
    const Json overlays[] = {
        overlayOf({ { "novo", 1 }, { "novo", 2 } }),
        overlayOf({ { "0", nullptr }, { "0", 2 } }),
        overlayOf({ { "0", nullptr }, { "0", Json{ { "v", 2 } } } }),
        overlayOf({ { "0", nullptr }, { "0", nullptr }, { "1", 5 } }),
        overlayOf({ { "novo", Json{ { "a", 1 } } }, { "novo", Json{ { "b", 2 } } }, { "novo", nullptr } }),
        ParseJson(PositionalText(40, R"({"n":1})")),
    };
    const Json starts[] = { ParseJson(PositionalText(17, "1")), Json::object(), ParseJson(R"({"a":1})") };
    for (const auto& overlay : overlays) {
        for (const auto& start : starts) {
            Json expected = start;
            Json actual = start;
            ReferenceMerge(expected, overlay);
            MergeJson(actual, overlay);
            CHECK(actual.dump() == expected.dump());
        }
    }
}

// Custo dos caminhos que a revisão da UNBOUND-020 achou quadráticos: overlay grande sobre base vazia,
// StripDirectives com muitos nulls, paths com valores que não são objeto e atores de sala de mod.
void TestReviewCosts() {
    auto start = std::chrono::steady_clock::now();
    Json merged = Json::object();
    MergeJson(merged, ParseJson(PositionalText(65535, "{}")));
    const double mergeMs = MillisecondsSince(start);
    CHECK(merged.size() == 65535);

    start = std::chrono::steady_clock::now();
    Json nulls = ParseJson(PositionalText(32768, "null"));
    Json mixed = ParseJson(R"({"$replace":true,)" + PositionalText(32768, R"({"a":null,"b":1})").substr(1));
    StripDirectives(nulls);
    StripDirectives(mixed);
    const double stripMs = MillisecondsSince(start);
    CHECK(nulls.empty() && mixed.size() == 32768 && mixed.front().dump() == R"({"b":1})");

    start = std::chrono::steady_clock::now();
    TranscodeContext context = Context();
    std::string pathList = PositionalText(32767, "7");
    pathList.back() = ',';
    pathList += R"("32767":{"points":[[1,2,3]]}})";
    const std::string paths = TranscodePaths(ParseJson(R"({"paths":)" + pathList + "}"), context);
    const double pathsMs = MillisecondsSince(start);
    CHECK(Count(paths, "<PathPoint ") == 1 && paths.find("X=\"1\" Y=\"2\" Z=\"3\"") != std::string::npos);

    start = std::chrono::steady_clock::now();
    RoomActorsResult mod;
    std::string error;
    CHECK(ApplyRoomActorLayers({}, 0,
                               { { "mod",
                                   R"({"$schema":"unbound/room/1","setups":{"0":{"actors":)" +
                                       PositionalText(16384, R"({"id":16})") + "}}}",
                                   0 } },
                               mod, error));
    CHECK(mod.actors.size() == 16384 && error.empty());
    // Lista vanilla grande com camada que apaga, muda, substitui e acrescenta (caminho indexado do MergeLayer).
    std::vector<RoomActor> vanilla(16384);
    for (size_t i = 0; i < vanilla.size(); ++i) {
        vanilla[i].params = static_cast<int16_t>(i);
    }
    RoomActorsResult patched;
    CHECK(ApplyRoomActorLayers(vanilla, 0,
                               { { "mod",
                                   R"({"$schema":"unbound/room/1","setups":{"0":{"actors":{"1":{"id":9},"2":{"params":7},)"
                                   R"("3":{"$replace":true,"id":5},"4":null,"novo":{"id":16,"params":-1}}}}})",
                                   0 } },
                               patched, error));
    CHECK(patched.actors.size() == 16384 && error.empty());
    CHECK(patched.actors.size() > 4 && patched.actors[1].id == 9 && patched.actors[1].params == 1 &&
          patched.actors[2].params == 7 && patched.actors[3].id == 5 && patched.actors[3].params == 0 &&
          patched.actors[4].params == 5 && patched.actors.back().params == -1);
    const double roomMs = MillisecondsSince(start);

    std::printf("revisão: mescla sobre base vazia %.0f ms, StripDirectives %.0f ms, paths %.0f ms, atores %.0f ms\n",
                mergeMs, stripMs, pathsMs, roomMs);
    CHECK(mergeMs < 2000 && stripMs < 2000 && pathsMs < 2000 && roomMs < 3000);
}

// Custo das listas grandes: parse, mescla de duas camadas completas e transcode, com teto folgado para a máquina
// de teste (antes: ~4,4 s de parse e ~4,6 s de transcode para 65 535 surface types).
void TestLargeListCost() {
    const auto start = std::chrono::steady_clock::now();
    const std::string text = CollisionText(65535, 65535);
    MergedDocument merged;
    CHECK(MergeLayers({ { "base", text, 0 }, { "mod", text, 0 } }, true, merged));
    const double mergedMs = MillisecondsSince(start);
    TranscodeContext context = Context();
    const std::string xml = TranscodeCollision(merged.doc, context);
    const double totalMs = MillisecondsSince(start);
    std::printf("65535 surface types + 65535 water boxes, duas camadas: mescla %.0f ms, total %.0f ms\n", mergedMs,
                totalMs);
    CHECK(Count(xml, "<SurfaceType ") == 65535 && Count(xml, "<WaterBox ") == 65535);
    CHECK(totalMs < 3000);
}

// M06: salas por cena. O índice de sala é s16: até 32 768 salas (0..32767) todas alcançáveis; acima, nota.
void TestRoomCount() {
    for (const size_t n : { 32767u, 32768u, 32769u }) {
        const std::string doc = R"({"rooms":)" + PositionalText(n, R"("limites/sala.json")") + R"(,"setups":{"0":{}}})";
        TranscodeContext context = Context();
        const std::string xml = TranscodeScene(ParseJson(doc), false, context);
        CHECK(Count(xml, "<RoomEntry ") == n);
        CHECK(AnyNote(context, "salas; o índice de sala vai até 32767") == (n > 32768));
    }
}

// M07: atores por sala (numSetupActors u16) e M09: objetos por sala (banco de 1 024).
void TestActorAndObjectCounts() {
    for (const size_t n : { 65534u, 65535u, 65536u }) {
        const std::string doc = R"({"setups":{"0":{"actors":)" +
                                PositionalText(n, R"({"id":16,"pos":[0,0,0]})") + "}}}";
        TranscodeContext context = Context();
        const std::string xml = TranscodeScene(ParseJson(doc), true, context);
        CHECK(Count(xml, "<ActorEntry ") == n);
        CHECK(AnyNote(context, "atores; o jogo carrega os primeiros 65535") == (n > 65535));
    }
    // O banco tem 1 024 vagas, mas até 4 são permanentes (gameplay_keep, Link, keep da cena e cavalo).
    for (const size_t n : { 1019u, 1020u, 1021u }) {
        const std::string doc = R"({"setups":{"0":{"objects":)" + PositionalText(n, "1") + "}}}";
        TranscodeContext context = Context();
        const std::string xml = TranscodeScene(ParseJson(doc), true, context);
        CHECK(Count(xml, "<ObjectEntry ") == n);
        CHECK(AnyNote(context, "objetos; o banco do jogo tem 1024 vagas, até 4 delas") == (n > 1020));
    }
}

// R03: posição de câmera fixa é Vec3s; o §2 embrulha, e o autor recebe nota em vez de silêncio.
void TestCameraPositions() {
    const std::string base = R"({"bounds":{"min":[0,0,0],"max":[1,1,1]},"bulk":{"file":"x.bin"},"surfaceTypes":{},)";
    TranscodeContext inside = Context();
    TranscodeCollision(ParseJson(base + R"("cameraPositions":{"0":[32767,0,-32768],"1":[0,0,0]}})"), inside);
    CHECK(!AnyNote(inside, "cameraPositions"));
    TranscodeContext outside = Context();
    const std::string xml = TranscodeCollision(
        ParseJson(base + R"("cameraPositions":{"0":[32767,0,0],"1":[32768,0,0],"2":[-32768,0,0],"3":[-32769.4,0,0]}})"),
        outside);
    CHECK(AnyNote(outside, "2 valor(es) fora de -32768..32767 embrulhados (ex.: 1[0]=32768)"));
    // O valor continua o do §2 (embrulhado), só que agora com aviso.
    CHECK(xml.find("RotX=\"-32768\"") != std::string::npos && xml.find("PosX=\"32767\"") != std::string::npos);
}

// M01: além de ±1 048 576 o jogo apaga os EffectSs (z_effect_soft_sprite.c); spawn, ator, transition actor e bounds
// avisam. O limiar vale para o valor que o jogo recebe: f32 nas posições, o inteiro arredondado nos bounds.
void TestWorldLimit() {
    const auto scene = [](const std::string& spawn, const std::string& actor, const std::string& door) {
        return R"({"setups":{"0":{"spawns":{"0":{"id":0,"pos":)" + spawn + R"(}},"actors":{"a":{"id":16,"pos":)" +
               actor + R"(}},"transitionActors":{"0":{"id":9,"pos":)" + door + "}}}}}";
    };
    // 1 048 576 exato e 1 048 576,03 (f32 = 1 048 576) ainda desenham os efeitos.
    TranscodeContext inside = Context();
    TranscodeScene(ParseJson(scene("[1048576,0,-1048576]", "[1048576.03,0,0]", "[0,-1048576,0]")), true, inside);
    CHECK(!AnyNote(inside, "além de ±1048576"));
    // 1 048 576,063: o cast direto daria o f32 1 048 576,125, mas o XML leva "1048576.06" (%.9g) e a fábrica lê
    // 1 048 576. Os dois sinais, nas três listas e nos três eixos, não têm nota.
    for (const char* v : { "1048576.063", "-1048576.063" }) {
        const std::string p = v;
        const std::string written = p.substr(0, p.size() - 1); // "%.9g": 1048576.06
        const std::string axes[] = { "PosX", "PosY", "PosZ" };
        for (int axis = 0; axis < 3; ++axis) {
            const std::string pos = axis == 0 ? "[" + p + ",0,0]" : axis == 1 ? "[0," + p + ",0]" : "[0,0," + p + "]";
            TranscodeContext rounding = Context();
            const std::string xml = TranscodeScene(ParseJson(scene(pos, pos, pos)), true, rounding);
            CHECK(!AnyNote(rounding, "além de ±1048576"));
            // Spawn, ator e transition actor, cada um com o valor inteiro no atributo do eixo.
            CHECK(Count(xml, axes[axis] + "=\"" + written + "\"") == 3);
        }
    }
    // Finito no JSON, além do f32: o FloatAttribute lê ±inf e a nota sai (antes dependia da biblioteca).
    const std::pair<const char*, const char*> huges[] = { { "1e39", "1e+39" }, { "-1e39", "-1e+39" },
                                                          { "1e100", "1e+100" } };
    for (const auto& [v, shown] : huges) {
        const std::string pos = std::string("[0,") + v + ",0]";
        TranscodeContext huge = Context();
        TranscodeScene(ParseJson(scene(pos, pos, pos)), true, huge);
        CHECK(AnyNote(huge, std::string("limites spawns: 1 posição(ões) além de ±1048576 (ex.: 0=0,") + shown + ",0)"));
        CHECK(AnyNote(huge, "limites actors: 1 posição(ões)"));
        CHECK(AnyNote(huge, "limites transitionActors: 1 posição(ões)"));
    }
    TranscodeContext outside = Context();
    const std::string xml = TranscodeScene(
        ParseJson(scene("[1048577,0,0]", "[0,0,-1048576.07]", "[0,1048600.5,0]")), true, outside);
    CHECK(AnyNote(outside, "limites spawns: 1 posição(ões) além de ±1048576 (ex.: 0=1048577,0,0)"));
    // -1 048 576,07 vira o f32 -1 048 576,125.
    CHECK(AnyNote(outside, "limites actors: 1 posição(ões) além de ±1048576 (ex.: a=0,0,-1048576.12)"));
    CHECK(AnyNote(outside, "limites transitionActors: 1 posição(ões) além de ±1048576 (ex.: 0=0,1048600.5,0)"));
    // A posição continua indo ao XML como veio: a nota não muda o documento.
    CHECK(xml.find("PosX=\"1048577\"") != std::string::npos);

    const std::string collision = R"({"bulk":{"file":"x.bin"},"bounds":{"min":[-1048576,0,0],"max":)";
    TranscodeContext edge = Context();
    TranscodeCollision(ParseJson(collision + R"([1048576.4,10,10]}})"), edge);
    CHECK(!AnyNote(edge, "bounds"));
    // 1 048 576,5 arredonda para 1 048 577 no XML (Integral), e o mesmo valor entra na conta.
    for (const char* max : { "[1048576.5,10,10]", "[10,1048800,10]", "[10,10,-1048577]" }) {
        TranscodeContext beyond = Context();
        TranscodeCollision(ParseJson(collision + max + "}}"), beyond);
        CHECK(AnyNote(beyond, "limites bounds: 1 posição(ões) além de ±1048576 (ex.: max="));
    }
    TranscodeContext rounded = Context();
    TranscodeCollision(ParseJson(collision + R"([1048576.5,10,10]}})"), rounded);
    CHECK(AnyNote(rounded, "(ex.: max=1048577,10,10)"));
}

// M12: a malha tipo 2 só considera as primeiras 1 024 entradas (SHAPE_SORT_MAX, z_room.c); a tipo 0 não tem teto.
void TestMeshEntries() {
    for (const int type : { 0, 2 }) {
        const std::string item = type == 2 ? R"({"pos":[0,0,0],"radius":10,"opa":"limites/dl","xlu":null})"
                                           : R"({"opa":"limites/dl","xlu":null})";
        for (const size_t n : { 1023u, 1024u, 1025u }) {
            const std::string doc = R"({"setups":{"0":{"mesh":{"type":)" + std::to_string(type) + R"(,"entries":)" +
                                    PositionalText(n, item) + "}}}}";
            TranscodeContext context = Context();
            const std::string xml = TranscodeScene(ParseJson(doc), true, context);
            CHECK(Count(xml, "<Polygon ") == n);
            CHECK(AnyNote(context, std::to_string(n) + " entradas na malha tipo 2; o jogo só considera as primeiras "
                                                       "1024 (SHAPE_SORT_MAX)") == (type == 2 && n > 1024));
        }
    }
}

// M03: posição decimal de ator e luz chega ao XML sem perder a fração.
void TestDecimalPositions() {
    const std::string doc = R"({"setups":{"0":{"actors":{"0":{"id":16,"pos":[32768.5,1.25,-0.5]}},)"
                            R"("lights":{"0":{"type":0,"pos":[100000.5,-0.25,3.75],"color":[1,2,3]}}}}})";
    TranscodeContext context = Context();
    const std::string xml = TranscodeScene(ParseJson(doc), true, context);
    CHECK(xml.find("PosX=\"32768.5\" PosY=\"1.25\" PosZ=\"-0.5\"") != std::string::npos);
    CHECK(xml.find("X=\"100000.5\" Y=\"-0.25\" Z=\"3.75\"") != std::string::npos);
}

} // namespace

int main() {
    TestRoomCount();
    TestActorAndObjectCounts();
    TestCameraPositions();
    TestWorldLimit();
    TestMeshEntries();
    TestDecimalPositions();
    TestParseEquivalence();
    TestMergeEquivalence();
    TestMergeEdgeCases();
    TestReviewCosts();
    TestLargeListCost();
    TestPathPoints();
    TestLightSettings();
    TestMessageBytes();
    TestCollisionCounts();
    if (gFailures) {
        std::fprintf(stderr, "%d falha(s)\n", gFailures);
        return 1;
    }
    std::puts("limits_tests: ok");
    return 0;
}
