**English** · [Português (Brasil)](native-mods.pt-BR.md)

<a id="service-engine"></a>

# Link-Span native mods in Shipwright

This integration uses the Link-Span 0.5 SDK and loads `.zip`, `.shipmod` or
directories with a root `manifest.toml`. Native providers are enabled in the
host. The DLL belongs to the mod; its functions need no registration in the loader.

The legacy `linkspan.oot.engine` service remains at version 1 for existing mods.
The separate `linkspan.oot.movement` v1 service provides virtual input, physical
SDL buttons, transient bindings, sticks, grounded/rolling state, and native jump
and roll actions. Version 2 adds physical axes, a trigger bound to a virtual
button, native Lens/mask shortcuts, and C-button positions in the HUD. Lua calls
the DLL's private functions through `ship.native.call`; events use the existing
Lua API.

Pointers stay in native code and must not be retained across scenes or frames.
You may retain a service table until shutdown; query gameplay pointers again on
each call. Outside gameplay, player and scene pointers are null. Use the game
thread, except where a service explicitly documents another execution context.

<a id="service-resources"></a>

## `linkspan.oot.resources` v1 service (OOT-CORE-001)

Contract header: `soh/soh/native/oot_resources.h`. Name/version:
`LINKSPAN_OOT_RESOURCES_SERVICE` / version 1. `ShipOotResourcesV1` contains
`size` and eight function pointers:

- `has_file(path)`
- `read_file(path, output, capacity, output_size)`
- `list_files(search_mask, callback, user)`
- `dirty_resources(search_mask)`
- `unload_resource(path)`
- `mount_archive(archive_path, &handle)`
- `unmount_archive(handle)`
- `get_game_versions(output, capacity, output_count)`

The boundary is a C ABI with opaque handles. Buffers and callbacks belong to the
caller; no libultraship C++ type crosses the DLL boundary. `read_file` and
`get_game_versions` accept an initial call with `output=NULL, capacity=0` to query
the required size, and return `SHIP_NATIVE_LIMIT` for insufficient capacity.
`list_files` uses a glob mask, such as `objects/gameplay_keep/gArrow*`, sorts paths
and calls back once per path. A non-OK callback status stops enumeration.

`mount_archive` accepts `.o2r`/`.zip` (ZIP), `.otr`/`.mpq` (MPQ when compiled in),
or a directory. It returns an opaque uint64 handle. The provider must unmount
its archives during shutdown; the host also unmounts them on shutdown as a
safety net (`ClearNativeResourceArchives`). Mounting and mutations require the
game thread. VFS reads are also allowed inside a JSON transcode callback on a
resource thread (see resources v3); other contexts are rejected.

Known v1 concurrency limits: the thread guard blocks external calls but does
not serialize against the ResourceManager's internal thread pool or audio
thread, which read the same ArchiveManager maps without a mutex.
`dirty_resources` and `unload_resource` are best-effort cache operations.
`mount_archive`/`unmount_archive` have the highest risk: use windows without
in-flight loads, such as `game.ready` before entering a scene. The structural
fix is an ArchiveManager mutex in upstream libultraship, outside this slice.

The host initializes ArchiveManager with an empty set of allowed versions, so
`mount_archive` also accepts archives without a `version` file.

Inside an archive, `version` has endianness at byte 0 (0 = little, 1 = big),
followed by a uint32 game version. For example, `01 d4 3d a8 1f` is big-endian
0xD43DA81F. The runtime probe mounted a ZIP with `version` copied from `oot.o2r`.

The example provider (`soh/native-sdk/example/provider.cpp`) registers
`resource_probe` (synthetic read) and `resource_runtime_probe` (real VFS proof).
Its `main.lua` calls through `ship.native.call` at `game.ready` and logs with the
`core-001-probe` marker.

Validation on 2026-09-12: runtime session `core-001-20260912-1155`, with evidence
under `build/runtime-evidence/core-001-20260912-1155/`, completed boot to the
title screen without exceptions. Executable and archive SHA-256 hashes were
unchanged before/after. Recorded proof line:

`core-001-probe: versions=1:0xD43DA81F; has=1/0; version_file=0xD43DA81F listed=1; arrows=13 first=objects/gameplay_keep/gArrow1Anim; mount=ok handle=1 marker=match unmount=ok gone=1`.

<a id="service-registry"></a>

## `linkspan.oot.registry` v1 service (OOT-CORE-002)

Contract header: `soh/soh/native/oot_registry.h`. This is a generic dynamic
registry: Shipwright stores identity and bytes, while the coremod decides
whether they represent scenes, entrances, items, transformations or another
catalogue. `ShipOotRegistryV1` contains:

- `create_space` / `find_space` / `destroy_space`;
- `register_entry` / `unregister_entry`;
- `find_entry_by_name` / `find_entry_by_id`;
- `read_entry`, which copies the name, payload and ID;
- `list_entries` for deterministic enumeration.

Each space declares a numeric range and `stride`. With
`LINKSPAN_OOT_REGISTRY_AUTO_ID`, the host chooses the lowest free aligned ID.
Names and IDs are unique within a space. The host copies all data, limits names
to 255 bytes and payloads to 64 KiB, allows at most 64 spaces and 65,536 live
entries, and rejects calls outside the game thread.

`read_entry` accepts an initial query with both buffers null and capacities zero.
The returned name has an explicit length without a NUL terminator.
`destroy_space` removes entries in that space. The provider that created it must
destroy it during shutdown, so disabling or replacing a `.shipmod` does not
leave IDs occupied in the process.

