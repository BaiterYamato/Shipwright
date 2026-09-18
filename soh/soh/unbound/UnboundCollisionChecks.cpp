#include "global.h"
#include "soh/resource/type/CollisionHeader.h"
#include "soh/unbound/CollisionVertexWords.h"

#include <cstddef>
#include <limits>
#include <type_traits>

namespace {

using PolyVertexWords = decltype(((CollisionPoly*)nullptr)->vtxData);
using ResourceVertexWords = decltype(((SOH::CollisionPoly*)nullptr)->vtxData);
using VertexCount = decltype(((::CollisionHeader*)nullptr)->numVertices);
using PolygonCount = decltype(((::CollisionHeader*)nullptr)->numPolygons);
using ResourceVertexCount = decltype(((SOH::CollisionHeaderData*)nullptr)->numVertices);
using ResourcePolygonCount = decltype(((SOH::CollisionHeaderData*)nullptr)->numPolygons);
using NodePolyId = decltype(((SSNode*)nullptr)->polyId);
using NodeIndex = decltype(((SSNode*)nullptr)->next);
using ListHead = decltype(((SSList*)nullptr)->head);
using StaticNodeMax = decltype(((SSNodeList*)nullptr)->max);
using StaticNodeCount = decltype(((SSNodeList*)nullptr)->count);
using DynaNodeMax = decltype(((DynaSSNodeList*)nullptr)->max);
using DynaPolyStart = decltype(((DynaLookup*)nullptr)->polyStartIndex);
using DynaVtxStart = decltype(((BgActor*)nullptr)->vtxStartIndex);

static_assert(std::is_same_v<PolyVertexWords, u32[3]>, "CollisionPoly vertex words are narrower than 32 bits");
static_assert(std::is_same_v<ResourceVertexWords, PolyVertexWords>,
              "SOH::CollisionPoly does not mirror the runtime vertex words");
static_assert(COLPOLY_VTX_INDEX_MASK == UNBOUND_COLPOLY_INDEX_MASK && COLPOLY_VIA_FLAGS_MASK == UNBOUND_COLPOLY_FLAGS_MASK &&
                  COLPOLY_VIA_FLAGS_SHIFT == UNBOUND_COLPOLY_INDEX_BITS,
              "COLPOLY_* bit layout diverges from CollisionVertexWords.h");
static_assert((COLPOLY_VTX_INDEX_MASK & COLPOLY_VIA_FLAGS_MASK) == 0 &&
                  (COLPOLY_VTX_INDEX_MASK | COLPOLY_VIA_FLAGS_MASK) == 0xFFFFFFFFu,
              "Vertex index and flag bits overlap or leave gaps");
static_assert(COLPOLY_VIB_CONVEYOR == UNBOUND_COLPOLY_CONVEYOR &&
                  (COLPOLY_VIB_CONVEYOR & COLPOLY_VIA_FLAGS_MASK) == COLPOLY_VIB_CONVEYOR,
              "Conveyor flag left the flag bits");

constexpr bool MacrosMatchVertexWordHelpers() {
    constexpr u32 words[] = { 0u, 0x1FFFu, 0x1FFFFFFFu, 0x20000000u, 0x40001234u, 0x80000000u, 0xE0000000u, 0xFFFFFFFFu };

    for (u32 word : words) {
        if (COLPOLY_VTX_INDEX(word) != UNBOUND_COLPOLY_VTX_INDEX(word) ||
            ((word & COLPOLY_VIB_CONVEYOR) != 0) != ((word & UNBOUND_COLPOLY_CONVEYOR) != 0)) {
            return false;
        }
        for (u32 flags = 0; flags < 16; flags++) {
            if ((COLPOLY_VIA_FLAG_TEST(word, flags) != 0) != (UNBOUND_COLPOLY_FLAG_TEST(word, flags) != 0)) {
                return false;
            }
        }
    }
    return true;
}

static_assert(MacrosMatchVertexWordHelpers(), "COLPOLY_* macros diverge from the tested vertex-word helpers");

#define UNBOUND_SAME_OFFSET(Runtime, Resource, field) (offsetof(Runtime, field) == offsetof(Resource, field))

static_assert(sizeof(CollisionPoly) == 0x1C && // dist s32 (OOT-CORE-007)
                   sizeof(SOH::CollisionPoly) == sizeof(CollisionPoly),
              "CollisionPoly layout changed or the resource mirror diverged");
static_assert(UNBOUND_SAME_OFFSET(CollisionPoly, SOH::CollisionPoly, type) &&
                  UNBOUND_SAME_OFFSET(CollisionPoly, SOH::CollisionPoly, flags_vIA) &&
                  UNBOUND_SAME_OFFSET(CollisionPoly, SOH::CollisionPoly, flags_vIB) &&
                  UNBOUND_SAME_OFFSET(CollisionPoly, SOH::CollisionPoly, vIC) &&
                  UNBOUND_SAME_OFFSET(CollisionPoly, SOH::CollisionPoly, normal) &&
                  UNBOUND_SAME_OFFSET(CollisionPoly, SOH::CollisionPoly, dist),
              "SOH::CollisionPoly fields do not mirror CollisionPoly");

static_assert(sizeof(SOH::CollisionHeaderData) == sizeof(::CollisionHeader),
              "SOH::CollisionHeaderData size does not mirror CollisionHeader");
static_assert(UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, minBounds) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, maxBounds) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, numVertices) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, vtxList) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, numPolygons) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, polyList) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, surfaceTypeList) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, cameraDataList) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, numWaterBoxes) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, waterBoxes) &&
                  UNBOUND_SAME_OFFSET(::CollisionHeader, SOH::CollisionHeaderData, cameraDataListLen),
              "SOH::CollisionHeaderData fields do not mirror CollisionHeader");

#undef UNBOUND_SAME_OFFSET

static_assert(std::is_same_v<VertexCount, u32> && std::is_same_v<PolygonCount, u32>,
              "CollisionHeader counts still have a 16-bit limit");
static_assert(std::is_same_v<ResourceVertexCount, VertexCount> && std::is_same_v<ResourcePolygonCount, PolygonCount>,
              "Resource collision counts do not mirror the runtime layout");

static_assert(std::is_same_v<NodePolyId, s32>, "SSNode::polyId still truncates polygon indices");
static_assert(std::is_same_v<NodeIndex, u32> && std::is_same_v<ListHead, u32>,
              "SSNode/SSList indices still have a 16-bit limit");
static_assert(sizeof(SSNode) == 8, "SSNode size changed; update the node-size proofs");
static_assert(std::is_same_v<StaticNodeMax, u32> && std::is_same_v<StaticNodeCount, u32>,
              "SSNodeList capacity still has a 16-bit limit");
static_assert(std::is_same_v<decltype(SSNodeList_GetNextNodeIdx(nullptr)), u32> &&
                  std::is_same_v<decltype(DynaSSNodeList_GetNextNodeIdx(nullptr)), u32>,
              "Node index allocators still return 16-bit indices");
static_assert(std::numeric_limits<DynaNodeMax>::max() >= 16384, "DynaSSNodeList cannot hold its initial capacity");
static_assert(std::is_same_v<DynaPolyStart, u32> && std::is_same_v<DynaVtxStart, u32>,
              "Dyna poly/vertex start indices still have a 16-bit limit");

} // namespace
