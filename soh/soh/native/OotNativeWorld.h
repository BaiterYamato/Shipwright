#pragma once

#include <cstdint>
#include <thread>

#include "oot_colliders.h"
#include "oot_world.h"

namespace ShipLuaHost {

// Operações no jogo que linkspan.oot.world/colliders delegam. O binding real fica em
// OotNativeWorldGame.cpp; os testes usam um falso. Nulo = UNSUPPORTED.
struct OotWorldBridge {
    // PlayState atual, ou nullptr fora do gameplay.
    void* (*gameplay)() = nullptr;
    ShipNativeStatus (*raycastFloor)(void* play, float x, float y, float z, ShipOotWorldHitV1* hit) = nullptr;
    ShipNativeStatus (*lineTest)(void* play, const float* from, const float* to, uint32_t surfaces,
                                 ShipOotWorldHitV1* hit) = nullptr;
    ShipNativeStatus (*wallCheck)(void* play, const float* from, const float* to, float radius, float height,
                                  ShipOotWorldHitV1* hit) = nullptr;
    ShipNativeStatus (*waterSurface)(void* play, float x, float z, float* y) = nullptr;
    // Collider do jogo alocado pelo binding; nullptr em falha.
    void* (*colliderCreate)(void* play, const ShipOotCylinderSpecV1& spec) = nullptr;
    // play nulo quando a cena do collider já acabou.
    void (*colliderFree)(void* play, void* collider) = nullptr;
    void (*colliderSubmit)(void* play, void* collider) = nullptr;
    void (*colliderRead)(void* collider, ShipOotColliderHitsV1* hits) = nullptr;
    void (*knockback)(void* play, void* source, float speed, int16_t yaw, float yVelocity, uint32_t damage,
                      uint8_t large) = nullptr;
};

void SetOotWorldBridge(const OotWorldBridge& bridge);
void InitializeOotNativeWorld(std::thread::id ownerThread = std::this_thread::get_id());
// Libera todos os colliders.
void ResetOotNativeWorld();
const ShipOotWorldV1& GetOotNativeWorldService();
const ShipOotCollidersV1& GetOotNativeCollidersService();

// Fim de cada frame, depois das checagens de colisão: libera colliders destruídos
// num frame anterior.
void FlushOotColliders();
// Fim da PlayState (troca de cena ou saída do gameplay): todos os atores já saíram.
void ReleaseOotSceneColliders();
uint32_t OotColliderCount();

// Integração com o jogo (OotNativeWorldGame.cpp, só no jogo).
void RegisterOotWorldGameHooks();

} // namespace ShipLuaHost
