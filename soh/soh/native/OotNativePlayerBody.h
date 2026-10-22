#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <thread>

#include "oot_player_body.h"

namespace ShipLuaHost {

// HOST-SLIM-001 (RFC 0029): corpo do Link por mod nativo (linkspan.oot.player-body V1). Validação, dono, geração,
// controle e pedidos de animação ficam aqui (entra no oot_native_engine_tests); o ator e o draw ficam em
// OotNativeSkeletonsGame.cpp.
struct OotPlayerBodyBridge {
    // Corpo Lua (ship.oot.player.set_body) definido: os dois não convivem.
    bool (*luaBodyActive)() = nullptr;
    // Ator auxiliar que atualiza e desenha `skeleton`; esconde o Link.
    ShipNativeStatus (*activate)(uint64_t skeleton, float groundOffset, void** instance) = nullptr;
    // inScene: o ator ainda existe (mata e, com showLink, mostra o Link); senão só solta o registro.
    void (*deactivate)(void* instance, bool inScene, bool showLink) = nullptr;
    // Segmentos 1..15 do draw, caminhos sem "__OTR__"; falha deixa os anteriores.
    ShipNativeStatus (*segments)(void* instance, const std::map<uint32_t, std::string>& segments) = nullptr;
    // MOVEACTOR do esqueleto no Player: deslocamento XZ se `move`; `restart` neutraliza a primeira amostra.
    void (*rootMotion)(void* instance, bool move, bool restart) = nullptr;
    // Só NOMOVE, sem enfileirar outro MOVEACTOR: o já enfileirado neste frame não move mais o Player.
    void (*rootStop)(void* instance) = nullptr;
};

void SetOotPlayerBodyBridge(const OotPlayerBodyBridge& bridge);
void InitializeOotPlayerBody(std::thread::id gameThread);
const ShipOotPlayerBodyV1& GetOotNativePlayerBodyService();
void ReleaseOotPlayerBodyOwner(std::string_view owner);
// Corpo nativo definido (ativo ou à espera da cena nova).
bool OotPlayerBodyInUse();
// Handle do esqueleto do corpo ativo; 0 sem corpo.
uint64_t OotPlayerBodySkeleton();

// Thread do jogo.
void OotPlayerBodyPlayerUpdate(void* play, void* player);      // fim do update do Link: recria na cena nova
void OotPlayerBodyTick(void* play, void* player);              // update do ator do corpo
bool OotPlayerBodyDraw(void* play, void* player, void* actor); // true = desenhar o esqueleto
void OotPlayerBodySceneEnded();                                // Play_Destroy

void RegisterOotPlayerBodyGameBridge(); // OotNativeSkeletonsGame.cpp, só no jogo

} // namespace ShipLuaHost
