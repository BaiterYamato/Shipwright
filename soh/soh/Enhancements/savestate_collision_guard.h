#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

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

// Process-heap tables are not part of sysHeapCopy. Capture full capacities,
// including inactive entries: actors can retain raw CollisionPoly* aliases.
// Only Capture allocates; Restore runs after the lifetime guard and heap copies.
struct SaveStateCollisionTables {
    std::vector<uint8_t> bgActors;
    std::vector<uint8_t> bgActorFlags;
    std::vector<uint8_t> dynaNodes;
    std::vector<uint8_t> dynaPolys;
    std::vector<uint8_t> dynaVertices;

    static void CaptureBytes(std::vector<uint8_t>& copy, const void* source, size_t bytes) {
        copy.resize(bytes);
        if (bytes != 0) {
            std::memcpy(copy.data(), source, bytes);
        }
    }

    static void RestoreBytes(void* destination, const std::vector<uint8_t>& copy) {
        if (!copy.empty()) {
            std::memcpy(destination, copy.data(), copy.size());
        }
    }

    void Capture(const CollisionContext& current) {
        const auto& dyna = current.dyna;
        CaptureBytes(bgActors, dyna.bgActors, static_cast<size_t>(dyna.bgActorMax) * sizeof(BgActor));
        CaptureBytes(bgActorFlags, dyna.bgActorFlags, static_cast<size_t>(dyna.bgActorMax) * sizeof(uint16_t));
        CaptureBytes(dynaNodes, dyna.polyNodes.tbl, static_cast<size_t>(dyna.polyNodes.max) * sizeof(SSNode));
        CaptureBytes(dynaPolys, dyna.polyList, static_cast<size_t>(dyna.polyListMax) * sizeof(CollisionPoly));
        CaptureBytes(dynaVertices, dyna.vtxList, static_cast<size_t>(dyna.vtxListMax) * sizeof(Vec3i));
    }

    void Restore(const CollisionContext& current) const {
        const auto& dyna = current.dyna;
        RestoreBytes(dyna.bgActors, bgActors);
        RestoreBytes(dyna.bgActorFlags, bgActorFlags);
        RestoreBytes(dyna.polyNodes.tbl, dynaNodes);
        RestoreBytes(dyna.polyList, dynaPolys);
        RestoreBytes(dyna.vtxList, dynaVertices);
    }
};
