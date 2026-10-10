# Provenance of the Unbound 0.9 port

Link-Span's own work in this package (DLL, Lua, tool, header, schemas, and documentation) is in the public domain
under CC0 1.0 Universal (`LICENSE`). The parts adapted from Unbound, described below, remain with roborich and
contributors; the project does not publish a license, and this package grants none for those parts. The DLL and
tool embed nlohmann/json 3.12.0 (MIT), with the license text in `docs/LICENSES.md`.

The type reader in `actor_registry.cpp` was adapted from `ActorRegistry.cpp` and
`DeclaredActorType.h` in the [roborich/Shipwright project, tag 9.2.3-unbound0.9](https://github.com/roborich/Shipwright/releases/tag/9.2.3-unbound0.9),
commit `cf7db7f7f9b65247347d54abea386669ce9c909f`.

The Link-Span host adapts `DeclaredActor.cpp` in the generic `linkspan.oot.actor-models` service.
The JSON format and behaviors follow `unbound-docs/SPEC.md`, §§4.3 and 7.2.

No license file was found at the root of that tag. This record preserves attribution
and does not grant a redistribution license. The examples use resource paths;
the game's models and textures are not included in the package.
