#pragma once

#include <cstdint>
#include <cstring>

// CollisionContext must be declared by the caller. This is private to savestates,
// outside the native SDK layout headers. Never serialize this guard into the heap.
struct SaveStateCollisionGuard {
    const CollisionContext* owner = nullptr;
    uint64_t generation = 0;
    CollisionContext saved = {};
    uint32_t staticPolyCount = 0;

    void Capture(const CollisionContext& current, uint64_t currentGeneration) {
        owner = &current;
        generation = currentGeneration;
        // Inactive retired-buffer slots need not be initialized. Copy their
        // representation without evaluating those pointer values.
        std::memcpy(&saved, &current, sizeof(saved));
        staticPolyCount = current.colHeader != nullptr ? current.colHeader->numPolygons : 0;
    }

    bool Matches(const CollisionContext& current, uint64_t currentGeneration) const {
        // Check the lifetime before inspecting any resource-owned header. Pointer
        // equality alone misses allocator address reuse after a scene transition.
        if (owner != &current || generation != currentGeneration) {
            return false;
        }
        if (saved.colHeader != current.colHeader || saved.lookupTbl != current.lookupTbl ||
            saved.subdivAmount.x != current.subdivAmount.x || saved.subdivAmount.y != current.subdivAmount.y ||
            saved.subdivAmount.z != current.subdivAmount.z ||
            staticPolyCount != (current.colHeader != nullptr ? current.colHeader->numPolygons : 0) ||
            saved.polyNodes.tbl != current.polyNodes.tbl || saved.polyNodes.max != current.polyNodes.max ||
            saved.polyNodes.polyCheckTbl != current.polyNodes.polyCheckTbl) {
            return false;
        }
        const auto& before = saved.dyna;
        const auto& now = current.dyna;
        if (before.bgActors != now.bgActors || before.bgActorFlags != now.bgActorFlags ||
            before.bgActorMax != now.bgActorMax || before.polyList != now.polyList ||
            before.polyListMax != now.polyListMax || before.vtxList != now.vtxList ||
            before.vtxListMax != now.vtxListMax || before.polyNodes.tbl != now.polyNodes.tbl ||
            before.polyNodes.max != now.polyNodes.max || before.polyNodesMax != now.polyNodesMax ||
            before.retiredCount != now.retiredCount || now.retiredCount < 0 ||
            now.retiredCount > DYNA_RETIRED_BUFFERS_MAX) {
            return false;
        }
        for (int i = 0; i < now.retiredCount; ++i) {
            if (before.retiredBuffers[i] != now.retiredBuffers[i]) {
                return false;
            }
        }
        return true;
    }
};
