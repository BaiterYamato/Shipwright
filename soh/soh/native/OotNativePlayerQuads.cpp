#include "OotNativePlayerQuads.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "OotNativePlayerAction.h"

namespace ShipLuaHost {
namespace {

// Domínio próprio no topo do token, como os da F2d: token de outro serviço nunca passa por um daqui.
constexpr uint64_t kLeaseDomain = UINT64_C(0xC200000000000000);
constexpr uint64_t kSequenceMask = UINT64_C(0x0000FFFFFFFFFFFF);
constexpr uint32_t kAllSlots = SHIP_OOT_QUAD_MASK_ATTACK | SHIP_OOT_QUAD_MASK_DEFENSE;
constexpr float kMaxCoordinate = 1.0e6f;

struct QuadLease {
    uint64_t token = 0;
    std::string owner;
    uint64_t action = 0;
    uint32_t mask = 0;
    void* player = nullptr;
};

struct SlotState {
    uint64_t lease = 0;   // 0 = livre
    bool pending = false; // inscrito desde o último snapshot
    ShipOotPlayerQuadHitsV2 hits{};
};

std::thread::id gameThread;
OotPlayerQuadBridge bridge;
std::vector<QuadLease> leases; // slots disjuntos: no máximo dois (ATTACK e DEFENSE)
SlotState slots[kOotPlayerQuadSlots];
uint64_t nextSequence = 1;

bool OnGameThread() {
    return std::this_thread::get_id() == gameThread;
}

uint32_t SlotBit(uint32_t slot) {
    return 1u << slot;
}

bool KnownLease(uint64_t token) {
    const uint64_t sequence = token & kSequenceMask;
    return (token & ~kSequenceMask) == kLeaseDomain && sequence != 0 && sequence < nextSequence;
}

QuadLease* FindLease(uint64_t token) {
    for (auto& lease : leases) {
        if (lease.token == token) {
            return &lease;
        }
    }
    return nullptr;
}

ShipOotPlayerQuadHitsV2 EmptyHits(uint64_t tick) {
    ShipOotPlayerQuadHitsV2 hits{};
    hits.size = sizeof(hits);
    hits.scene_epoch = OotSceneEpoch();
    hits.resolved_tick = tick;
    hits.hits.size = sizeof(hits.hits);
    return hits;
}

// Solta o lease e devolve os slots ao jogo. Nada aqui chama a DLL.
void Drop(uint64_t token) {
    const auto found =
        std::find_if(leases.begin(), leases.end(), [token](const QuadLease& lease) { return lease.token == token; });
    if (found == leases.end()) {
        return;
    }
    const QuadLease lease = std::move(*found);
    leases.erase(found);
    void* const current = bridge.player ? bridge.player() : nullptr;
    for (uint32_t slot = 0; slot < kOotPlayerQuadSlots; ++slot) {
        if (!(lease.mask & SlotBit(slot))) {
            continue;
        }
        slots[slot] = {};
        if (bridge.restore && current && current == lease.player) {
            bridge.restore(lease.player, slot);
        }
    }
}

// Ação do lease caiu (aviso da F2d): os quads caem junto.
void OnActionRevoked(uint64_t action) {
    uint64_t dead[kOotPlayerQuadSlots];
    size_t count = 0;
    for (const auto& lease : leases) {
        if (lease.action == action && count < kOotPlayerQuadSlots) {
            dead[count++] = lease.token;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        Drop(dead[i]);
    }
}

// Lease vivo: ação viva e do mesmo dono, mesmo Player, dono carregado. Senão cai.
QuadLease* Live(uint64_t token) {
    QuadLease* lease = FindLease(token);
    if (!lease) {
        return nullptr;
    }
    std::string_view actionOwner;
    const bool actionLive = OotActionTokenLive(lease->action, &actionOwner); // o aviso pode derrubar este lease
    lease = FindLease(token);
    if (!lease) {
        return nullptr;
    }
    void* const current = bridge.player ? bridge.player() : nullptr;
    if (!actionLive || actionOwner != lease->owner || current != lease->player ||
        OotServiceOwnerBlocked(lease->owner)) {
        Drop(token);
        return nullptr;
    }
    return lease;
}

// INVALID_ARGUMENT: token que este serviço nunca deu; UNSUPPORTED: lease que já caiu.
ShipNativeStatus LeaseStatus(uint64_t token, QuadLease** out) {
    *out = nullptr;
    if (!OnGameThread() || !KnownLease(token)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *out = Live(token);
    return *out ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}

bool Finite(float value) {
    return std::isfinite(value) && std::fabs(value) <= kMaxCoordinate;
}

// Bits de resultado que o jogo escreve na resolução (z64collision_check.h): o mod não os põe no material.
constexpr uint8_t kAtResultBits = 0x06;    // AT_HIT | AT_BOUNCED
constexpr uint8_t kAcResultBits = 0x82;    // AC_HIT | AC_BOUNCED
constexpr uint8_t kTouchResultBits = 0x42; // TOUCH_HIT | TOUCH_DREW_HITMARK
constexpr uint8_t kBumpResultBits = 0x82;  // BUMP_HIT | BUMP_DRAW_HITMARK
constexpr uint8_t kMaxColType = 13;        // COLTYPE_TREE: o jogo indexa tabelas por col_type

bool ValidQuad(uint32_t slot, const ShipOotPlayerQuadSpecV2* spec) {
    if (!spec || spec->size < sizeof(ShipOotPlayerQuadSpecV2) || spec->defending > 1 || spec->col_type > kMaxColType ||
        (spec->at_flags & kAtResultBits) || (spec->ac_flags & kAcResultBits) ||
        (spec->touch_flags & kTouchResultBits) || (spec->bump_flags & kBumpResultBits)) {
        return false;
    }
    for (const auto& vertex : spec->vertices) {
        for (const float value : vertex) {
            if (!Finite(value)) {
                return false;
            }
        }
    }
    if (slot != SHIP_OOT_QUAD_DEFENSE) {
        return spec->defending == 0;
    }
    const float* m = spec->defense_matrix; // m[coluna * 4 + linha]
    for (int i = 0; i < 16; ++i) {
        if (!Finite(m[i])) {
            return false;
        }
    }
    return m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f && m[15] == 1.0f;
}

} // namespace

void SetOotPlayerQuadBridge(const OotPlayerQuadBridge& next) {
    while (!leases.empty()) {
        Drop(leases.front().token);
    }
    bridge = next;
}

void InitializeOotPlayerQuads(std::thread::id thread) {
    gameThread = thread;
    AddOotActionRevokeListener(OnActionRevoked);
}

ShipNativeStatus SHIP_NATIVE_CALL AcquireOotPlayerQuads(const char* owner, uint64_t actionToken, uint32_t slotMask,
                                                        uint64_t* lease) {
    if (lease) {
        *lease = 0;
    }
    if (!OnGameThread() || !lease || !ValidOotServiceOwner(owner) || !slotMask || (slotMask & ~kAllSlots) ||
        ((slotMask & SHIP_OOT_QUAD_MASK_ATTACK) &&
         (slotMask & SHIP_OOT_QUAD_MASK_ATTACK) != SHIP_OOT_QUAD_MASK_ATTACK)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (OotServiceOwnerBlocked(owner) || !bridge.player || !bridge.reserve || !bridge.apply || !bridge.withdraw ||
        !bridge.restore || !bridge.read || !bridge.ready) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    std::string_view actionOwner;
    if (!OotActionTokenLive(actionToken, &actionOwner) || actionOwner != owner) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    void* const player = bridge.player();
    if (!player) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const ShipNativeStatus ready = bridge.ready();
    if (ready != SHIP_NATIVE_OK) {
        return ready;
    }
    if (bridge.luaBodyActive && bridge.luaBodyActive()) {
        return SHIP_NATIVE_LIMIT;
    }
    for (uint32_t slot = 0; slot < kOotPlayerQuadSlots; ++slot) {
        if ((slotMask & SlotBit(slot)) && slots[slot].lease) {
            return SHIP_NATIVE_LIMIT;
        }
    }
    if (nextSequence > kSequenceMask) {
        return SHIP_NATIVE_LIMIT;
    }
    QuadLease next;
    try {
        next.owner = owner;
        leases.reserve(leases.size() + 1);
    } catch (...) { return SHIP_NATIVE_FAILURE; }
    next.token = kLeaseDomain | nextSequence++;
    next.action = actionToken;
    next.mask = slotMask;
    next.player = player;
    for (uint32_t slot = 0; slot < kOotPlayerQuadSlots; ++slot) {
        if (!(slotMask & SlotBit(slot))) {
            continue;
        }
        bridge.reserve(player, slot);
        slots[slot] = {};
        slots[slot].lease = next.token;
        slots[slot].hits = EmptyHits(0); // resolved_tick 0: nada resolvido ainda
    }
    *lease = next.token;
    leases.push_back(std::move(next));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL SubmitOotPlayerQuad(uint64_t token, uint32_t slot,
                                                      const ShipOotPlayerQuadSpecV2* spec) {
    if (slot >= kOotPlayerQuadSlots || !ValidQuad(slot, spec)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    QuadLease* lease = nullptr;
    const ShipNativeStatus status = LeaseStatus(token, &lease);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    if (!(lease->mask & SlotBit(slot))) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const ShipNativeStatus ready = bridge.ready();
    if (ready != SHIP_NATIVE_OK) {
        return ready;
    }
    const ShipNativeStatus applied = bridge.apply(lease->player, slot, *spec);
    if (applied != SHIP_NATIVE_OK) {
        return applied;
    }
    slots[slot].pending = true; // Falha nao cria pendencia nem apaga submit anterior bem-sucedido.
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL DisableOotPlayerQuad(uint64_t token, uint32_t slot) {
    if (slot >= kOotPlayerQuadSlots) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    QuadLease* lease = nullptr;
    const ShipNativeStatus status = LeaseStatus(token, &lease);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    if (!(lease->mask & SlotBit(slot))) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    bridge.withdraw(lease->player, slot);
    slots[slot].pending = false;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL ReadOotPlayerQuadHits(uint64_t token, uint32_t slot, ShipOotPlayerQuadHitsV2* hits) {
    if (slot >= kOotPlayerQuadSlots || !hits || hits->size < sizeof(ShipOotPlayerQuadHitsV2)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    QuadLease* lease = nullptr;
    const ShipNativeStatus status = LeaseStatus(token, &lease);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    if (!(lease->mask & SlotBit(slot))) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *hits = slots[slot].hits;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL ReleaseOotPlayerQuads(uint64_t token) {
    if (!OnGameThread() || !KnownLease(token)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    Drop(token); // lease que já caiu: nada a fazer
    return SHIP_NATIVE_OK;
}

void ReleaseOotPlayerQuadOwner(std::string_view owner) {
    uint64_t dead[kOotPlayerQuadSlots];
    size_t count = 0;
    for (const auto& lease : leases) {
        if (lease.owner == owner && count < kOotPlayerQuadSlots) {
            dead[count++] = lease.token;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        Drop(dead[i]);
    }
}

bool OotPlayerQuadsInUse() {
    return !leases.empty();
}

bool OotPlayerQuadSlotOwned(void* player, uint32_t slot) {
    if (slot >= kOotPlayerQuadSlots || !slots[slot].lease) {
        return false;
    }
    const QuadLease* lease = FindLease(slots[slot].lease);
    return lease && lease->player == player;
}

void OotPlayerQuadsResolved(void* player) {
    if (!OnGameThread() || leases.empty()) {
        return;
    }
    const uint64_t tick = bridge.tick ? bridge.tick() : 0;
    for (uint32_t slot = 0; slot < kOotPlayerQuadSlots; ++slot) {
        SlotState& state = slots[slot];
        const QuadLease* lease = state.lease ? FindLease(state.lease) : nullptr;
        if (!lease || lease->player != player) {
            continue;
        }
        ShipOotPlayerQuadHitsV2 hits = EmptyHits(tick);
        if (state.pending && bridge.read) {
            bridge.read(player, slot, &hits.hits);
            hits.hits.size = sizeof(hits.hits);
            hits.hits.oc_hit = 0;
            hits.hits.oc_actor = nullptr;
        }
        state.hits = hits;
        state.pending = false;
    }
}

} // namespace ShipLuaHost
