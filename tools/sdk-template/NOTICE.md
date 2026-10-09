# Notice — Link-Span OoT SDK

- Link-Span's own work in this SDK — the Link-Span headers (`include/shiplua`, `include/linkspan`,
  `include/oot-native` and `include/oot_layout_id.h`), the CMake configuration, the examples, the tools, the
  guides and the documentation — is dedicated to the public domain under CC0 1.0 Universal (`LICENSE`).
- `include/host` holds headers of Ship of Harkinian (HarbourMasters/Shipwright) and of the zeldaret decompilation
  of Ocarina of Time. They publish no license; the headers stay with their authors, and this SDK grants no license
  for them. They are here so that providers compile against the exact game layout of the host.
- `include/lus` holds libultraship headers, under the MIT license in `licenses/libultraship-LICENSE.txt`.
- No ROM, game asset, save, import library or host binary is in this SDK. The Dynamic Movement Remake example
  ships without its HUD textures; its release package carries them.
