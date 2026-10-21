#pragma once

#include <cstdint>
#include <string_view>
#include <thread>

#include "oot_engine.h"

namespace ShipLuaHost {

// HOST-SLIM-001 (RFC 0029): estado de vida dos serviços da F2 e árbitro de linkspan.oot.movement V3. Sem tipos do
// jogo, para entrar no oot_native_engine_tests; o binding real fica em OotNativeHooksGame.cpp. Thread do jogo.
struct OotActionSnapshot {
    void* play = nullptr;
    void* player = nullptr;
    const void* action = nullptr; // Player::actionFunc, só comparado
    uint64_t tick = 0;            // PlayState::gameplayFrames
    uint32_t flags = 0;           // SHIP_OOT_ACTION_STATE_*
    bool eligible = false;        // locomoção do jogo em gameplay normal
    bool neutral = false;         // eligible e no chão: Idle pode ser imposto
};

struct OotActionBridge {
    // false fora de gameplay (sem PlayState ou sem Player).
    bool (*snapshot)(OotActionSnapshot* out) = nullptr;
    // releaseIdle: põe o Link em Idle e toca a animação de Idle; senão só a animação (a ação já é Idle).
    ShipNativeStatus (*restore)(const OotActionSnapshot& at, bool releaseIdle) = nullptr;
};

void InitializeOotPlayerAction(std::thread::id gameThread);
void SetOotActionBridge(const OotActionBridge& bridge);
// OotNativeHooksGame.cpp, só no jogo.
void RegisterOotPlayerActionGame();

// Epoch: todo Play_Destroy. Geração do Player: criado, destruído ou visto noutro endereço.
uint64_t OotSceneEpoch();
uint64_t OotPlayerGeneration();
void AdvanceOotSceneEpoch();
void NotifyOotPlayerCreated(void* player);
void NotifyOotPlayerDestroyed(void* player);
// Toda troca de Player::actionFunc: o lease de ação cai.
void NotifyOotPlayerActionChanged(void* player);

// Dono = id do manifesto, 1..128 bytes, sem controle. Descarregado, fica bloqueado até o fim do processo.
bool ValidOotServiceOwner(const char* owner);
bool OotServiceOwnerBlocked(std::string_view owner);
void BlockOotServiceOwner(std::string_view owner);

// Quem depende do action_token (quads do colliders V2, root motion do player-body V1) é avisado quando o lease cai.
// O aviso nunca chama a DLL.
using OotActionRevokeFn = void (*)(uint64_t token);
void AddOotActionRevokeListener(OotActionRevokeFn listener);
// Lease vivo; `owner` (opcional) recebe o dono.
bool OotActionTokenLive(uint64_t token, std::string_view* owner = nullptr);

ShipNativeStatus SHIP_NATIVE_CALL GetOotActionState(ShipOotPlayerActionStateV1* state);
ShipNativeStatus SHIP_NATIVE_CALL AcquireOotAction(const char* owner, const ShipOotPlayerActionSpecV1* spec,
                                                   uint64_t* token);
ShipNativeStatus SHIP_NATIVE_CALL ReleaseOotAction(uint64_t token);
ShipNativeStatus SHIP_NATIVE_CALL RestoreOotPlayerAnimation(uint64_t token);
void ReleaseOotActionOwner(std::string_view owner);

// Na ponte de oot.player.action: com lease vivo para este Player, chama o dono.
// true = SUPPRESS válido; contexto perdido ou trocado devolve false e segue o gate vanilla.
bool RunOotOwnedAction(void* play, void* player, void* input, uint8_t vanillaAllowed);

} // namespace ShipLuaHost
