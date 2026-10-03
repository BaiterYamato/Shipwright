# Unbound 2 JSON Schemas

These are JSON Schema draft 2020-12 schemas for the JSON documents accepted by the Link-Span Unbound reader. They describe an individual archive layer, not only a fully merged archive: a layer may be a partial patch, may omit `$schema` when a lower layer supplies it, and may use `$replace`/`$order` where the reader supports those directives.

| Schema | Archive path(s) |
| --- | --- |
| `manifest.schema.json` | `unbound.json` |
| `scene.schema.json` | `scenes/<scene>/scene.json` (including `exits`, `entrances`, spawns, and transition actors) |
| `room.schema.json` | `scenes/<scene>/rooms/<n>.json` |
| `collision.schema.json` | `scenes/<scene>/collision.json` |
| `paths.schema.json` | `scenes/<scene>/paths/<name>.json` |
| `text.schema.json` | `text/<lang>/messages.json` |
| `scene-registry.schema.json` | `unbound/scenes.json` |
| `actor-type.schema.json` | `unbound/actors/<name>.json` |
| `common.schema.json` | Shared definitions used by the document schemas; not an archive document. |

Run `python ../validate_schemas.py` (in `schemas/`) to validate the example asset roots of this repository. `--root <path>` may be repeated to validate other roots. The script removes `//` and `/* ... */` comments before parsing because the Unbound reader accepts them, while Python's standard JSON parser does not.

Known limits:

- JSON Schema cannot validate archive layering, contiguous positional keys after merge, first-byte requirements, binary `collision.bin` length, resource existence/type, or runtime limits that depend on another document.
- The schemas deliberately keep most scene, room, collision, text, and registry objects open because their readers ignore unknown fields or accept partial layers. `actor-type.schema.json` is closed where the actor reader rejects unknown final keys.
- A schema pass is structural evidence only. It does not prove that a registered actor name, entrance name, asset path, collision count, or runtime resource resolves in Shipwright.
