# Unbound 0.9 actor demo

This data mod adds four actors to Hyrule Field, in front of the Market exit:
an animated carpenter with head tracking and dialogue, Malon in a fixed pose,
a pot with collision and dialogue, and a translucent glass pane.

The files reference models and textures from the base game. Dialogue uses
messages `0xA001`–`0xA003` in the English message table.

Install the `.o2r` in `mods` alongside Unbound Framework 0.6.0 and the updated
host. Enter Hyrule Field through the Market exit. The mod uses its own actor
keys, so it preserves trees and gates added by other mods.

The models and parameters were adapted from the official Unbound 0.9 example;
see [NOTICE.md](../NOTICE.md). The `assets` folder contains the complete `.o2r`.
