# Wind Waker Style for Link-Span

A native Link-Span mod for Ship of Harkinian (Ocarina of Time) that brings in the Wind Waker-style rendering of
roborich's SoH fork ([`wind-waker-style-cel-shading`](https://github.com/roborich/Shipwright/tree/wind-waker-style-cel-shading),
commit `cbcd3e719d`). It has four feature families plus the Deku stick light:

| Feature | What it does | Default |
|---|---|---|
| Cel Shading | Relights actors and objects with one dominant key light and a soft toon ramp | on |
| Light Casting | Point lights (torches, fairies, a lit Deku stick) cast faceted light pools on the world | off |
| Actor Shadows | Each actor casts its own silhouette onto the real ground, along the cel key light | off |
| Sky | Gradient dome, drifting clouds and horizon band, twinkling stars, wind wisps; reacts to weather | off |

The static scene is untouched by Cel Shading; the sky only replaces the overworld's normal sky (the scenes where
the game itself draws it).

## Requirements

- Ship of Harkinian with the Link-Span host that provides the services this mod uses: `linkspan.oot.render` v3,
  `linkspan.oot.lights` v1, `linkspan.oot.engine`, `linkspan.oot.world` and the render hooks for actors, world
  lights and the sky. The native ABI must be 1.2 or newer, and the host layout must match the one the DLL was built
  for (the loader refuses a mismatched package).
- Optional: `linkspan.oot.resources` v1, for cloud texture packs.
- Game: OoT only (`games = ["oot"]`). Link-Span API `>=0.5.0 <0.6.0`.
- No dependency on Unbound or Not Enough Items.

## Install

Copy `LinkSpan-WindWakerStyle-<version>.shipmod` into the game's `mods/` folder and start the game. The options are
in the menu (Esc), **Mods** tab, under the four **Wind Waker Style** sections. Every change applies immediately;
nothing needs a restart. Settings live in the mod's own storage, not in the game's CVars.

## Options

### Cel Shading

| Control | Default | Range | Effect |
|---|---|---|---|
| Enable Cel Shading | on | | Relights actors and objects with a single key light and the toon ramp |
| Ramp Center | 50% | 0–100% | Where the dark-to-light transition sits; higher keeps more of the surface in shadow |
| Ramp Softness | 0.02 | 0.01–0.20 | Width of the transition band: low is a hard cel edge, high a soft gradient |
| Highlight Intensity | 60% | 0–200% | Brightness of the lit side |
| Shadow Intensity | 60% | 0–100% | How dark the shadow side gets (0% = flat) |
| Point Light Range (x) | 1.5 | 1.0–4.0 | How far a point light can stay the key light, as a multiple of its radius (key selection only) |
| Use Navi as a Light Source | on | | Navi can be the key light; off keeps the key steady on the sun, moon or a torch |
| Transition Time (s) | 1.0 | 0.1–6.0 | How long the key light takes to travel from one source to another |
| Reset All to Defaults | | | Resets the sliders above |

### Lights

| Control | Default | Range | Effect |
|---|---|---|---|
| Hide Vanilla Torch Glow | on | | Hides the game's flat glow billboard over glowing lights while Light Casting is on |
| Improve Flame Flicker | on | | Slow, organic Wind Waker flicker instead of the jagged per-frame one; applies at the source, so it also affects vanilla lighting and Cel Shading |
| Flicker Speed (x) | 1.00 | 0.10–3.00 | How often flames pick a new brightness |
| Navi's Light Tint | 20% | 0–100% | Tints Navi's light toward her current colour (0% = white) |
| Enable Light Casting | off | | Casts a light pool from each point light onto the static world |
| Use Wind Waker default movement | on | | Pins the pool's tumble and size pulse to Wind Waker's rates |
| Rotation Speed (x) | 1.00 | 0.00–3.00 | Two-axis tumble speed (only with the default movement off) |
| Size Flicker | 1.00 | 0.00–3.00 | Depth of the size pulse (only with the default movement off; Navi excluded) |
| Cast Size (x) | 0.50 | 0.10–4.00 | Pool size, as a multiple of the light's radius |
| Light Intensity | 20% | 0–200% | Pool brightness |
| Enable Navi Light Casting | on | | Navi casts a pool too |
| Navi Cast Size (x) | 0.75 | 0.10–4.00 | Navi's pool size |
| Navi Light Intensity | 20% | 0–200% | Navi's pool brightness |
| Enable Other Fairy Light Casting | off | | Non-Navi fairies (Kokiri Forest, healing fairies) emit light, which Cel Shading also uses |
| Other Fairy Cast Size (x) | 0.75 | 0.10–4.00 | Their pool size |
| Other Fairy Intensity | 20% | 0–200% | Their pool brightness |
| Enable Deku Stick Light Casting | on | | A lit, held Deku stick becomes a real light at its tip: a possible key light, a shadow caster and, with Light Casting on, a pool |
| Deku Stick Cast Size (x) | 0.50 | 0.10–4.00 | The stick's pool size |
| Reset Sliders to Defaults | | | Resets the sliders of this section |

### Actor Shadows

