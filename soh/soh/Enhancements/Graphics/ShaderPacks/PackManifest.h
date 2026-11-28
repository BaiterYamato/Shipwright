#pragma once

#include "PassSource.h"
#include "PackHistory.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace ShaderPacks {

inline unsigned PackManifestVersion(std::string_view text) {
    const auto manifest = nlohmann::json::parse(text);
    const auto& version = manifest.at("version");
    return version.is_number_unsigned() ? version.get<unsigned>() : 0;
}

// O provedor V2 fornece o JSON e os mesmos textos já carregados na V1.
// Não lê disco nem aceita paths novos; exige correspondência de ordem e arquivo.
// maxVersion 3 (ponte V3, RFC 0035) aceita também 16 passes, 8 entradas, saídas múltiplas e shaderModel 5.
inline void ApplyPackManifest(std::string_view text, std::vector<PassSource>& sources, unsigned maxVersion = 2) {
    if (text.empty() || text.size() > 128u * 1024u || sources.empty() || sources.size() > MaxPasses)
        throw std::runtime_error("Manifesto ou número de passes fora do limite.");
    const auto manifest = nlohmann::json::parse(text);
    const auto& versionField = manifest.at("version");
    const unsigned version = versionField.is_number_unsigned() ? versionField.get<unsigned>() : 0;
    if ((version != 2 && version != 3) || version > maxVersion || !manifest.at("passes").is_array() ||
        manifest.at("passes").size() != sources.size())
        throw std::runtime_error(maxVersion < 3 ? "Manifesto v2 incompatível." : "Manifesto v2/v3 incompatível.");
    const bool v3 = version == 3;
    const size_t maxPasses = v3 ? MaxPasses : LegacyMaxPasses;
    const size_t maxInputs = v3 ? MaxPassInputs : LegacyPassInputs;
    if (sources.size() > maxPasses)
        throw std::runtime_error("Manifesto ou número de passes fora do limite.");
    unsigned shaderModel = 4;
    if (manifest.contains("shaderModel")) {
        const auto& model = manifest.at("shaderModel");
        if (!v3 || !model.is_number_unsigned() || (model != 4 && model != 5))
            throw std::runtime_error("shaderModel exige manifesto v3 com valor 4 ou 5.");
        shaderModel = model.get<unsigned>();
    }
    const auto& passes = manifest.at("passes");
    std::map<std::string, size_t> names;
    bool hasFog = false;
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& pass = passes[i];
        const auto name = pass.at("name").get<std::string>();
        if (name.empty() || name.size() > 160 || name.starts_with("history:") || name.starts_with("texture:") || name == "scene" || name == "zero" || name == "one" ||
            name.starts_with("scene.") || name.find('#') != name.npos || !names.emplace(name, i).second ||
            pass.at("file").get<std::string>() != sources[i].file)
            throw std::runtime_error("Nome ou arquivo de passe inválido.");
        sources[i].persistent = pass.value("persistent", false);
        sources[i].fogOver = false;
        sources[i].shaderModel = shaderModel;
        sources[i].outputs = 1;
        if (pass.contains("outputs")) {
            const auto& outputs = pass.at("outputs");
            if (!v3 || !outputs.is_number_unsigned() || outputs < 1 || outputs > MaxPassOutputs ||
                (i + 1 == sources.size() && outputs != 1))
                throw std::runtime_error("outputs exige manifesto v3, de 1 a 8, e o passe final tem uma saída.");
            sources[i].outputs = outputs.get<unsigned>();
        }
        if (pass.contains("fog")) {
            if (!pass.at("fog").is_string() || pass.at("fog") != "over" || hasFog ||
                i + 1 == sources.size() || pass.contains("size") || sources[i].persistent || sources[i].outputs != 1)
                throw std::runtime_error("fog exige over em um único passe intermediário, relativo e sem histórico.");
            sources[i].fogOver = hasFog = true;
        }
        sources[i].vertexFile.clear();
        if (pass.contains("vertex")) {
            const auto& vertex = pass.at("vertex");
            if (!v3 || !vertex.is_string() || vertex.get<std::string>().empty() || vertex.get<std::string>().size() > 160 ||
                sources[i].fogOver)
                throw std::runtime_error("vertex exige manifesto v3, um include existente e um passe sem fog.");
            sources[i].vertexFile = vertex.get<std::string>();
        }
        sources[i].width = sources[i].height = 0;
        const auto divisor = pass.value("divisor", nlohmann::json(1u));
        if (!divisor.is_number_unsigned() || (divisor != 1 && divisor != 2 && divisor != 4))
            throw std::runtime_error("Divisor inválido.");
        sources[i].divisor = divisor.get<unsigned>();
        if (pass.contains("size")) {
            const auto& size = pass.at("size");
            if (pass.contains("divisor") || !size.is_array() || size.size() != 2 || !size[0].is_number_unsigned() ||
                !size[1].is_number_unsigned())
                throw std::runtime_error("Use size [largura, altura] ou divisor.");
            if (size[0] < 1 || size[1] < 1 || size[0] > 1024 || size[1] > 1024)
                throw std::runtime_error("Tamanho fixo fora do limite 1..1024.");
            sources[i].width = size[0].get<unsigned>();
            sources[i].height = size[1].get<unsigned>();
            if (!sources[i].width || !sources[i].height || sources[i].width > 1024 || sources[i].height > 1024)
                throw std::runtime_error("Tamanho fixo fora do limite 1..1024.");
        }
        if (i + 1 == sources.size() && (sources[i].persistent || sources[i].width || sources[i].divisor != 1))
            throw std::runtime_error("O passe final deve usar o alvo do mundo, sem histórico.");
    }
    std::map<std::string, int> sceneInputs{ { "scene", SceneColorInput },
                                            { "scene.depth", SceneDepthInput },
                                            { "scene.sunshadow", SceneSunShadowInput },
                                            { "scene.geometryshadow", SceneGeometryShadowInput },
                                            { "scene.fogshadow", SceneFogShadowInput },
                                            { "scene.pointshadow", ScenePointShadowInput } };
    if (v3) {
        sceneInputs.emplace("scene.noise", SceneNoiseInput);
        sceneInputs.emplace("scene.material.normal", SceneMaterialNormalInput);
        sceneInputs.emplace("scene.material.specular", SceneMaterialSpecularInput);
        sceneInputs.emplace("zero", ZeroInput);
        sceneInputs.emplace("one", OneInput);
    }
    // v3: "textures": [{"name", "file"}], com o arquivo (PNG) entre os includes; os passes leem "texture:nome".
    std::shared_ptr<PackTextures> textures;
    if (manifest.contains("textures")) {
        const auto& list = manifest.at("textures");
        if (!v3 || !list.is_array() || list.empty() || list.size() > MaxPackTextures)
            throw std::runtime_error("textures exige manifesto v3 com 1 a 6 texturas.");
        textures = std::make_shared<PackTextures>();
        const auto& includes = sources.front().includes;
        for (const auto& entry : list) {
            const auto name = entry.at("name").get<std::string>();
            const auto file = entry.at("file").get<std::string>();
            const bool validName = !name.empty() && name.size() <= 64 &&
                                   std::all_of(name.begin(), name.end(), [](char c) {
                                       return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                                              c == '-';
                                   });
            const auto found = includes ? includes->find(file) : IncludeFiles::const_iterator{};
            if (!validName || std::any_of(textures->begin(), textures->end(),
                                          [&](const PackTexture& texture) { return texture.name == name; }) ||
                !includes || found == includes->end())
                throw std::runtime_error("Textura do pack inválida ou ausente nos includes: " + name);
            const auto flip = entry.value("flip", nlohmann::json(false));
            if (!flip.is_boolean())
                throw std::runtime_error("flip da textura do pack deve ser booleano: " + name);
            sceneInputs.emplace("texture:" + name, PackTextureInputBase - static_cast<int>(textures->size()));
            textures->push_back({ name, file, found->second, flip.get<bool>() });
        }
    }
    for (auto& source : sources)
        source.textures = textures;
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& inputs = passes[i].at("inputs");
        if (!inputs.is_array() || inputs.size() > maxInputs)
            throw std::runtime_error("Entradas inválidas.");
        sources[i].inputs.fill(UnboundInput);
        for (size_t slot = 0; slot < inputs.size(); ++slot) {
            const auto input = inputs[slot].get<std::string>();
            if (const auto found = sceneInputs.find(input); found != sceneInputs.end()) {
                sources[i].inputs[slot] = found->second;
                continue;
            }
            const bool history = input.starts_with("history:");
            auto reference = history ? input.substr(8) : input;
            unsigned output = 0;
            if (const auto hash = reference.find('#'); hash != reference.npos) {
                const auto digits = reference.substr(hash + 1);
                if (!v3 || digits.empty() || digits.size() > 1 || digits[0] < '0' || digits[0] > '7')
                    throw std::runtime_error("Saída de passe inválida: " + input);
                output = static_cast<unsigned>(digits[0] - '0');
                reference.resize(hash);
            }
            const auto found = names.find(reference);
            if (found == names.end() || (history ? !sources[found->second].persistent : found->second >= i) ||
                output >= sources[found->second].outputs)
                throw std::runtime_error("Dependência ausente, futura ou não persistente.");
            const auto index = static_cast<unsigned>(found->second);
            sources[i].inputs[slot] = history ? HistoryInput(index + output * PassOutputStride)
                                              : PassOutputInput(index, output);
        }
    }
}

} // namespace ShaderPacks
