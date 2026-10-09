# Notice — Wind Waker Style for Link-Span

## Ported code

| Source | What was used |
|---|---|
| roborich's Ship of Harkinian fork, branch [`wind-waker-style-cel-shading`](https://github.com/roborich/Shipwright/tree/wind-waker-style-cel-shading), commit `cbcd3e719d` | The policy code of the four feature families and the Deku stick light (`ToonLighting`, `WorldLighting`, `DekuStickLight`, `WWSkyEnv`, `WWSkyGradient`, `WWNightSky`, `WWClouds`, `WWWindWisps`), their constants and tuning, the debug views, and the menu labels, defaults and tooltips. Ported to a Link-Span native mod over the host's public services. |
| The same fork, `gen-ww-cloud-textures.py` | The cloud texture generator, ported to C++ with byte-identical output (checked against the script by hash in `tests/cloud_textures_tests.cpp`). |

## Data

- **Sky palette:** the dawn-to-night sky colours (clear and rain sets) are the fork's `kSeaPalette`. The fork's
  documentation says they were extracted from Wind Waker's `sea/Stage.arc` (`stage.dzs`: EnvR, Colo, Pale and Virt
  chunks), and that "only colour values ship — no Nintendo assets". This package ships the same colour values and
  no Nintendo files.

## Research, not code

- **Torch light pool:** the fork's documentation credits its Light Casting flicker and tumble constants to the
  noclip.website reproduction of Wind Waker's torch light (`bonbori`). They reach this mod only through the fork's
  code.

## Host

The mod runs on Ship of Harkinian with the Link-Span modloader. The projects behind them (HarbourMasters'
Shipwright and libultraship, the zeldaret decompilation) are credited in the Link-Span overlay's `NOTICE.md` and
`THIRD-PARTY-NOTICES.md`.

## Licence

Link-Span's own work in this package (the native mod around the ported code, the Lua, the tests and the
documentation) is dedicated to the public domain under CC0 1.0 Universal (`LICENSE`). The code ported from
roborich's fork stays with its authors: the fork publishes no licence, and this package grants none for that code.
