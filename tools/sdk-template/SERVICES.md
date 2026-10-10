**English** · [Português (Brasil)](SERVICES.pt-BR.md)

# OoT native service reference

Query services through `ShipNativeRuntime.get_service` using the exact service macro, version macro and `sizeof` of the matching table. Check null, table `size` and function pointers before use. A table/header declaration is not a guarantee of runtime availability. The list below comes only from the bundled header seeds; [the contract](OOT-NATIVE-CONTRACT.md) explains ABI/lifetimes. Every table has a `size` field. Fields shown for a newer prefix-compatible version are additions to the earlier table; randomizer v2 is explicitly a different table.

## `linkspan.oot.actor-models`

Header: [include/oot-native/oot_actor_models.h](include/oot-native/oot_actor_models.h). Service macro: `LINKSPAN_OOT_ACTOR_MODELS_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ACTOR_MODELS_VERSION` | `1u` | `ShipOotActorModelsV1` | `size`, `register_type`, `remove_owner`, `list_types` |

Generic static/NPC model profiles: skeleton/animation or display list, transforms, texture segments, hidden limbs, collider, talk and look settings. The host copies strings/lists; paths accept `__OTR__`, root joint is 1. Register outside gameplay and outside actor callbacks on the game thread. `remove_owner` destroys instance resources before killing actors; `list_types` includes vanilla and active mod actors.

Data structs in this header:

- `ShipOotActorModelSegmentV1`: `segment`, `texture`.
- `ShipOotActorModelSpecV1`: `size`, `owner`, `name`, `display_name`, `skeleton`, `animation`, `display_list`, `hold_frame`, `translucent`, `frame`, `speed`, `scale`, `y_offset`, `shadow`, `cull_radius`, `draw_distance`, `segments`, `segment_count`, `hide_limbs`, `hide_limb_count`, `radius`, `height`, `y_shift`, `talks`, `message`, `talk_range`, `looks`, `look_limb`, `look_pivot`, `look_range`, `turn_axis`, `nod_axis`.

## `linkspan.oot.actors`

Header: [include/oot-native/oot_actors.h](include/oot-native/oot_actors.h). Service macro: `LINKSPAN_OOT_ACTORS_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ACTORS_VERSION` | `1u` | `ShipOotActorsV1` | `size`, `register_actor_type`, `unregister_actor_type`, `find_actor_type`, `draw_display_list` |

Registers named mod actor types in ActorDB and supplies IDs for engine `spawn_actor`. Callback `actor` is an `Actor*` with `instance_size` bytes and `play_state` is `PlayState*`; `instance_size` must be at least `sizeof(Actor)`. Unregister on shutdown: live instances are killed and further mod callbacks stop; destroy is ignored after unregister. Re-registering a name reuses its ID. `draw_display_list` is draw-only, uses the current actor matrix, a VFS path without `__OTR__`, and opaque/translucent selection. Game thread.

Data structs in this header:

- `ShipOotActorTypeSpecV1`: `size`, `name`, `category`, `flags`, `object_id`, `instance_size`, `init`, `destroy`, `update`, `draw`, `user`.

## `linkspan.oot.anchor`

Header: [include/oot-native/oot_anchor.h](include/oot-native/oot_anchor.h). Service macro: `LINKSPAN_OOT_ANCHOR_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ANCHOR_VERSION` | `1u` | `ShipOotAnchorV1` | `size`, `connected`, `share_namespace`, `unshare_namespace` |

Game-thread multiplayer namespace sharing using save handles. `connected` requires an item/flag-sync room. The team’s latest content replaces a matching-schema local namespace and emits `oot.anchor.state`; mismatched schemas are rejected. The host emits `oot.save.saving` before constructing requested team state outside a save. At most 32 shared namespaces across all mods. Synthetic get-items are synchronized by registered name; peers without that registration ignore/log them.

## `linkspan.oot.camera`

Header: [include/oot-native/oot_camera.h](include/oot-native/oot_camera.h). Service macro: `LINKSPAN_OOT_CAMERA_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_CAMERA_VERSION` | `1u` | `ShipOotCameraV1` | `size`, `acquire`, `release`, `set_view`, `get_view`, `is_owned` |