The example creates `example/dynamic_movement/actions` and registers `jump` and
`sprint` during init. `registry_probe` checks lookup by name, payload reads and
enumeration. The host test checks that the space disappears on unload. Adding
entries to this catalogue requires rebuilding only the mod DLL.

Validation on 2026-09-12: the isolated session
`build/runtime-evidence/core-002-20260912-1445/` started Shipwright 9.2.3, loaded
ZIP 0.2.4, reached `ShipLua inicializado`, shut down normally and recorded:

`core-002-registry: space=ok; entries=2; first=128; jump=example/dynamic_movement/jump:action=jump; id=128`.

The session result was `valid: true`. Hashes of `soh.exe`, `oot.o2r`, `soh.o2r`
and tracked ZIPs were unchanged before/after.

## `linkspan.oot.resources` v2 service (OOT-UNBOUND-002A)

Contract header: `soh/soh/native/oot_resources.h`. Version 2 preserves the v1
binary prefix and adds `read_file_layers`. The host enumerates **every** mounted
copy of a virtual path, from lowest to highest priority, invoking a synchronous
callback per layer with:

- file bytes, valid only during the callback;
- source archive path (`base.o2r`, `override.shipmod`, etc.);
- archive game version, or 0 when absent;
- 64-bit FNV-1a content hash and byte size;
- `layer_index` / `layer_count` for order and total.

Merge rules (schema-based JSON, binary replacement, etc.) belong to the provider.
The host only supplies layers in stable order. A non-OK callback status stops
enumeration and propagates that status. Reads outside the game thread or a JSON
transcode callback are rejected with `SHIP_NATIVE_INVALID_ARGUMENT`.
`ArchiveManager::LoadFileFromAllLayers` exposes the same enumeration to internal
libultraship consumers.

The example exposes `layer_probe`, which builds a deterministic hash in received
layer order. `oot_native_engine_tests` checks order, bytes, source, hash, callback
interruption and external-thread rejection using two synthetic archives. The
independent proof rebuilds the DLL outside the tree against the same `soh.exe`.
This is the substrate Unbound uses for JSON deltas over vanilla scenes without
an Unbound-specific loader in the executable.

`layer_runtime_probe` mounts two ZIPs with `unbound/layer-probe.json`, verifies
base → override order, builds the hash and unmounts both before returning. It
validates the contract; an Unbound factory uses the same function with its own schema.

Robustness warning discovered during playtest, outside the service's scope:
boot crashes with 0xc0000005 in `Fast3dGui::LoadGuiTexture` when the installed
`soh.o2r` lacks custom SoH textures, such as
`textures/parameter_static/gTriforcePiece`, `gClimbTex`, `gCrawlTex`, `gGrabTex`
or `gOpenChestsTex`. The host does not null-check GUI icon loads. Generate
`soh.o2r` from the same tree/branch as the executable.

<a id="service-unbound-json-factory"></a>

## `linkspan.unbound.json_factory` v1 coremod (OOT-UNBOUND-002B)

The external project `soh/native-sdk/unbound-core` demonstrates a core extension
implementing a factory without adding Unbound rules to the executable. The
coremod queries resources v2, registers schema `linkspan.unbound.actor-patch/v1`
and publishes the C table `LinkSpanUnboundJsonFactoryV1` for other mods.

The service enumerates schemas, loads every layer of a path, validates `$schema`,
merges objects recursively, and replaces arrays/scalars with the highest-priority
layer. Results use opaque coremod-owned handles. The consumer queries JSON,
layer count and 64-bit FNV-1a hash, then calls `release`. JSON and handles have
limits; calls run on the game thread.

`tools/package-unbound-factory-demo.ps1` generates two independent mods, two
fixture layers and a bundle installed only in `mods/`. The isolated session
`build/runtime-evidence/unbound-002b-20260912-1938/` loaded the framework before
the consumer without rebuilding or changing `soh.exe`, and recorded:

`unbound-002b-factory: schema=linkspan.unbound.actor-patch/v1; layers=2; hash=412c9d58a4e6de21; json={..."health":8,"speed":1..."drops":["heart"]...}; cleanup=ok`

This historical slice did not connect the result to a ResourceLoader scene
factory. `null`, `$replace`, `$order`, scene/room factories and the resource
materialization adapter belonged to later Unbound slices. The current resources
v3 bridge is described below.

### Unbound layers without the framework

An Unbound layer (`.o2r` containing format-2 `unbound.json`), placed directly in
`mods/` or inside a mod directory, enters the root VFS only when
`linkspan.unbound.framework` is loaded. Otherwise the log says
`ShipLua ignorou a camada Unbound '…': o framework Unbound (…) não carregou`
and the game continues without the layer.

<a id="service-movement"></a>

## `linkspan.oot.movement` v2 service (OOT-MOVE-004)

Contract header: `soh/soh/native/oot_engine.h`; decision:
`extern/ship-lua/rfcs/0018-oot-movement-v2.md`. `ShipOotMovementV2` repeats v1
and adds:

- `get_gamepad_axis(port, sdl_axis)`: raw SDL physical-axis value;
- `bind_gamepad_axis(port, virtual_button, sdl_axis, direction)`: one half-axis
  bound to a virtual button in memory only. With ZR holding a C button, bow and
  hookshot aiming follows the native button path;
