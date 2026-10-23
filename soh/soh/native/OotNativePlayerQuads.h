#pragma once

#include <cstdint>
#include <string_view>
#include <thread>

#include "oot_colliders.h"

namespace ShipLuaHost {

// HOST-SLIM-001 (RFC 0029): quads reais do Link arrendados por mod nativo (linkspan.oot.colliders V2). Lease, dono,
// action_token e snapshot dos acertos ficam aqui (entra no oot_native_engine_tests); o ColliderQuad do Player fica em
// OotNativeWorldGame.cpp. Thread do jogo.
constexpr uint32_t kOotPlayerQuadSlots = 3; // SHIP_OOT_QUAD_ATTACK_0, SHIP_OOT_QUAD_ATTACK_1, SHIP_OOT_QUAD_DEFENSE

struct OotPlayerQuadBridge {
    // Player atual; nullptr fora do gameplay.
    void* (*player)() = nullptr;
    // Corpo Lua definido (ship.oot.player.set_body): ele escreve nos mesmos quads.
    bool (*luaBodyActive)() = nullptr;
    // Guarda o material do slot (e o shieldMf no DEFENSE) para o release.
    void (*reserve)(void* player, uint32_t slot) = nullptr;
    // Material, vértices e inscrição AT/AC do slot no frame; tira antes a inscrição anterior do mesmo slot.
    // Falha preserva material/listas/defesa anteriores. LIMIT: capacidade; FAILURE: SetAT/AC recusou.
    ShipNativeStatus (*apply)(void* player, uint32_t slot, const ShipOotPlayerQuadSpecV2& spec) = nullptr;
    // Tira o slot das listas AT/AC do frame e desliga a defesa que o host ligou.
    void (*withdraw)(void* player, uint32_t slot) = nullptr;
    // withdraw e devolve o material guardado no reserve.
    void (*restore)(void* player, uint32_t slot) = nullptr;
    // Acertos do slot na resolução deste frame; não limpa as flags (o Player as lê e reseta).
    void (*read)(void* player, uint32_t slot, ShipOotColliderHitsV1* hits) = nullptr;
    // PlayState::gameplayFrames.
    uint64_t (*tick)() = nullptr;
    // Preflight antes de reserve/submit. UNSUPPORTED em SAC; cleanup nao depende disto.
    ShipNativeStatus (*ready)() = nullptr;
};

// Solta todos os leases com a ponte anterior antes de trocar.
void SetOotPlayerQuadBridge(const OotPlayerQuadBridge& bridge);
// Registra o aviso de revogação da ação (OotNativePlayerAction).
void InitializeOotPlayerQuads(std::thread::id gameThread);
void ReleaseOotPlayerQuadOwner(std::string_view owner);
// Algum slot arrendado: o corpo Lua não entra.
bool OotPlayerQuadsInUse();
// Guards das escritas do jogo (LinkSpan_PlayerQuadOwned).
bool OotPlayerQuadSlotOwned(void* player, uint32_t slot);
// Play_Update, logo depois do CollisionCheck_Damage: a colisão do frame já rodou e nenhum ator atualizou.
void OotPlayerQuadsResolved(void* player);

ShipNativeStatus SHIP_NATIVE_CALL AcquireOotPlayerQuads(const char* owner, uint64_t actionToken, uint32_t slotMask,
                                                        uint64_t* lease);
ShipNativeStatus SHIP_NATIVE_CALL SubmitOotPlayerQuad(uint64_t lease, uint32_t slot,
                                                      const ShipOotPlayerQuadSpecV2* spec);
ShipNativeStatus SHIP_NATIVE_CALL DisableOotPlayerQuad(uint64_t lease, uint32_t slot);
ShipNativeStatus SHIP_NATIVE_CALL ReadOotPlayerQuadHits(uint64_t lease, uint32_t slot, ShipOotPlayerQuadHitsV2* hits);
ShipNativeStatus SHIP_NATIVE_CALL ReleaseOotPlayerQuads(uint64_t lease);

} // namespace ShipLuaHost
