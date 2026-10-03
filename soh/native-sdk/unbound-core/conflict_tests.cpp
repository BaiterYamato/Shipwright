// Testes da checagem de conflito de cenas entre mods (UNBOUND-018, plano §10.5/§14.3).
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "converter.h"
#include "scene_conflicts.h"
#include "unbound_format.h"

namespace {

using namespace LinkSpanUnbound;
namespace fs = std::filesystem;

int gFailures = 0;

#define CHECK(expr)                                                                  \
    do {                                                                             \
        if (!(expr)) {                                                               \
            std::fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #expr);  \
            ++gFailures;                                                             \
        }                                                                            \
    } while (0)

DocumentAnalysis Analyze(const std::vector<std::string>& jsons) {
    std::vector<LayerDocument> layers;
    char name = 'A';
    for (const auto& json : jsons) {
        layers.push_back({ std::string("D:/Jogo/mods/") + name++ + ".o2r", json, 0 });
    }
    return AnalyzeDocument(layers);
}

void TestMerge() {
    // Delta disjunto: cada mod acrescenta o próprio ator, nada se perde.
    auto r = Analyze({ R"({"$schema":"unbound/room/1","setups":{"0":{"actors":{"a":{"id":1}}}}})",
                       R"({"$schema":"unbound/room/1","setups":{"0":{"actors":{"b":{"id":2}}}}})" });
    CHECK(r.conflicts.empty() && r.notes.empty());

    // Mesma folha com valor diferente: a camada de cima vence e a de baixo perde.
    r = Analyze({ R"({"s":{"x":1,"y":2}})", R"({"s":{"x":5}})" });
    CHECK(r.conflicts.size() == 1);
    if (r.conflicts.size() == 1) {
        CHECK(r.conflicts[0].lower == "D:/Jogo/mods/A.o2r" && r.conflicts[0].upper == "D:/Jogo/mods/B.o2r");
        CHECK(r.conflicts[0].lost == 1 && r.conflicts[0].keys == std::vector<std::string>{ "s.x" });
    }

    // Valor igual nas duas camadas não é conflito (1 e 1.0 são o mesmo número).
    r = Analyze({ R"({"s":{"x":1,"pos":[1,2,3]}})", R"({"s":{"x":1.0,"pos":[1,2,3]}})" });
    CHECK(r.conflicts.empty());

    // $replace na camada de cima descarta o que a de baixo trouxe...
    r = Analyze({ R"({"actors":{"a":{"id":1}}})", R"({"actors":{"$replace":true,"b":{"id":2}}})" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].keys == std::vector<std::string>{ "actors.a.id" });
    // ...a menos que repita os mesmos valores.
    r = Analyze({ R"({"actors":{"a":{"id":1}}})", R"({"actors":{"$replace":true,"a":{"id":1},"b":{}}})" });
    CHECK(r.conflicts.empty());
    // $replace na camada de baixo e acréscimo disjunto em cima: mescla.
    r = Analyze({ R"({"actors":{"$replace":true,"a":{"id":1}}})", R"({"actors":{"b":{"id":2}}})" });
    CHECK(r.conflicts.empty());
    // $replace na raiz da camada de cima.
    r = Analyze({ R"({"x":1})", R"({"$replace":true,"y":2})" });
    CHECK(r.conflicts.size() == 1);

    // Escalar sobre objeto e objeto sobre escalar.
    r = Analyze({ R"({"room":{"echo":{"a":1,"b":2}}})", R"({"room":{"echo":5}})" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].lost == 2);
    r = Analyze({ R"({"room":{"echo":5}})", R"({"room":{"echo":{"a":1}}})" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].keys == std::vector<std::string>{ "room.echo" });

    // null: remoção que outro mod desfaz, remoção do que o de baixo pôs, remoção repetida.
    r = Analyze({ R"({"actors":{"a":null}})", R"({"actors":{"a":{"id":3}}})" });
    CHECK(r.conflicts.size() == 1);
    r = Analyze({ R"({"actors":{"a":{"id":1}}})", R"({"actors":{"a":null}})" });
    CHECK(r.conflicts.size() == 1);
    r = Analyze({ R"({"actors":{"a":null}})", R"({"actors":{"a":null}})" });
    CHECK(r.conflicts.empty());

    // Objeto vazio que outro mod troca por escalar perde; $schema não é dado.
    r = Analyze({ R"({"actors":{"a":{}}})", R"({"actors":{"a":0}})" });
    CHECK(r.conflicts.size() == 1);
    r = Analyze({ R"({"$schema":"unbound/scene/1","x":1})", R"({"$schema":"unbound/scene/2","y":2})" });
    CHECK(r.conflicts.empty());

    // Três mods: só o par que perde aparece, com quem vence.
    r = Analyze({ R"({"x":1})", R"({"y":2})", R"({"x":3})" });
    CHECK(r.conflicts.size() == 1);
    if (r.conflicts.size() == 1) {
        CHECK(r.conflicts[0].lower == "D:/Jogo/mods/A.o2r" && r.conflicts[0].upper == "D:/Jogo/mods/C.o2r");
    }

    // Exemplos limitados, contagem inteira; comentário depois do '{' aceito; camada inválida vira nota.
    r = Analyze({ R"({"a":1,"b":2,"c":3,"d":4,"e":5})", "{ // cópia completa\n\"a\":0,\"b\":0,\"c\":0,\"d\":0,\"e\":0}" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].lost == 5 && r.conflicts[0].keys.size() == kConflictKeyExamples);
    r = Analyze({ R"({"x":1})", "{quebrado", R"({"x":2})" });
    CHECK(r.notes.size() == 1 && r.conflicts.size() == 1);
    // Cena: camada sem '{' no primeiro byte é pulada pelo jogo (MergeLayers estrito), então não conflita.
    r = Analyze({ R"({"x":1})", " {\"x\":2}", "// nota\n{\"x\":3}" });
    CHECK(r.notes.size() == 2 && r.conflicts.empty());
}

DocumentAnalysis AnalyzeRegistry(const std::vector<std::string>& jsons) {
    std::vector<LayerDocument> layers;
    char name = 'A';
    for (const auto& json : jsons) {
        layers.push_back({ std::string("D:/Jogo/mods/") + name++ + ".o2r", json, 0 });
    }
    return AnalyzeDocument(layers, MergeRule::Registry);
}

void TestRegistry() {
    // Cenas diferentes no registro mesclam; comentário antes do '{' vale no registro.
    auto r = AnalyzeRegistry({ R"({"mod/a":{"scene":"scenes/a"}})", "// registro\n{\"mod/b\":{\"scene\":\"scenes/b\"}}" });
    CHECK(r.conflicts.empty() && r.notes.empty());
    // A mesma cena com outro caminho conflita.
    r = AnalyzeRegistry({ R"({"mod/a":{"scene":"scenes/a"}})", R"({"mod/a":{"scene":"scenes/outra"}})" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].keys == std::vector<std::string>{ "mod/a.scene" });
    // No registro $replace é dado, não diretiva: chave nova ao lado não descarta a de baixo.
    r = AnalyzeRegistry({ R"({"mod/a":{"scene":"scenes/a"}})", R"({"mod/a":{"$replace":true,"name":"A"}})" });
    CHECK(r.conflicts.empty());
    // JSON inválido no registro derruba o registro inteiro no jogo: nota própria.
    r = AnalyzeRegistry({ R"({"mod/a":{}})", "{quebrado" });
    CHECK(r.notes.size() == 1 && r.notes[0].find("registro inteiro") != std::string::npos);
}

void TestOtherRules() {
    // Arquivo inteiro: conteúdo igual é cópia idêntica; cada camada diferente da mais alta perde o arquivo.
    std::vector<LayerDocument> files = { { "A", "abc", 0 }, { "B", "abc", 0 } };
    auto r = AnalyzeDocument(files, MergeRule::WholeFile);
    CHECK(r.identical && r.conflicts.empty());
    files = { { "A", "abc", 0 }, { "B", "xyz", 0 }, { "C", "abc", 0 } };
    r = AnalyzeDocument(files, MergeRule::WholeFile);
    CHECK(!r.identical && r.conflicts.size() == 1);
    if (r.conflicts.size() == 1) {
        CHECK(r.conflicts[0].wholeFile && r.conflicts[0].lower == "B" && r.conflicts[0].upper == "C");
    }

    // Texto: comentário antes do '{' vale (BuildTextTable não é estrito); mensagens diferentes mesclam e a mesma
    // mensagem com outro texto conflita.
    files = { { "A", R"({"messages":{"4096":{"text":"a"}}})", 0 },
              { "B", "// pt\n" R"({"messages":{"4097":{"text":"b"}}})", 0 } };
    r = AnalyzeDocument(files, MergeRule::Text);
    CHECK(r.notes.empty() && r.conflicts.empty());
    files = { { "A", R"({"messages":{"4096":{"text":"a"}}})", 0 }, { "B", R"({"messages":{"4096":{"text":"b"}}})", 0 } };
    r = AnalyzeDocument(files, MergeRule::Text);
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].keys == std::vector<std::string>{ "messages.4096.text" });
}

