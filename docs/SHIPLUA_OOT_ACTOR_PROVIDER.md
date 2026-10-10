**English** · [Português (Brasil)](SHIPLUA_OOT_ACTOR_PROVIDER.pt-BR.md)

# Native OoT actor provider

`OOT-MODSDK-001` implements the `shipwright-native` provider for Link-Span 0.4's generic actor API.

## Contract for this scope

- only the game's main thread may create, query or destroy actors;
- the mod supplies an allowlisted logical ID, never a numeric `ActorId`;
- no `Actor*` leaves the provider;
- each actor receives a `ShipLua::Handle` with a slot, generation, scene generation and ownership;
- the initial limit is 16 live actors per mod and 256 on the host;
- the object dependency must be loaded before spawning;
- handles are invalidated when the native actor is destroyed, the mod is unloaded or the `PlayState` ends;
- `ACTOR_PLAYER` is rejected even if it is accidentally included in the allowlist.

Initial IDs:

| Logical ID | OoT actor | Required object | Fixed params |
|---|---|---|---|
| `oot.en_dog` | `ACTOR_EN_DOG` | `OBJECT_DOG` | `0x8000` |
| `oot.en_torch2` | `ACTOR_EN_TORCH2` | `OBJECT_TORCH2` | `0` |

The provider registers the experimental capabilities `actor.spawn`, `actor.destroy` and `actor.exists` with version `0.1.0` and is published in `LuaApiHostContext`. The bindings `ship.actor.spawn`, `ship.actor.destroy` and `ship.actor.exists` come from Link-Span 0.4 (`MODSDK-005`, PR `link-span#41`). The manifest must declare the capabilities, the grants `world.entities.create`, `world.entities.destroy` and `world.entities.read`, and an `actors` limit greater than zero for spawning.

Positions arrive as generic numbers and are converted to `float` only at the native call. Rotations are expressed in degrees by the portable API and converted to OoT's binary angle format at the host boundary.

## Lifecycle

`ShipLuaBootstrap` registers `OnActorDestroy` and `OnPlayDestroy` hooks. The first immediately invalidates the handle of an actor removed by the game itself. The second kills actors still owned by mods and advances the scene generation before `PlayState` teardown.

For future hot reload support, the host must call `OotActorProvider::ReleaseMod(modId)` before destroying that mod's `LuaRuntime`.

## Validation

The `oot_actor_provider_tests` test covers:

- capability descriptors;
- the `En_Dog` and `En_Torch2` allowlist;
- blocking `ACTOR_PLAYER`;
- object dependencies;
- ownership across mods;
- invalid handles and stale generations;
- the per-mod limit;
- cleanup by actor, mod and scene;
- rejection outside the main thread.

Integrated validation generates `x64/Release/soh.exe` in Release and runs 57/57 applicable tests from the host and the embedded SDK. The legacy `prism` test, which does not produce an executable in this host configuration, is excluded from that count.