Single-owner gameplay subcamera. `acquire` requires gameplay with the main camera active; cutscenes or another owner return `LIMIT`. Ownership is lost on scene change or when the game activates another camera; check `is_owned`. `set_view` persists until the next call and is reapplied each frame; `get_view` reads any active camera. Release on shutdown; releasing a token that lost ownership returns OK. Game thread.

Data structs in this header:

- `ShipOotCameraViewV1`: `size`, `eye`, `at`, `fov`.

## `linkspan.oot.colliders`

Header: [include/oot-native/oot_colliders.h](include/oot-native/oot_colliders.h). Service macro: `LINKSPAN_OOT_COLLIDERS_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_COLLIDERS_VERSION` | `1u` | `ShipOotCollidersV1` | `size`, `create_cylinder`, `destroy`, `submit`, `read_hits`, `knockback_player` |

Host-owned cylinders during gameplay on the game thread. `submit` runs each owner actor update to move/register AT/AC/OC checks. `read_hits` returns and clears previous-frame hits; hit actor pointers are frame-scoped. Destroy in actor destroy; storage is freed next frame after current collision checks. Scene changes/reset remove scene colliders. Flags follow `ColliderCylinderInit`; knockback damage is in 1/16-heart health units.

Data structs in this header:

- `ShipOotCylinderSpecV1`: `size`, `actor`, `col_type`, `at_flags`, `ac_flags`, `oc1_flags`, `oc2_flags`, `elem_type`, `touch_dmg_flags`, `touch_effect`, `touch_damage`, `bump_dmg_flags`, `bump_effect`, `bump_defense`, `touch_flags`, `bump_flags`, `oc_elem_flags`, `radius`, `height`, `y_shift`.
- `ShipOotColliderHitsV1`: `size`, `at_hit`, `ac_hit`, `oc_hit`, `at_actor`, `ac_actor`, `oc_actor`, `ac_dmg_flags`.

## `linkspan.oot.engine`

Header: [include/oot-native/oot_engine.h](include/oot-native/oot_engine.h). Service macro: `LINKSPAN_OOT_ENGINE_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ENGINE_VERSION` | `1u` | `ShipOotEngineV1` | `size`, `layout_id`, `play_state_size`, `player_size`, `save_context_size`, `get_play_state`, `get_player`, `get_save_context`, `spawn_actor`, `kill_actor` |

Game-thread-only access to `PlayState`, `Player` and `SaveContext`, plus actor spawn/kill. Validate `layout_id` and all three structure sizes before casting. Gameplay pointers do not survive frames or scene changes; the provider owns the consequences of its writes.

## `linkspan.oot.movement`

Header: [include/oot-native/oot_engine.h](include/oot-native/oot_engine.h). Service macro: `LINKSPAN_OOT_MOVEMENT_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_MOVEMENT_VERSION` | `1u` | `ShipOotMovementV1` | `size`, `layout_id`, `get_input_current`, `get_input_pressed`, `get_input_released`, `get_stick_x`, `get_stick_y`, `get_right_stick_x`, `get_right_stick_y`, `has_gamepad`, `get_gamepad_buttons`, `clear_gamepad_button_bindings`, `bind_gamepad_button`, `reload_gamepad_mappings`, `get_setting_int`, `set_setting_int`, `is_player_grounded`, `is_player_rolling`, `player_jump`, `player_roll` |
| `LINKSPAN_OOT_MOVEMENT_VERSION_2` | `2u` | `ShipOotMovementV2` | Previous prefix + `get_gamepad_axis`, `bind_gamepad_axis`, `player_use_item_shortcut`, `get_item_button_rect` |

V1 covers virtual input, sticks, SDL buttons, transient bindings, settings, grounded/rolling state, jump and roll. V2 preserves the V1 prefix and adds raw SDL axes (-32768..32767), half-axis bindings (`direction` 1 or -1), item shortcuts and current/previous-frame C-button HUD rectangles. Shortcuts still enforce game conditions; unavailable/unchanged actions and hidden HUD rectangles return `UNSUPPORTED`. Lens, masks, ocarinas, tunics and boots follow the specific rules in the header. Game thread; validate `layout_id`.

## `linkspan.oot.items`

