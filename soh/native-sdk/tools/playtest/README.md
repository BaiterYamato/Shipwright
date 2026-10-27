# Link-Span OoT playtest tool

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
The pure C++ test executable does not link socket or game code. Python runtime dependencies are `mcp` and the standard
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
