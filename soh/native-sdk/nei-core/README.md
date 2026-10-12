# Not Enough Items (Link-Span)

Coremod `linkspan.nei`. Brings items from the [skijer/Not-Enough-Items](https://github.com/skijer/Not-Enough-Items) fork
(commit `c29262b`) to Ship of Harkinian with Link-Span, without rebuilding the game:

- an extra inventory page (L switches pages in the item menu);
- items with their own get-item, model and text;
- ownership, quantity and level stored in the save;
- the `linkspan.nei.items` service for other mods to create items.

## Installation

1. Have Ship of Harkinian with the Link-Span overlay installed. The package name includes the host's layout id
   (`layout-xxxxxxxx`), and it must match that of the installed `soh.exe`. Also install the `soh.symbols`
   generated for that same executable next to `soh.exe`.
2. Copy the `.shipmod` to the game's `mods` folder without extracting it.
3. Launch the game. A `nei-core:` line in the log (`logs/Ship of Harkinian.log`) shows the coremod's state.

## Test items through the interface

Starting with NEI 0.3.10, with a save file open, go to **Mods > Not Enough Items > NEI Items**. Items
appear in an icon grid: a colored icon with a white border means active in the
save; a darkened icon means disabled. Click the icon to enable or disable it.
Disabling an equipped item also removes it from the C button. Press **L** on the
game's item page to reach the NEI page. The grid includes items from
add-ons registered in this session. The result appears in **Status** and in the log.

## Items and abilities in version 0.3.23

### Animations and effects

- **Roc's Cape**: the second jump uses OoT's native flip when MM animations are unavailable. The first jump, heights, landing and limit of one extra jump are preserved.
- **Winter**: contact with frozen water is checked on the current Player, including the first landing. Ripples, splashes and water footsteps are suppressed on ice; normal water and swimming below the surface retain their original effects.

### Inventory controls with Dynamic Movement Remake 0.2.14

- Tap A and release it to open the three hotbar slots. Select with either stick or the D-pad and confirm with A; B cancels.
- Hold A for 0.4 seconds on the cell to open variant switching (rods, canes, runes, bottles and other NEI selectors). Releasing that A press does not equip the item.
- In variant selectors, use either stick or the D-pad. Tap A to close the selection.
- Outside the inventory, hold R, choose a slot with either stick or the D-pad and release R to select it for ZR.
- The D-pad works in selectors even when normal D-pad inventory navigation is disabled.

- **Sheikah Slate**, **Rod of Seasons** and **Phantom Hourglass** enter the regular registry: they can be
  granted through the grid or with `give_item linkspan skijer.nei.sheikah_slate` (replace the last name with
  `rod_of_seasons` or `phantom_hourglass`). They appear on the NEI page and use persistent IDs on C buttons.
- **Mods > Not Enough Items > Modes & Abilities** lets you individually grant/remove six Elemental Wand
  modes, five Slate runes, four seasons and six cane abilities. It does not change
  the medallions or the game's progress flags. The default randomizer mode still requires medallions.
- Cane type selection uses the original counting and navigation functions instead of stubs.
- The C button and DMR selector icons follow the selected rune/mode. Pictograph's I5 pixels
  now persist in `nei.state`, alongside the other fields.
- The Slate, Hourglass, Seasons and Wand use routines run again on every Link update;
  the port's glue code restores the calls that existed in the fork's Player and were missing in the host.
- The remote bomb discards the old reference when changing or reloading the scene, including when
  the actor's address is reused. The Hourglass trail uses frame geometry and discards
  very short segments to prevent small NPC movements from filling the screen.

### Added backends and controls

- **Sheikah Sensor**: configure up to five wishes in its dedicated Mods tab. It queries the current randomizer
  seed and ignores checks already collected/saved. The original confirmation permanently costs one Heart
  Container; no cost is charged when no hint is available, there is no randomizer seed or you cancel.
- **Rod of Seasons**: original adapters for water, the lake, Water Temple, sand, weather and spring
  magic beans. Unbound's wide collision uses 32-bit coordinates; removing the Rod returns water
  to its normal state. Golden summer Deku Flowers depend on the form expansion, which is still missing.
- **NEI Equipment**: grants/removes equipment and equips it when age permits. To switch
  between pieces you already own, use L on the inventory's Equipment page. The Four Sword is for child Link:
  hold R+B to summon clones and L to select the formation. The Kite Shield allows surfing:
  press R in the air; A jumps, B spins and R+B dismounts.
  The Four Sword requires an owned/equipped vanilla shield to raise your guard and charge
  the clones. With DMR active, these items' R+B combination takes priority over the
  regular attack; other equipment retains the normal control rule.
- **Bottles**: adding a bottle activates the two four-position wheels in slots 1 and 2,
  preserving their current contents. Regular slots 3 and 4 remain available. Bottomless Bottle
  uses slot 4 and retains the original content and counter; disabling it preserves the bottle/content.
  Bottles with the same contents remain distinct positions. The net and MM-exclusive contents
  remain outside this port.

Compiled code, automated tests and in-game tests are different kinds of evidence. Consult the
report in the playable folder for the scenarios actually observed in this delivery.

The package includes the SHA-256 of the `soh.exe` it was built for:

- **With that executable and the matching `soh.symbols`:** the fork's item code runs through the escape hatch
  (`fork: ativo`). The log should report defined items; `definidos=0` and `fork: desligado` indicate that NEI
  did not initialize the items.
- **With another executable:** only item registration works, and the log line shows the fork disabled. The game runs
  normally in both cases.

## Optional components

None of these are included in the package.

| Component | What changes | How to obtain it |
|---|---|---|
| `nei-assets/nei-assets-core.o2r` | item models, icons and textures | generated on your computer (below) |
| `nei-assets-form.*`, `nei-assets-expansion.*` | forms and expansions | generated by the same script; not yet used in this version |
| Super Mario 64 ROM | Mario expansion | not used in this version; never distributed |

**Without `nei-assets-core.o2r`:** the coremod defines only Roc's Feather and Roc's Cape, with placeholder
icons and models. The fork's assets have no declared license, so they are not included in any Link-Span package.

**To generate them**, from a Link-Span host checkout with the fork commit fetched
(`git fetch https://github.com/skijer/Not-Enough-Items c29262b`):

```text
python tools/build-nei-assets.py --repo . --packer build/x64/Release/soh-o2r-packer.exe \
    --base <oot.o2r> --base <soh.o2r> --fork-source build/nei-core-native/nei-fork/fork --out build/nei-assets
```

Copy `build/nei-assets/nei-assets-*.o2r` to `<game>/nei-assets/`. The coremod mounts whatever it finds there on startup,
and the log shows `assets: core+...`.

Starting with version 0.3.6, the generator replaces only `sSpinnerTex`, whose fork image is a colored pattern
unsuited to the model, with the bronze material in `assets/spinner-bronze.otex`. The Spinner's model and movement
remain those of the fork. The model for the Deku Leaf and other items mounted after initialization is
located through the active resource service; the leaf appears in Link's hand when he blows and above his hands when he glides.
You must regenerate `nei-assets-core.o2r` and install an NEI 0.3.6 or newer package.

In version 0.3.7, the Gust Jar draws the fork's cone and wind ribbons again. Suction mode shows
white wind; when blowing, the color follows the selected element (blue ice, for example). This effect uses the
`object_nei_tornado` resources from `nei-assets-core.o2r`, so the package and the locally generated assets
must be installed together.

## Randomizer and Anchor

- **Randomizer:** with the "Link-Span Mod Items" seed option enabled, NEI items enter the pool through the
  host's `linkspan.oot.randomizer` service. Without the mod loaded, these checks give a blue rupee.
- **Console:** with a save file loaded, open `Dev Tools > Console` and use
  `give_item linkspan skijer.nei.rocs_feather` to receive Roc's Feather. The command uses the registered name
  and also works in regular saves; `give_item randomizer <id>` depends on the association made by the seed.
- **Anchor:** in a room that synchronizes items and flags, an NEI item's get-item reaches partners by the
  item's name. The team state carries the `nei.items` and `nei.state` blocks. All clients in the room need
  Link-Span with this coremod.

## Create an add-on

An add-on is a native mod that uses `linkspan.nei.items` v1 (`include/linkspan/nei/nei_items.h`, included in the
package).

Declare the dependency in the manifest:

```toml
[provider]
abi_version = "1.2"
win64 = "provider/meu_mod.dll"

[dependencies]
"linkspan.nei" = ">=0.3.0 <0.4.0"
```

In the DLL's `Init`:

1. get the service:
   `runtime->get_service(ctx, LINKSPAN_NEI_ITEMS_SERVICE, LINKSPAN_NEI_ITEMS_VERSION, sizeof(NeiItemsV1))`;
2. call `define_item` with a `NeiItemDefinitionV1`, with:
   - id `autor.mod.item`;
   - a 32x32 RGBA32 icon per level;
   - names in English, German and French;
   - the get-item model and text;
   - `use` and `received` callbacks;
3. in `Shutdown`, call `remove_item`.

NEI handles the rest:

- the C button;
- counter rendering;
- save (the `nei.items` block, by id);
- get-item, with `give_item`;
- randomizer and Anchor.

The fork also recognizes when a C button contains a native item from another mod.
In that case, it passes the original runtime ID to the host so that item use reaches its
owner. This avoids collisions between the NEI page's logical IDs and the
temporary IDs of the generic OoT registry.

The complete example is in `docs/examples/heart-seeds`:

- an item with a quantity and an upgrade;
- each use spends one seed and drops a heart;
- the bag levels up on the third use.

Build against the OoT native SDK headers with the same layout id.

## Diagnostics

The `nei-core:` log line shows:

- defined and owned items;
- the state of the fork, randomizer, kaleido and inventory;
- the loaded file;
- mounted assets;
- the Anchor connection.

It is only written when something changes.