void TestPaths() {
    CHECK(IsComparableEntry("scenes/spot04/scene.json"));
    CHECK(IsComparableEntry("textures/x/icone"));
    CHECK(!IsComparableEntry("scenes/spot04/"));
    CHECK(!IsComparableEntry("unbound.json"));
    CHECK(!IsComparableEntry(""));

    // Regra por caminho: registro, texto, JSON tipado por qualquer camada (a parcial de cima pode omitir o
    // $schema), e o resto inteiro, inclusive JSON sem $schema "unbound/..." (ex.: cena registrada fora de scenes/).
    const std::vector<LayerDocument> typed = { { "base", R"({"$schema":"unbound/scene/1"})", 0 },
                                               { "mod", R"({"setups":{}})", 0 } };
    const std::vector<LayerDocument> untyped = { { "a", R"({"x":1})", 0 }, { "b", R"({"$schema":"outro/1"})", 0 } };
    CHECK(RuleFor("unbound/scenes.json", untyped) == MergeRule::Registry);
    CHECK(RuleFor("text/eng/messages.json", untyped) == MergeRule::Text);
    CHECK(RuleFor("custom/templo.json", typed) == MergeRule::Scene);
    CHECK(RuleFor("scenes/spot04/scene.json", untyped) == MergeRule::WholeFile);
    // O carregador tipa pelo conteúdo ('{' + $schema), sem olhar extensão; binário e texto de outro arquivo são inteiros.
    CHECK(RuleFor("scenes/templo/scene", typed) == MergeRule::Scene);
    const std::vector<LayerDocument> binary = { { "a", std::string("\x01\x02{\"$schema\":\"unbound/scene/1\"}"), 0 } };
    CHECK(RuleFor("scenes/shared/spot04_scene/dl", binary) == MergeRule::WholeFile);
    CHECK(RuleFor("text/eng/notas.json", untyped) == MergeRule::WholeFile);

    CHECK(ArchiveLabel("C:\\Jogo\\mods\\Pack\\assets\\camada.o2r") == "Pack/assets/camada.o2r");
    CHECK(ArchiveLabel("D:/Jogo/MODS/solta.o2r") == "solta.o2r");
    CHECK(ArchiveLabel("D:/Jogo/mods/.shiplua-cache/pack/assets/b.o2r") == "pack/assets/b.o2r");
    CHECK(ArchiveLabel("mods/a.o2r") == "a.o2r");
    CHECK(ArchiveLabel("D:/Jogo/oot-unbound.o2r") == "oot-unbound.o2r");
}