Header: [include/oot-native/oot_items.h](include/oot-native/oot_items.h). Service macro: `LINKSPAN_OOT_ITEMS_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ITEMS_VERSION` | `1u` | `ShipOotItemsV1` | `size`, `register_item`, `unregister_item`, `find_item`, `get_button_item`, `set_button_item` |
| `LINKSPAN_OOT_ITEMS_VERSION_2` | `2u` | `ShipOotItemsV2` | Previous prefix + `set_get_item`, `give_item` |
| `LINKSPAN_OOT_ITEMS_VERSION_3` | `3u` | `ShipOotItemsV3` | Previous prefix + `set_item_icon`, `set_item_ammo`, `get_language` |

V1 registers synthetic item IDs 0xA0..0xEF with names, RGBA32 32×32 VFS icons and C-button use callbacks. The host stores equipped items by name in `linkspan.items`; unregister empties their C buttons. V1 does not supply inventory/get-item. V2 adds a copied get-item model/message and a delayed receive callback; unavailable player conditions give `LIMIT`. V3 adds icon replacement, caller-controlled ammo display (up to 99; `NO_AMMO` removes it) and game language. The host does not store or decrement that ammo. Versions preserve the earlier table prefixes. Game thread.

Data structs in this header:

- `ShipOotItemSpecV1`: `size`, `name`, `icon_path`, `age`, `use`, `user`.
- `ShipOotGetItemSpecV1`: `size`, `model_path`, `model_layer`, `model_scale`, `message`, `receive`, `user`.

## `linkspan.oot.lights`

Header: [include/oot-native/oot_lights.h](include/oot-native/oot_lights.h). Service macro: `LINKSPAN_OOT_LIGHTS_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_LIGHTS_VERSION` | `1u` | `ShipOotLightsV1` | `size`, `create_point_light`, `update_point_light`, `destroy_point_light`, `get_point_light_info` |

Point lights on the game thread during gameplay; at most 8 mod lights, also using the game’s shared light buffer. Updates go through the vanilla light-color hook. Scene changes/unload remove lights; stale handles return `UNSUPPORTED` on update. Operations are not reentrant. `get_point_light_info` returns a comparison-only identity, not writable storage. Radius zero turns the light off; negative radius is invalid.

Data structs in this header:

- `ShipOotPointLightV1`: `size`, `position`, `radius`, `color`, `glow`.

## `linkspan.oot.ocarina`

Header: [include/oot-native/oot_ocarina.h](include/oot-native/oot_ocarina.h). Service macro: `LINKSPAN_OOT_OCARINA_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_OCARINA_VERSION` | `1u` | `ShipOotOcarinaV1` | `size`, `is_active`, `get_available_song_flags`, `get_song_count`, `get_song_pattern`, `submit_song` |

Game-thread instrument support. Active state and allowed-song bits reflect the latest ocarina input update. There are 12 fixed songs, or 13 after recording Scarecrow. Patterns use game note indices 0..4 and support null/zero-capacity count queries; insufficient capacity returns `LIMIT`. `submit_song` delivers an accepted song at the next ocarina update, with vanilla effect; delivery is discarded if the instrument closes first.

## `linkspan.oot.randomizer`

Header: [include/oot-native/oot_randomizer.h](include/oot-native/oot_randomizer.h). Service macro: `LINKSPAN_OOT_RANDOMIZER_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_RANDOMIZER_VERSION` | `1u` | `ShipOotRandomizerV1` | `size`, `add_item`, `remove_item`, `seed_item_count`, `seed_item_name` |
| `LINKSPAN_OOT_RANDOMIZER_VERSION_2` | `2u` | `ShipOotRandomizerV2` | **Different table:** `size`, `item_count`, `item_info`, `find_uncollected` |

V1 offers registered get-item-capable synthetic items to subsequently generated seeds with Link-Span Mod Items enabled. Copies are 1..4, at most 64 distinct items; they replace junk without logic weight. It also reads loaded-seed mod names. Missing mods give a blue rupee and log the stable item name. **V2 is a different table, not a V1 extension:** read-only current-seed `RandomizerGet` item metadata and uncollected-check hints, using SDK IDs rather than runtime item IDs. It neither grants items nor changes checks. `UNSUPPORTED` outside a randomizer save, `FAILURE` without a result. Strings are copied UTF-8/NUL-terminated. Both versions require the game thread.

Data structs in this header:

- `ShipOotRandomizerItemSpecV1`: `size`, `item`, `copies`, `reserved`.
- `ShipOotRandomizerItemInfoV2`: `size`, `item`, `name`.
- `ShipOotRandomizerHintV2`: `size`, `item`, `check`, `item_name`, `area_name`, `description`.