- `player_use_item_shortcut(item)`: Lens of Truth, mask, ocarina, tunic or boots
  without requiring a C-button assignment. The host applies player, scene, age,
  inventory and magic rules. Lens/masks use `Player_UseItem`; the ocarina starts
  in the same frame through `Player_ActionHandler_13`; tunics/boots use
  `AssignableTunicsAndBoots`, and using the equipped item returns to Kokiri gear;
- `get_item_button_rect(button, x, y, size, alpha)`: final HUD position of
  C-Left, C-Down and C-Right in the current or previous frame.

The shortcut-activated Lens stays active outside buttons until switched off or
assigned to one (the `Magic_Update` exception in `z_parameter.c`). A mask outside
buttons requires `gEnhancements.PersistentMasks`; otherwise use is rejected.
`hook.oot.hud.draw` runs before `Interface_Draw`, so the previous frame's capture
is accepted. This header participates in the layout ID: rebuild providers
against the new host SDK.

<a id="service-ocarina"></a>

## `linkspan.oot.ocarina` v1 service (OOT-MIC-001)

Contract header: `soh/soh/native/oot_ocarina.h`; decision:
`extern/ship-lua/rfcs/0019-oot-ocarina-service.md`. Ocarina input update
(`func_800EE6F4`) publishes whether a song is expected and which songs are accepted.
The service provides:

- `is_active()` and `get_available_song_flags()`: one bit per `OcarinaSongId`;
- `get_song_count()` and `get_song_pattern(song, notes, capacity, count)`: note
  indices from `gOcarinaSongNotes` (0=A, 1=C-Down, 2=C-Right, 3=C-Left, 4=C-Up);
- `submit_song(song)`: submits an accepted song; the game receives it on the next
  update as if it had been played.

`soh/native-sdk/mic-ocarina` uses the service. With the ocarina open, Start/+
activates the default microphone, YIN recognizes a hummed melody's contour, and
B turns capture off. The provider links static SDL2 and needs the SDK headers:

```powershell
$sdl2 = "<vcpkg>/installed/x64-windows-static/share/sdl2"
cmake -S soh/native-sdk/mic-ocarina -B build/mic-ocarina -G "Visual Studio 17 2022" -A x64 -DOOT_NATIVE_SDK="$sdk" -DSDL2_DIR="$sdl2"
cmake --build build/mic-ocarina --config Release
ctest --test-dir build/mic-ocarina -C Release
```

<a id="service-scenes"></a>

## `linkspan.oot.scenes` v1 service (OOT-UNBOUND-003C)

Contract header: `soh/soh/native/oot_scenes.h`; decision:
`extern/ship-lua/rfcs/0020-oot-scene-registry.md`; layout:
`docs/architecture/unbound-scene-registry.md`. The host can use registered scenes
and entrances instead of depending only on compiled tables:

- `register_scene(definition, handle, id)`: namespaced name, not a `SCENE_*` enum,
  VFS resource path, draw config and ID 128–32767 or automatic;
- `register_entrance(scene_handle, definition, index)`: a group of four entrance
  positions (child/adult, day/night), from the first free group after the 1,556
  vanilla entries, with final name `"<scene>/<entrance>"`;
- `unregister_scene(handle)`: removes the scene and entrances; the table does not shrink;
- `find_scene` and `find_entrance`: vanilla names (`SCENE_HYRULE_FIELD`,
  `ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN`) and mod names;
- `travel_to_entrance(index)`: fade transition, only with a player in the scene
  and no other transition underway.

Internally, `EntranceInfo.scene` became `s16` and `gEntranceTable` became a pointer.
It points at the vanilla table until a mod registers entrances, then at the
combined table. `OTRPlay_SpawnScene` resolves IDs from 128 through the registry.
Their saved flags live outside the fixed 124-entry array (`LinkSpan_SceneFlags`),
keyed by scene name during the session. A save with an entrance outside the table
falls back to Hyrule Field with a log message.

Unbound framework 0.2.0 uses this service to read `unbound/scenes.json` from every
layer at `game.ready` (§7 of the Unbound SPEC).
[`oot-native/unbound/scene-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/unbound/scene-demo)
registers a Hyrule Field copy and travels there;
`tools/package-unbound-scene-demo.ps1` in link-span-examples packages both demos.
The changes in `soh/include` and the new header change the layout ID: rebuild
native providers against the new host SDK.

`titleCardTexture` is now supported through scenes v2. The current framework
reads title cards and different entrance `layers`. New scenes still have no
dedicated minimap; the pause screen uses the scene's world-map area.

<a id="native-hooks"></a>

## Native OoT hooks (COREEXT-006)

With provider `abi_version = "1.2"`, a mod enters the game flow through
`register_hook` (contract: `extern/ship-lua/rfcs/0021-native-hooks.md`).
`soh/soh/native/oot_hooks.h` declares:

| v1 point | Payload | Modes | Location |
| --- | --- | --- | --- |
| `oot.play.update` | `ShipOotPlayHookV1` | observe, replace | `Play_Main` → `Play_Update` |
| `oot.actor.update` | `ShipOotActorHookV1` | observe, replace | `Actor_UpdateAll`, after culling and GameInteractor |
| `oot.actor.draw` | `ShipOotActorHookV1` | observe, replace | `Actor_Draw`, between segments and shadow |

- Without a hook, the game calls the original directly; cost is one registry query per actor.
- `replace` is exclusive per point. A replacement filters by `actor_id` and
  calls `call->call_original(call)` for other actors.
- A second `replace` receives `SHIP_NATIVE_LIMIT`; the current owner is logged.
- The mod's hooks are removed on unload before `shutdown`.

[`oot-native/hooks-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/hooks-demo)
counts frames/updates with observe and makes Link blink with a draw replacement.
Package with `tools/package-hooks-demo.ps1` in link-span-examples.

