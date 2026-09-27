// Estado compartilhado do mod Wind Waker Style: serviços do host, configuração e o estado de cada módulo.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_lights.h"
#include "oot_render.h"
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
};

struct Mod {
    const ShipNativeRuntime* runtime = nullptr;
    const ShipOotRenderV3* render = nullptr;
    const ShipOotLightsV1* lights = nullptr;
    const ShipOotWorldV1* world = nullptr;
    uint64_t renderState = 0;
    Settings cfg;
    ToonState toon;
    NaviLights navi;
    // Fadas soltas que este mod acendeu; desligar a opção devolve o raio 0 só a elas.
    std::unordered_set<const void*> litFairies;
    Stats stats;
};

// toon.cpp
ShipNativeStatus ApplyRenderState(Mod& mod);
void ToonFrame(Mod& mod, PlayState* play);
void ToonActorDraw(Mod& mod, const ShipOotRenderActorDrawHookV1& payload);

// fairy.cpp
void FairyLights(Mod& mod, ShipOotFairyLightHookV1& payload);

} // namespace WWStyle