| Control | Default | Range | Effect |
|---|---|---|---|
| Enable Actor Shadows | off | | Shape shadow per actor, cast from the cel key light and wrapped onto the ground (works with Cel Shading off too) |
| Suppress Vanilla Shadows | on | | Hides the game's own actor shadows |
| Opacity | 20% | 0–100% | Darkness of the shadow core |
| Edge Softness | 0 | 0–2 | 0 = hard edge; 1 = one lighter step; 2 = finer ramp and a slightly wider fringe |
| Length | 0.20 | 0.00–1.00 | How long a low light may stretch the shadow |
| Slab Depth | 8 | 5–200 | How far below the feet the shadow follows the ground |
| Slab Rise | 8 | 0–120 | How far above the feet the shadow climbs onto rising ground |
| Render Distance | 550 | 300–5000 | Actors farther than this from the camera get no shape shadow |
| Reset All to Defaults | | | Resets the sliders above |

### Sky

| Control | Default | Range | Effect |
|---|---|---|---|
| Use Sky | off | | Replaces the overworld sky |
| Horizon Height | -408 | -2000–2000 | Moves the horizon line (gradient haze and cloud band together) |
| Horizon Parallax | 75% | 0–150% | How much the horizon sinks as the camera climbs (0% follows the camera) |
| Replace Sky Texture | on | | Smooth gradient that follows the time of day |
| Gradient Brightness | 100% | 50–150% | Overall gradient brightness |
| Enable Clouds | on | | Drifting clouds plus the horizon band |
| Cloud Opacity | 85% | 0–100% | Cloud opacity |
| Coverage | 30% | 0–100% | From scattered clouds to overcast; bad weather raises it on its own |
| Drift Speed (x) | 1.0 | 0.0–4.0 | Cloud speed on the wind; storms blow faster |
| Enable Stars | on | | Night starfield, fading in at dusk and out at dawn |
| Star Count | 1000 | 50–1000 | Stars at full night |
| Star Brightness | 100% | 0–200% | Star brightness |
| Twinkle Speed (x) | 1.0 | 0.1–5.0 | Twinkle rate (1x is about ten seconds per cycle) |
| Enable Wind Wisps | on | | White wind streaks, sometimes looping |
| Wisp Amount (x) | 1.0 | 0.5–10.0 | How many wisps ride the wind |
| Wisp Speed (x) | 1.00 | 0.25–1.50 | How fast they fly |

## Debug tools

| Control | Section | Effect |
|---|---|---|
| Light Source Viewer | Cel Shading | A ray from each actor to every candidate light (light colour, longer when stronger), a cyan range ring around each point light and a magenta needle along the chosen key |
| Highlight Lit Objects | Cel Shading | Relit objects drawn flat white (lit) and black (shadow) |
| Show Light Spheres | Lights | The faceted icosphere used for each light pool |
| Show Shadow Volume | Actor Shadows | The 3D shadow volumes, translucent |
| Split-Screen Compare | Sky | The mod's sky on the left half, the original sky on the right |

The `ww_style_stats` hotkey (Link-Span hotkeys) writes the mod's counters to the log.

## Cloud texture packs

The clouds are drawn from five textures. The mod generates them itself at load, with a byte-for-byte port of the
fork's generator script, so the package carries no archive. A texture pack can replace them: an `.o2r` in `mods/`
with files at these paths wins over the generated ones.

| Path | Default size | Limits |
|---|---|---|
| `textures/wind-waker/clouds/cloudtx_01` | 64×64 | power of two, up to 512×512 |
| `textures/wind-waker/clouds/cloudtx_02` | 64×64 | power of two, up to 512×512 |
| `textures/wind-waker/clouds/cloudtx_03` | 64×64 | power of two, up to 512×512 |
| `textures/wind-waker/clouds/cloud_mae` | 256×64 | power of two, up to 256 wide and 512 tall |
| `textures/wind-waker/clouds/cloud_naka` | 256×64 | power of two, up to 256 wide and 512 tall |

- **Format:** libultraship texture resources (OTEX), RGBA32, up to 4 MiB each. A file that fails the check is
  ignored and the generated texture stays.
- **Clouds:** every drifting cloud layers all three `cloudtx_*` images, slightly offset, so make them three different
  shapes.
- **Horizon strips:** `cloud_mae` is the front layer, with gaps between clusters; `cloud_naka` is the continuous
  back bank. They must tile left to right, and the bottom edge sits on the horizon.
- **Colour:** transparency is the cloud shape. Keep the visible part near white, because the game tints the clouds
  for time of day and weather.
- **When it applies:** the mod reads the pack when a scene loads. Adding or removing a pack takes effect on the next
  scene load.

## Differences from the fork

- **Cloud textures:** generated in the DLL instead of shipped in `soh.o2r`. The override paths are the fork's.
- **Menu and storage:** Link-Span's per-mod menu and storage, instead of the SoH menu and
  `gEnhancements.Graphics.*` CVars. Old fork settings are not imported.
- **Renderer primitives:** the toon, stencil and shadow primitives come from the Link-Span host, not from a patched
  renderer in the mod. The mod holds only the policy: which light is the key, who is excluded, the look.
- **Navi and fairies:** Navi's light and the other fairies are reached through a host hook, not by editing the fairy
  actor.
- **Shadow receivers:** the host's receiver list is per actor type, so the graveyard gate (`BG_HAKA_GATE`) is a
  receiver as a whole, where the fork accepted only its floor and statue.

## Credits and licence

See [`NOTICE.md`](./NOTICE.md).