<a id="service-save"></a>

## `linkspan.oot.save` v1 service (OOT-CORE-006)

Header: `soh/soh/native/oot_save.h`; decision:
`extern/ship-lua/rfcs/0022-oot-save-namespaces.md`. Each mod stores a JSON block
per namespace in the save file's `linkspan` section:

- `open_namespace("author.mod", version, handle)`: `linkspan.` is reserved for the host;
- `read`/`write`/`erase`: JSON up to 1 MiB; writes reach disk on the next game save;
- `get_stored_version`: stored schema version for migration;
- `begin`/`commit`/`rollback`: transactions; saving records the pre-begin state;
- `set_required`: records the dependency; loading without the mod warns;
- `get_slot`: loaded slot or -1.

Events are ABI 1.2 observe hooks: `oot.save.loaded`, `oot.save.saving`,
`oot.save.deleted` and `oot.save.copied`, with `ShipOotSaveHookV1`.

Blocks from absent mods are preserved. Mod-scene flags (RFC 0020) are saved in
`linkspan.scenes`, keyed by scene name.

[`oot-native/save-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/save-demo)
counts save loads in namespace `linkspan-demo.save`. Package with
`tools/package-save-demo.ps1` in link-span-examples.

## Synthetic items and actor types (OOT-CORE-003)

<a id="service-items"></a>

### `linkspan.oot.items` v1

Header: `soh/soh/native/oot_items.h`.

- `register_item({name, icon, age, use, user}, &id)` allocates from `0xA0`–`0xEF`.
  The icon is a resource-manager 32x32 RGBA32 texture without `__OTR__`.
- `set_button_item(1..3, id)` assigns a C button; `0xFF` clears it.
  `get_button_item` reads a vanilla or synthetic assignment.
- Pressing the button calls `use(user, id, button)` inside Player update instead
  of the vanilla action. The host checks age.
- Synthetic button assignments are saved by name in `linkspan.items` and restored
  even if the runtime ID changes. An unregistered name clears the button and warns.
- `unregister_item` clears all assignments, including those stored for the other
  age. Register on init and unregister on shutdown.

Version 1 does not provide an inventory page (NEI-003).

### `linkspan.oot.items` v2 (OOT-CORE-003B)

Binary-prefix compatible with v1; it adds:

- `set_get_item(id, {model, layer, scale, message, receive, user})`: the model is
  a display list shown above Link; scale multiplies the native get-item scale
  of 0.2. Message is ASCII up to 200 bytes with CustomMessage codes (`&`, `%r`, `%w`, etc.).
- `give_item(id)`: Link raises it as an NPC gift with fanfare and a textbox.
  It returns `LIMIT` outside normal gameplay (including the title opening scene),
  in a cutscene or while Player is busy. When the textbox opens, the host calls
  `receive(user, id)`; the mod decides where the item goes (C button, save counter, etc.).

The host uses a `GetItemEntry` with its own `modIndex`. `z_player.c` redirects
only that modIndex to `receive`, bypassing vanilla tables. Text uses `OnOpenText`
with ID `0x7F00`.

<a id="service-actors"></a>

### `linkspan.oot.actors` v1

Header: `soh/soh/native/oot_actors.h`.

- `register_actor_type({name, category, flags, object, size, init, destroy,
  update, draw, user}, &id)` creates an ActorDB entry. Spawn it through engine's `spawn_actor`.
- Host wrappers invoke the callbacks. `unregister_actor_type` kills live instances
  and prevents later calls into the mod. Registering the same name again reuses its ID.
- Registering a type from inside an actor callback returns `UNSUPPORTED`, because
  `Actor_Spawn` retains a pointer to the table.
- `draw_display_list(play, path, translucent)` is valid only during draw and uses
  the actor matrix.

ActorDB is created before loading mods, so init may register types.

### Demo

[`oot-native/item-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/item-demo)
(0.2.0) registers `linkspan-demo.orb-wand` and actor `linkspan-demo.orb`.
In gameplay, if no C button holds the item, Link receives it through `give_item`.
`receive` equips an empty C button or C-Right. Each use launches a heart-model
orb that rises, spins and disappears. Package with
`tools/package-item-demo.ps1` in link-span-examples.

## Camera, rendering and Player limbs (OOT-CORE-005)

<a id="service-camera"></a>

### `linkspan.oot.camera` v1

Header: `soh/soh/native/oot_camera.h`.

- `acquire(owner, &token)` creates and activates a subcamera from the current view.
  It requires gameplay and the main camera. Cutscenes or another owner return `LIMIT`.
- `set_view(token, {eye, at, fov})` lasts until the next call and is reapplied
  before each camera update.
- Ownership is lost on a scene change or when the game activates another camera.
  `is_owned` checks the token; releasing a lost token returns `OK`.
- `get_view` reads the active camera regardless of owner.
- Release on mod shutdown.

<a id="service-render"></a>

### `linkspan.oot.render` v1

Header: `soh/soh/native/oot_render.h`.

- `draw_display_list(play, path, layer)`, opaque or translucent;
- `matrix_push`/`matrix_pop`, `matrix_translate`, `matrix_scale` and
  `matrix_rotate_zyx` operate on the current matrix;
