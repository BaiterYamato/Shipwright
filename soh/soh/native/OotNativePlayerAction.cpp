#include "OotNativePlayerAction.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace ShipLuaHost {
namespace {

constexpr size_t kMaxOwner = 128;
// Tokens de ação levam o domínio no topo: um token de outro serviço nunca passa por um daqui.
constexpr uint64_t kTokenDomain = UINT64_C(0xA300000000000000);
constexpr uint64_t kSequenceMask = UINT64_C(0x0000FFFFFFFFFFFF);

struct ActionLease {
    uint64_t token = 0; // 0 = livre
    std::string owner;
    uint64_t epoch = 0;
    uint64_t playerGeneration = 0;
    uint64_t actionGeneration = 0;
    void* play = nullptr;
    void* player = nullptr;
    const void* action = nullptr;
    ShipOotPlayerActionFnV1 callback = nullptr;
    void* user = nullptr;
};

struct IdleRequest {
    bool pending = false;
    uint64_t epoch = 0;
    uint64_t playerGeneration = 0;
    uint64_t actionGeneration = 0;
    void* play = nullptr;
    void* player = nullptr;
    const void* action = nullptr;
};

std::thread::id gameThread;
OotActionBridge bridge;
// Contadores nunca voltam: nem a policy nova nem a cena nova revalidam um token velho.
uint64_t sceneEpoch = 1;
uint64_t playerGeneration = 1;
uint64_t actionGeneration = 1;
uint64_t nextSequence = 1;
void* knownPlayer = nullptr;
ActionLease lease;
IdleRequest deferredIdle;
bool inCallback = false;
bool restoring = false;
bool revoking = false;
std::vector<std::string> blockedOwners;
std::vector<OotActionRevokeFn> revokeListeners;

bool OnGameThread() {
    return std::this_thread::get_id() == gameThread;
}

bool KnownToken(uint64_t token) {
    const uint64_t sequence = token & kSequenceMask;
    return (token & ~kSequenceMask) == kTokenDomain && sequence != 0 && sequence < nextSequence;
}

void SeePlayer(void* player) {
    if (player == knownPlayer)
        return;
    knownPlayer = player;
    ++playerGeneration;
    ++actionGeneration;
}

bool Snapshot(OotActionSnapshot& out) {
    out = {};
    if (!bridge.snapshot || !bridge.snapshot(&out) || !out.play || !out.player)
        return false;
    SeePlayer(out.player);
    return true;
}

// Solta o lease e avisa os dependentes. Nada aqui chama o dono.
void Drop() {
    if (!lease.token)
        return;
    const uint64_t token = lease.token;
    lease = {}; // callback e user já não podem chamar a DLL
    struct RevokeGuard {
        bool previous = revoking;
        RevokeGuard() {
            revoking = true;
        }
        ~RevokeGuard() {
            revoking = previous;
        }
    } guard;
    const size_t count = revokeListeners.size();
    for (size_t i = 0; i < count; ++i) {
        const auto listener = revokeListeners[i];
        listener(token);
    }
}

// Idle só no mesmo Player, na mesma ação em que o dono a tomou e com o Link no chão.
void RestoreIdle(const IdleRequest& request) {
    OotActionSnapshot now;
    if (lease.token != 0 || !bridge.restore || !Snapshot(now) || request.epoch != sceneEpoch ||
        request.playerGeneration != playerGeneration || request.actionGeneration != actionGeneration ||
        request.play != now.play || request.player != now.player || request.action != now.action || !now.neutral)
        return;
    restoring = true;
    bridge.restore(now, true);
    restoring = false;
}

// O dono soltou (ou saiu): Idle agora, ou depois do callback em curso.
void DropAndRequestIdle() {
    const IdleRequest request{ true,         lease.epoch, lease.playerGeneration, lease.actionGeneration, lease.play,
                               lease.player, lease.action };
    Drop();
    if (inCallback)
        deferredIdle = request;
    else
        RestoreIdle(request);
}

// Lease vivo: mesma cena, mesmo Player, mesma ação, Link elegível e dono carregado. Senão cai sem Idle.
bool Live(OotActionSnapshot* out = nullptr) {
    if (!lease.token)
        return false;
    OotActionSnapshot now;
    if (!Snapshot(now) || lease.epoch != sceneEpoch || lease.playerGeneration != playerGeneration ||
        lease.actionGeneration != actionGeneration || now.play != lease.play || now.player != lease.player ||
        now.action != lease.action || !now.eligible || OotServiceOwnerBlocked(lease.owner)) {
        Drop();
        return false;
    }
    if (out)
        *out = now;
    return true;
}

ShipOotPlayerActionStateV1 MakeState(const OotActionSnapshot* now) {
    ShipOotPlayerActionStateV1 state{};
    state.size = sizeof(state);
    state.scene_epoch = sceneEpoch;
    state.player_generation = playerGeneration;
    if (now) {
        state.tick = now->tick;
        state.flags = now->flags | (now->eligible ? 0u : SHIP_OOT_ACTION_STATE_BLOCKED);
    } else {
        state.flags = SHIP_OOT_ACTION_STATE_BLOCKED;
    }
    state.owned = lease.token != 0;
    return state;
}

} // namespace

