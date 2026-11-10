#include "PolicromaBridge.h"
#include "PassSource.h"
#include "WorldPass.h"

#ifdef _WIN32
#include <ship/Context.h>

#include <algorithm>
#include <cstring>

namespace {

void WriteError(char* output, uint32_t capacity, std::string_view message) {
    if (!output || !capacity) return;
    const size_t length = std::min<size_t>(message.size(), capacity - 1);
    std::memcpy(output, message.data(), length);
    output[length] = '\0';
}

uint32_t AppDirectory(char* output, uint32_t capacity) {
    try {
        const auto path = Ship::Context::GetAppDirectoryPath();
        if (path.empty() || path.size() >= 32768) return 0;
        const auto size = static_cast<uint32_t>(path.size() + 1);
        if (output && capacity >= size) std::memcpy(output, path.c_str(), size);
        return size;
    } catch (...) {
        return 0;
    }
}

bool ValidText(PolicromaText text, uint32_t limit) {
    return text.data && text.size > 0 && text.size <= limit;
}

std::string Copy(PolicromaText text) {
    return std::string(text.data, text.size);
}

bool Activate(const PolicromaProgram* program, char* error, uint32_t capacity) {
    try {
        if (!program || !ValidText(program->name, 1024) || !program->passes ||
            program->passCount == 0 || program->passCount > 8 ||
            program->includeCount > 32 || (program->includeCount && !program->includes) ||
            program->materialCount > PolicromaMaxMaterials || (program->materialCount && !program->materials)) {
            WriteError(error, capacity, "Programa Policroma incompatível.");
            return false;
        }
        auto includes = std::make_shared<ShaderPacks::IncludeFiles>();
        for (uint32_t i = 0; i < program->includeCount; ++i) {
            const auto& source = program->includes[i];
            if (!ValidText(source.path, 160) || !ValidText(source.text, 128 * 1024)) {
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
            if (!ValidText(pass.source.path, 160) || !ValidText(pass.source.text, 128 * 1024) ||
                (pass.divisor != 1 && pass.divisor != 2 && pass.divisor != 4) ||
                (i + 1 == program->passCount && pass.divisor != 1) ||
                !std::all_of(std::begin(pass.inputs), std::end(pass.inputs), [i](int32_t input) {
                    return input >= PolicromaScenePointShadowInput && input <= static_cast<int32_t>(i);
                })) {
                WriteError(error, capacity, "Passe HLSL inválido.");
                return false;
            }
            ShaderPacks::PassSource source;
            source.file = Copy(pass.source.path);
            source.source = Copy(pass.source.text);
            source.divisor = pass.divisor;
            std::copy_n(pass.inputs, PolicromaMaxInputs, source.inputs.begin());
            if (!includes->empty()) source.includes = includes;
            if (!materials->empty()) source.transmissionMaterials = materials;
            sources.push_back(std::move(source));
        }
        std::string diagnostic;
        if (ShaderPacks::ActivateWorldPass(sources, Copy(program->name), diagnostic)) return true;
        WriteError(error, capacity, diagnostic);
    } catch (...) {
        WriteError(error, capacity, "Falha ao preparar o programa DirectX 11.");
    }
    return false;
}

} // namespace

extern "C" __declspec(dllexport) const PolicromaBridgeV1* LinkSpan_GetPolicromaBridgeV1() {
    static const PolicromaBridgeV1 bridge{sizeof(PolicromaBridgeV1), AppDirectory, Activate,
                                         ShaderPacks::DisableWorldPass};
    return &bridge;
}
#endif
