#pragma once

#include "PolicromaBridge.h"
#include "PackHistory.h"

#include <array>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ShaderPacks {

// O manifesto v2 usa só as 4 primeiras entradas e 8 passes; o v3 (RFC 0035) chega a 16 entradas e 16 passes.
constexpr size_t LegacyPassInputs = PolicromaMaxInputs;
constexpr size_t MaxPassInputs = PolicromaV3MaxInputs;
constexpr size_t MaxPasses = PolicromaV3MaxPasses;
constexpr size_t LegacyMaxPasses = 8;
constexpr size_t MaxPassOutputs = PolicromaV3MaxOutputs;
constexpr int UnboundInput = PolicromaUnboundInput;
constexpr int SceneColorInput = PolicromaSceneColorInput;
constexpr int SceneDepthInput = PolicromaSceneDepthInput;
constexpr int SceneSunShadowInput = PolicromaSceneSunShadowInput;
constexpr int SceneGeometryShadowInput = PolicromaSceneGeometryShadowInput;
constexpr int SceneFogShadowInput = PolicromaSceneFogShadowInput;
constexpr int ScenePointShadowInput = PolicromaScenePointShadowInput;
constexpr int SceneNoiseInput = PolicromaSceneNoiseInput;
constexpr int ZeroInput = PolicromaZeroInput;
constexpr int OneInput = PolicromaOneInput;
// Entrada interna do manifesto v3, fora da faixa de históricos válidos e dos valores da ponte.
constexpr int SceneMaterialNormalInput = -1024;
constexpr int SceneMaterialSpecularInput = -1025;
using IncludeFiles = std::map<std::string, std::string>;

// v3: textura própria do pack (PNG entregue como include, decodificada pelo host); a entrada k usa
// PackTextureInputBase - k.
constexpr int PackTextureInputBase = PolicromaPackTextureInput;
constexpr size_t MaxPackTextures = PolicromaV3MaxTextures;
inline constexpr bool IsPackTextureInput(int input) {
    return input <= PackTextureInputBase && input > PackTextureInputBase - static_cast<int>(MaxPackTextures);
}
inline constexpr unsigned PackTextureIndex(int input) {
    return static_cast<unsigned>(PackTextureInputBase - input);
}
struct PackTexture {
    std::string name;
    std::string file;
    std::string bytes;
    bool flip = false; // "flip": true guarda a última linha do arquivo no topo (v = 0) do alvo D3D
};
using PackTextures = std::vector<PackTexture>;

// Saída k do passe p: p + 1 + k * PassOutputStride (k = 0 é o valor antigo, p + 1). O histórico usa o mesmo índice
// combinado em HistoryInput.
constexpr unsigned PassOutputStride = 64;
static_assert(SceneMaterialNormalInput < HistoryInput((MaxPassOutputs - 1) * PassOutputStride + MaxPasses - 1));
static_assert(!IsPackTextureInput(SceneMaterialNormalInput));
static_assert(SceneMaterialSpecularInput < SceneMaterialNormalInput);
static_assert(!IsPackTextureInput(SceneMaterialSpecularInput));
inline constexpr int PassOutputInput(unsigned pass, unsigned output) {
    return static_cast<int>(pass + 1 + output * PassOutputStride);
}
inline constexpr unsigned InputPass(int input) {
    return static_cast<unsigned>(input - 1) % PassOutputStride;
}
inline constexpr unsigned InputOutput(int input) {
    return static_cast<unsigned>(input - 1) / PassOutputStride;
}
inline constexpr unsigned HistoryPass(int input) {
    return HistoryInputIndex(input) % PassOutputStride;
}
inline constexpr unsigned HistoryOutput(int input) {
    return HistoryInputIndex(input) / PassOutputStride;
}

inline constexpr std::array<int, MaxPassInputs> UnboundInputs() {
    std::array<int, MaxPassInputs> inputs{};
    inputs.fill(UnboundInput);
    return inputs;
}

inline constexpr size_t MaxTransmissionMaterials = PolicromaMaxMaterials;
struct TransmissionMaterial {
    unsigned scene;
    std::string texture;
    std::array<float, 4> tint;
    std::array<float, 4> light;
};
using TransmissionMaterials = std::vector<TransmissionMaterial>;

struct PassSource {
    std::string file;
    std::string source;
    unsigned divisor = 1;
    // Zero mantém a escala relativa antiga; size no JSON define um alvo fixo.
    unsigned width = 0, height = 0;
    bool persistent = false;
    std::array<int, MaxPassInputs> inputs = UnboundInputs();
    std::shared_ptr<const IncludeFiles> includes;
    std::shared_ptr<const TransmissionMaterials> transmissionMaterials;
    bool fogOver = false;
    unsigned outputs = 1;     // v3: alvos de cor escritos de uma vez (SV_Target0..n-1).
    unsigned shaderModel = 4; // v3: 5 compila ps_5_0.
    // v3: vertex shader do passe (o .vsh de um programa Iris), entregue como include; vazio usa o de tela cheia.
    std::string vertexFile;
    std::string vertexSource;
    // v3: texturas do pack, compartilhadas por todos os passes do programa.
    std::shared_ptr<const PackTextures> textures;
};

inline bool ValidPassSources(std::span<const PassSource> sources) {
    if (sources.empty() || sources.size() > MaxPasses)
        return false;
    bool hasFog = false;
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& pass = sources[i];
        const bool final = i + 1 == sources.size();
        if (pass.outputs < 1 || pass.outputs > MaxPassOutputs || (final && pass.outputs != 1) ||
            (pass.shaderModel != 4 && pass.shaderModel != 5) || pass.shaderModel != sources.front().shaderModel)
            return false;
        if (pass.fogOver) {
            if (hasFog || final || pass.width || pass.height || pass.persistent || pass.outputs != 1)
                return false;
            hasFog = true;
        }
        if ((pass.divisor != 1 && pass.divisor != 2 && pass.divisor != 4) || (pass.width == 0) != (pass.height == 0) ||
            pass.width > 1024 || pass.height > 1024 || (pass.width && pass.divisor != 1) ||
            (final && (pass.width || pass.persistent || pass.divisor != 1)))
            return false;
        for (int input : pass.inputs) {
            if (input == SceneMaterialNormalInput || input == SceneMaterialSpecularInput)
                continue;
            if (IsHistoryInput(input)) {
                const auto index = HistoryPass(input);
                if (index >= sources.size() || !sources[index].persistent || HistoryOutput(input) >= sources[index].outputs)
                    return false;
            } else if (input > 0) {
                if (InputPass(input) >= i || InputOutput(input) >= sources[InputPass(input)].outputs)
                    return false;
            } else if (IsPackTextureInput(input)) {
                if (!pass.textures || PackTextureIndex(input) >= pass.textures->size())
                    return false;
            } else if (input < OneInput)
                return false;
        }
    }
    return true;
}

} // namespace ShaderPacks
