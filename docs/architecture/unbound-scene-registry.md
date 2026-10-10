**English** · [Português (Brasil)](unbound-scene-registry.pt-BR.md)

# Unbound: scene and entrance registry

## Goal

The `OOT-UNBOUND-003C` work item removes the limits imposed by the compiled `gSceneTable` and `gEntranceTable` tables
from the executable. Mods register scenes and entrances by name at runtime without recompiling the host, and the
Unbound framework reads the merged `unbound/scenes.json` to register them.

## Capabilities

| Structure | Before | Now |
| --- | ---: | ---: |
| `EntranceInfo.scene` | `s8` (127) | `s16` (32.767) |
| `EntranceInfo` | 4 bytes | 6 bytes |
| Scenes | 110 fixed | vanilla + ids 128 to 32.767 registered by mods |
| Entrances | 1.556 fixed | vanilla + groups of 4 up to index 32.764 |
| `gEntranceTable` | array | pointer to the current table |
| Saved flags per scene | `gSaveContext.sceneFlags[124]` | vanilla array for ids up to 123; registry by name for mod scenes |

## Responsibilities

- `soh/soh/native/OotNativeScenes.cpp` is the standalone registry: ids, indices, names, combined table, and flags. It
  does not touch game globals and runs in `oot_native_scenes_tests` without a ROM.
- `soh/soh/native/OotNativeScenesGame.cpp` exists only in `soh`: it supplies the vanilla tables (enum names from the
  X-macros), sets the `gEntranceTable` pointer, exposes `LinkSpan_SceneFlags`, `LinkSpan_EntranceCount`, and
  `LinkSpan_ReportInvalidEntrance` to C code, and implements travel.
- `soh/native-sdk/unbound-core` (`linkspan.unbound.framework`) reads and merges `unbound/scenes.json` and calls the service.
  No Unbound format rules reside in the executable.

## Entrance table

- Until a mod registers an entrance, `gEntranceTable` points to `gEntranceTableVanilla`.
- The first mod entrance creates the combined table: a copy of the vanilla entries, unused slots marked with
  `SCENE_ID_MAX` (such as `SCENE_UNUSED_6E`), and the mod groups. The binding listener updates the pointer on each change.
- The table does not shrink when a scene is removed. Its slots revert to `SCENE_ID_MAX`, and indices already read by
  the game remain within the table.
- The host reset (init and shutdown) restores the pointer to the vanilla table before freeing the combined table.

## In-game guards

- `Play_Init`: an entrance outside the table (a save from a different mod stack) becomes `ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN`;
  a layer outside the table uses layer 0. Both cases produce a log line.
- `OTRPlay_SpawnScene`: an id above the vanilla range with no registration uses the upstream fallback (Dodongo's Cavern). A mod scene
  uses a local `SceneTableEntry` with the registered draw config and registry path.
- `Actor_InitContext`, `Play_SaveSceneFlags`, `GameInteractor::RawAction::SetSceneFlag/UnsetSceneFlag`, and the save
  editor read flags through `LinkSpan_SceneFlags`.
- `SohUtils::GetSceneName` returns the mod scene's display name; the crash handler writes `mod scene`.

## Known limits

- Mod scene flags exist only for the session, stored by name; they are not yet written to the save.
- The SPEC's `titleCardTexture` and `layers` are not applied. All four layers of an entrance are identical.
- The map, minimap, MQ selection, debug warp, and randomizer treat mod ids as scenes without data.
- Code that indexes its own arrays by `play->sceneNum` outside the points above has not been fully audited.

## Built-in verification

- `UnboundLayoutChecks.cpp` checks that `EntranceInfo::scene` can represent 32.767.
- `oot_native_scenes_tests`: automatic and explicit ids, rejections (vanilla name, duplicate, out of range, draw
  config, definition size, thread), entrance groups and `field` packing, preservation of vanilla entries in the
  combined table, lookup by name, travel, removal without shrinking, flags by name, and reset.
- `oot_native_engine_tests` checks the service published by the host policy.
- `linkspan_unbound_json_tests` covers merging without `$schema` (with `null` removing a key) and registry reading.
