# Unbound Framework (Link-Span)

`linkspan.unbound.framework` is the core extension that reads format 2 of SoH: Unbound (the SPEC in the
`roborich/Shipwright` fork, `unbound-docs/SPEC.md`) on the Shipwright 9.2.3 Link-Span host. It does not link
against `soh.exe`: it uses only the `linkspan.oot.*` services.

Unbound layers (`.o2r` files with `unbound.json`), placed directly in `mods/` or inside a mod, only enter the game
with this framework loaded. Without it, the host ignores the layer and logs the reason.

## What version 0.6.0 does

### Unbound 0.9: actors declared in JSON

- Each type resides in `unbound/actors/<nome>.json`; `unbound/actors/meumod/guarda.json`
  is the `meumod/guarda` type. Use that name in a room actor's `id`.
- The model can be a normal/flex skeleton with animation or a static display list,
  opaque or translucent, from the game or a mod. Resources are loaded by path.
- Supports looping animation, a pose at a single frame, scale, shadow, draw distance,
  textures in segments 8–12, hidden joints, cylindrical collision, dialogue, and a head
  that tracks Link with configurable axes.
- Types at the same path merge by key across mods; `null` removes the type.
  Existing actor names, such as `En_Kanban`, also work in the room's actor list.
- Unknown names, numeric IDs in the dynamic range, and object-valued `params` are
  skipped with a diagnostic. Transition actors still use numeric IDs;
  an invalid entry retains its index and does not create an actor.
- Incompatible models, animations, and textures are rejected before reaching rendering.
  The host rejects spawning nonexistent IDs and removes the old limit of 255 instances per type.

The framework requires the new `linkspan.oot.actor-models` v1 service. Types are
registered at startup; restart the game to change type files.

Example:

```json
{
  "name": "Guarda",
  "model": {
    "skeleton": "objects/object_toryo/object_toryo_Skel_007150",
    "animation": "objects/object_toryo/object_toryo_Anim_000E50",
    "shadow": 42
  },
  "collision": { "radius": 18, "height": 63 },
  "talk": { "message": "0xA001" },
  "look": { "limb": 15 }
}
```

Place the message in `text/eng/messages.json` and the actor in the room's JSON:
`"guarda": { "id": "meumod/guarda", "pos": [0, 0, 0], "params": 0 }`.
The actor number is assigned at load time and must never be saved in the map.
See [NOTICE.md](./NOTICE.md) for the code's origin.

- **Converted base (UNBOUND-006).** During init, before SoH mounts mods, converts vanilla scenes from the game's
  archives to format 2 and writes `oot-unbound.o2r` beside `oot.o2r`, with provenance in
  `oot-unbound.o2r.source.json` (converter version, ROM versions, size and date of `oot.o2r`,
  `oot-mq.o2r`, and the executable). Matching provenance reuses the file; different provenance triggers conversion
  again. Writing uses a `.tmp` followed by a rename: a partial file never replaces the previous one. The base is
  mounted immediately afterward, below the mods.
- **JSON types (UNBOUND-002/005).** Registers `unbound/scene/1`, `unbound/room/1`, `unbound/collision/3`, and
  `unbound/paths/1` in `linkspan.oot.resources` v3. A JSON resource with that `$schema` is merged across all
  layers (SPEC §3) and transcoded to the XML read by the host factories. A rejected document does not load,
  and the reason goes to `logs/linkspan-unbound.log` and the game log.
- **Vanilla scenes through JSON (§1.3/§1.6).** At `game.ready`, with a layer whose `unbound.json` includes
  `"scenes"` in `features`, each vanilla scene with `scenes/<cena>/scene.json` in the VFS starts loading from it
  (`override_scene`). A mod changes a vanilla scene with a layer at the same path: only the keys it supplies
  change. With the base active, the 0.3.0 actor patch (the `oot.room.actors` hook) is no longer used; the same
  document now merges directly into the room.
- **Registry (§7).** `unbound/scenes.json` registers scenes and entrances (`linkspan.oot.scenes` v2, with
  `titleCardTexture`), in the key order from §3.5. The game numbers scenes and entrances in that order; `sceneId`
  and `entrances.*.index` are obsolete and ignored with a note. Named exits (`ENTR_*`, `ENTR_RETURN_*`, or
  `"<cena>/<entrada>"`) are resolved when the scene loads.