## `linkspan.oot.registry`

Header: [include/oot-native/oot_registry.h](include/oot-native/oot_registry.h). Service macro: `LINKSPAN_OOT_REGISTRY_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_REGISTRY_VERSION` | `1u` | `ShipOotRegistryV1` | `size`, `create_space`, `find_space`, `destroy_space`, `register_entry`, `unregister_entry`, `find_entry_by_name`, `find_entry_by_id`, `read_entry`, `list_entries` |
| `LINKSPAN_OOT_REGISTRY_VERSION_2` | `2u` | `ShipOotRegistryV2` | Previous prefix + `create_space_owned`, `register_entry_owned`, `read_entry_owner` |

Generic named spaces and entries, game thread only. The host copies names/payloads. `read_entry` supports null buffers/zero capacities to query lengths; names exclude NUL. Enumeration names live only during the synchronous callback. The creator destroys its space on shutdown. V2 preserves V1 and adds explicit mod owners, unload/failed-init cleanup and owner reads; duplicate names/IDs are rejected.

## `linkspan.oot.render`

Header: [include/oot-native/oot_render.h](include/oot-native/oot_render.h). Service macro: `LINKSPAN_OOT_RENDER_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_RENDER_VERSION` | `1u` | `ShipOotRenderV1` | `size`, `draw_display_list`, `matrix_push`, `matrix_pop`, `matrix_translate`, `matrix_scale`, `matrix_rotate_zyx` |
| `LINKSPAN_OOT_RENDER_VERSION_2` | `2u` | `ShipOotRenderV2` | Previous prefix + `acquire_state`, `release_state`, `set_state`, `emit_toon_key`, `emit_stencil`, `emit_toon_shadow`, `flush_toon_shadows`, `set_toon_ramp`, `set_toon_shadow_params` |
| `LINKSPAN_OOT_RENDER_VERSION_3` | `3u` | `ShipOotRenderV3` | Previous prefix + `set_actor_toon_enabled`, `matrix_translate_new`, `matrix_rotate_axis`, `export_current_matrix`, `draw_native_display_list`, `get_frame_info`, `interpolation_begin`, `interpolation_end` |

V1 draws VFS display lists and transforms the current actor/limb matrix in an allowed draw scope; paths omit `__OTR__`. Unbalanced matrix pushes are unwound by the host. V2 preserves V1, adds per-mod feature state, toon/stencil/shadow emissions and global toon/shadow parameters; states are removed on unload. Emissions require draw scope; `set_toon_ramp` and `set_toon_shadow_params` run outside draw on the game thread, with last-writer behavior. V3 preserves V2 and adds native geometry, frame information, axis rotation, matrix export and interpolation children. `set_actor_toon_enabled` is restricted to `oot.render.actor_draw`; `matrix_translate_new` requires a mod matrix push. Native display lists use layout-matching Gfx/Vtx and remain valid to frame end. V3 additions need draw scope except `get_frame_info`; interpolation sibling keys must be unique and stable.

Data structs in this header:

- `ShipOotRenderFrameInfoV1`: `size`, `frame`, `camera_epoch`, `delta_seconds`, `aspect_ratio`.

## `linkspan.oot.resources`

Header: [include/oot-native/oot_resources.h](include/oot-native/oot_resources.h). Service macro: `LINKSPAN_OOT_RESOURCES_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_RESOURCES_VERSION` | `1u` | `ShipOotResourcesV1` | `size`, `has_file`, `read_file`, `list_files`, `dirty_resources`, `unload_resource`, `mount_archive`, `unmount_archive`, `get_game_versions` |
| `LINKSPAN_OOT_RESOURCES_VERSION_2` | `2u` | `ShipOotResourcesV2` | Previous prefix + `read_file_layers` |
| `LINKSPAN_OOT_RESOURCES_VERSION_3` | `3u` | `ShipOotResourcesV3` | Previous prefix + `register_json_type`, `unregister_json_type` |

V1 provides VFS existence/read/list, cache invalidation/unload, archive mount/unmount and game-version enumeration. Output buffers belong to the caller; `read_file` and `get_game_versions` support null/zero-capacity size queries. Unmount owned archives on shutdown. Enumeration paths are synchronous callback-only. V2 preserves V1 and adds stable low-to-high-priority layer reads; the provider defines merge policy. V3 preserves V2 and registers JSON `$schema` type/version ranges with a JSON-to-host-XML transcode callback; unregister on shutdown and invalidate cached resources when required. Normal operations are game-thread-only, but the transcode callback runs on the resource-loading thread/pool. Its header permits `read_file_layers` there: protect state and do not call game-thread-only services from the callback. Callback pointers/writer are call-scoped.

