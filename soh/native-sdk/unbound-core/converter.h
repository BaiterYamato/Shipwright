#pragma once

// Conversor de cenas vanilla (recursos binários do SoH) para o formato 2 do Unbound (SPEC.md §4):
// scene.json, rooms/<n>.json, collision.json + collision.bin e paths/<nome>.json. Puro: lê bytes por uma
// função, então roda no jogo (serviço de recursos) e nos testes (arquivos de fixture).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace LinkSpanUnbound {

constexpr const char* kConverterVersion = "linkspan-unbound-converter 1";

struct ConvertedFile {
    std::string path;
    std::string bytes;
};

struct ConvertReport {
    uint32_t scenes = 0;
    uint32_t rooms = 0;
    uint32_t collisions = 0;
    uint32_t paths = 0;
    uint32_t failures = 0;
    std::vector<std::string> errors;
};

// Lê o recurso inteiro (com o cabeçalho OTR de 64 bytes); false se não existe.
using ReadResourceFn = std::function<bool(const std::string& path, std::string& bytes)>;

// "spot00_scene" + mq -> "scenes/spot00" / "scenes/spot00_mq".
std::string SceneDirName(const std::string& sceneFileName, bool mq);

// Converte uma cena e tudo o que ela referencia. Uma sala que falha deixa a cena de fora (a lista de
// salas é posicional). Os arquivos entram em `out`; os erros, em `report`.
bool ConvertScene(const ReadResourceFn& read, const std::string& scenePath, const std::string& sceneDir,
                  std::vector<ConvertedFile>& out, ConvertReport& report);

struct SceneSource {
    std::string resource; // recurso binário da cena ("scenes/shared/spot00_scene/spot00_scene")
    std::string dir;      // pasta de saída ("scenes/spot00")
};

// Converte todas as cenas e acrescenta o unbound.json (SPEC.md §6). `sourceJson` é o objeto "source"
// (proveniência). Só declara a feature "scenes" se todas converteram: base incompleta não vale como base.
std::vector<ConvertedFile> BuildBase(const ReadResourceFn& read, const std::vector<SceneSource>& scenes,
                                     const std::string& sourceJson, ConvertReport& report);

// Arquivo ZIP sem compressão (entradas "stored"), com CRC-32, em ordem.
std::string BuildStoredZip(const std::vector<ConvertedFile>& files);

} // namespace LinkSpanUnbound