void TestZip() {
    const std::vector<ConvertedFile> files = {
        { "unbound.json", R"({"format":"unbound","formatVersion":2})" },
        { "scenes/spot04/scene.json", "{}" },
        { "scenes/spot04/rooms/0.json", "{}" },
    };
    const std::string zip = BuildStoredZip(files);
    std::vector<std::string> names;
    std::string error;
    CHECK(ListZipNames(zip, names, error));
    CHECK((names == std::vector<std::string>{ "unbound.json", "scenes/spot04/scene.json", "scenes/spot04/rooms/0.json" }));

    // Comentário no fim do ZIP: o registro final continua achado.
    std::string commented = zip;
    commented[commented.size() - 2] = 4;
    commented += "nota";
    CHECK(ListZipNames(commented, names, error) && names.size() == 3);

    CHECK(!ListZipNames("isto não é um zip, só texto comprido o bastante", names, error));
    CHECK(!ListZipNames(zip.substr(zip.size() / 2), names, error));
    std::string zip64 = zip;
    zip64[zip64.size() - 22 + 10] = '\xFF';
    zip64[zip64.size() - 22 + 11] = '\xFF';
    CHECK(!ListZipNames(zip64, names, error) && error.find("ZIP64") != std::string::npos);

    // Arquivo no disco e pasta montada como archive.
    const fs::path root = fs::temp_directory_path() / "linkspan-unbound-conflict-tests";
    std::error_code code;
    fs::remove_all(root, code);
    fs::create_directories(root / "pasta" / "scenes" / "spot04");
    {
        std::ofstream(root / "camada.o2r", std::ios::binary) << zip;
        std::ofstream(root / "pasta" / "scenes" / "spot04" / "scene.json") << "{}";
    }
    CHECK(ListArchiveNames((root / "camada.o2r").string(), names, error) && names.size() == 3);
    CHECK(ListArchiveNames((root / "pasta").string(), names, error) &&
          names == std::vector<std::string>{ "scenes/spot04/scene.json" });
    CHECK(!ListArchiveNames((root / "ausente.o2r").string(), names, error));
    fs::remove_all(root, code);
}

