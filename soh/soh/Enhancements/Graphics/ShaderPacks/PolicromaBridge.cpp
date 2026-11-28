#include "PolicromaBridge.h"
#include "PassSource.h"
#include "WorldPass.h"
#include "PackManifest.h"

#ifdef _WIN32
#include <ship/Context.h>

#include <algorithm>
#include <cstring>

namespace {

void WriteError(char* output, uint32_t capacity, std::string_view message) {
    if (!output || !capacity)
        return;
    const size_t length = std::min<size_t>(message.size(), capacity - 1);
    std::memcpy(output, message.data(), length);
    output[length] = '\0';
}

uint32_t AppDirectory(char* output, uint32_t capacity) {
    try {
        const auto path = Ship::Context::GetAppDirectoryPath();
        if (path.empty() || path.size() >= 32768)
            return 0;
        const auto size = static_cast<uint32_t>(path.size() + 1);
        if (output && capacity >= size)
            std::memcpy(output, path.c_str(), size);
        return size;
    } catch (...) { return 0; }
}

bool ValidText(PolicromaText text, uint32_t limit) {
    return text.data && text.size > 0 && text.size <= limit;
}

std::string Copy(PolicromaText text) {
    return std::string(text.data, text.size);
}

bool ActivateProgram(const PolicromaProgram* program, PolicromaText manifest, char* error, uint32_t capacity,
                     unsigned maxVersion = 2) {
    try {
        const uint32_t maxPasses = maxVersion >= 3 ? PolicromaV3MaxPasses : 8;
        const uint32_t maxSource = maxVersion >= 3 ? 1024 * 1024 : 128 * 1024;
        // A V3 entrega também os vertex shaders dos passes como includes.
        const uint32_t maxIncludes = maxVersion >= 3 ? 64 : 32;
        if (!program || !ValidText(program->name, 1024) || !program->passes || program->passCount == 0 ||
            program->passCount > maxPasses || program->includeCount > maxIncludes || (program->includeCount && !program->includes) ||
            program->materialCount > PolicromaMaxMaterials || (program->materialCount && !program->materials)) {
            WriteError(error, capacity, "Programa Policroma incompatível.");
            return false;
        }
        auto includes = std::make_shared<ShaderPacks::IncludeFiles>();
        for (uint32_t i = 0; i < program->includeCount; ++i) {
            const auto& source = program->includes[i];
            if (!ValidText(source.path, 160) || !ValidText(source.text, maxSource)) {
                WriteError(error, capacity, "Include HLSL inválido.");
                return false;
            }
            includes->emplace(Copy(source.path), Copy(source.text));
        }
        auto materials = std::make_shared<ShaderPacks::TransmissionMaterials>();
        for (uint32_t i = 0; i < program->materialCount; ++i) {
            const auto& source = program->materials[i];
            if (!ValidText(source.texture, 160)) {
                WriteError(error, capacity, "Material de transmissão inválido.");
                return false;
            }
            ShaderPacks::TransmissionMaterial material{};
            material.scene = source.scene;
            material.texture = Copy(source.texture);
            std::copy_n(source.tint, 4, material.tint.begin());
            std::copy_n(source.light, 4, material.light.begin());
            materials->push_back(std::move(material));
        }
        std::vector<ShaderPacks::PassSource> sources;
        for (uint32_t i = 0; i < program->passCount; ++i) {
            const auto& pass = program->passes[i];
            const bool validLegacy =
                (pass.divisor == 1 || pass.divisor == 2 || pass.divisor == 4) &&
                (i + 1 != program->passCount || pass.divisor == 1) &&
                std::all_of(std::begin(pass.inputs), std::end(pass.inputs), [i](int32_t input) {
                    return input >= PolicromaScenePointShadowInput && input <= static_cast<int32_t>(i);
                });
            if (!ValidText(pass.source.path, 160) || !ValidText(pass.source.text, maxSource) ||
                (!manifest.data && !validLegacy)) {
                WriteError(error, capacity, "Passe HLSL inválido.");
                return false;
            }
            ShaderPacks::PassSource source;
            source.file = Copy(pass.source.path);
            source.source = Copy(pass.source.text);
            source.divisor = pass.divisor;
            std::copy_n(pass.inputs, PolicromaMaxInputs, source.inputs.begin()); // 4..7 só pelo manifesto v3.
            if (!includes->empty())
                source.includes = includes;
            if (!materials->empty())
                source.transmissionMaterials = materials;
            sources.push_back(std::move(source));
        }
        if (manifest.data) {
            if (!ValidText(manifest, 128 * 1024)) {
                WriteError(error, capacity, "Manifesto v2 inválido.");
                return false;
            }
            ShaderPacks::ApplyPackManifest(std::string_view(manifest.data, manifest.size), sources, maxVersion);
            for (auto& source : sources) {
                if (source.vertexFile.empty())
                    continue;
                const auto vertex = includes->find(source.vertexFile);
                if (vertex == includes->end()) {
                    WriteError(error, capacity, "Vertex shader ausente nos includes: " + source.vertexFile);
                    return false;
                }
                source.vertexSource = vertex->second;
            }
        }
        if (!ShaderPacks::ValidPassSources(sources)) {
            WriteError(error, capacity, "Grafo de passes inválido.");
            return false;
        }
        std::string diagnostic;
        if (ShaderPacks::ActivateWorldPass(sources, Copy(program->name), diagnostic))
            return true;
        WriteError(error, capacity, diagnostic);
    } catch (const std::exception& exception) { WriteError(error, capacity, exception.what()); } catch (...) {
        WriteError(error, capacity, "Falha ao preparar o programa DirectX 11.");
    }
    return false;
}

void SetParameters(const float* values, uint32_t count) {
    ShaderPacks::SetProviderParameters(values, count);
}

bool Activate(const PolicromaProgram* program, char* error, uint32_t capacity) {
    return ActivateProgram(program, {}, error, capacity);
}

bool ActivateV2(const PolicromaProgram* program, PolicromaText manifest, char* error, uint32_t capacity) {
    if (!ValidText(manifest, 128 * 1024)) {
        WriteError(error, capacity, "Manifesto v2 obrigatório.");
        return false;
    }
    return ActivateProgram(program, manifest, error, capacity);
}

bool ActivateV3(const PolicromaProgram* program, PolicromaText manifest, char* error, uint32_t capacity) {
    if (!ValidText(manifest, 128 * 1024)) {
        WriteError(error, capacity, "Manifesto v2/v3 obrigatório.");
        return false;
    }
    return ActivateProgram(program, manifest, error, capacity, 3);
}

} // namespace

