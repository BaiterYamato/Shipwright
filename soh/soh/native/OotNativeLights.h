#pragma once

#include <cstdint>
#include <string_view>
#include <thread>

#include "oot_lights.h"

namespace ShipLuaHost {

// Operações no jogo que linkspan.oot.lights delega, por vaga (0..LINKSPAN_OOT_LIGHTS_MAX-1). O binding real fica
// em OotNativeLightsGame.cpp, com um LightInfo fixo por vaga; os testes usam um falso. Nulo = UNSUPPORTED.
struct OotLightsBridge {
    // PlayState atual, ou nullptr fora do gameplay.
    void* (*gameplay)() = nullptr;
    // Grava a luz no LightInfo da vaga e o insere na lista de luzes de `play`; false com o buffer do jogo cheio.
    bool (*insert)(void* play, uint32_t slot, const ShipOotPointLightV1& light) = nullptr;
    void (*update)(uint32_t slot, const ShipOotPointLightV1& light) = nullptr;
    // Tira a luz da vaga da lista de `play`.
    void (*remove)(void* play, uint32_t slot) = nullptr;
    // LightInfo da vaga (a identidade que o mod compara com node->info).
    const void* (*info)(uint32_t slot) = nullptr;
};

void SetOotLightsBridge(const OotLightsBridge& bridge);
void InitializeOotNativeLights(std::thread::id ownerThread = std::this_thread::get_id());
// Tira da cena todas as luzes de mod.
void ResetOotNativeLights();
const ShipOotLightsV1& GetOotNativeLightsService();
// Unload de um mod: tira as luzes dele.
void ReleaseOotLightOwner(std::string_view owner);
// Fim da PlayState (troca de cena ou saída do gameplay), com a lista de luzes ainda de pé.
void ReleaseOotSceneLights();
uint32_t OotLightCount();

// Integração com o jogo (OotNativeLightsGame.cpp, só no jogo).
void RegisterOotLightsGameHooks();

} // namespace ShipLuaHost