Data structs in this header:

- `ShipOotResourceLayerV2`: `size`, `layer_index`, `layer_count`, `game_version`, `content_hash`, `data_size`, `archive_path_length`.
- `ShipOotJsonTypeSpecV1`: `size`, `type`, `min_version`, `max_version`, `transcode`, `user`.

## `linkspan.oot.save`

Header: [include/oot-native/oot_save.h](include/oot-native/oot_save.h). Service macro: `LINKSPAN_OOT_SAVE_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_SAVE_VERSION` | `1u` | `ShipOotSaveV1` | `size`, `open_namespace`, `read`, `write`, `erase`, `get_stored_version`, `set_required`, `begin`, `commit`, `rollback`, `get_slot` |

Per-namespace JSON blocks in the save’s `linkspan` section, game thread only. Loaded-file changes replace the data; writes reach disk on the next game save. Unopened namespaces are preserved. Names contain a dot and allowed characters; `linkspan.` is host-reserved. Read supports null/zero-capacity size query. Schema version mismatches on reopening are invalid; use stored version for migration. Transactions save the pre-begin state until commit. `get_slot` is 0..2 or -1; maximum JSON size is 1048576 bytes.

## `linkspan.oot.scenes`

Header: [include/oot-native/oot_scenes.h](include/oot-native/oot_scenes.h). Service macro: `LINKSPAN_OOT_SCENES_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_SCENES_VERSION` | `1u` | `ShipOotScenesV1` | `size`, `register_scene`, `register_entrance`, `unregister_scene`, `find_scene`, `find_entrance`, `travel_to_entrance` |
| `LINKSPAN_OOT_SCENES_VERSION_2` | `2u` | `ShipOotScenesV2` | Previous prefix + `register_scene_v2`, `get_scene_count`, `get_scene_info`, `override_scene` |
| `LINKSPAN_OOT_SCENES_VERSION_3` | `3u` | `ShipOotScenesV3` | Previous prefix + `register_scene_v3` |

Named scene/entrance registration and travel on the game thread. The host copies names/paths. Custom scene IDs start at 128; entrances occupy four consecutive slots and use scene-name/key names. Travel needs a player in a scene without an active transition. Unregister scenes on shutdown. V2 preserves V1, adds I8 144×24 title-card texture, scene inspection and single-owner vanilla resource overrides effective next load; clear your overrides on shutdown. V3 preserves V2, adds `register_scene_v3` and horse enable/spawn fields; with `horse_enabled` zero the horse fields are ignored.

Data structs in this header:

- `ShipOotSceneDefinitionV1`: `size`, `name`, `display_name`, `scene_path`, `requested_id`, `draw_config`.
- `ShipOotEntranceDefinitionV1`: `size`, `key`, `requested_index`, `spawn`, `continue_bgm`, `show_title_card`, `end_transition`, `start_transition`.
- `ShipOotSceneDefinitionV2`: `size`, `name`, `display_name`, `scene_path`, `requested_id`, `draw_config`, `title_card_texture`.
- `ShipOotSceneInfoV1`: `size`, `scene_id`, `draw_config`, `is_custom`, `has_master_quest`, `overridden`, `name`, `file_name`, `scene_path`.
- `ShipOotSceneDefinitionV3`: `size`, `name`, `display_name`, `scene_path`, `requested_id`, `draw_config`, `title_card_texture`, `horse_enabled`, `horse_has_spawn`, `horse_x`, `horse_y`, `horse_z`, `horse_angle`.

## `linkspan.oot.skeletons`

Header: [include/oot-native/oot_skeletons.h](include/oot-native/oot_skeletons.h). Service macro: `LINKSPAN_OOT_SKELETONS_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_SKELETONS_VERSION` | `1u` | `ShipOotSkeletonsV1` | `size`, `create`, `destroy`, `play_animation`, `update`, `get_frame`, `draw` |

