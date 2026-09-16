#include "OotNativeWorld.h"

#include <cmath>
#include <map>

namespace ShipLuaHost {
namespace {

struct ColliderRecord {
    void* collider = nullptr;
    void* play = nullptr;
    bool destroyed = false;
    uint64_t destroyedFrame = 0;
};

struct WorldState {
    std::thread::id ownerThread;
    OotWorldBridge bridge;
    uint64_t nextHandle = 1;
    uint64_t frame = 0;
    std::map<uint64_t, ColliderRecord> colliders;
};

WorldState& State() {
    static WorldState state;
    return state;
}

bool OnOwnerThread() {
    return State().ownerThread == std::this_thread::get_id();
}

void* Gameplay() {
    const auto& bridge = State().bridge;
    return bridge.gameplay ? bridge.gameplay() : nullptr;
}

bool Finite(const float* v) {
    return v && std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

bool ValidHit(const ShipOotWorldHitV1* hit) {
    return hit && hit->size >= sizeof(ShipOotWorldHitV1);
}

void ClearHit(ShipOotWorldHitV1* hit) {
    *hit = ShipOotWorldHitV1{};
    hit->size = sizeof(ShipOotWorldHitV1);
}

ShipNativeStatus SHIP_NATIVE_CALL RaycastFloor(float x, float y, float z, ShipOotWorldHitV1* hit) {
    const float pos[3]{ x, y, z };
    if (!OnOwnerThread() || !Finite(pos) || !ValidHit(hit)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    ClearHit(hit);
    void* play = Gameplay();
    const auto& bridge = State().bridge;
    return play && bridge.raycastFloor ? bridge.raycastFloor(play, x, y, z, hit) : SHIP_NATIVE_UNSUPPORTED;
}

ShipNativeStatus SHIP_NATIVE_CALL LineTest(const float* from, const float* to, uint32_t surfaces,
                                          ShipOotWorldHitV1* hit) {
    const uint32_t all = LINKSPAN_OOT_WORLD_LINE_WALL | LINKSPAN_OOT_WORLD_LINE_FLOOR | LINKSPAN_OOT_WORLD_LINE_CEILING;
    if (!OnOwnerThread() || !Finite(from) || !Finite(to) || !surfaces || (surfaces & ~all) || !ValidHit(hit)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    ClearHit(hit);
    void* play = Gameplay();
    const auto& bridge = State().bridge;
    return play && bridge.lineTest ? bridge.lineTest(play, from, to, surfaces, hit) : SHIP_NATIVE_UNSUPPORTED;
}

ShipNativeStatus SHIP_NATIVE_CALL WallCheck(const float* from, const float* to, float radius, float height,
                                           ShipOotWorldHitV1* hit) {
    if (!OnOwnerThread() || !Finite(from) || !Finite(to) || !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(height) || !ValidHit(hit)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    ClearHit(hit);
    void* play = Gameplay();
    const auto& bridge = State().bridge;
    return play && bridge.wallCheck ? bridge.wallCheck(play, from, to, radius, height, hit) : SHIP_NATIVE_UNSUPPORTED;
}

ShipNativeStatus SHIP_NATIVE_CALL WaterSurface(float x, float z, float* y) {
    if (!OnOwnerThread() || !std::isfinite(x) || !std::isfinite(z) || !y) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *y = 0.0f;
    void* play = Gameplay();
    const auto& bridge = State().bridge;
    return play && bridge.waterSurface ? bridge.waterSurface(play, x, z, y) : SHIP_NATIVE_UNSUPPORTED;
}

// Collider vivo da cena atual.
ColliderRecord* Live(uint64_t handle, void* play) {
    auto& colliders = State().colliders;
    const auto found = colliders.find(handle);
    if (found == colliders.end() || found->second.destroyed || found->second.play != play) {
        return nullptr;
    }
    return &found->second;
}

ShipNativeStatus SHIP_NATIVE_CALL CreateCylinder(const ShipOotCylinderSpecV1* spec, uint64_t* handle) {
    if (!OnOwnerThread() || !spec || spec->size < sizeof(ShipOotCylinderSpecV1) || !spec->actor || !handle ||
        spec->radius <= 0 || spec->height <= 0) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *handle = 0;
    auto& state = State();
    void* play = Gameplay();
    if (!play || !state.bridge.colliderCreate || !state.bridge.colliderFree) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (state.colliders.size() >= LINKSPAN_OOT_COLLIDERS_MAX) {
        return SHIP_NATIVE_LIMIT;
    }
    try {
        void* collider = state.bridge.colliderCreate(play, *spec);
        if (!collider) {
            return SHIP_NATIVE_FAILURE;
        }
        const uint64_t id = state.nextHandle++;
        try {
            state.colliders.emplace(id, ColliderRecord{ collider, play });
        } catch (...) {
            state.bridge.colliderFree(play, collider);
            throw;
        }
        *handle = id;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL Destroy(uint64_t handle) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    const auto found = state.colliders.find(handle);
    if (found == state.colliders.end() || found->second.destroyed) {
        return SHIP_NATIVE_FAILURE;
    }
    found->second.destroyed = true;
    found->second.destroyedFrame = state.frame;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Submit(uint64_t handle) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    void* play = Gameplay();
    auto* record = play ? Live(handle, play) : nullptr;
    if (!record || !bridge.colliderSubmit) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    bridge.colliderSubmit(play, record->collider);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL ReadHits(uint64_t handle, ShipOotColliderHitsV1* hits) {
    if (!OnOwnerThread() || !hits || hits->size < sizeof(ShipOotColliderHitsV1)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *hits = ShipOotColliderHitsV1{};
    hits->size = sizeof(ShipOotColliderHitsV1);
    const auto& bridge = State().bridge;
    void* play = Gameplay();
    auto* record = play ? Live(handle, play) : nullptr;
    if (!record || !bridge.colliderRead) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    bridge.colliderRead(record->collider, hits);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL KnockbackPlayer(void* source, float speed, int16_t yaw, float yVelocity,
                                                 uint32_t damage, uint8_t large) {
    if (!OnOwnerThread() || !source || !std::isfinite(speed) || !std::isfinite(yVelocity) || damage > 0xFF ||
        large > 1) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    void* play = Gameplay();
    if (!play || !bridge.knockback) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    bridge.knockback(play, source, speed, yaw, yVelocity, damage, large);
    return SHIP_NATIVE_OK;
}

const ShipOotWorldV1 worldV1{ sizeof(ShipOotWorldV1), RaycastFloor, LineTest, WallCheck, WaterSurface };
const ShipOotCollidersV1 collidersV1{ sizeof(ShipOotCollidersV1), CreateCylinder, Destroy, Submit, ReadHits,
                                      KnockbackPlayer };

void FreeRecord(const ColliderRecord& record, void* currentPlay) {
    const auto& bridge = State().bridge;
    if (bridge.colliderFree) {
        bridge.colliderFree(record.play == currentPlay ? currentPlay : nullptr, record.collider);
    }
}

} // namespace

void SetOotWorldBridge(const OotWorldBridge& bridge) {
    State().bridge = bridge;
}

void InitializeOotNativeWorld(std::thread::id ownerThread) {
    ResetOotNativeWorld();
    State().ownerThread = ownerThread;
}

void ResetOotNativeWorld() {
    auto& state = State();
    void* play = Gameplay();
    for (const auto& [handle, record] : state.colliders) {
        FreeRecord(record, play);
    }
    state.colliders.clear();
}

const ShipOotWorldV1& GetOotNativeWorldService() {
    return worldV1;
}

const ShipOotCollidersV1& GetOotNativeCollidersService() {
    return collidersV1;
}

void FlushOotColliders() {
    auto& state = State();
    void* play = state.bridge.gameplay ? state.bridge.gameplay() : nullptr;
    for (auto it = state.colliders.begin(); it != state.colliders.end();) {
        const auto& record = it->second;
        if (record.destroyed && record.destroyedFrame < state.frame) {
            FreeRecord(record, play);
            it = state.colliders.erase(it);
        } else {
            ++it;
        }
    }
    ++state.frame;
}

void ReleaseOotSceneColliders() {
    ResetOotNativeWorld();
}

uint32_t OotColliderCount() {
    return static_cast<uint32_t>(State().colliders.size());
}

} // namespace ShipLuaHost
