# Notice — Link-Span overlay for Ship of Harkinian 9.2.3

This overlay replaces `soh.exe` and `soh.o2r` of a Ship of Harkinian 9.2.3 installation and adds the Link-Span
files (`soh.symbols`, extractor YAMLs, controller database, README).

- `soh.exe` and `soh.o2r` are built from Ship of Harkinian (HarbourMasters/Shipwright, base `78dc6d970`), which
  publishes no license, and from the zeldaret decompilation it builds on. That code and those assets stay with
  their authors; this overlay grants no license for them.
- Link-Span's own work in them — the modloader, the native services and SDK, and the Link-Span changes to
  Shipwright files — is dedicated to the public domain under CC0 1.0 Universal (`LICENSE`).
- The third-party components compiled into `soh.exe` or packed in `soh.o2r` keep their licenses; the list and
  the license texts are in `THIRD-PARTY-NOTICES.md`.
- No ROM, extracted game asset or save is part of this overlay. `oot.o2r` is extracted on the player's machine
  from their own ROM.
