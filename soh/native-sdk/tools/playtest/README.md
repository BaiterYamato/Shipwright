# Link-Span OoT playtest tool

## VFS layers and RC11 evidence (0.2.2, protocol v2)

`vfs_layers(paths, max_bytes=8388608)` queries `linkspan.oot.resources` v2 on the game thread, at the
next `oot.input.pad` callback, including the title screen. It returns `frame`, `bytes_hashed`,
`instrumentation`, `complete`, and `observations`. Each observation contains `path`, `layers`,
`hostStatus`, `hasFile`, `absent`, and `complete`; each layer is `{archive, sha256}`. Order is **low to high priority**;
the last layer has the highest priority. SHA-256 covers the callback bytes, including binary NULs and empty files,
not the archive container and not the host's 64-bit `content_hash`. Transformed archives expose transformed bytes.
Archive identifiers are copied verbatim; prefixed mounts may include `#linkspan-mount-N`.

Limits per call: 1–8 distinct paths, 1–1024 UTF-8 bytes per path, no embedded NUL, at most 32 layers total,
1024 bytes per archive identifier, and 1–8388608 bytes **hashed by the observer** (default 8 MiB).
`max_bytes` is a byte count, so values below 1 MiB are also accepted. Exceeding a limit yields host status 4;
that path's partial layer list is discarded. The budget includes bytes hashed before a later error.
The host loads all matching layers before invoking callbacks, so this API cannot bound host allocations,
decompression, I/O or frame duration. The observer uses fixed buffers and a BCrypt reusable hash initialized before
the socket starts; it adds no JSON construction or explicit heap allocation to the frame callback. JSON serialization
runs on the TCP thread. No callback-owned pointer survives the call; only the static service table is retained.
Operations use the existing lease, pending ticket and `result` polling. Cancel, scene destruction, disconnect,
takeover and shutdown invalidate VFS tickets. Requests are not automatically replayed.