// Casos da revisão do Codex (UNBOUND-018): fronteiras do merge, três camadas e o leitor de ZIP.
uint16_t ZipU16(const std::string& s, size_t p) {
    return static_cast<uint16_t>(static_cast<unsigned char>(s[p]) | static_cast<unsigned char>(s[p + 1]) << 8);
}

uint32_t ZipU32(const std::string& s, size_t p) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        result |= static_cast<uint32_t>(static_cast<unsigned char>(s[p + i])) << (8 * i);
    }
    return result;
}

void ZipSet(std::string& s, size_t p, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
        s[p + i] = static_cast<char>(value >> (8 * i));
    }
}

void ZipPut(std::string& s, uint64_t value, unsigned bytes) {
    const size_t p = s.size();
    s.resize(p + bytes);
    ZipSet(s, p, value, bytes);
}

std::vector<ConvertedFile> ZipFiles() {
    return {
        { "unbound.json", R"({"format":"unbound","formatVersion":2})" },
        { "scenes/spot04/scene.json", R"({"$schema":"unbound/scene/1","x":1})" },
        { "scenes/spot04/rooms/0.json", R"({"$schema":"unbound/room/1","echo":2})" },
    };
}

const std::vector<std::string> kZipNames = { "unbound.json", "scenes/spot04/scene.json", "scenes/spot04/rooms/0.json" };

std::string ZipComment(std::string zip, const std::string& comment) {
    ZipSet(zip, zip.size() - 2, comment.size(), 2);  // BuildStoredZip grava o registro final sem comentário
    return zip + comment;
}

std::string FakeEnd(uint16_t entries, uint32_t size, uint32_t offset) {
    std::string record;
    ZipPut(record, 0x06054b50, 4);
    ZipPut(record, 0, 2);
    ZipPut(record, 0, 2);
    ZipPut(record, entries, 2);
    ZipPut(record, entries, 2);
    ZipPut(record, size, 4);
    ZipPut(record, offset, 4);
    ZipPut(record, 0, 2);
    return record;
}

void TestReviewMerge() {
    // $order é array: outra ordem perde a folha inteira, mesmo que a ordem final calculada coincida.
    auto r = Analyze({ R"({"actors":{"$order":["a","b"],"a":{"id":1},"b":{"id":2}}})",
                       R"({"actors":{"$order":["b","a"]}})" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].lost == 1 &&
          r.conflicts[0].keys == std::vector<std::string>{ "actors.$order" });
    r = Analyze({ R"({"actors":{"$order":["a"],"a":{}}})", R"({"actors":{"$order":["a"],"b":{}}})" });
    CHECK(r.conflicts.empty());

    // Array vazio é valor; objeto vazio continua objeto.
    r = Analyze({ R"({"pos":[]})", R"({"pos":[1,2,3]})" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].lost == 1);
    r = Analyze({ R"({"x":{}})", R"({"x":{"y":1}})" });
    CHECK(r.conflicts.empty());

    // $replace:false não descarta nada; $schema aninhado é dado; objeto com $replace trocado por escalar perde.
    r = Analyze({ R"({"x":{"a":1}})", R"({"x":{"$replace":false,"b":2}})" });
    CHECK(r.conflicts.empty());
    r = Analyze({ R"({"x":{"$schema":"a"}})", R"({"x":{"$schema":"b"}})" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].keys == std::vector<std::string>{ "x.$schema" });
    r = Analyze({ R"({"x":{"$replace":true}})", R"({"x":0})" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].lost == 1);

    // Remoção sob pai removido ou descartado sobrevive; descartar e repor a chave removida perde.
    r = Analyze({ R"({"x":{"a":null}})", R"({"x":null})" });
    CHECK(r.conflicts.empty());
    r = Analyze({ R"({"x":{"a":null}})", R"({"x":{"$replace":true,"b":2}})" });
    CHECK(r.conflicts.empty() && r.notes.empty());
    r = Analyze({ R"({"x":{"a":null}})", R"({"x":{"$replace":true,"a":1}})" });
    CHECK(r.conflicts.size() == 1 && r.notes.empty());

    // Chave repetida: o parser fica com a última ocorrência, como o MergeLayers.
    r = Analyze({ R"({"x":1,"x":2})", R"({"x":2})" });
    CHECK(r.conflicts.empty());
    r = Analyze({ R"({"x":2,"x":1})", R"({"x":2})" });
    CHECK(r.conflicts.size() == 1);

    // Objetos dentro de array com as mesmas chaves em outra ordem são o mesmo valor.
    r = Analyze({ R"({"data":[{"a":1,"b":2}]})", R"({"data":[{"b":2,"a":1}]})" });
    CHECK(r.conflicts.empty());
}

