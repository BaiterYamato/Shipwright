# Notice — Link-Span fork of Ship of Harkinian

This repository is a fork of Ship of Harkinian (HarbourMasters/Shipwright, base `78dc6d970`) that hosts the
Link-Span modloader (`extern/ship-lua`) and its OoT mods.

- **Shipwright** publishes no license. Its code and assets, and the zeldaret decompilation of Ocarina of Time it
  builds on, stay with their authors. Nothing in this fork grants a license for them. Its credits are in
  `docs/CREDITS.md`.
- **Link-Span's own work** in this fork is dedicated to the public domain under CC0 1.0 Universal
  (`docs/licensing/CC0-1.0.txt`): the native SDK and mods in `soh/native-sdk/`, the native services in
  `soh/soh/native/`, `soh/soh/ShipLuaBootstrap.cpp`, the Link-Span tools in `tools/`, the files in
  `docs/licensing/`, and the Link-Span changes to Shipwright files. The modloader itself is CC0 in its own
  repository.
- **Ported code** keeps its authors: Not Enough Items (skijer, commit `c29262b`), Unbound (roborich, tag
  `9.2.3-unbound0.9` and earlier) and Wind Waker Style (roborich, commit `cbcd3e719d`) publish no license. The
  `NOTICE.md` of each mod says what it ports.
- **libultraship** is MIT (`libultraship/LICENSE`). The other third-party components compiled into `soh.exe`
  are listed with their license texts in `docs/licensing/THIRD-PARTY-NOTICES.md`.
- **Game data:** no ROM, extracted asset or save is distributed. Players extract `oot.o2r` from their own ROM.