- **Text (§5).** `text/<lang>/messages.json` (eng, ger, fra, jpn, staff) merges by message over the game's
  table (`linkspan.oot.text`): it adds, replaces, and removes with `null`. `$replace` in `messages`
  clears the table first. Messages over 8 192 bytes are truncated with a warning.
- **Mod conflicts (0.6.1).** At `game.ready`, before gameplay, every file that two or more Unbound mods
  provide at the same path is compared. Documents that Unbound merges (JSON with an `unbound/...` `$schema`
  in any layer, `unbound/scenes.json`, and `text/**`) are compared leaf by leaf using the same rule as the
  game: if the merge keeps everything each mod supplied (different keys, or the same value), it is a mergeable
  delta and is only logged. If a mod overwrites, removes, or discards with `$replace` a value another mod
  supplied, the warning is `unbound: conflito: <arquivo>: <mod de cima> sobrescreve N valor(es) de <mod de baixo>
  (no par, vale <mod de cima>, montado depois)`, with up to three example keys. Any other file (binary
  or untyped JSON) is delivered whole by the VFS: identical content is an identical copy; different content is
  `conflito: ... substitui
  o arquivo inteiro de ...`. The archive mounted later wins (path order in `mods/`). The summary is
  `unbound: mods: camadas=... conflitos=...`, and the full list goes to `logs/linkspan-unbound.log`. The converted
  base does not count as a mod.
- **Resource references (0.6.2).** Resource names change between ROM versions (display lists and textures with
  an offset in their names, for example), and a mod scene made against another version points to something the
  user's `oot.o2r` does not have; the game would only discover this upon entering the scene. At the same
  `game.ready`, each `.json` an Unbound mod supplies is merged with the lower layers and transcoded as the game
  would do (only the types the framework registers: `unbound/scene/1`, `room/1`, `collision/3`, `paths/1`), and
  every resource the XML tells the host to load is checked in the VFS: `mesh` (`opa`, `xlu`, background images),
  `collision`, `rooms`, `paths`, `cutscene`, `texCycle` textures, `sound.song`, and the collision's `bulk.file`.
  A missing resource produces the warning
  `unbound: aviso: <documento>: <campo> aponta para <caminho>, que nenhum archive montado tem (<mods>)`;
  a document the game would reject produces `aviso: <documento> seria recusado ao carregar: <motivo>`. The summary
  is `unbound: referências: documentos=... conferidas=... ausentes=... recusados=... jogo=<versões do oot.o2r>`;
  transcoder notes (unknown actors, discarded entries) go only to the Unbound log. Up to
  4 096 documents per boot.
- **Prelude limits (0.6.3).** Positional lists at the cap no longer cost the square of their size:
  `ordered_json` looks up keys sequentially, and a document with 65 535 surface types took ~9 s to parse and
  transcode. Parsing now builds the object with a key index (`ParseJson`), merging indexes large objects, and
  transcoding iterates through lists without looking up each key; the same document takes ~120 ms, and two
  complete layers take ~330 ms. This also applies to directive removal, room actors (`unbound/room/1`), and a
  large overlay over a small base. New notes, before gameplay for mod documents: more than 32 768 rooms (s16
  index), more than 65 535 actors in a room (8 192 since 0.6.9), more than 1 020 objects in a room (the bank has 1 024 slots, up to 4 of them
  with permanent objects: gameplay_keep, Link, the scene keep, and the horse), a fixed camera position outside
  s16 (§2 still wraps the value), and setups above 255 (ignored). The `ready` summary includes `ms=` for the check.
  The limit-1/limit/limit+1 boundaries are in `limits_tests.cpp`.
- **World limit (0.6.4).** A spawn, actor, transition actor, or collision `bounds` with a coordinate whose
  absolute value exceeds 1 048 576 (`BGCHECK_XYZ_ABSMAX`), in the value the game receives (f32 for positions, rounded
  integer for bounds), produces a note: there, the game deletes `EffectSs` particle effects (dust, sparks,
  splashes) instead of drawing them. Collision continues: `BgCheck_PosErrorCheck` only logs. In UNBOUND-021 the
  floor extends past the limit to the edge at ±1 048 800, and Link walks on it; the probe records the loss of ground
  just beyond the edge.