void TestReviewThreeLayers() {
    // C repõe o valor de A: o par A/C não perde nada, mas A/B e B/C perdem.
    auto r = Analyze({ R"({"x":1})", R"({"x":2})", R"({"x":1})" });
    CHECK(r.conflicts.size() == 2);
    if (r.conflicts.size() == 2) {
        CHECK(r.conflicts[0].upper == "D:/Jogo/mods/B.o2r" && r.conflicts[1].lower == "D:/Jogo/mods/B.o2r");
    }
    // $replace aninhado só afeta aquele objeto; cada par aponta as próprias chaves.
    r = Analyze({ R"({"x":{"a":1},"y":4})", R"({"x":{"$replace":true,"b":2}})", R"({"x":{"b":3}})" });
    CHECK(r.conflicts.size() == 2);
    if (r.conflicts.size() == 2) {
        CHECK(r.conflicts[0].keys == std::vector<std::string>{ "x.a" });
        CHECK(r.conflicts[1].keys == std::vector<std::string>{ "x.b" });
    }
    // Aninhamento demais no meio vale como camada pulada.
    const std::string deep = std::string(kMaxJsonDepth + 1, '[') + "0" + std::string(kMaxJsonDepth + 1, ']');
    r = Analyze({ R"({"x":1})", "{\"deep\":" + deep + "}", R"({"x":2})" });
    CHECK(r.notes.size() == 1 && r.conflicts.size() == 1);
    // Paridade com o jogo: camada com espaço ou comentário antes do '{' não entra no merge real.
    const std::vector<LayerDocument> layers = { { "A", R"({"$schema":"unbound/scene/1","x":1})", 0 },
                                                { "B", "// pulada\n{\"x\":2}", 0 },
                                                { "C", R"({"x":1})", 0 } };
    MergedDocument actual;
    CHECK(MergeLayers(layers, true, actual) && actual.layersUsed == 2 && actual.doc["x"] == 1);
    r = AnalyzeDocument(layers);
    CHECK(r.notes.size() == 1 && r.conflicts.empty());
}

void TestReviewCost() {
    // Objeto de 5 000 chaves contra um delta vazio: a comparação indexada é linear (a busca em ordem do
    // ordered_json faria ~1,25*10^7 comparações por par). O parse do ordered_json continua quadrático em chaves por
    // objeto, mas esse custo é o mesmo que o MergeLayers do jogo paga ao carregar o documento.
    std::string big = "{\"actors\":{";
    for (int i = 0; i < 5000; ++i) {
        big += (i ? ",\"k" : "\"k") + std::to_string(i) + "\":" + std::to_string(i);
    }
    big += "}}";
    auto r = Analyze({ big, "{}" });
    CHECK(r.conflicts.empty() && r.compared == 2);
    r = Analyze({ big, R"({"actors":{"k4999":0}})" });
    CHECK(r.conflicts.size() == 1 && r.conflicts[0].keys == std::vector<std::string>{ "actors.k4999" });

    // Acima de kMaxComparedLayers camadas, só as mais altas entram, com nota.
    std::vector<std::string> many(kMaxComparedLayers + 2, R"({"x":1})");
    r = Analyze(many);
    CHECK(r.compared == kMaxComparedLayers && r.notes.size() == 1 && r.conflicts.empty());
}