Game-thread normal/flex SkelAnime skeletons from VFS paths without `__OTR__`. Curve/missing resources fail; the provider must supply matching animation joints. Update once per owner actor update; ONCE animations stop at the last frame. Draw is opaque and requires render draw scope. Skeletons outlive scenes but own game resource poses; destroy in actor destroy or shutdown. Host reset releases leftovers.

## `linkspan.oot.text`

Header: [include/oot-native/oot_text.h](include/oot-native/oot_text.h). Service macro: `LINKSPAN_OOT_TEXT_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_TEXT_VERSION` | `1u` | `ShipOotTextV1` | `size`, `set_message`, `remove_message`, `clear_language`, `reset_language`, `list_messages` |

Edit game message tables on the game thread outside textbox display, between frames. Messages are raw game bytes/control codes, with 0x02 appended when missing. IDs are at most 0xFFFE; box type/position are 0..15; max message including terminator is 8192 bytes. Tables cover English, German, French, Japanese and staff. `reset_language` restores boot resources/overrides; list callback bytes are call-scoped.

## `linkspan.oot.world`

Header: [include/oot-native/oot_world.h](include/oot-native/oot_world.h). Service macro: `LINKSPAN_OOT_WORLD_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_WORLD_VERSION` | `1u` | `ShipOotWorldV1` | `size`, `raycast_floor`, `line_test`, `wall_check`, `water_surface` |

Read-only current-scene collision queries on the game thread during gameplay; otherwise `UNSUPPORTED`. Floor raycast with no floor returns OK and hit=0. Line tests filter wall/floor/ceiling; wall-check corrects a moving sphere’s destination. `water_surface` returns `FAILURE` outside water. Hit normals are unit vectors; bg_id identifies scene or DynaPoly and floor_type follows the game’s surface type.

Data structs in this header:

- `ShipOotWorldHitV1`: `size`, `hit`, `pos`, `normal`, `bg_id`, `floor_type`.

## `linkspan.nei.items`

Header: [include/linkspan/nei/nei_items.h](include/linkspan/nei/nei_items.h). Service macro: `LINKSPAN_NEI_ITEMS_SERVICE`.

| Version macro | Value | Table | Fields/functions |
| --- | --- | --- | --- |
| `LINKSPAN_NEI_ITEMS_VERSION` | `1u` | `NeiItemsV1` | `size`, `define_item`, `remove_item`, `find_item`, `list_items`, `get_state`, `give_item`, `grant_item`, `revoke_item`, `set_count`, `add_count`, `set_level`, `equip`, `unequip`, `get_name` |

Provided by the `linkspan.nei` coremod, not by header inclusion. Game thread only. Definitions have stable namespaced IDs; runtime game IDs may change between sessions. Ownership/count/level persist by stable ID in save namespace `linkspan.nei`, even while a definition is absent. `define_item` copies its definition and restores saved state; remove your definitions on shutdown without erasing saved state. Up to 4 levels, counts up to 99, mandatory English names/get-messages with German/French fallback. `give_item` applies ownership/count when its textbox opens, then calls received; `grant_item` skips animation. Use callbacks do not consume count automatically. `add_count` refuses a negative result with `LIMIT`; level changes clamp count. Enumerated ID strings are callback-only; `get_name` supports null/zero-capacity size queries excluding the terminator.

Data structs in this header:

- `NeiItemLevelV1`: `size`, `icon_path`, `names`, `max_count`.
- `NeiItemDefinitionV1`: `size`, `id`, `age`, `model_path`, `model_layer`, `model_scale`, `get_messages`, `level_count`, `levels`, `give_count`, `use`, `received`, `user`.
- `NeiItemStateV1`: `size`, `owned`, `level`, `count`, `max_count`, `runtime_id`, `button`.

## Hooks and Unbound scope

[oot_hooks.h](include/oot-native/oot_hooks.h) defines named hook points/payloads used through runtime hook functions; it does not declare a `LINKSPAN_OOT_*_SERVICE` table. See [the native contract](OOT-NATIVE-CONTRACT.md).

The current builder seeds all `oot_*.h`, the generic ABI, host z64 headers, generated layout and NEI. The resulting checked dependency walk includes no dedicated Unbound service header. Therefore this SDK reference does not invent an Unbound service name, version or table. `oot_resources.h` declares the JSON transcode bridge and uses `unbound/scene` only as a schema-type example. For Unbound examples and their own framework contracts, use [link-span-examples](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/unbound).
