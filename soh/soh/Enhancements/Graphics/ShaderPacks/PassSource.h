#pragma once

#include "PolicromaBridge.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ShaderPacks {

constexpr size_t MaxPassInputs = PolicromaMaxInputs;
constexpr int UnboundInput = PolicromaUnboundInput;
constexpr int SceneColorInput = PolicromaSceneColorInput;
constexpr int SceneDepthInput = PolicromaSceneDepthInput;
constexpr int SceneSunShadowInput = PolicromaSceneSunShadowInput;
constexpr int SceneGeometryShadowInput = PolicromaSceneGeometryShadowInput;
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
    std::array<int, MaxPassInputs> inputs{UnboundInput, UnboundInput, UnboundInput, UnboundInput};
    std::shared_ptr<const IncludeFiles> includes;
    std::shared_ptr<const TransmissionMaterials> transmissionMaterials;
};

} // namespace ShaderPacks
