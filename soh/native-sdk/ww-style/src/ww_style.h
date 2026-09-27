// Estado compartilhado do mod Wind Waker Style: serviços do host, configuração e o estado de cada módulo.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_lights.h"
#include "oot_render.h"
#include "oot_resources.h"
#include "oot_world.h"
#include "settings.h"
#include "z64.h"

namespace WWStyle {

constexpr const char* kOwner = "linkspan.ww_style";

// Luz-chave de um ator, suavizada entre fontes de luz. Chave por endereço do ator; o id e o último frame
// desenhado detectam a vaga reaproveitada por outro ator e as vagas abandonadas.
struct ToonKeyState {
    float dir[3];
    float col[3];
    float colVel[3];
    float shadowScale;    // tamanho da sombra, suavizado 0..1 para crescer/sumir sem estalo
    float shadowScaleVel; // velocidade do SmoothDamp de shadowScale
    // Chão por raycast em cache, para atores que nunca rodam bg check (floorPoly nulo): um ator parado paga um
    // raycast só; um que anda refaz depois de ~4 unidades.
    float floorY;
    float floorPos[3];
    bool floorValid;
    bool floorSampled;
    int16_t actorId;
    uint32_t lastFrame;
};

struct ToonState {
    std::unordered_map<const Actor*, ToonKeyState> keys;
    const void* play = nullptr;
    uint32_t frame = 0;
    float dt = 3.0f / 60.0f; // segundos por frame de jogo
    float alpha = 0.2f;      // fração do slerp por frame; chega a ~99% em transitionTime segundos
};

// As duas luzes da Navi, vistas pelo hook oot.light.fairy. Só comparadas com node->info, nunca lidas.
struct NaviLights {
    const void* actor = nullptr;
    const void* glow = nullptr;
    const void* noGlow = nullptr;
};

// Tremulação de uma chama: caminhada aleatória suavizada entre alvos, com a amostra anterior para reconhecer o
// ruído por frame das tochas.
struct FlameFlickerState {
    float cur;
    float prevTarget;
    float nextTarget;
    float phase;
    float maxSeen; // brilho cheio visto, esquecido devagar
    int lastInMax;
    int hold;      // frames que a luz ainda conta como chama depois do último salto
};

// Projeção de uma luz pontual: giro nos dois eixos, pulso de tamanho e de alfa, e o raio suavizado da Navi.
struct WorldLightState {
    float angleY;
    float angleX;
    float sizeCur;
    float sizeTarget;
    float sizeTimer;
    float alphaCur;
    float alphaTarget;
    float alphaTimer;
    float spawnRadius;
    uint32_t gen; // geração do último frame em que a luz apareceu
};

// Luzes do mundo (world.cpp). Chaves por LightInfo, o node->info da lista play->lightCtx.
struct LightingState {
    const void* play = nullptr;
    std::unordered_map<const void*, FlameFlickerState> flicker;
    std::unordered_map<const void*, WorldLightState> lights;
    // Fada solta → a luz sem brilho dela, vista pelo hook de fada. A luz fica dentro do ator, então o par não muda
    // enquanto o ator existir; o frame de desenho confere quem está vivo pela lista de atores.
    std::unordered_map<const void*, const void*> fairyNoGlow;
    std::vector<const void*> wildLights; // luzes das fadas soltas vivas neste frame
    std::vector<Gfx> dl;                 // projeções do frame; o renderer lê até o fim do frame
    uint64_t stickLight = 0;
    const void* stickInfo = nullptr;
    uint32_t gen = 0;
    uint32_t rng = 0x9E3779B9u;
    float dt = 3.0f / 60.0f;
};

struct Stats {
    uint32_t configures = 0;
    uint32_t actorDraws = 0;
    uint32_t excluded = 0;
    uint32_t toonOff = 0;
    uint32_t pointKeys = 0;
    uint32_t envKeys = 0;
    uint32_t keyFailures = 0;
    uint32_t shadowsArmed = 0;
    uint32_t shadowsOff = 0;
    uint32_t raycasts = 0;
    uint32_t fairyCalls = 0;
    uint32_t naviSeen = 0;
    uint32_t naviSkipped = 0;
    uint32_t naviTinted = 0;
    uint32_t wildFairyLit = 0;
    uint32_t flickered = 0;
    uint32_t pools = 0;
    uint32_t poolFailures = 0;
    uint32_t stickLights = 0;
    uint32_t skyDomes = 0;        // cúpulas desenhadas
    uint32_t skyStars = 0;        // estrelas do último frame
    uint32_t skyBands = 0;        // faixas do horizonte desenhadas
    uint32_t skyClouds = 0;       // nuvens do último frame
    uint32_t skyWisps = 0;        // fiapos do último frame
    uint32_t skyTexOverrides = 0; // texturas de nuvem trocadas por um pacote
    uint32_t skyFailures = 0;
};

struct Mod {
    const ShipNativeRuntime* runtime = nullptr;
    const ShipOotEngineV1* engine = nullptr;
    const ShipOotRenderV3* render = nullptr;
    const ShipOotLightsV1* lights = nullptr;
    const ShipOotWorldV1* world = nullptr;
    const ShipOotResourcesV1* resources = nullptr; // opcional: texturas de nuvem de um pacote montado
    uint64_t renderState = 0;
    Settings cfg;
    ToonState toon;
    NaviLights navi;
    // Fadas soltas que este mod acendeu; desligar a opção devolve o raio 0 só a elas.
    std::unordered_set<const void*> litFairies;
    LightingState lighting;
    Stats stats;
};

// toon.cpp
ShipNativeStatus ApplyRenderState(Mod& mod);
void ToonFrame(Mod& mod, PlayState* play);
void ToonActorDraw(Mod& mod, const ShipOotRenderActorDrawHookV1& payload);

// fairy.cpp
void FairyLights(Mod& mod, ShipOotFairyLightHookV1& payload);

// world.cpp
void WorldFrame(Mod& mod, PlayState* play);
void FlameFlicker(Mod& mod, ShipOotPointLightColorHookV2& light);
void DrawWorldLights(Mod& mod, PlayState* play);
void DekuStickLight(Mod& mod, PlayState* play);
void DropDekuStickLight(Mod& mod);

} // namespace WWStyle
