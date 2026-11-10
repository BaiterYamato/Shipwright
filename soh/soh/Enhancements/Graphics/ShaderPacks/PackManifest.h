#pragma once

#include "PassSource.h"
#include "PackHistory.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace ShaderPacks {

// O provedor V2 fornece o JSON e os mesmos textos já carregados na V1.
// Não lê disco nem aceita paths novos; exige correspondência de ordem e arquivo.
inline void ApplyPackManifest(std::string_view text, std::vector<PassSource>& sources) {
    if (text.empty() || text.size() > 128u * 1024u || sources.empty() || sources.size() > 8)
        throw std::runtime_error("Manifesto ou número de passes fora do limite.");
    const auto manifest = nlohmann::json::parse(text);
    if (manifest.at("version") != 2 || !manifest.at("passes").is_array() ||
        manifest.at("passes").size() != sources.size())
        throw std::runtime_error("Manifesto v2 incompatível.");
    const auto& passes = manifest.at("passes");
    std::map<std::string, size_t> names;
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& pass = passes[i];
        const auto name = pass.at("name").get<std::string>();
        if (name.empty() || name.size() > 160 || name.starts_with("history:") || name == "scene" ||
            name.starts_with("scene.") || !names.emplace(name, i).second ||
            pass.at("file").get<std::string>() != sources[i].file)
            throw std::runtime_error("Nome ou arquivo de passe inválido.");
        sources[i].persistent = pass.value("persistent", false);
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
    const std::map<std::string, int> sceneInputs{ { "scene", SceneColorInput },
                                                  { "scene.depth", SceneDepthInput },
                                                  { "scene.sunshadow", SceneSunShadowInput },
                                                  { "scene.geometryshadow", SceneGeometryShadowInput },
                                                  { "scene.fogshadow", SceneFogShadowInput },
                                                  { "scene.pointshadow", ScenePointShadowInput } };
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& inputs = passes[i].at("inputs");
        if (!inputs.is_array() || inputs.size() > MaxPassInputs)
            throw std::runtime_error("Entradas inválidas.");
        sources[i].inputs.fill(UnboundInput);
        for (size_t slot = 0; slot < inputs.size(); ++slot) {
            const auto input = inputs[slot].get<std::string>();
            if (const auto found = sceneInputs.find(input); found != sceneInputs.end()) {
                sources[i].inputs[slot] = found->second;
                continue;
            }
            const bool history = input.starts_with("history:");
            const auto found = names.find(history ? input.substr(8) : input);
            if (found == names.end() || (history ? !sources[found->second].persistent : found->second >= i))
                throw std::runtime_error("Dependência ausente, futura ou não persistente.");
            sources[i].inputs[slot] =
                history ? HistoryInput(static_cast<unsigned>(found->second)) : static_cast<int>(found->second + 1);
        }
    }
}

} // namespace ShaderPacks
