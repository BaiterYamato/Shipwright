// Linha de comando do Unbound (UNBOUND-006/012).
//   linkspan_unbound_convert <pasta-extraida> <saida.o2r>   converte os recursos de uma pasta extraída de um
//                                                           oot.o2r no archive base do formato 2
//   linkspan_unbound_convert --check <pasta>                valida os documentos de uma pasta de assets do
//                                                           jogo como o framework faria (uma camada só)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "converter.h"
#include "scene_registry.h"
#include "transcode.h"
#include "unbound_docs.h"
#include "unbound_format.h"

namespace fs = std::filesystem;
using namespace LinkSpanUnbound;

namespace {

bool ReadFile(const fs::path& path, std::string& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    bytes = text.str();
    return true;
}

struct CheckResult {
    uint32_t checked = 0;
    uint32_t rejected = 0;
    size_t xmlBytes = 0;
};

// Um documento: §4 vira XML pelo transcodificador do jogo; texto, registro e manifesto pelos leitores deles.
// Nomes de entrada não são resolvidos aqui (não há jogo): qualquer nome vale 0.
void CheckDocument(const std::string& path, const std::string& bytes, CheckResult& result) {
    auto report = [&](const std::string& note) { std::fprintf(stderr, "nota: %s: %s\n", path.c_str(), note.c_str()); };
    try {
        if (path == "unbound.json") {
            const auto check = CheckManifest(bytes);
            if (!check.valid) {
                throw DocumentError(check.note);
            }
            ++result.checked;
            return;
        }
        if (path == "unbound/scenes.json") {
            MergeResult merged;
            SceneRegistryDocument registry;
            std::string error;
            if (!MergeSchemaFreeDocuments({ { "check", bytes, 0 } }, merged, error) ||
                !ParseSceneRegistry(merged.json, registry, error)) {
                throw DocumentError(error);
            }
            for (const auto& note : registry.notes) {
                report(note);
            }
            ++result.checked;
            return;
        }
        if (path.rfind("text/", 0) == 0) {
            TextTable table;
            if (!BuildTextTable({ { "check", bytes, 0 } }, table)) {
                throw DocumentError("não é um documento JSON");
            }
            for (const auto& note : table.notes) {
                report(note);
            }
            ++result.checked;
            return;
        }
        MergedDocument merged;
        TranscodeContext context;
        context.path = path;
        context.resolveEntrance = [](const std::string&) { return 0; };
        if (!MergeLayers({ { "check", bytes, 0 } }, true, merged)) {
            throw DocumentError("não é um objeto JSON que começa com '{'");
        }
        std::string xml;
        if (merged.type == "unbound/scene") {
            xml = TranscodeScene(merged.doc, false, context);
        } else if (merged.type == "unbound/room") {
            xml = TranscodeScene(merged.doc, true, context);
        } else if (merged.type == "unbound/collision" && merged.version == 3) {
            xml = TranscodeCollision(merged.doc, context);
        } else if (merged.type == "unbound/paths") {
            xml = TranscodePaths(merged.doc, context);
        } else {
            throw DocumentError("$schema não suportado: '" + merged.schema + "'");
        }
        for (const auto& note : context.notes) {
            report(note);
        }
        result.xmlBytes += xml.size();
        ++result.checked;
    } catch (const std::exception& error) {
        ++result.rejected;
        std::fprintf(stderr, "recusado: %s: %s\n", path.c_str(), error.what());
    }
}

int Check(const fs::path& root) {
    CheckResult result;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        std::string bytes;
        if (!ReadFile(entry.path(), bytes)) {
            ++result.rejected;
            continue;
        }
        CheckDocument(fs::relative(entry.path(), root).generic_string(), bytes, result);
    }
    std::printf("verificados=%u recusados=%u xml=%zu bytes\n", result.checked, result.rejected, result.xmlBytes);
    return result.rejected ? 1 : 0;
}

int Convert(const fs::path& root, const fs::path& output) {
    std::vector<SceneSource> scenes;
    for (const auto& [variant, mq] : { std::pair<const char*, bool>{ "shared", false }, { "nonmq", false }, { "mq", true } }) {
        const fs::path folder = root / "scenes" / variant;
        if (!fs::is_directory(folder)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(folder)) {
            const std::string name = entry.path().filename().string();
            if (entry.is_directory() && fs::is_regular_file(entry.path() / name)) {
                scenes.push_back({ std::string("scenes/") + variant + "/" + name + "/" + name, SceneDirName(name, mq) });
            }
        }
    }
    const ReadResourceFn read = [&](const std::string& path, std::string& bytes) {
        return ReadFile(root / fs::u8path(path), bytes);
    };
    ConvertReport report;
    const auto files = BuildBase(read, scenes, R"({"origin":"linha de comando"})", report);
    for (const auto& error : report.errors) {
        std::fprintf(stderr, "erro: %s\n", error.c_str());
    }
    // Autoverificação: cada documento gerado passa pelo transcodificador que o jogo usa.
    CheckResult checked;
    for (const auto& file : files) {
        if (file.path.size() > 5 && file.path.compare(file.path.size() - 5, 5, ".json") == 0) {
            CheckDocument(file.path, file.bytes, checked);
        }
    }
    std::printf("verificados=%u recusados=%u xml=%zu bytes\n", checked.checked, checked.rejected, checked.xmlBytes);
    // Escreve ao lado e troca pelo nome final: um archive pela metade nunca substitui o anterior.
    const fs::path temp = output.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        const std::string zip = BuildStoredZip(files);
        out.write(zip.data(), static_cast<std::streamsize>(zip.size()));
        if (!out) {
            std::fprintf(stderr, "falha ao gravar %s\n", temp.string().c_str());
            return 1;
        }
    }
    std::error_code error;
    fs::rename(temp, output, error);
    if (error) {
        std::fprintf(stderr, "falha ao renomear para %s: %s\n", output.string().c_str(), error.message().c_str());
        return 1;
    }
    std::printf("cenas=%u salas=%u colisoes=%u paths=%u falhas=%u arquivos=%zu\n", report.scenes, report.rooms,
                report.collisions, report.paths, report.failures, files.size());
    return report.failures || checked.rejected ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--check") {
        return Check(fs::u8path(argv[2]));
    }
    if (argc != 3) {
        std::fprintf(stderr, "uso: linkspan_unbound_convert <pasta-extraida> <saida.o2r>\n"
                             "     linkspan_unbound_convert --check <pasta-de-assets>\n");
        return 2;
    }
    return Convert(fs::u8path(argv[1]), fs::u8path(argv[2]));
}