- These require a draw scope: mod actor-type draw, `oot.actor.draw` or
  `oot.player.limb_draw`. Outside it they return `UNSUPPORTED`;
- At scope end the host unwinds unmatched pushes, up to 16 levels.

### `oot.player.limb_draw` v1 hook

- Observe only; payload `ShipOotPlayerLimbHookV1{play_state, actor, limb}`.
- Dispatches at the beginning of `Player_PostLimbDrawGameplay` with the limb matrix;
  the host restores the matrix afterward.
- Dark Link uses the same drawing path: filter by `actor->id`.

### Demo

[`oot-native/view-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/view-demo)
draws a heart in Link's right hand and alternates four seconds of orbiting camera
with four seconds of game camera. Package with `tools/package-view-demo.ps1`
in link-span-examples.

## World and colliders (OOT-CORE-004)

<a id="service-world"></a>

### `linkspan.oot.world` v1

Header: `soh/soh/native/oot_world.h`. Gameplay-only queries that do not change the game:

- `raycast_floor(x, y, z, &hit)`: first floor below the point, including position,
  normal, `bg_id` and floor type;
- `line_test(from, to, surfaces, &hit)`: wall/floor/ceiling between two points;
- `wall_check(from, to, radius, height, &hit)`: moving sphere with wall-corrected position;
- `water_surface(x, z, &y)`: water height, or `FAILURE` outside water.

<a id="service-colliders"></a>

### `linkspan.oot.colliders` v1

Header: `soh/soh/native/oot_colliders.h`.

- `create_cylinder(spec, &handle)`: host-owned cylinder using native AT/AC/OC flags,
  attached to a mod actor;
- `submit(handle)`: call every actor-update frame to follow the actor and submit
  enabled collision checks;
- `read_hits(handle, &hits)`: returns and clears previous-frame hits, including the
  other actor and attacker's `dmgFlags`;
- `destroy(handle)`: frees at the end of the next frame, because current-frame
  checks may still use it. Remaining colliders are removed at scene end
  (`OnPlayDestroy`) and host reset;
- `knockback_player(source, speed, direction, vertical_speed, damage, large)`: knocks Link back.

### Demo

[`oot-native/world-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/world-demo)
creates a target ahead of Link, grounded by `raycast_floor`. Sword hits rotate
the target, contact pushes Link, and logs show floor/water/line/wall queries.
Package with `tools/package-world-demo.ps1` in link-span-examples.

## JSON room actors (OOT-UNBOUND-004A, slice D1)

<a id="room-actors"></a>

### `oot.room.actors` v2 hook

Header: `soh/soh/native/oot_hooks.h`. Accepts TRANSFORM and OBSERVE. It dispatches
when a room's actor-list command runs, before spawning actors.

`ShipOotRoomActorsHookV2` contains:

- scene, room and scene layer (the alternate header being executed);
- room resource path;
- a copied list of `ShipOotActorEntryV2`, with float position, rotation and actor params;
- `count` and `capacity`.

TRANSFORM may edit, reorder, remove or append up to capacity. Actors spawn in
the resulting order. A failing transform must leave `entries` intact: the host
only restores payload fields. For an invalid returned payload, the host keeps
the original list and logs the issue.

### Unbound framework 0.3.0

The `linkspan.unbound.framework` coremod attaches a TRANSFORM to this hook. For
each room it:

1. Reads `scenes/<scene>/rooms/<n>.json` (SPEC §4.1) from every mounted layer.
2. Merges over the vanilla list, the lowest layer with keys `"0"` through `"n-1"`.
3. Returns `setups.<layer>.actors` in engine order.

The scene folder comes from the room path's `*_scene` folder; Master Quest adds
`_mq`. Merge follows SPEC §3: objects merge by key; `null` removes; arrays replace;
`$replace` discards lower layers; `$order` orders entries and remaining keys use
key order. The final `$schema` must be `unbound/room/1`.

Reading follows §2: integers accept fractions (truncated), decimal or `0x`
strings, and booleans. Values outside field width wrap. An invalid JSON layer
is skipped while the others continue.

`room_report` returns the latest room and patch. The framework's `main.lua`
logs the report when it changes.

### Demo

[`oot-native/unbound/field-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/unbound/field-demo)
mounts `assets/scenes/spot00/rooms/0.json` and, with a save open, travels once to
`ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN`. It reuses `scene-demo/scene_demo.cpp`.
`tools/package-unbound-scene-demo.ps1` in link-span-examples packages scene demo
0.1.3 and field demo 0.1.2. They require framework 0.6.x, packaged with
`tools/package-unbound-framework.ps1`.

## Animated skeletons (OOT-CORE-005B, slice D4)

<a id="service-skeletons"></a>

### `linkspan.oot.skeletons` v1

Header: `soh/soh/native/oot_skeletons.h`. Game thread only.

- `create(skeleton, animation, &handle)`: normal or flex SkelAnime with looping
  animation. Curve skeletons or missing resources return `FAILURE`. Joint tables
  live in the host heap outside the scene arena;
- `play_animation(handle, animation, speed, mode, morph)`: `LINKSPAN_OOT_ANIM_LOOP`
  or `LINKSPAN_OOT_ANIM_ONCE`. Positive morph transitions from the current pose;
- `update(handle, &finished)`: advances one frame; call once per actor update;
- `get_frame(handle, &frame, &last)`;
- `draw(play, handle)`: opaque draw with the current matrix, only inside a
  `linkspan.oot.render` draw scope;
- `destroy(handle)`.

Use resource-manager paths without `__OTR__`. The host does not verify that an
animation matches the skeleton's joints. Host reset frees remaining skeletons.

### Demo

[`oot-native/familiar-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/familiar-demo)
registers `linkspan-demo.keese-whistle` and gives it through `give_item`.
`receive` equips an empty C button or one holding a vanilla item.

