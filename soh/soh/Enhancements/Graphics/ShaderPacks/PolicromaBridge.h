#pragma once

#include <stdint.h>

// Private Windows bridge between this host and the Policroma native provider.
// Inputs are borrowed for the duration of activate(). The host owns GPU resources.
enum {
    PolicromaMaxInputs = 4,
    PolicromaUnboundInput = -1,
    PolicromaSceneColorInput = 0,
    PolicromaSceneDepthInput = -2,
    PolicromaSceneSunShadowInput = -3,
    PolicromaSceneGeometryShadowInput = -4,
    PolicromaSceneFogShadowInput = -5,
    PolicromaScenePointShadowInput = -6,
    PolicromaMaxMaterials = 4,
};

struct PolicromaText {
    const char* data;
    uint32_t size;
};

struct PolicromaSource {
    PolicromaText path;
    PolicromaText text;
};

struct PolicromaPass {
    PolicromaSource source;
    uint32_t divisor;
    int32_t inputs[PolicromaMaxInputs];
};

struct PolicromaMaterial {
    uint32_t scene;
    PolicromaText texture;
    float tint[4];
    float light[4];
};

struct PolicromaProgram {
    PolicromaText name;
    const PolicromaPass* passes;
    const PolicromaSource* includes;
    const PolicromaMaterial* materials;
    uint32_t passCount;
    uint32_t includeCount;
    uint32_t materialCount;
};

struct PolicromaBridgeV1 {
    uint32_t size;
    // UTF-8 byte count, including NUL. A null buffer queries the required size.
    uint32_t (*appDirectory)(char* output, uint32_t capacity);
    bool (*activate)(const PolicromaProgram* program, char* error, uint32_t capacity);
    void (*off)();
};

// Export separado: não altera layout, stride ou semântica de V1.
struct PolicromaBridgeV2 {
    uint32_t size;
    uint32_t (*appDirectory)(char* output, uint32_t capacity);
    bool (*activate)(const PolicromaProgram* program, PolicromaText manifest, char* error, uint32_t capacity);
    void (*off)();
};
using GetPolicromaBridgeV2 = const PolicromaBridgeV2* (*)();

using GetPolicromaBridgeV1 = const PolicromaBridgeV1* (*)();

// RFC 0035: export separado que aceita manifestos v2 e v3 (16 passes, 16 entradas, 8 saídas, SM 5, b10 Iris).
// PolicromaPass.inputs[4] não muda; com manifesto, as entradas vêm do JSON.
enum {
    PolicromaV3MaxPasses = 16,
    PolicromaV3MaxInputs = 16,
    PolicromaV3MaxOutputs = 8,
    PolicromaSceneNoiseInput = -7,
    PolicromaZeroInput = -8,
    PolicromaOneInput = -9,
    // -10..-15: texturas próprias do pack ("textures" no manifesto v3), lidas como "texture:nome".
    PolicromaPackTextureInput = -10,
    PolicromaV3MaxTextures = 6,
};
enum : uint32_t {
    PolicromaFeatureShaderModel5 = 1u,
    PolicromaFeatureIrisConstants = 2u,
    // "textures" no manifesto v3 (entradas -10..-15) e o sampler s1 linear com repetição.
    PolicromaFeaturePackTextures = 4u,
    // setParameters: quatro números do provider no vetor 11 do LinkSpanIris, lidos a cada quadro.
    PolicromaFeatureProviderParameters = 8u,
    // b10 LinkSpanIris clock.zw = rampa da névoa nativa em "u" (a escala de gSPFogPosition): z = início, w = fim,
    // suavizados no host. {0, 0} = sem rampa válida neste quadro.
    PolicromaFeatureFogRamp = 16u,
    // b10 LinkSpanIris vetor 12 "sky" (13º float4, depois de parameters): x = quanto céu o jogador vê (0..1: interior 0,
    // céu visível 1, ao ar livre com o céu desligado pela sala 0,5), suavizado no host (~1 s, salta em troca de cena);
    // y = envCtx.indoors, z = envCtx.skyboxDisabled, w = skyboxId, crus. Sem lista de cenas.
    PolicromaFeatureSkyExposure = 32u,
};
struct PolicromaBridgeV3 {
    uint32_t size;
    uint32_t (*appDirectory)(char* output, uint32_t capacity);
    bool (*activate)(const PolicromaProgram* program, PolicromaText manifest, char* error, uint32_t capacity);
    void (*off)();
    uint32_t maxPasses, maxInputs, maxOutputs, features;
    // Só com PolicromaFeatureProviderParameters (e size que o inclua): até 4 valores finitos; os ausentes ficam 0.
    // Pode ser chamado de qualquer thread; vale a partir do próximo quadro e sobrevive a trocas de pack.
    void (*setParameters)(const float* values, uint32_t count);
};
using GetPolicromaBridgeV3 = const PolicromaBridgeV3* (*)();
