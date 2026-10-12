# Notice — Link-Span overlay for Ship of Harkinian 9.3.0

This overlay replaces `soh.exe` and `soh.o2r` of a Ship of Harkinian 9.3.0 installation and adds the Link-Span
files (`soh.symbols`, extractor YAMLs, controller database, README).

- `soh.exe` and `soh.o2r` are built from Ship of Harkinian
  ([HarbourMasters/Shipwright](https://github.com/HarbourMasters/Shipwright), base 9.3.0, `ecd889c201`), which
  publishes no license, and from the zeldaret decompilation it builds on. That code and those assets stay with
  their authors; this overlay grants no license for them.
- Link-Span's own work in them — the modloader, the native services and SDK, and the Link-Span changes to
  Shipwright files — is dedicated to the public domain under CC0 1.0 Universal (`LICENSE-LinkSpan.txt`).
- The third-party components compiled into `soh.exe` or packed in `soh.o2r` keep their licenses; the list and
  the license texts are in `THIRD-PARTY-NOTICES-LinkSpan.md`.
- No ROM, extracted game asset or save is part of this overlay. `oot.o2r` is extracted on the player's machine
  from their own ROM.

## Credits and sources

The projects Link-Span takes code, data or design from. All of them except 2Ship2Harkinian publish no license;
their code stays with their authors, and this overlay grants no license for it.

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