void TestReviewZip() {
    const auto zip = BuildStoredZip(ZipFiles());
    const size_t end = zip.size() - 22;
    const size_t central = ZipU32(zip, end + 16);
    std::vector<std::string> names;
    std::string error;

    // Assinatura do registro final dentro dos dados ou no maior comentário possível não engana o leitor.
    auto files = ZipFiles();
    files.push_back({ "data.bin", "antes" + FakeEnd(0, 0, 0) + "depois" });
    CHECK(ListZipNames(BuildStoredZip(files), names, error) && names.size() == 4);
    CHECK(ListZipNames(ZipComment(zip, std::string(65535, 'x')), names, error) && names == kZipNames);
    // Registro final falso no comentário, terminando no fim do arquivo: vale o que fecha com o diretório.
    CHECK(ListZipNames(ZipComment(zip, FakeEnd(0, 0, 0)), names, error) && names == kZipNames);
    CHECK(ListZipNames(ZipComment(zip, FakeEnd(1, 46, 0)), names, error) && names == kZipNames);

    // Nome que atravessa o diretório central e offset que daria a volta em 32 bits são recusados.
    auto truncatedName = zip;
    ZipSet(truncatedName, central + 28, 65535, 2);
    CHECK(!ListZipNames(truncatedName, names, error));
    auto wrapping = zip;
    ZipSet(wrapping, end + 16, 0xfffffff0u, 4);
    ZipSet(wrapping, end + 12, 0x100u, 4);
    CHECK(!ListZipNames(wrapping, names, error));

    // Entrada de pasta não entra na comparação; nome com '\' fica literal, como o archive do host o procura.
    CHECK(ListZipNames(BuildStoredZip({ { "scenes/spot04/", "" }, { "scenes/spot04/scene.json", "{}" } }), names,
                       error) &&
          names.size() == 2 && !IsComparableEntry(names[0]) && IsComparableEntry(names[1]));
    CHECK(ListZipNames(BuildStoredZip({ { "scenes\\spot04\\scene.json", "{}" } }), names, error) &&
          names == std::vector<std::string>{ "scenes\\spot04\\scene.json" });

    // ZIP64: registro final clássico saturado, localizador e registro final ZIP64 apontando o mesmo diretório.
    const uint64_t count = ZipU16(zip, end + 10);
    const uint64_t directorySize = ZipU32(zip, end + 12);
    std::string z = zip.substr(0, end);
    const uint64_t end64 = z.size();
    ZipPut(z, 0x06064b50, 4);
    ZipPut(z, 44, 8);
    ZipPut(z, 45, 2);
    ZipPut(z, 45, 2);
    ZipPut(z, 0, 4);
    ZipPut(z, 0, 4);
    ZipPut(z, count, 8);
    ZipPut(z, count, 8);
    ZipPut(z, directorySize, 8);
    ZipPut(z, central, 8);
    ZipPut(z, 0x07064b50, 4);
    ZipPut(z, 0, 4);
    ZipPut(z, end64, 8);
    ZipPut(z, 1, 4);
    auto classic = zip.substr(end);
    ZipSet(classic, 8, 0xffff, 2);
    ZipSet(classic, 10, 0xffff, 2);
    ZipSet(classic, 12, 0xffffffffu, 4);
    ZipSet(classic, 16, 0xffffffffu, 4);
    CHECK(ListZipNames(z + classic, names, error) && names == kZipNames);
    // Saturado sem localizador: recusa com motivo.
    CHECK(!ListZipNames(zip.substr(0, end) + classic, names, error) && error.find("ZIP64") != std::string::npos);
}

} // namespace

int main() {
    TestMerge();
    TestRegistry();
    TestOtherRules();
    TestPaths();
    TestZip();
    TestReviewMerge();
    TestReviewThreeLayers();
    TestReviewCost();
    TestReviewZip();
    if (gFailures) {
        std::fprintf(stderr, "%d falha(s)\n", gFailures);
        return 1;
    }
    std::puts("conflict_tests: ok");
    return 0;
}