void InitializeOotPlayerAction(std::thread::id thread) {
    gameThread = thread;
    Drop();
    deferredIdle = {};
}

void SetOotActionBridge(const OotActionBridge& next) {
    Drop();
    deferredIdle = {};
    bridge = next;
}

uint64_t OotSceneEpoch() {
    return sceneEpoch;
}

uint64_t OotPlayerGeneration() {
    return playerGeneration;
}

void AdvanceOotSceneEpoch() {
    Drop();
    deferredIdle = {};
    knownPlayer = nullptr;
    ++sceneEpoch;
    ++playerGeneration;
    ++actionGeneration;
}

void NotifyOotPlayerCreated(void* player) {
    Drop();
    deferredIdle = {};
    knownPlayer = player;
    ++playerGeneration;
    ++actionGeneration;
}

void NotifyOotPlayerDestroyed(void* player) {
    if (player != knownPlayer)
        return;
    Drop();
    deferredIdle = {};
    knownPlayer = nullptr;
    ++playerGeneration;
    ++actionGeneration;
}

void NotifyOotPlayerActionChanged(void* player) {
    if (player != knownPlayer)
        return;
    ++actionGeneration;
    if (!restoring)
        Drop();
}

bool ValidOotServiceOwner(const char* owner) {
    if (!owner)
        return false;
    const size_t length = strnlen(owner, kMaxOwner + 1);
    if (length == 0 || length > kMaxOwner)
        return false;
    for (size_t i = 0; i < length; ++i) {
        const auto c = static_cast<unsigned char>(owner[i]);
        if (c < 0x20 || c == 0x7f)
            return false;
    }
    return true;
}

bool OotServiceOwnerBlocked(std::string_view owner) {
    return std::find(blockedOwners.begin(), blockedOwners.end(), owner) != blockedOwners.end();
}

void BlockOotServiceOwner(std::string_view owner) {
    if (!owner.empty() && !OotServiceOwnerBlocked(owner))
        blockedOwners.emplace_back(owner);
}

void AddOotActionRevokeListener(OotActionRevokeFn listener) {
    if (listener && std::find(revokeListeners.begin(), revokeListeners.end(), listener) == revokeListeners.end())
        revokeListeners.push_back(listener);
}

bool OotActionTokenLive(uint64_t token, std::string_view* owner) {
    if (!token || token != lease.token || !Live())
        return false;
    if (owner)
        *owner = lease.owner;
    return true;
}