- **Type 2 mesh (0.6.5).** A room with `mesh.type = 2` and more than 1 024 entries produces a note: `z_room.c`
  only considers the first 1 024 (`SHAPE_SORT_MAX`), before the distance test, and does not draw the rest. The
  game's own warning uses `osSyncPrintf`, disabled in the build. Type 0 does not have this cap.
- **Collision cameras (0.6.6).** A surface or water box camera is an index into the `cameras` table, and the
  game does not check its size (`BgCheck_GetBgCamSettingImpl`, `WaterBox_GetCameraSType`): outside the table, it
  reads beyond the list. The surface camera is always read, even -1; the water camera is read only when positive
  (`Camera_GetWaterBoxDataIdx` treats 0 and negative values as no camera). Notes are produced for a camera outside
  the table (including any camera in an empty table), a camera above 32 767 (the game's camera state is s16), a
  camera whose positions do not fit in `cameraPositions` (the game reads `max(count, 3)` vectors starting at
  `positionIndex`: the fixed camera's `BgCamFuncData` has three, while the crawlspace reads `count` points),
  `count` without `positionIndex` (the factory supplies a single zero vector), and `count` outside
  0–32 767. Which preset uses positions is not checked: `count` 0 without `positionIndex` in a preset that reads
  them passes without a note. The XML does not change. None of the base game's 101 collision documents produces these notes.
- **Scene ↔ collision and room graph (0.6.7).** At `game.ready`, each mod scene is cross-checked with its
  collision and rooms (merged, from a mod or the base), where the host uses an index from one document to read
  another document's list without checking. Notes are produced for: a surface exit outside 1..N exits of each
  emitted setup (`z_player.c` reads `setupExitList[exit - 1]`), a water box room outside the rooms (the water is not
  active in any room; -1 means all), a door-side room beyond the rooms (`z_room.c` reads `roomList[room]` when
  setting up the scene), and a door-side camera outside the collision table (`Camera_ChangeDoorCam`; -1 and -99
  do not read). Anything that cannot be checked is reported as "grafo incompleto" and does not count as a note:
  a missing or rejected dependency, a `.meta` alias (the game loads the target; this also applies to a mod document
  and to the scene itself), or a dependency beyond the budget (4 096 reads and 64 MB triggered by the graph, from
  mods or the base), and a room that provides `exits` (replaces the list depending on the route). The check's cache
  stores only what the graph reads from rooms and collisions. For a delta over a base scene, the graph also runs
  on the base-only scene, and each item that is identical in both (same location, same limit, same field=value)
  is excluded and counted in `herdadas=`: vanilla `spot04` has cutscene setups with fewer exits than the collision
  exits, and that is not caused by the mod. Gaps are never excluded; rereading the base counts toward the budget,
  and with no budget left all notes remain, with a gap.
  The summary gains `grafos=`, `incompletos=`, `herdadas=`, and
  `dependencias=`. Only scenes supplied by a mod are roots; a mod that only replaces a base collision is not
  cross-checked with base scenes.
- **Minimap and pause map (0.6.8).** Two local scene notes. A vanilla dungeon scene, its MQ variant
  (`scenes/<cena>_mq`; bosses have no MQ), or its boss scene produces a note when it exceeds the dungeon minimap's
  rooms or room 31: `z_map_exp.c` (`Map_InitData`, `Map_SetPaletteData`, `Map_InitRoomData`) reads textures, palettes,
  and compass data by room without checking, and the tables are contiguous. The note specifies, for each table,
  which rooms cause reads into subsequent dungeons and from which room onward reads go beyond the entire table
  (239 texture names, rows of 32 palettes and 44 compass offsets per dungeon), and that visits stop being marked
  from room 32 onward. The palette read beyond the table is a write index into `mapPalette` and can write beyond
  it. In `ice_doukutu`, last in the list, the texture already goes beyond the list at room 12; in `MIZUsin` and
  `HIDAN`, the note can appear starting at 32, before their textures run out.
  The note states what the game reads: bosses do not draw the minimap, and drawing depends on the dungeon map. The
  other note is `cameraSettings.worldMapArea` outside 0..22 in an emitted setup: the game stores the value as s16;
  a negative value passes the pause world map's `< 22` guards, which read beyond the area tables, and in a vanilla
  outdoor scene the value also indexes `gBitFlags` and marks `worldMapAreaData` in the save. 22 (outside the map:
  fairy fountains and grottos) does not produce a note, but the host reads it beyond the pause frame's four tables
  of 22 (`func_80823A0C`, as in vanilla): the note does not certify the pause screen, and the guard belongs to the
  host. New scenes have no minimap (`Map_Init` and `Minimap_Draw` only know vanilla scenes) and, on the pause screen,
  show the world map in their `worldMapArea`. No base scene produces these notes (the 18 dungeon scenes fit in the
  minimap; areas range from 0 to 22).
- **Room actors and lights (0.6.9).** A room with more than 8 192 actors writes only the first 8 192, with a note.
  The host copies the room list into the `oot.room.actors` buffer (`LinkSpan_RoomActors`), which has 8 192 slots,
  and with more it writes beyond the buffer: a room with 8 200 rupees corrupted the heap in-game (UNBOUND-029).
  The total number of live actors also stops at 8 192, including Link, so the excess would never spawn. Before
  that, the game's arena may run out: in e-fixtures, 4 132 rupees (En_Item00) fit, and each of the remaining ones
  fails with "Cannot allocate actor" in the log.
- **Two-level light notes (0.6.10).** Per scene or room setup, a `lights` list with 33 to 255 entries receives
  the old pool note: without the host changes, there are only 32 slots in total, shared by lists, actors,
  and the environment, and the rest are discarded. With the changes, the limit is 255 per list command and 765
  cumulative nodes per scene for lists, separate from the 32 actor/environment slots. List lights do not return
  to the pool on room changes; reloads can exhaust the 765 nodes even with lists of up to 255. Above 255, the note
  warns that entries beyond 255 are discarded even with the changes (without them, beyond 32), with a warning in
  the host log.
  The framework does not detect the changes and retains all entries in the XML; these caps do not guarantee
  available slots or that all lights illuminate geometry at runtime. There is no cumulative note above 765:
  transcoding receives one document at a time and does not know the scene + rooms or the loading/reload history.
  Adding up all rooms or alternative setups would produce false positives; considering only the scene + current
  room would miss the accumulation.
  Discarding due to a lack of slots is still diagnosed by the host at runtime.

## Unbound 0.8 (new in 0.5.0)

The format remains at version 2, and old mods still load. What changed (SPEC §4.2 and §7,
`prelude-handoff.md` from 2026-09-19 and 2026-09-20):

- **Scene and entrance numbers.** The number depends on mounted mods, so two mods that fixed the same
  number collided, and the second disappeared. `sceneId` and `entrances.*.index` are now ignored, with a note.
- **Exits.** A number between `ENTR_MAX` (1556) and `0x7FF8`, the mod entrance range, rejects the document:
  mod entrances must be specified by name, `"<cena>/<entrada>"`. Vanilla and dynamic return entrances,
  `0x7FF9`–`0x7FFF` (grottos, fountains, shooting gallery, and bazaar), remain valid as numbers and also accept
  the `ENTR_RETURN_*` name.
- **`sound`.** `seq` ranges from 0 to 109 or 127 (no music); `natureAmbience` ranges from 0 to 19, with 19 = none.
  Values outside this range become "none", with a note: the engine indexes tables with these bytes, and the 255
  exporters used as "none" crashed the audio thread at the first sunset. `song` with `seq` 127 does not play,
  also with a note.
- **Save.** The host saves the entrance, saved scene, and Farore's Wind by name. Adding or removing a mod no
  longer sends the save to another mod's scene.

## Known differences from Unbound 0.8

- The base does not export text: the vanilla table serves as the lower layer. For a mod, the effect is as
  specified by the SPEC. Only the precedence of `override/` changes: JSON is applied after it.
- The transcoder runs on a thread in libultraship's resource pool, one at a time, while the game thread waits
  for the resource. Inside it, `has_file`, `read_file`, `read_file_layers`, and
  `find_entrance` are valid. The comment in `oot_resources.h` still says "thread do jogo" and will be corrected
  in the next SDK layout change.

### Prelude and old map files

The `oot-unbound.o2r` generated by Link-Span is a scene layer for this host. Although it has the same name,
it is not the complete base file produced by the official SoH: Unbound executable. Prelude checks the file's
provenance and rejects the `linkspan-unbound-converter 1` converter. To use Prelude, generate the
base with [SoH: Unbound 9.2.3-unbound0.8](https://github.com/roborich/Shipwright/releases/tag/9.2.3-unbound0.8)
from your own `oot.o2r`, or import your ROM directly into Prelude and select Unbound. Do not merely change
the name or the `source.converter` field: resources from the complete base are missing.

A distribution ZIP containing an `.o2r` must be extracted before being added with the editor's
**Add override .o2r or .otr** button. Importing into the editor only proves that it can read and
display the resources; it does not prove that the change will be used by the game. With an Unbound base mounted,
vanilla scenes are read from `scenes/<cena>/scene.json`: binary replacements in
`scenes/shared/<cena>_scene/` do not change those scenes. A new scene also needs an entry in
`unbound/scenes.json` to be accessible in-game. See the [Unbound SPEC, §§1 and 7](https://github.com/roborich/Shipwright/blob/unbound/unbound-docs/SPEC.md).
To use binary replacements for vanilla scenes without conversion, open the normal `oot.o2r` in Prelude and
add the `.o2r` files as layers; this runs normal SoH in the browser. A new scene still needs
its own entrance path in-game.

## Command-line tool

`tools/linkspan_unbound_convert.exe`:

```
linkspan_unbound_convert <pasta-extraida> <saida.o2r>   # converts resources extracted from an oot.o2r
linkspan_unbound_convert --check <pasta-de-assets>      # validates documents as the framework would
```

`--check` passes each `.json` through the same path as the game (single-layer merge, transcoding, text
reader, registry, and manifest) and exits with code 1 if any document is rejected. Entrance names are not
resolved outside the game.

## JSON schemas

The package includes a JSON Schema (draft 2020-12) in `schemas/unbound/` for each format 2 document:
`unbound.json`, scene, room, collision, paths, text, scene registry (`unbound/scenes.json`), and actor type
(`unbound/actors/<nome>.json`). They follow what this reader accepts; where the Unbound SPEC is more
permissive (integers as strings, `horse` as 0/1), the reader takes precedence. Each schema describes a layer, not
the merge result: partial layers and the `$replace`/`$order` directives pass.

```text
python schemas/validate_schemas.py --root <folder with unbound.json>
```

The validator requires Python's `jsonschema` package. Passing the schema only proves structure: it does not check
registered names, game resources, `collision.bin`, or limits that depend on another document.

## Native functions (for `main.lua`)

| Function | When | Result |
|---|---|---|
| `ready` | `game.ready` | base, registry, mod conflicts, and resource references with the current layers (calling again repeats the work) |
| `apply_text` | first `game.frame` | text; SoH only loads message tables after `game.ready` |
| `unbound_report` | anytime | base status, document count, and the latest notes |
| `load_scene_registry` | 0.2/0.3 compatibility | registry only |
| `room_report` | 0.3 compatibility | rooms seen by the actor patch |

The C service `linkspan.unbound.json_factory` v1 (`include/linkspan/unbound/json_factory.h`) remains as it was in
0.1.0.

## Build and test

```powershell
$sdk = (Resolve-Path ..\..\..\build\x64\native-sdk\Release\OotNativeSdk.cmake).Path
cmake -S . -B ..\..\..\build\unbound-scene-native -A x64 -DOOT_NATIVE_SDK="$sdk"
cmake --build ..\..\..\build\unbound-scene-native --config Release
ctest --test-dir ..\..\..\build\unbound-scene-native -C Release --output-on-failure
```

At the root of the host worktree:

- `tools/package-unbound-framework.ps1` generates the framework's single `.shipmod` (DLL, Lua, tool,
  header, documentation, and licenses), with SHA-256 and a content report;
- the scene, room actor, and 0.9 actor demos live in [link-span-examples](https://github.com/BaiterYamato/link-span-examples)
  (`oot-native/unbound/`), with packaging scripts in `tools/`;
- `tools/package-unbound-e-fixtures.ps1 -BaseArchive <oot-unbound.o2r>` generates the phase E fixtures. They
  come from the local base and contain game data, so they stay in `build/` and are not published.
