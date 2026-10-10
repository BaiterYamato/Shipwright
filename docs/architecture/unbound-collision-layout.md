**English** · [Português (Brasil)](unbound-collision-layout.pt-BR.md)

# Unbound: expanded collision layout

## Goal

The `OOT-UNBOUND-003B` work item removes the 16-bit collision limits inherited from the N64. Scenes and dynamic actors
with more than 8.192 vertices or 65.535 polygons can now be represented, and lookup tables no longer depend on the
scene arena's fixed byte budget.

## Capabilities

| Structure | Before | Now |
| --- | ---: | ---: |
| Vertex index in `CollisionPoly` | 13 bits | 29 bits |
| Exclusion (`xpFlags`) and conveyor flags | top 3 bits of `u16` | top 3 bits of `u32` (bits 29 to 31) |
| `numVertices` and `numPolygons` in `CollisionHeader` | 16 bits | 32 bits |
| `SSNode::polyId` | signed 16 bits | signed 32 bits |
| Node indices (`SSNode::next`, `SSList::head`, `SS_NULL`) | 16 bits (`0xFFFF`) | 32 bits (`0xFFFFFFFF`) |
| `DynaLookup::polyStartIndex` and `BgActor::vtxStartIndex` | 16 bits | 32 bits |
| Static collision nodes | whatever remained of the scene's `memSize` | `max(2 × polygons, 4096)`, growing on demand |
| Dynamic collision nodes | 1.000 | 16.384, growing on demand |
| Dynamic polygons and vertices | 512 (1.024 with the SoH adjustment) | 16.384, fixed |
| Static lookup grid | fixed per scene | unchanged up to 16.384 polygons; above that, grows with the cube root of the count, up to 64 per axis |

`CollisionPoly` grew from 0x10 to 0x18 bytes. `SOH::CollisionPoly` and `SOH::CollisionHeaderData` mirror the runtime
layout field by field because the game directly uses the loaded resource's pointer.

## OTR v0 compatibility

The binary format still writes each vertex word as the N64's `u16`. The importer unpacks the 13-bit index and moves
the 3 flag bits to the top of the 32-bit word (`Unbound_UnpackLegacyVtxWord`), so existing `.otr` and `.o2r` files
load without regeneration.

The XML importer accepts two forms:

- legacy: `VertexA` and `VertexB` with the packed N64 word;
- expanded: plain indices in `VertexA`, `VertexB`, and `VertexC`, with `XpFlags` (0 to 7) and `Conveyor` as separate
  attributes. The presence of either attribute selects this form.

The XML also now writes `numVertices` and `numPolygons` as 32 bits; previously, both were truncated to 16.

## Memory and lifecycle

- The lookup table, static nodes, `polyCheckTbl`, and dynamic polygon, vertex, and node lists are allocated from the
  heap instead of the scene's `THA` arena. `Play_Destroy` calls `BgCheck_Free` after clearing the actors.
- The game state is not zero-initialized. `Play_Init` zeroes `CollisionContext` before loading the scene so that
  `BgCheck_Free` only encounters `NULL` or heap tables.
- Node tables double in size through `Unbound_GrowNodeCapacity`. Growth is rejected when capacity would exceed
  32 bits (static), `s32` (dynamic), or the addressable size in bytes. Nodes are addressed by index, and
  `StaticLookup_AddPolyToSSList` recalculates the node pointer after each insertion, so `realloc` does not leave
  dangling pointers.
- If the dynamic node table cannot grow, the polygon is excluded from the lookup for that frame, with no out-of-bounds
  write. A failure to grow the static table is fatal (`LOG_HUNGUP_THREAD`), as is running out of memory in the
  original `BgCheck_Allocate`.
- Dynamic polygon and vertex lists do not grow during gameplay because actors keep direct pointers to
  `dyna.polyList` (`floorPoly`, `wallPoly`). A `BgActor` that does not fit in the 16.384 entries has no collision for
  that frame instead of writing beyond the table.
- `memSize` no longer limits anything and is still calculated only for the log.

## Built-in verification

- `soh/soh/unbound/UnboundCollisionChecks.cpp` is compiled with the host and contains `static_assert`s for:
  - 32-bit width of vertex words, counts, node indices, and dynamic list start indices;
  - field-by-field correspondence between `CollisionPoly`/`CollisionHeader` and the resource types;
  - equality between the `COLPOLY_*` macros and the helpers in `soh/soh/unbound/CollisionVertexWords.h`.
- `oot_unbound_collision_tests` runs without a ROM and verifies that:
  - all 65.536 legacy `u16` words retain their index, `xpFlags`, and conveyor flag after unpacking;
  - 29-bit indices coexist with the 3 flag bits;
  - index remapping in `DynaPoly_ExpandSRT` exceeds 13 bits without losing flags;
  - node table growth respects indices, the 32-bit limit, the `s32` limit, and size in bytes.

## Limits left for subsequent work items

- Vertices remain in `Vec3s`, so collision coordinates range from -32.768 to 32.767.
- `SSNode::polyId` is signed: each lookup addresses at most 2³¹ - 1 polygons.
- `BG_ACTOR_MAX` remains 50, and dynamic polygon and vertex lists still have a fixed cap.
- `numWaterBoxes`, the surface type index (`CollisionPoly::type`), and camera data remain 16-bit.
- The OTR v0 binary format still writes 13-bit words. Scenes with more than 8.192 vertices require the expanded XML or
  a new version of the binary format.
- Collision headers have changed, so the layout id changes: native mods compiled against the previous headers are
  rejected and must be recompiled.
