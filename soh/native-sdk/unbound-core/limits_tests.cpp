// Fronteiras da matriz de limites do Prelude (plano §10.3/§14.3, UNBOUND-020): para cada teto que passa pelo
// framework, os casos limite-1, limite e limite+1, com o que o XML leva e a nota ou recusa que sai.
#include <algorithm>
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

// R09 (UNBOUND-028): o minimapa das dungeons vanilla lê textura, bússola e paleta pela sala. A nota diz, por tabela,
// em que salas a leitura cai nas dungeons seguintes e de que sala em diante passa da tabela; a visita para na 31. A
// Deku Tree tem 13 salas no minimapa; a Ice Cavern, última das tabelas, passa da lista de texturas já na sala 12; a
// Water Temple tem 44, mas a paleta e a visita param em 32. A variante MQ usa as mesmas tabelas. Cena fora da lista
// e cena nova não têm nota.
void TestMinimapRooms() {
    const auto note = [](const std::string& path, size_t n) {
        const std::string doc = R"({"rooms":)" + PositionalText(n, R"("x/sala.json")") + R"(,"setups":{"0":{}}})";
        TranscodeContext context = Context();
        context.path = path;
        const std::string xml = TranscodeScene(ParseJson(doc), false, context);
        CHECK(Count(xml, "<RoomEntry ") == n);
        std::string found;
        for (const auto& text : context.notes) {
            if (text.find("minimapa") != std::string::npos) {
                CHECK(found.empty());
                found = text;
            }
        }
        return found;
    };
    const auto head = [](const std::string& path, size_t n, const std::string& dungeon, size_t own) {
        return path + " rooms: " + std::to_string(n) + " salas, e o minimapa de " + dungeon + " tem " +
               std::to_string(own) + " (z_map_exp.c lê as tabelas pela sala sem conferir): ";
    };
    const std::string ydan = "scenes/ydan/scene.json";
    const std::string visit = "visita não marcada da sala 32 em diante";
    CHECK(note(ydan, 13).empty());
    CHECK(note(ydan, 14) == head(ydan, 14, "ydan", 13) + "textura da dungeon seguinte na sala 13");
    // Até a 31, a textura é a da Dodongo's Cavern (19 salas a partir da 13); da 32, já é a da seguinte.
    CHECK(note(ydan, 32) == head(ydan, 32, "ydan", 13) + "textura da dungeon seguinte nas salas 13 a 31");
    CHECK(note(ydan, 33) == head(ydan, 33, "ydan", 13) + "textura das dungeons seguintes nas salas 13 a 32; " + visit +
                                "; paleta da dungeon seguinte na sala 32");
    CHECK(note(ydan, 240) == head(ydan, 240, "ydan", 13) +
                                 "textura das dungeons seguintes nas salas 13 a 238 e além da lista de 239 nomes da sala "
                                 "239 em diante; " +
                                 visit + "; paleta das dungeons seguintes nas salas 32 a 239; bússola das dungeons "
                                         "seguintes nas salas 44 a 239");
    CHECK(note(ydan, 441) == head(ydan, 441, "ydan", 13) +
                                 "textura das dungeons seguintes nas salas 13 a 238 e além da lista de 239 nomes da sala "
                                 "239 em diante; " +
                                 visit +
                                 "; paleta das dungeons seguintes nas salas 32 a 319 e além da tabela da sala 320 em "
                                 "diante (risco de escrita fora de mapPalette); bússola das dungeons seguintes nas salas 44 a 439 "
                                 "e além da tabela da sala 440 em diante");
    const std::string ice = "scenes/ice_doukutu/scene.json";
    const std::string iceTexture = "textura além da lista de 239 nomes da sala 12 em diante";
    CHECK(note(ice, 12).empty());
    CHECK(note(ice, 13) == head(ice, 13, "ice_doukutu", 12) + iceTexture);
    CHECK(note(ice, 32) == head(ice, 32, "ice_doukutu", 12) + iceTexture);
    CHECK(note(ice, 33) == head(ice, 33, "ice_doukutu", 12) + iceTexture + "; " + visit +
                               "; paleta além da tabela da sala 32 em diante (risco de escrita fora de mapPalette)");
    CHECK(note(ice, 44) == note(ice, 33).replace(note(ice, 33).find("33 salas"), 2, "44"));
    CHECK(note(ice, 45) == head(ice, 45, "ice_doukutu", 12) + iceTexture + "; " + visit +
                               "; paleta além da tabela da sala 32 em diante (risco de escrita fora de mapPalette); bússola além "
                               "da tabela da sala 44 em diante");
    const std::string water = "scenes/MIZUsin/scene.json";
    CHECK(note(water, 32).empty());
    CHECK(note(water, 33) == head(water, 33, "MIZUsin", 44) + visit + "; paleta da dungeon seguinte na sala 32");
    CHECK(note(water, 45) == head(water, 45, "MIZUsin", 44) + "textura da dungeon seguinte na sala 44; " + visit +
                                 "; paleta da dungeon seguinte nas salas 32 a 44; bússola da dungeon seguinte na sala 44");
    CHECK(note(water, 161).find("paleta das dungeons seguintes nas salas 32 a 159 e além da tabela da sala 160 em "
                                "diante") != std::string::npos);
    CHECK(note(water, 221).find("bússola das dungeons seguintes nas salas 44 a 219 e além da tabela da sala 220 em "
                                "diante") != std::string::npos);
    const std::string boss = "scenes/moribossroom/scene.json";
    CHECK(note(boss, 28) == head(boss, 28, "moribossroom", 27) + "textura da dungeon seguinte na sala 27");
    const std::string mq = "scenes/ydan_mq/scene.json";
    CHECK(note(mq, 13).empty());
    CHECK(note(mq, 14) == head(mq, 14, "ydan", 13) + "textura da dungeon seguinte na sala 13");
    CHECK(!note("scenes/MIZUsin_mq/scene.json", 33).empty());
    CHECK(note("scenes/spot04/scene.json", 200).empty());
    CHECK(note("scenes/linkspan_e/ydan/scene.json", 200).empty());
    CHECK(note("scenes/ydan_boss_mq2/scene.json", 200).empty());
    // Chefe não tem variante MQ: o nome com _mq não é a rota vanilla.
    CHECK(note("scenes/ydan_boss_mq/scene.json", 14).empty());
    CHECK(note("scenes/HAKAdan_bs_mq/scene.json", 200).empty());
    CHECK(!note("scenes/ydan_boss/scene.json", 14).empty());
}