Using it launches an animated Keese (`object_firefly`) ahead of Link with a
Kokiri-sword AT cylinder. On a hit it plays a wing animation once and disappears.
Package with `tools/package-familiar-demo.ps1` in link-span-examples;
`-Extension shipmod` selects the `.shipmod` container.

## Escape hatch (COREEXT-008)

Contract: `extern/ship-lua/rfcs/0023-native-escape-hatch.md`. A core extension with
`abi_version = "1.3"` and `[provider].host_fingerprints` containing the SHA-256
of `soh.exe` can:

- `resolve_symbol("Interface_Draw")` or `resolve_symbol("z_player.c!Player_Action_Roll")`;
- `install_patch(target, detour, &original, &patch)`: MinHook v1.3.4 detour inside
  the executable only; a second patch on the same target returns `LIMIT`;
- `remove_patch(patch)`; remaining patches are removed on mod unload.

Patches created during init are enabled together after init returns. Do not
depend on them during init or install patches during shutdown. If removal fails,
the runtime can quarantine the library to avoid unloading referenced code.
A matching layout alone is insufficient.

The host resolves names through `soh.symbols` beside the executable:

```powershell
cmake --build build/x64 --config Release --target soh_symbols -- /m:1
```

`linkspan_symdump` reads `soh.pdb` and writes functions from `soh/src` and
`soh/soh` (about 13,000). Names with RVAs folded together by `/OPT:ICF` are marked
and rejected: detouring one would detour them all. Inlined functions are absent.
A symbols file from another executable is ignored with a log warning.

`tools/package-linkspan-overlay.ps1 -HostSymbols x64/Release/soh.symbols` includes
the file in the overlay and refuses a file that does not match `soh.exe`.

