#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// CollisionContext must be declared by the caller. This is private to savestates,
// outside the native SDK layout headers. Never serialize this guard into the heap.
struct SaveStateCollisionGuard {
    struct WaterResource {
        const CollisionHeader* header;
        WaterBox* boxes;
        uint16_t count;
    };

    const CollisionContext* owner = nullptr;
    uint64_t generation = 0;
    CollisionContext saved = {};
    uint32_t staticPolyCount = 0;
    std::vector<WaterResource> waterResources;

    void CaptureWaterResource(const CollisionHeader* header) {
        if (header == nullptr) {
            return;
        }
        for (const auto& resource : waterResources) {
            if (resource.header == header) {
                return;
            }
        }
        waterResources.push_back({ header, header->waterBoxes, header->numWaterBoxes });
    }

    void Capture(const CollisionContext& current, uint64_t currentGeneration) {
        owner = &current;
        generation = currentGeneration;
        // Inactive retired-buffer slots need not be initialized. Copy their
        // representation without evaluating those pointer values.
        std::memcpy(&saved, &current, sizeof(saved));
        staticPolyCount = current.colHeader != nullptr ? current.colHeader->numPolygons : 0;
        waterResources.clear();
        CaptureWaterResource(current.colHeader); // Include zero capacity for the scene.
        for (int i = 0; i < current.dyna.bgActorMax; ++i) {
            if ((current.dyna.bgActorFlags[i] & 3) == 1 && current.dyna.bgActors[i].actor != nullptr) {
                const auto* header = current.dyna.bgActors[i].colHeader;
                // Empty dyna headers have no external water payload to restore.
                if (header != nullptr && header->numWaterBoxes != 0) {
                    CaptureWaterResource(header);
                }
            }
        }
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
        for (const auto& resource : waterResources) {
            // Never dereference a saved dyna header that is no longer reachable
            // from live collision. Conservatively refuse removal/replacement of
            // a water-bearing resource, even if a cache might still own it.
            bool live = resource.header == current.colHeader;
            for (int i = 0; !live && i < now.bgActorMax; ++i) {
                live = (now.bgActorFlags[i] & 3) == 1 && now.bgActors[i].actor != nullptr &&
                       now.bgActors[i].colHeader == resource.header;
            }
            if (!live || resource.header->waterBoxes != resource.boxes ||
                resource.header->numWaterBoxes != resource.count ||
                (resource.count != 0 && resource.boxes == nullptr)) {
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
    std::vector<std::vector<uint8_t>> waterBoxes;

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

    void Capture(const CollisionContext& current, const SaveStateCollisionGuard& guard) {
        const auto& dyna = current.dyna;
        CaptureBytes(bgActors, dyna.bgActors, static_cast<size_t>(dyna.bgActorMax) * sizeof(BgActor));
        CaptureBytes(bgActorFlags, dyna.bgActorFlags, static_cast<size_t>(dyna.bgActorMax) * sizeof(uint16_t));
        CaptureBytes(dynaNodes, dyna.polyNodes.tbl, static_cast<size_t>(dyna.polyNodes.max) * sizeof(SSNode));
        CaptureBytes(dynaPolys, dyna.polyList, static_cast<size_t>(dyna.polyListMax) * sizeof(CollisionPoly));
        CaptureBytes(dynaVertices, dyna.vtxList, static_cast<size_t>(dyna.vtxListMax) * sizeof(Vec3i));
        waterBoxes.resize(guard.waterResources.size());
        for (size_t i = 0; i < guard.waterResources.size(); ++i) {
            const auto& resource = guard.waterResources[i];
            // A malformed nonempty/null resource remains unloadable by Matches.
            CaptureBytes(waterBoxes[i], resource.boxes,
                         resource.boxes != nullptr ? static_cast<size_t>(resource.count) * sizeof(WaterBox) : 0);
        }
    }

    void Restore(const CollisionContext& current, const SaveStateCollisionGuard& guard) const {
        const auto& dyna = current.dyna;
        RestoreBytes(dyna.bgActors, bgActors);
        RestoreBytes(dyna.bgActorFlags, bgActorFlags);
        RestoreBytes(dyna.polyNodes.tbl, dynaNodes);
        RestoreBytes(dyna.polyList, dynaPolys);
        RestoreBytes(dyna.vtxList, dynaVertices);
        for (size_t i = 0; i < guard.waterResources.size(); ++i) {
            RestoreBytes(guard.waterResources[i].boxes, waterBoxes[i]);
        }
    }
};