// R08 (UNBOUND-028): área do mapa-múndi em s16; 0..22 valem (22 é fora do mapa, como as grutas vanilla).
void TestWorldMapArea() {
    for (const int64_t value : { 0LL, 21LL, 22LL, 23LL, -1LL, 32767LL, 65536LL, 65558LL, 65535LL }) {
        const std::string doc = R"({"setups":{"0":{"cameraSettings":{"worldMapArea":)" + std::to_string(value) +
                                R"(}},"2":{"cameraSettings":{"worldMapArea":5}}}})";
        TranscodeContext context = Context();
        TranscodeScene(ParseJson(doc), false, context);
        const int16_t stored = static_cast<int16_t>(static_cast<int32_t>(value));
        const bool outside = stored < 0 || stored > 22;
        CHECK(AnyNote(context, "1 setup(s) fora de 0..22") == outside);
        CHECK(!outside || AnyNote(context, "(ex.: setups.0=" + std::to_string(value) + ")"));
    }
    // Alias de setup ("01" e "1"): só um vira o <AlternateHeader> 1, e a nota segue o que foi emitido.
    TranscodeContext context = Context();
    const std::string xml = TranscodeScene(ParseJson(R"({"setups":{"0":{},"1":{"cameraSettings":{"worldMapArea":3}},
        "01":{"cameraSettings":{"worldMapArea":-5}}}})"), false, context);
    const bool negative = xml.find(R"(WorldMapArea="-5")") != std::string::npos;
    CHECK(negative != (xml.find(R"(WorldMapArea="3")") != std::string::npos));
    CHECK(AnyNote(context, "1 setup(s) fora de 0..22") == negative);
}

