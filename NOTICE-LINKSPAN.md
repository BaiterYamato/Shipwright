# Notice — Link-Span fork of Ship of Harkinian

This repository is a fork of Ship of Harkinian (HarbourMasters/Shipwright, base 9.3.0, `ecd889c201`) that hosts the
Link-Span modloader (`extern/ship-lua`) and its OoT mods.

- **[Shipwright](https://github.com/HarbourMasters/Shipwright)** publishes no license. Its code and assets, and the
  [zeldaret decompilation of Ocarina of Time](https://github.com/zeldaret/oot) it builds on, stay with their authors. Nothing in this fork grants a license for them. Its credits are in
  `docs/CREDITS.md`.
- **Link-Span's own work** in this fork is dedicated to the public domain under CC0 1.0 Universal
  (`docs/licensing/CC0-1.0.txt`): the native SDK and mods in `soh/native-sdk/`, the native services in
  `soh/soh/native/`, `soh/soh/ShipLuaBootstrap.cpp`, the Link-Span tools in `tools/`, the files in
  `docs/licensing/`, and the Link-Span changes to Shipwright files. The modloader itself is CC0 in its own
  repository.
- **Ported code** keeps its authors. Every project Link-Span takes code, data or design from is listed below with
  its link and version; all of them except 2Ship2Harkinian (CC0 1.0) publish no license. The `NOTICE.md` of each
  mod says what it ports.
- **libultraship** is MIT (`libultraship/LICENSE`). The other third-party components compiled into `soh.exe`
  are listed with their license texts in `docs/licensing/THIRD-PARTY-NOTICES.md`.
- **Game data:** no ROM, extracted asset or save is distributed. Players extract `oot.o2r` from their own ROM.

## Credits and sources

| Project | Source | What Link-Span uses | License |
|---|---|---|---|
| Ship of Harkinian (HarbourMasters and contributors) | [HarbourMasters/Shipwright](https://github.com/HarbourMasters/Shipwright), tag 9.3.0 (`ecd889c201`) | the host: `soh.exe` and `soh.o2r` are built from it | none published |
| Ocarina of Time decompilation (zeldaret) | [zeldaret/oot](https://github.com/zeldaret/oot) | the code Shipwright builds on | none published |
| Majora's Mask decompilation (zeldaret) | [zeldaret/mm](https://github.com/zeldaret/mm), through the copy in 2Ship2Harkinian | the MM sound effect engine in `soh/soh/mmaudio/mmseq` | none published |
| 2Ship2Harkinian (HarbourMasters and contributors) | [2ship2harkinian/2ship2harkinian](https://github.com/2ship2harkinian/2ship2harkinian) | the MM host: Link-Span mounts its `mm.o2r` cross-world and plays MM audio from it | CC0 1.0 |
| Not Enough Items (skijer and contributors) | [skijer/Shipwright, branch `Not-Enough-Items`](https://github.com/skijer/Shipwright/tree/Not-Enough-Items), commit `c29262b` | the item code in the NEI mod, the MM Goron form in `soh/soh/mmform`, and the `nei-assets` players generate on their own machine | none published |
| Unbound mod API (skijer) | [skijer/Shipwright, branch `unbound-modapi`](https://github.com/skijer/Shipwright/tree/unbound-modapi), commit `27bb6c445` | host fixes (UNBOUND-032): unsigned item icon byte in messages, test order in the kaleido equipment panel, bounds in the audio tables | none published |
| Unbound (roborich and contributors) | [roborich/Shipwright](https://github.com/roborich/Shipwright), releases up to [9.3.0-unbound0.11](https://github.com/roborich/Shipwright/releases/tag/9.3.0-unbound0.11), including [9.2.3-unbound0.9](https://github.com/roborich/Shipwright/releases/tag/9.2.3-unbound0.9) and [9.3.0-unbound0.10](https://github.com/roborich/Shipwright/releases/tag/9.3.0-unbound0.10) | the Unbound Framework mod (format, type reader, `baseVersion` from `504e2c0c4`) and host fixes (UNBOUND-032): randomizer spawn points (`e4e933ff8`), Anchor flags in mod scenes (`28ecc36ef`), 16-bit room ids (`53d903e58`) | none published |
| Mic Ocarina (skijer) | [`MicOcarina.cpp` in skijer/2ship2harkinian, branch `Not-Enough-Items`](https://github.com/skijer/2ship2harkinian/blob/Not-Enough-Items/mm/2s2h/Enhancements/Ocarina/MicOcarina.cpp), commit `5608ab477d` | the pitch detector of the Mic Ocarina mod, modified by the maintainer | CC0 1.0 |
| Wind Waker Style (roborich) | [roborich/Shipwright, branch `wind-waker-style-cel-shading`](https://github.com/roborich/Shipwright/tree/wind-waker-style-cel-shading), commit `cbcd3e719d` | the Wind Waker Style mod | none published |
| libultraship and Torch | [Kenix3/libultraship](https://github.com/Kenix3/libultraship), [HarbourMasters/Torch](https://github.com/HarbourMasters/Torch) | engine and asset extractor, listed with the other compiled components in `THIRD-PARTY-NOTICES` | MIT |

Consulted while designing, with no code taken from them: [OoTMM](https://github.com/OoTMM/OoTMM) and
[ComboShip](https://github.com/Varuuna/ComboShip), on how MM forms can run inside OoT; and
[noclip.website](https://noclip.website), whose reproduction of Wind Waker's torch light the Wind Waker Style fork
credits.

The Navi icon on the DMR D-pad HUD is a cropped, downscaled capture of Navi from Ocarina of Time; the reference
image is [Zelda Wiki's `File:OoT_Navi_Model.png`](https://zeldawiki.wiki/wiki/File:OoT_Navi_Model.png). Navi belongs to Nintendo, so the icon is not
covered by the CC0 dedication.
