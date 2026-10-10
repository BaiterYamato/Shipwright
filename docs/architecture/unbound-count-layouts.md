**English** · [Português (Brasil)](unbound-count-layouts.pt-BR.md)

# Unbound: foundation for expanded counts

## Goal

The `OOT-UNBOUND-003A` work item removes the first structural limits inherited from the N64 that prevented larger
scenes and mods. It changes the host once so that external packages can use higher capacities without recompiling
Shipwright for each mod.

## Capabilities

| Structure | Before | Now |
| --- | ---: | ---: |
| Live actors (`ACTOR_NUMBER_MAX`) | 2.000 | 8.192 |
| Live actor counter | 8 bits | 16 bits |
| Object bank | 128 | 1.024 |
| Object index in `Actor` | signed 8 bits | signed 16 bits |
| Setup actors, rooms, and transition actors | 8 bits | 16 bits |
| Display list count for type 0/2 meshes | 8 bits | 32 bits |
| Type 2 meshes sorted per room | 64 | 1.024 |

The binary importer still reads the mesh count in the OTR v0 format as `u8`, but now correctly preserves values
between 128 and 255. The XML importer and future resource producers can populate the 32-bit representation.
Actor, room, and transition counts from 32-bit resources are explicitly clamped to the largest value representable
at runtime, with a diagnostic, instead of silently wrapping.

When an object list exceeds 1.024 entries, the loader keeps the internal count within the bank, logs the number
discarded, and prevents an invalid resource from leaving `ObjectContext` pointing beyond the array. When a room has
more than 1.024 type 2 meshes, the renderer logs the excess and draws only the first 1.024.

`Object_Spawn` also rejects an insertion when the bank is full before accessing the array, and `Object_IsLoaded`
rejects negative indices or indices outside the active count. The space in bytes reserved for object data remains
limited by the scene allocator; expanding this memory and the private `s8` indices kept by specific actors belongs
to subsequent work items.

## Built-in verification

`soh/soh/unbound/UnboundLayoutChecks.cpp` is compiled with the host and contains `static_assert`s to prevent a future
change from silently reverting to narrow types or creating a mismatch between the resource layout and the runtime.

## Limits left for subsequent work items

This work item does not yet expand actor, path, and collision coordinates, room IDs stored in `s8`, dynamic collision
tables, private object indices kept by some actors, or the dynamic catalog of scenes and entrances.
These changes require an audit of pointers and conversions to prevent truncation and will be implemented in separate
stages.
