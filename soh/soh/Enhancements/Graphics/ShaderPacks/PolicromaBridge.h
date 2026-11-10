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

using GetPolicromaBridgeV1 = const PolicromaBridgeV1* (*)();