// M07: atores por sala (UNBOUND-030: o buffer do oot.room.actors tem 8 192 vagas, e o excesso não é gravado) e M09:
// objetos por sala (banco de 1 024).
void TestActorAndObjectCounts() {
    for (const size_t n : { 8191u, 8192u, 8193u, 65536u }) {
        const std::string doc = R"({"setups":{"0":{"actors":)" +
                                PositionalText(n, R"({"id":16,"pos":[0,0,0]})") + "}}}";
        TranscodeContext context = Context();
        const std::string xml = TranscodeScene(ParseJson(doc), true, context);
        CHECK(Count(xml, "<ActorEntry ") == std::min<size_t>(n, 8192));
        CHECK(AnyNote(context, std::to_string(n) + " atores; o framework grava só os primeiros 8192") == (n > 8192));
    }
    // Ator que não resolve não ocupa vaga: 8 192 válidos depois de um nome desconhecido ainda cabem.
    {
        std::string actors = R"({"0":{"id":"mod.desconhecido","pos":[0,0,0]})";
        for (size_t i = 1; i <= 8192; ++i) {
            actors += ",\"" + std::to_string(i) + R"(":{"id":16,"pos":[0,0,0]})";
        }
        TranscodeContext context = Context();
        const std::string xml = TranscodeScene(ParseJson(R"({"setups":{"0":{"actors":)" + actors + "}}}}"), true,
                                               context);
        CHECK(Count(xml, "<ActorEntry ") == 8192);
        CHECK(!AnyNote(context, "o framework grava só os primeiros"));
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

// Notas de luzes por setup, em cena e sala: host antigo e host com a rodada.
void TestLightListCounts() {
    const char* light = R"({"type":0,"pos":[0,0,0],"color":[255,255,255],"glow":0,"radius":100})";
    const std::string low = " luzes; sem a rodada de host, só 32 vagas no total para listas, atores e ambiente; "
                            "o resto é descartado. Com a rodada, o limite é 255 por comando de lista e 765 nós "
                            "cumulativos por cena para listas, separados das 32 vagas de atores/ambiente; "
                            "luzes de lista não voltam ao pool nas trocas de sala";
    const std::string high = " luzes; entradas além de 255 são descartadas mesmo com a rodada de host "
                             "(sem ela, além de 32), com aviso no log do host";
    for (const bool room : { false, true }) {
        for (const size_t n : { 31u, 32u, 33u, 254u, 255u, 256u, 764u, 765u, 766u }) {
            const std::string doc = R"({"setups":{"0":{"lights":)" + PositionalText(n, light) + "}}}";
            TranscodeContext context = Context();
            const std::string xml = TranscodeScene(ParseJson(doc), room, context);
            CHECK(Count(xml, "<LightInfo ") == n); // Nota não corta nem recusa o XML.
            CHECK(context.notes.size() == (n > 32 ? 1u : 0u));
            if (n > 32 && !context.notes.empty()) {
                CHECK(context.notes.front() == context.path + " lights: " + std::to_string(n) +
                                               (n > 255 ? high : low));
            }
            CHECK(AnyNote(context, low) == (n > 32 && n <= 255));
            CHECK(AnyNote(context, high) == (n > 255));
        }
        // Quatro setups alternativos de 255: quatro notas baixas, nenhuma soma para nota alta/cumulativa.
        const std::string list = PositionalText(255, light);
        const std::string doc = R"({"setups":{"0":{"lights":)" + list + R"(},"1":{"lights":)" + list +
                                R"(},"2":{"lights":)" + list + R"(},"3":{"lights":)" + list + "}}}";
        TranscodeContext context = Context();
        const std::string xml = TranscodeScene(ParseJson(doc), room, context);
        CHECK(Count(xml, "<LightInfo ") == 1020);
        CHECK(context.notes.size() == 4);
        for (const auto& note : context.notes) {
            CHECK(note == context.path + " lights: 255" + low);
        }
        CHECK(!AnyNote(context, high));

        // Setups de níveis distintos continuam independentes; não usar o total do documento.
        TranscodeContext mixed = Context();
        const std::string mixedDoc = R"({"setups":{"0":{"lights":)" + PositionalText(32, light) +
                                     R"(},"1":{"lights":)" + PositionalText(33, light) +
                                     R"(},"2":{"lights":)" + PositionalText(256, light) + "}}}";
        CHECK(Count(TranscodeScene(ParseJson(mixedDoc), room, mixed), "<LightInfo ") == 321);
        CHECK(mixed.notes.size() == 2);
        CHECK(AnyNote(mixed, " lights: 33" + low));
        CHECK(AnyNote(mixed, " lights: 256" + high));
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

// M17: a câmera de superfície e de água é índice da tabela `cameras`. Fora dela o jogo lê fora da lista; acima de
// 32 767 o estado da câmera (s16) não guarda o índice; positionIndex + count além das posições lê fora. Só notas.
void TestCollisionCameras() {
    const auto collision = [](size_t cameras, int64_t surface, int64_t water) {
        return std::string(R"({"bounds":{"min":[0,0,0],"max":[1,1,1]},"bulk":{"file":"x.bin"},)") +
               R"("surfaceTypes":{"0":{"camera":)" + std::to_string(surface) + "}}," +
               R"("cameras":)" + PositionalText(cameras, R"({"sType":1,"count":0,"positionIndex":null})") + "," +
               R"("waterBoxes":{"0":{"xMin":0,"ySurface":0,"zMin":0,"xLength":1,"zLength":1,"camera":)" +
               std::to_string(water) + "}}}";
    };
    const std::string outside = "câmera(s) fora da tabela de ";
    for (const int64_t camera : { 299, 300, 301, -1 }) {
        TranscodeContext context = Context();
        const std::string xml = TranscodeCollision(ParseJson(collision(301, camera, camera)), context);
        const bool out = camera < 0 || camera >= 301;
        CHECK(AnyNote(context, "limites surfaceTypes: 1 " + outside + "301") == out);
        CHECK(AnyNote(context, "limites waterBoxes: 1 " + outside + "301") == (camera >= 301));
        CHECK(!out || AnyNote(context, "(ex.: 0.camera=" + std::to_string(camera) + ")"));
        // O XML leva o índice como veio.
        CHECK(Count(xml, "Camera=\"" + std::to_string(camera) + "\"") == 2);
    }
    // Sem câmeras, até a câmera 0 está fora da tabela.
    TranscodeContext empty = Context();
    TranscodeCollision(ParseJson(collision(0, 0, 0)), empty);
    CHECK(AnyNote(empty, "limites surfaceTypes: 1 " + outside + "0"));
    // Dentro de uma tabela de 32 770, o índice 32 768 não cabe no estado s16 da câmera; 32 767 cabe.
    for (const int64_t camera : { 32767, 32768 }) {
        TranscodeContext wide = Context();
        TranscodeCollision(ParseJson(collision(32770, camera, camera)), wide);
        CHECK(!AnyNote(wide, outside));
        CHECK(AnyNote(wide, "limites surfaceTypes: 1 câmera(s) acima de 32767") == (camera > 32767));
        CHECK(AnyNote(wide, "limites waterBoxes: 1 câmera(s) acima de 32767") == (camera > 32767));
    }

    // Água com câmera 0 ou negativa é "sem câmera" (Camera_GetWaterBoxDataIdx não consulta a tabela); a superfície
    // -1 é lida.
    for (const int64_t water : { 0, -1 }) {
        TranscodeContext none = Context();
        TranscodeCollision(ParseJson(collision(0, -1, water)), none);
        CHECK(!AnyNote(none, "limites waterBoxes"));
        CHECK(AnyNote(none, "limites surfaceTypes: 1 " + outside + "0"));
    }
    TranscodeContext waterOne = Context();
    TranscodeCollision(ParseJson(collision(1, 0, 1)), waterOne);
    CHECK(AnyNote(waterOne, "limites waterBoxes: 1 " + outside + "1;"));

    // Seis vetores em cameraPositions: o jogo lê max(count, 3) a partir de positionIndex (BgCamFuncData são três
    // vetores; o crawlspace lê `count` pontos). Sem positionIndex, a fábrica dá um único vetor zero.
    const auto positions = [](const std::string& camera) {
        return std::string(R"({"bounds":{"min":[0,0,0],"max":[1,1,1]},"bulk":{"file":"x.bin"},"surfaceTypes":{},)") +
               R"("cameras":{"0":)" + camera + "}," + R"("cameraPositions":)" + PositionalText(6, "[0,0,0]") + "}";
    };
    struct PositionCase {
        const char* camera;
        bool outside, missing, range;
    };
    const PositionCase cases[] = {
        { R"({"sType":1,"count":3,"positionIndex":3})", false, false, false },
        { R"({"sType":1,"count":3,"positionIndex":4})", true, false, false },
        { R"({"sType":1,"count":0,"positionIndex":3})", false, false, false },
        { R"({"sType":1,"count":0,"positionIndex":4})", true, false, false },
        { R"({"sType":1,"count":1,"positionIndex":5})", true, false, false },
        { R"({"sType":1,"count":0,"positionIndex":6})", true, false, false },
        { R"({"sType":1,"count":6,"positionIndex":0})", false, false, false },
        { R"({"sType":1,"count":0,"positionIndex":null})", false, false, false },
        { R"({"sType":1,"count":3,"positionIndex":null})", false, true, false },
        { R"({"sType":1,"count":-1,"positionIndex":3})", false, false, true },
        { R"({"sType":1,"count":32767,"positionIndex":null})", false, true, false },
        { R"({"sType":1,"count":32768,"positionIndex":null})", false, true, true },
        // positionIndex + count transbordaria int64.
        { R"({"sType":1,"count":9223372036854775807,"positionIndex":1})", true, false, true },
    };
    for (const auto& c : cases) {
        TranscodeContext context = Context();
        TranscodeCollision(ParseJson(positions(c.camera)), context);
        CHECK(AnyNote(context, "limites cameras: 1 câmera(s) com posições além das 6 de cameraPositions") == c.outside);
        CHECK(AnyNote(context, "limites cameras: 1 câmera(s) com count sem positionIndex") == c.missing);
        CHECK(AnyNote(context, "limites cameras: 1 câmera(s) com count fora de 0..32767") == c.range);
    }
    TranscodeContext example = Context();
    TranscodeCollision(ParseJson(positions(R"({"sType":1,"count":3,"positionIndex":4})")), example);
    CHECK(AnyNote(example, "(ex.: 0: positionIndex=4 count=3)"));
    TranscodeContext nullExample = Context();
    TranscodeCollision(ParseJson(positions(R"({"sType":1,"count":3,"positionIndex":null})")), nullExample);
    CHECK(AnyNote(nullExample, "(ex.: 0: positionIndex=null count=3)"));
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
    TestMinimapRooms();
    TestWorldMapArea();
    TestActorAndObjectCounts();
    TestLightListCounts();
    TestCameraPositions();
    TestWorldLimit();
    TestMeshEntries();
    TestCollisionCameras();
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
