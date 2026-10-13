# Notice — Not Enough Items (Link-Span)

- Link-Span's own work in this package — the coremod glue, item registry, save blocks, Lua, header and
  documentation — is dedicated to the public domain under CC0 1.0 Universal (`LICENSE`).
- The item code compiled into `provider/linkspan_nei_core.dll` is ported from Not Enough Items by skijer and
  contributors (the [`Not-Enough-Items` branch of skijer/Shipwright](https://github.com/skijer/Shipwright/tree/Not-Enough-Items), commit
  `c29262b76ead6f00786885ae4be68916cbee77f5`). It publishes no license;
  that code stays with its authors, and this package grants no license for it.
- The fork's assets (models, icons, textures and item names) are in this package, in `assets/nei-assets-*.o2r`,
  generated from commit `c29262b76ead6f00786885ae4be68916cbee77f5` by `tools/build-nei-assets.py`. The fork
  publishes no license; the rights to them stay with skijer and contributors, and this package grants no license
  for them. `assets/nei-assets-manifest.json` lists the origin and SHA-256 of each file. The only replacement is
  the Spinner texture, a material generated for Link-Span. No game data is in this package: fork textures that are
  copies of the game's own are left out, and the models read them by their game path from the player's `oot.o2r` and
  `mm.o2r` (`game_data` in the manifest).
- The DLL embeds nlohmann/json 3.12.0 (MIT, Copyright (c) 2013-2025 Niels Lohmann); its license text is in
  `docs/LICENSES.md`.
- The `docs/examples/heart-seeds` add-on example is Link-Span's own work, under the same CC0 dedication.