Host status codes: 0 OK, 1 INVALID_ARGUMENT, 2 UNSUPPORTED, 3 FAILURE, 4 LIMIT. **Status 2 alone conflates a
missing file and an unavailable VFS.** Since 0.2.2 the agent certifies absence in the same frame: when
`read_file_layers` returns 2, `has_file` is false, the path has no glob metacharacter (`* ? [ \`) and
`list_files(path)` returns OK with no entry (it only returns OK while the archive manager is alive), the observation
has `absent=true`, `complete=true`, `layers=[]` and keeps `hostStatus=2`. Any other status 2 stays `complete=false`,
`absent=false`. `complete=true` otherwise means status 0 with at least one layer; the host itself can omit unreadable
layers. Diagnostic receipts (`complete=false`) must not automatically pass a gate.

Use `game_vfs_snapshot(case, phase, paths, max_bytes=8388608)` to save the gate receipt:

```python
game_vfs_snapshot("alias-A-B", "after-ready", ["private/A/file.json", "B/file.json"])
# Perform the intended mod unload and confirm its completion externally.
game_vfs_snapshot("alias-A-B", "after-unload", ["private/A/file.json", "B/file.json"])
game_close()
```

Confirm `game.ready` and unload in the real host log before naming each phase. `phase` is supplied by the caller;
these tools do not trigger or certify either event. `case` accepts 1–80 ASCII letters, digits, `_` or `-`.
Each `(phase,path)` can occur once per case; several disjoint path batches can be accumulated.
The collector writes `<case>-rc11-vfs-1.pending.json` in the session evidence directory and returns
`status=pending_post_log`. This draft has `postLogSha256=null` and is **not final gate evidence**.
After the process exits, `game_close` copies the closed session log and writes `<case>-rc11-vfs-1.json` with
`schema="rc11-vfs-1"`, `case`, `instrumentation`, `exeSha256`, `postLogSha256`, `observations`, and `complete`.
Its `vfs_snapshots` result lists final paths. The adjacent `.post.log` contains the exact hashed bytes.
The exe hash is the launch-time SHA from session metadata, not an in-memory executable attestation.
Observer revision is a CMake SHA-256 over the named observer sources; `mcp_revision` hashes the snapshot writer.
Neither revision is a Git commit or a DLL hash. Rebuild the agent for this operation; older v2 agents reject it.

Finalization requires the actual `game-final.log`; it never invents a log hash. Failed finalization preserves drafts.
For offline recovery, call `linkspan_playtest.vfs.finalize_vfs_snapshots(session)` with an exited session whose
`directory`, `metadata["closed"]`, and `process.poll()` are available. Repeated finalization verifies the frozen log.
It rejects a changed frozen log or conflicting final receipt. Paths without certified absence remain diagnostics in the
final receipt, including after unload. Alias A/B queries prove visible origins and bytes; alias rules and owner identities
must be corroborated by the fixture and host log. C++/MSVC and live ready → query → unload → query → close validation
are still required. Python tests simulate the agent only.

This SDK tool controls port 0 through `oot.input.pad` and exposes a local agent to a Python MCP server. It requires
the RFC 0032 host hook and `--linkspan-tool` loader, ABI 1.4, Windows x64, and a matching exported host SDK. It is
development tooling: keep it outside `mods/` and release packages. Loading it must not enable the extended engine.

Build the agent from a Developer PowerShell with MSVC and CMake 3.26 or newer. Replace the SDK and executable paths
with files from the **same host build**:

```powershell
cmake -S agent -B agent/build -G "Visual Studio 17 2022" -A x64 `
  -DOOT_NATIVE_SDK=D:/host/build/x64/native-sdk/Release/OotNativeSdk.cmake `
  -DHOST_EXECUTABLE=D:/host/build/x64/Release/soh.exe
cmake --build agent/build --config Release
ctest --test-dir agent/build -C Release --output-on-failure
python -m pip install ./mcp
python -m unittest discover -s mcp/tests -p "test_*.py"
```

The generated package is `agent/build/mod/` (manifest, generated empty `main.lua`, and
`provider/linkspan_playtest.dll`). CMake writes `provider.host_layout_id`; no executable fingerprint is required.
Rebuild after a host layout change. nlohmann JSON comes from `OOT_NATIVE_INCLUDE_DIRS`; C++20 and `/utf-8` are enabled.
The pure C++ test executable links Windows BCrypt for SHA-256, but no socket or game code. Python runtime dependencies are `mcp` and the standard
library; the tests use `unittest` (pytest can also collect them).

Set `LINKSPAN_PLAYTEST_AGENT` to the generated package when using an installed Python package. When running directly
from the SDK source, `agent/build/mod` is the default. `LINKSPAN_PLAYTEST_PORT` defaults to 47811. The agent binds only
`127.0.0.1`, refuses to listen without `LINKSPAN_PLAYTEST_TOKEN`, and accepts one controlling connection at a time.
`game_launch` creates a random per-session token; never put tokens into MCP configuration.

Example Claude `.mcp.json` entry (merge into your existing configuration):

```json
{
  "mcpServers": {
    "linkspan-playtest": {
      "command": "C:/Program Files/Python314/Scripts/linkspan-playtest-mcp.exe",
      "env": { "LINKSPAN_PLAYTEST_AGENT": "D:/sdk/tools/playtest/agent/build/mod" }
    }
  }
}
```

Example Codex `config.toml` entry:

```toml
[mcp_servers.linkspan-playtest]
command = 'C:\Program Files\Python314\Scripts\linkspan-playtest-mcp.exe'

[mcp_servers.linkspan-playtest.env]
LINKSPAN_PLAYTEST_AGENT = 'D:\sdk\tools\playtest\agent\build\mod'
```

Use the interpreter/launcher path of the Python environment where you installed the package. For a source checkout,
`python -m linkspan_playtest.server` also works with `mcp/` as working directory or on `PYTHONPATH`.

| Tool | Behavior |
| --- | --- |
| `game_launch(runtime, console=false)` | Launch `runtime/soh.exe`, using the runtime as CWD; wait up to 90 seconds for authentication. Return PID, SHA-256, log and evidence directory. |
| `game_attach()` | Reconnect to the latest open session, or the directory in `LINKSPAN_PLAYTEST_SESSION`. Does not force an existing controller out. |
| `game_close()` | Send `WM_CLOSE`, wait up to 60 seconds, restore Sail CVars, return exit code and error/critical log lines. |
| `game_state()` | Last hook snapshot. Non-applicable gameplay fields are `null`. |
| `input(steps, wait=true)` | Queue 1–600 steps; each lasts 1–3600 pad reads. Wait for the frame after the final step. |
| `wait_for(condition, timeout=30)` | Poll locally until comparisons joined by `and` hold. |
| `screenshot(label="capture")` | Return PNG image plus path and `focus_used`. Labels allow ASCII letters, digits, `_` and `-`. |
| `console(command)` | Send a Sail console command; requires a console-enabled session. Success means dispatch accepted, not that a transition already completed. |
| `log(lines=100, pattern=null)` | Tail session log and captured stderr; optional literal substring filter. |

Proven sequence (27/10/2026, about 26 s): `game_launch(console=true)`, poll until `game_state == "title"`, repeat
`input([{"buttons":["START"],"frames":3},{"frames":40}])` until `file_select`, repeat the same with `A` until `play`,
`wait_for('game_state == "play" and textbox == false')`, `input([{"stick":[0,127],"frames":90}])`, compare `position`,
`screenshot`, `console('entrance 205')`, `wait_for('scene != <old>')`, then `game_close`.

Steps accept `buttons`, `stick: [x,y]`, `cstick: [x,y]`, `frames` and `mode` (`replace`, the default, or `merge`).
Axes range from -128 to 127. Button names are `A B Z L R START CUP CDOWN CLEFT CRIGHT DUP DDOWN DLEFT DRIGHT`.
Merge ORs buttons and replaces only nonzero axes; replace ignores physical input for the step. Physical input resumes
when the queue empties. Use explicit neutral steps between repeated presses. On cancellation, release, forced takeover
or owner disconnection, the next pad read is neutral. A socket failure never automatically replays `input`.

Snapshots expose `frame`, `game_state`, `scene`, `room`, `entrance`, `position.{x,y,z}`, `rotation.{x,y,z}`, `age`
(0 adult, 1 child), `health`, `max_health` (both in the game's sixteenths of a heart), `magic`, `rupees`, `pause`,
`textbox`, `queue_size`, `sail`, `game_mode` and `file_num` (from `gSaveContext`). Frame counts pad reads, not
rendered frames. Rotation uses game angle units. The state comes from the current `GameState::destroy` (the host clears
`init` after running it): `title` covers the logo, the opening and the title demo, which is a `PlayState` with
`game_mode == 1`; a state without a catalog symbol is `other`. Before the first pad callback, input is refused. Conditions accept
`== != < <= > >=`, `and`, JSON numbers/strings/booleans/null and the documented fields; arbitrary code is rejected.

Evidence goes under `LINKSPAN_PLAYTEST_EVIDENCE` or
`D:\Desenvolvimento\ship-lua-worktrees\logs\playtest`, in unique timestamped directories. Each session stores executable
SHA, received commands, PNGs, captured stderr and the final game log. `session.json` contains the temporary token for
attach; keep this directory private and do not publish that file. Successful close removes the token. Keep the game
open if the MCP process exits, then use attach to recover it; changing Sail CVars back occurs only after game exit.

With `console=true`, the server listens on local Sail port 43384 and temporarily changes `CVars.gRemote.Sail` in
`shipofharkinian.json`. It restores only those values, preserving other settings saved by the game. The host's Sail
mode also enables the Dark Link voidout fix; snapshots report `sail: true`. The flag records the requested Sail mode,
not proof of a connected Sail socket. No listener or CVar changes are made with the default `console=false`.

Capture first tries `PrintWindow(PW_RENDERFULLCONTENT)` without activation. Failure or an all-black image triggers a
foreground `BitBlt` fallback, reports `focus_used: true`, and attempts to restore the previous foreground window.
If Windows refuses focus, the capture fails explicitly. On 27/10/2026 `PrintWindow` captured the DX11 window in the
background without focus; minimized windows and genuinely black frames were not tested. The tool does not prove physical-controller behavior or microphone/gyro input.

The host CTest `oot_native_playtest_tool` builds this package against its exported SDK, runs the pure C++ tests, then
runs the Python tests with `unittest`. These checks do not replace the live title → file select → gameplay →
transition → screenshot proof.

## v2 — PLAYTEST-MCP-002 (0.2.0)

The nine earlier tools keep their signatures and behavior. v2 adds:

| Tool | Contract |
| --- | --- |
| `scene_map(offset=0, limit=128)` | Live scene: spawns, local entrances, exits, rooms, and transition actors with position, `rotY`, params, rooms, and crossing plane. |
| `scene_overview(y_min=null, y_max=null, bbox=null, include_actors=false, include_ceiling=false, stride=1, limit=128, max_pages=32, size=960, ...)` | Top-down PNG of the static collision plus a text summary: floor shaded by height, walls, water, Link with a yaw arrow, transition planes with their side rooms, and actors. |
| `actors(radius=null, category=null, offset=0, limit=128)` | Live actors sorted by 3D distance, with category/id/name/position/room. Categories are 0..11; NPC=4, prop=6, door=10. |
| `teleport(x,y,z,yaw=null,timeout=3)` | Checks floor, walls, and the Player; writes the current, previous, and home positions, clears velocity and pushed speed, then waits for two updates and bgcheck. |
| `camera_state()` | `eye`, `at`, yaw/pitch/distance, the real `input_yaw`, mode, frame, and epoch. |
| `camera_set(mode="free",yaw=null,pitch=null,distance=null,eye=null,at=null)` | Use yaw+pitch+distance, optionally with `at`, or eye+at. `mode="normal"` releases the camera. |
| `walk_to(x,z,tolerance=20,timeout=15)` | Straight-line pad control that compensates for the input yaw. Stops at a wall, a drop, the timeout, or 1.5 s without getting closer to the target. Returns progress. |

The new tools use **degrees**: yaw 0 faces +Z and +90 faces +X; a negative pitch looks down. Input `eye` and `at` are
`[x,y,z]` lists; returned positions are `{x,y,z}` objects. Map `rotY`/rotation and the `game_state` rotation stay in
the game's binary units (16384 = 90 degrees). `camera_state.yaw` is the eye-to-at view direction; `input_yaw` is
`Camera_GetInputDirYaw`, which `walk_to` uses.

The map reads `PlayState` and the alternate header the game actually ran. `PlayState` has no counts for spawns,
entrances, and exits, so the host captures them from the scene commands it executed and serves them through
`linkspan.oot.scene_query` v1 (`soh/native-sdk/include/oot_scene_query.h`, exported with the SDK). That header is a
versioned service contract outside the layout id globs, so adding it does not invalidate built mods; consumers still
check `layout_id` and the element stride before indexing. On a host without the service the agent returns
`complete=false`, `null` for the unknown lists, and a warning; rooms, transitions, and the selected spawn stay
available. The agent never opens O2R files.

`scene_map.entrance_index` is the global entrance; `current_entrance` is the local `setupEntranceList` index
(`curSpawn`). Spawns and entrances are separate lists: `entrances[i].spawn` indexes `spawns`. Exits are indices into
the global entrance table and carry no coordinates. Rooms are listed by id and current/previous state, with no floor
geometry. `counts` gives totals; advance `offset += limit` while `has_more=true`. `complete` reports whether the lists
are available, independent of paging. Read the pages again if `scene_epoch` changes.

`transitions[].crossing` carries the unit normal, the axis (`x`, `y`, `z`, or `oblique_xz`), and the rooms on the
negative and positive sides. The side is `dot(Link - transition.position, normal)`. A horizontal EnHoll uses the
normal `(sin(rotY),0,cos(rotY))`; vertical types 1/2/5 use Y. Types 2/5 have side 0 above; type 1 has a fixed
destination on side 1. Type 0 preloads the opposite room; type 3 depends on a switch. Horizontal types 4/6 switch
rooms only while `50 < |side| < 100`, so walk through the plane instead of stopping on it. `Door_Shutter` uses the
local-Z rule verified in the host. Other actors return `semantics="orientation_only"` and `null` side rooms, because
their orientation does not prove their activation rule. Their params stay available for inspection.

`scene_overview` reads `colCtx.colHeader` inside the game callback and pages through polygons with raw cursors
(at most 4096 polygons scanned and 128 returned per page); the MCP joins up to `max_pages` pages from the same
`scene_epoch` and renders the PNG with the standard library only. Floors, walls, and ceilings follow the normal
thresholds of `z_bgcheck.c`. The Y window defaults to Link-200..Link+300 and is fixed on the first page; narrow it on
maps with several floors, or pass `bbox=[x_min,z_min,x_max,z_max]`. Polygons carry no room, so rooms are located only
through transitions, actors, and water boxes. Dynamic collision (`DynaPoly`, moving platforms) is not drawn. The image
is saved in the session folder and returned as MCP image content; `partial=true` and `next_offsets` mean more pages
are left. It does not plan routes or prove that an area is walkable. In game, Kokiri Forest (1692 polygons) took 12
pages and under 1 s.

`actors` accepts radius 0 (coincident actors only), rejects negative values, and limits pages to 1..128. Names come
from `tables/actor_table.h`; dynamic types may have `name=null`. The scan guards against cycles, stops at 4096 actors,
and reports `truncated`. Pages are different snapshots; actors can spawn or despawn between them.

`teleport` refuses pause/text, cutscenes, automatic walking, transitions, doors, rolling, water, climbing, Epona, and
incompatible Player states; it never changes the action function, and the Player must be on the ground. The floor
probe starts at `y+50`, needs a floor within 200 units of `y` with normal Y >= 0.5, and the target must be clear of
walls. Y snaps to floor+1, and the fall height is reset to the target. Only `status="grounded"` confirms the position
after bgcheck; `refused`, `timeout`, `scene_changed`, and `unsupported` are distinct results. `mutated=true` reports
that the agent wrote state or took the pad even if confirmation failed afterwards; there is no rollback or retry. A
refusal before the operation takes the pad leaves the pending `input` queue untouched.

The camera goes through `linkspan.oot.camera` with exclusive ownership; the host reapplies the view. Ownership is lost
on cutscenes and scene changes; cancel/release, disconnect, and shutdown release it. Pending cleanup runs inside the
same critical section as the next request, so a cancel followed by a new `camera_set` cannot release the new view.
`camera_set` confirms that the service accepted the view; read `camera_state` on a later frame to check the effective
view. The agent never writes camera memory.

`walk_to` keeps the pad neutral between frames, checks walls and floor, and waits while a room loads; the wait does not
count against the 1.5 s progress window, and a request that arrives during a load starts when it ends. A scene or
Player change stops it. Being pushed sideways (conveyors, moving actors) does not count as progress. It does not plan
routes, open doors, or solve obstacles. `arrived`, `blocked`, `refused`, `timeout`, and `scene_changed` include
position and progress; an instant result does not remove residual inertia.

The snapshot gains `scene_epoch`, `grounded`, `controllable`, `blocked_reason`, and `camera`; `wait_for` accepts these
scalars plus `camera.yaw`, `camera.input_yaw`, and `camera.mode` (not the `blocked_reason` text).

### JSON protocol v2

Line-delimited JSON keeps `{id,op,...}` and `{id,ok,...}`. The MCP `hello` sends `protocol_version:2`; the reply
includes `protocol_version:2`, `agent_version:"0.2.0"`, `layout_id`, and capabilities. The new MCP refuses agents
without v2, including on attach, without waiting 90 s for a permanent mismatch. The new agent still accepts a v1
`hello` without the field from old clients; when present, the number must be the integer 2.

New operations run on the game thread. The TCP thread returns `{ticket,status:"pending"}`; poll
`{id,op:"result",ticket}` until `{status:"done",result:{...}}`. One operation can be pending at a time; a conflict
returns `ok:false` with busy. The last result lives until the next request or cancel. The MCP polls for you and
records each request and result in the evidence. `input` is busy while a v2 operation is pending. Cancel, an owner
change, and disconnect invalidate the ticket and leave input neutral; camera cleanup happens on the next game
callback. Without a callback the client cancels after timeout+5 s. Mutations are never replayed after a socket error.

Unknown or internal arguments, booleans where numbers are expected, non-finite values, timeouts outside 0.1..120 s,
coordinates beyond +/-10,000,000, pitch outside -89..89, and invalid camera combinations are refused. The CMake tests
include `playtest_gameplay_tests` (fake services plus the real TCP server, with regressions for the adversarial
review) next to the pure protocol/queue/lease tests. The in-game proof (Kokiri Forest, through the EnHoll between
rooms 0 and 1 and back, using only the new tools) passed on host `2E11252A`.
