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

constexpr size_t MaxPassInputs = PolicromaMaxInputs;
constexpr int UnboundInput = PolicromaUnboundInput;
constexpr int SceneColorInput = PolicromaSceneColorInput;
constexpr int SceneDepthInput = PolicromaSceneDepthInput;
constexpr int SceneSunShadowInput = PolicromaSceneSunShadowInput;
constexpr int SceneGeometryShadowInput = PolicromaSceneGeometryShadowInput;
constexpr int SceneFogShadowInput = PolicromaSceneFogShadowInput;
constexpr int ScenePointShadowInput = PolicromaScenePointShadowInput;
using IncludeFiles = std::map<std::string, std::string>;

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
    std::array<int, MaxPassInputs> inputs{ UnboundInput, UnboundInput, UnboundInput, UnboundInput };
    std::shared_ptr<const IncludeFiles> includes;
    std::shared_ptr<const TransmissionMaterials> transmissionMaterials;
};

inline bool ValidPassSources(std::span<const PassSource> sources) {
    if (sources.empty() || sources.size() > 8)
        return false;
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& pass = sources[i];
        if ((pass.divisor != 1 && pass.divisor != 2 && pass.divisor != 4) || (pass.width == 0) != (pass.height == 0) ||
            pass.width > 1024 || pass.height > 1024 || (pass.width && pass.divisor != 1) ||
            (i + 1 == sources.size() && (pass.width || pass.persistent || pass.divisor != 1)))
            return false;
        for (int input : pass.inputs) {
            if (IsHistoryInput(input)) {
                const auto index = HistoryInputIndex(input);
                if (index >= sources.size() || !sources[index].persistent)
                    return false;
            } else if (input < ScenePointShadowInput || input > static_cast<int>(i))
                return false;
        }
    }
    return true;
}

} // namespace ShaderPacks