ShipNativeStatus SHIP_NATIVE_CALL GetOotActionState(ShipOotPlayerActionStateV1* state) {
    if (!OnGameThread() || !state || state->size < sizeof(*state))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    OotActionSnapshot now;
    if (!Snapshot(now)) {
        Drop();
        *state = MakeState(nullptr);
        return SHIP_NATIVE_UNSUPPORTED;
    }
    Live(); // derruba o lease vencido antes de responder
    *state = MakeState(&now);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL AcquireOotAction(const char* owner, const ShipOotPlayerActionSpecV1* spec,
                                                   uint64_t* token) {
    if (token)
        *token = 0;
    if (!OnGameThread() || !token || !ValidOotServiceOwner(owner) || !spec || spec->size < sizeof(*spec) ||
        !spec->callback)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (revoking)
        return SHIP_NATIVE_LIMIT; // nenhum ouvinte pode tomar a ação durante o aviso
    if (OotServiceOwnerBlocked(owner) || !bridge.restore)
        return SHIP_NATIVE_UNSUPPORTED;
    if (inCallback || restoring || Live())
        return SHIP_NATIVE_LIMIT; // um dono por vez, inclusive o mesmo
    OotActionSnapshot now;
    if (!Snapshot(now) || !now.eligible)
        return SHIP_NATIVE_UNSUPPORTED;
    if (nextSequence > kSequenceMask)
        return SHIP_NATIVE_LIMIT;
    try {
        lease.owner = owner;
    } catch (...) {
        lease = {};
        return SHIP_NATIVE_FAILURE;
    }
    lease.token = kTokenDomain | nextSequence++;
    lease.epoch = sceneEpoch;
    lease.playerGeneration = playerGeneration;
    lease.actionGeneration = actionGeneration;
    lease.play = now.play;
    lease.player = now.player;
    lease.action = now.action;
    lease.callback = spec->callback;
    lease.user = spec->user;
    *token = lease.token;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL ReleaseOotAction(uint64_t token) {
    if (!OnGameThread() || !KnownToken(token))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (token == lease.token)
        DropAndRequestIdle();
    return SHIP_NATIVE_OK; // token que já caiu: soltar de novo não é erro
}

ShipNativeStatus SHIP_NATIVE_CALL RestoreOotPlayerAnimation(uint64_t token) {
    if (!OnGameThread() || !KnownToken(token))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (restoring)
        return SHIP_NATIVE_LIMIT;
    OotActionSnapshot now;
    if (token != lease.token || !Live(&now) || !now.neutral || !(now.flags & SHIP_OOT_ACTION_STATE_IDLE))
        return SHIP_NATIVE_UNSUPPORTED;
    restoring = true;
    const ShipNativeStatus status = bridge.restore(now, false);
    restoring = false;
    return status;
}

void ReleaseOotActionOwner(std::string_view owner) {
    BlockOotServiceOwner(owner);
    if (lease.token && lease.owner == owner)
        DropAndRequestIdle();
}

bool RunOotOwnedAction(void* play, void* player, void* input, uint8_t vanillaAllowed) {
    if (!OnGameThread() || inCallback || !lease.token || lease.play != play || lease.player != player)
        return false;
    OotActionSnapshot now;
    if (!Live(&now))
        return false;
    const uint64_t token = lease.token;
    const uint64_t epoch = sceneEpoch;
    const uint64_t generation = playerGeneration;
    ShipOotPlayerActionTickV1 tick{};
    tick.size = sizeof(tick);
    tick.action_token = token;
    tick.play_state = play;
    tick.player = player;
    tick.input = input;
    tick.state = MakeState(&now);
    tick.vanilla_allowed = vanillaAllowed != 0;
    uint32_t disposition = SHIP_OOT_ACTION_PASS;
    ShipNativeStatus status = SHIP_NATIVE_FAILURE;
    {
        struct CallbackGuard {
            CallbackGuard() {
                inCallback = true;
            }
            ~CallbackGuard() {
                inCallback = false;
            }
        } callbackGuard;
        try {
            status = lease.callback(lease.user, &tick, &disposition);
        } catch (...) { status = SHIP_NATIVE_FAILURE; }
    }
    OotActionSnapshot after;
    if (!Snapshot(after) || after.play != play || after.player != player || epoch != sceneEpoch ||
        generation != playerGeneration) {
        deferredIdle = {};
        Drop();
        return false; // contexto perdido ou trocado: a disposição velha não vale
    }
    if (deferredIdle.pending) {
        const IdleRequest request = deferredIdle;
        deferredIdle = {};
        RestoreIdle(request);
    }
    if (token != lease.token || !Live())
        return false; // o dono soltou ou a ação mudou no callback: vale a regra do jogo
    if (status != SHIP_NATIVE_OK || disposition > SHIP_OOT_ACTION_SUPPRESS) {
        Drop();
        return false;
    }
    return disposition == SHIP_OOT_ACTION_SUPPRESS;
}

} // namespace ShipLuaHost