extern "C" __declspec(dllexport) const PolicromaBridgeV1* LinkSpan_GetPolicromaBridgeV1() {
    static const PolicromaBridgeV1 bridge{ sizeof(PolicromaBridgeV1), AppDirectory, Activate,
                                           ShaderPacks::DisableWorldPass };
    return &bridge;
}
extern "C" __declspec(dllexport) const PolicromaBridgeV2* LinkSpan_GetPolicromaBridgeV2() {
    static const PolicromaBridgeV2 bridge{ sizeof(PolicromaBridgeV2), AppDirectory, ActivateV2,
                                           ShaderPacks::DisableWorldPass };
    return &bridge;
}
extern "C" __declspec(dllexport) const PolicromaBridgeV3* LinkSpan_GetPolicromaBridgeV3() {
    static const PolicromaBridgeV3 bridge{ sizeof(PolicromaBridgeV3), AppDirectory, ActivateV3,
                                           ShaderPacks::DisableWorldPass, PolicromaV3MaxPasses,
                                           PolicromaV3MaxInputs, PolicromaV3MaxOutputs,
                                           PolicromaFeatureShaderModel5 | PolicromaFeatureIrisConstants |
                                           PolicromaFeaturePackTextures | PolicromaFeatureProviderParameters |
                                           PolicromaFeatureFogRamp | PolicromaFeatureSkyExposure,
                                           SetParameters };
    return &bridge;
}
#endif