[`oot-native/escape-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/escape-demo)
detours `Interface_Draw` to make the HUD blink.
`tools/package-escape-demo.ps1` in link-span-examples writes the SHA-256 of
`x64/Release/soh.exe` to the manifest.

## Crash log and boot guard (COREEXT-009)

The SoH crash log includes `Link-Span native:` immediately after build information:

- native activity on the crashing thread (`mod 'x' em hook oot.actor.update`);
- mod libraries in the stack, including escape-hatch detours;
- loaded modules.

If the game crashes while loading a native mod, or before 300 gameplay frames
with an identified culprit, the next start records that mod in
`mods/.shiplua-disabled` and rejects it with the reason in the log. To re-enable
it, remove its line. `mods/.shiplua-boot` disappears when boot is confirmed.

[`oot-native/crash-demo`](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/crash-demo),
packaged with `tools/package-crash-demo.ps1` in link-span-examples, intentionally
writes to a null pointer during init. With `-CoreExtension`, the same crash is a
core extension (`linkspan.crash-coremod`, ABI 1.2, `pre_game`).

## Build the host once

Configure `LINKSPAN_SDK_SOURCE_DIR` when testing another compatible SDK source.
The integrated checkout defaults to `extern/ship-lua`; an override is optional.
Do not mix ABI or layout inputs.

```powershell
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Release --target soh
```

CMake generates `build/x64/native-sdk/Release/OotNativeSdk.cmake` with the host's
includes and definitions. This local SDK references source and dependencies.
For authors outside the checkout, the release provides a relocatable
`LinkSpan-OoT-SDK-*.zip`, generated by `tools/build-linkspan-sdk-bundle.ps1`.
Use `find_package(LinkSpanOotSdk CONFIG REQUIRED)` with `CMAKE_PREFIX_PATH`
pointing at the extraction. The bundle also includes a compatibility
`OotNativeSdk.cmake`. Check exact MSVC version, x64, Release `/MT` and layout in
`linkspan-sdk.json`. Do not install the SDK ZIP in `mods/`.

## Build only the mod

```powershell
$sdk = (Resolve-Path build/x64/native-sdk/Release/OotNativeSdk.cmake).Path
$hostExe = (Resolve-Path x64/Release/soh.exe).Path
cmake -S soh/native-sdk/example -B build/dynamic-movement -A x64 -DOOT_NATIVE_SDK="$sdk" "-DJUMP_VELOCITY=7.0" "-DHOST_EXECUTABLE=$hostExe" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build/dynamic-movement --config Release
.\tools\package-linkspan-mod.ps1 -Source build/dynamic-movement/mod -OutputPath build/dynamic-movement-remake.zip
```

The packager uses `/` entry names with fixed ordering/timestamps and refuses to
overwrite an existing destination. Avoid `Compress-Archive` and
`ZipFile.CreateFromDirectory` on Windows PowerShell 5.1: their separators can
prevent manifest discovery.

Install only the resulting ZIP in the integrated host's `mods/`. The manifest
and `main.lua` must be at archive root; the DLL is
`provider/dynamic_movement_remake.dll`. Change jump impulse, timings or mechanics
by editing and rebuilding only the mod. Current DMR is an ABI 1.3 core extension:
configure uses `HOST_EXECUTABLE` to write its fingerprint into the manifest.
Keep that executable and matching symbols.

## Nintendo profile

The mod reads physical Switch Pro positions: B=`SDL A`, A=`SDL B`, Y=`SDL X`,
X=`SDL Y`. Bindings are applied in memory only, and user mappings are restored
when the mod unloads.

While loaded it enables `FreeLook`, `PersistentMasks`, `Controls.RightStickAim`
and `MoveInFirstPerson`, and sets `FreeLook.InvertYAxis`,
`Controls.InvertAimingYAxis` and `Controls.InvertZAimingYAxis` to 0. Previous
options are restored on unload. The right stick controls free camera with normal
vertical input (without the config key, SoH inverts it). While Link stands still,
the camera keeps its angle. Once Link moves with the right stick idle for 500 ms
(`configure "0,500"`, range 0–60000), FreeLook turns off and automatic camera
continues from the current position without a jump. Integer settings access is
a generic SDK primitive, not a hard-coded rule for this mod.

Some names never become CVars: the host intercepts them and keeps them in memory.
Nothing is saved to config, and replacing the settings bridge on mod unload
resets them. `linkspan.transient_settings` returns the sum of supported features:

- `1`: `linkspan.hud.hide_item_button.c_left`, `.c_down` and `.c_right` set to 1
  hide the C button, empty arrow, icon and ammo in the HUD;
- `2`: `linkspan.input.sword_over_shield` set to 1 removes R from Player input
  while B is pressed. If B was pressed with the shield raised, the press arrives
  on the next frame after lowering the shield; OoT otherwise cannot attack while defending;
- `4`: `linkspan.hud.dpad` set to 1 shows the D-pad even without `DpadEquips` and
  removes its item icons. The background draws at the beginning of
  `hook.oot.hud.draw`, before `Interface_Draw`, so provider icons appear above it.
  The provider uses `get_item_button_rect` positions 4–7 (up, down, left, right).

SoH cosmetic CVars are unsuitable for hiding buttons: changing a menu option
saves the config with the mod's value.

The following profile describes version 0.2.9 as a historical example reference:

1. X performs the dedicated jump.
2. A keeps its contextual action and normal roll while moving. Holding A through
   the end of the roll starts sprint; releasing A ends it.
3. Y invokes the normal sword action; B cancels/sheathes.
4. ZR uses the equipped C-button item and aims while held. Only that C button is
   shown, with a white ring. Other buttons are hidden in memory without changing config.
5. Holding R opens the item menu from the equipped item, using C buttons with
   items. The right stick selects left/right; releasing R equips the selection on
   ZR. Switching is refused while ZR is held.
6. ZL targets (N64 Z) and raises the shield (N64 R). Y or B lowers it, and sword
   attacks work while defending. `-` is N64 L; physical L remains free.
7. Tapping R3 toggles the Lens without occupying a C button. Holding it for 400 ms
   equips/removes the child-slot mask.
8. D-pad right is native C-Up: Navi and first person.
9. D-pad left takes the ocarina out without occupying a C button.
10. D-pad up equips a tunic; a tap returns to the last one used.
11. D-pad down toggles boots with Kokiri boots.
12. Holding up/down for 400 ms opens the HUD icon menu from the equipped item.
    The right stick selects sideways without automatic cycling; release equips it.
13. While a menu is open, the right stick does not rotate the camera. After closing,
    camera input resumes only after the stick passes through center.
14. In first person (D-pad right), bow/slingshot/hookshot aim, the right stick moves
    view with normal vertical input and the left stick moves Link
    (`Controls.RightStickAim` and `MoveInFirstPerson`). ZL aim also has normal
    right-stick vertical input. Free camera stays idle until the stick centers.
15. The HUD D-pad shows tunic above, boots below, ocarina left and Navi right. The
    Navi icon is a PNG converted with `tools/convert-png-to-hud-icon.ps1` into
    `assets/`, which the provider mounts during configure. The PNG is outside Git,
    which ignores `*.png` in `soh/`.

Current DMR source already contains hotbar controls; the historical profile above
is not a complete list of current controls.

## Compatibility and scope

Before accessing layouts, the provider checks header fingerprint and structure
sizes. `LINKSPAN_OOT_LAYOUT_ID`, generated in `ExportSdk.cmake`, covers the C
boundary: z64 structs in `soh/include`, service contracts in `soh/soh/native`,
SDK `shiplua/native` headers and compiler flags. Internal changes in `soh/src`,
the rest of `soh/soh` or libultraship do not change it by themselves. Providers
using the escape hatch still depend on exact executable fingerprint and matching
symbols. Changes to z64 structs or service contracts change the layout; rebuild
against the matching SDK. Native code has the process's privileges and memory
errors can crash the game. Layout verification does not sandbox the DLL.

These services are OoT-specific. The core is shared, but MM integration still
needs its own adapter. This stage does not export every internal executable
function or implement Mixins or live DLL reload. `oot_native_independent_mod`
uses the real bridge and structures with spawn/kill stubs; this proves ABI and
writes, not gameplay. Resources v1 has historical in-game evidence in
`core-001-20260912-1155`. The generic registry does not interpret payloads. The
current host also provides `scenes`, and Unbound materializes scenes through its
adapter; historical registry evidence is recorded above.

## Local validation on 2026-09-09

The engine v1 / movement v1 validation used final package
`Dynamic-Movement-Remake-0.2.3.zip`, impulse 7 and native jump animation. It covered
X jump, landing, contextual A roll, transition to sprint and rearming on A release.
The same host also loaded a second provider build with impulse 10 without being rebuilt.

## Additional contracts in the current SDK

The preceding sections preserve historical slices and evidence. Current headers
and `OotNativeEngine.cpp` also publish the versions below. Service versions are
independent of provider ABI. Query exact name, version and size; do not assume
every newer table preserves its predecessor's prefix.

### `linkspan.oot.resources` v3

Header: `soh/soh/native/oot_resources.h`. Preserves v2 and adds
`register_json_type`/`unregister_json_type`. A registered `$schema` routes JSON
resources to the provider's transcoder, which writes XML accepted by host
factories. Callbacks can run on the resource thread pool: protect your state and
do not call game-thread-only services. VFS reads are allowed in this scope.
Unregister on shutdown; created resources may remain cached until invalidated.
Current Unbound uses this bridge for scene/room resources; the 002B limitation
above is historical.

### `linkspan.oot.registry` v2

Header: `soh/soh/native/oot_registry.h`. Preserves v1 and adds
`create_space_owned`, `register_entry_owned` and `read_entry_owner`. Entries and
spaces with explicit owners are removed on unload, including after failed init.

### `linkspan.oot.items` v3

Header: `soh/soh/native/oot_items.h`. Preserves v2 and adds `set_item_icon`,
`set_item_ammo` and `get_language`. HUD counts go up to 99; the host does not
decrement or save this counter. `LINKSPAN_OOT_ITEMS_NO_AMMO` hides it.

### `linkspan.oot.scenes` v2/v3

Header: `soh/soh/native/oot_scenes.h`. V2 preserves v1 and adds
`register_scene_v2`, `get_scene_count`, `get_scene_info` and `override_scene`.
Title cards are I8 144x24 textures. Overrides take effect on the next scene load;
their owner must clear them on shutdown. V3 preserves v2 and adds
`register_scene_v3`, with horse support and optional spawn per scene.

### `linkspan.oot.render` v2/v3

Header: `soh/soh/native/oot_render.h`. V2 preserves v1 and adds owner-scoped render
state, toon, stencil and shadows. The host removes states on unload. V3 preserves
v2 and adds native geometry, exported matrices, frame data and interpolation
children. Mod display lists must remain valid until frame end and terminate in
`gSPEndDisplayList`. Use pure macros: `gSPVertex`/`gSPDisplayList` are host
functions. Interpolation identities must be unique among siblings and stable
between frames. Follow each operation's draw scope; `get_frame_info` is allowed
outside draw on the game thread.

<a id="service-actor-models"></a>

### `linkspan.oot.actor-models` v1

Header: `soh/soh/native/oot_actor_models.h`. `register_type` registers a static or
animated profile with model, collision, dialogue and look behavior. `list_types`
includes game and mod actors. `remove_owner` frees resources and kills that
owner's instances. The host copies strings/lists. Registration requires the game
thread outside gameplay and actor callbacks. This is Unbound 0.9's JSON actor bridge.

<a id="service-text"></a>

### `linkspan.oot.text` v1

Header: `soh/soh/native/oot_text.h`. `set_message`, `remove_message`,
`clear_language`, `reset_language` and `list_messages` manipulate per-language
tables. Supply raw game bytes with control codes; terminator 0x02 is appended
if missing. Game thread only, outside textbox display. Enumerated bytes are
valid only during the callback.

<a id="service-randomizer"></a>

### `linkspan.oot.randomizer` v1/v2

Header: `soh/soh/native/oot_randomizer.h`. V1 offers synthetic items with get-item
to future seeds through `Link-Span Mod Items`, replacing junk without logic
weight: up to 64 distinct items and 1–4 copies per item. Existing seeds do not
change. V2 is a separate query table, **not a v1 prefix**: `item_count`,
`item_info` and `find_uncollected` read the current seed without granting items or
changing checks. Its IDs are `RandomizerGet`, not synthetic runtime item IDs.
Both versions require the game thread.

<a id="service-anchor"></a>

### `linkspan.oot.anchor` v1

Header: `soh/soh/native/oot_anchor.h`. `connected`, `share_namespace` and
`unshare_namespace` connect `linkspan.oot.save` namespaces to Anchor multiplayer
on the game thread. At most 32 namespaces are shared in total; differing schema
versions are rejected. State updates dispatch `oot.anchor.state`. Synthetic
items synchronize by registered name; a peer missing the item ignores it with
a diagnostic.

<a id="service-lights"></a>

### `linkspan.oot.lights` v1

Header: `soh/soh/native/oot_lights.h`. Creates, updates, destroys and queries the
identity of point lights by handle. At most eight mod lights exist in total;
they also consume slots in the 32-light actor/environment buffer. Game thread
only; creation requires gameplay. Scene changes and unload remove lights;
recreate invalid old handles. Operations are not reentrant. `get_point_light_info`
provides identity for comparison only, not permission to write the host structure.

<a id="service-nei-items"></a>

### `linkspan.nei.items` v1 (core extension)

Header: `soh/native-sdk/nei-core/include/linkspan/nei/nei_items.h`. Published by
`linkspan.nei`, not by the host without NEI. `define_item`/`remove_item` manage
definitions; `give_item`/`grant_item`/`revoke_item` manage ownership;
`set_count`/`add_count`, `set_level`, `equip`/`unequip` and queries cover quantity,
upgrades and C buttons. Game thread only. State persists in `linkspan.nei` by
textual ID while runtime IDs may change. Declare the NEI dependency and check
the table before use.
