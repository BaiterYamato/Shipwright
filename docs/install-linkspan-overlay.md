**English** · [Português (Brasil)](install-linkspan-overlay.pt-BR.md)

# Install Link-Span over Shipwright 9.2.3

The Windows package is an overlay ZIP. It is designed to be extracted into the root
of a fresh [Shipwright 9.2.3 release](https://github.com/HarbourMasters/Shipwright/releases/tag/9.2.3) installation,
replacing the host files that need Link-Span support.

Since `OOT-UPSTREAM-001`, the overlay host has been built from the official
`develop` commit `78dc6d970` with Link-Span integrated. The base installation is still
the official 9.2.3 release: `develop` still reports itself as 9.2.3 and accepts the `oot.o2r` generated
by that release, because it only requires a new extraction when the major version stored in
the archive changes.

## What the overlay changes

- replaces `soh.exe` with the host built with Link-Span;
- replaces `soh.o2r` with the version generated alongside that executable;
- replaces `gamecontrollerdb.txt` with the controller database downloaded by the build;
- adds the `develop` Torch extractor's `.yml` files to `assets` (`config.yml` and
  one folder per ROM version). 9.2.3 used XML and ZAPD for extraction; those
  old files remain unused in the folder, and none of them are overwritten;
- adds the `mods` folder and, when requested during packaging, test mods;
- adds package metadata, instructions and checksums.

The overlay neither includes nor replaces `oot.o2r`, ROMs, saves or personal files.
`soh.exe` and `soh.o2r` must always be distributed and installed as a matching
pair. Mixing the Link-Span executable with the official `soh.o2r` causes a failure while
loading the interface.

`develop` registers `ConfigVersion7Updater`, which migrates `shipofharkinian.json`
to a format the official 9.2.3 release does not recognize. If you have already used the folder,
back up that file and the `Save` folder before applying the overlay.

## Player installation

1. Extract `SoH-Ackbar-Delta-Win64.zip` from release 9.2.3 into a new folder.
2. Run Shipwright once and complete the normal generation of your `oot.o2r`.
3. Close the game.
4. If the folder has already been used, copy `shipofharkinian.json` and `Save` to another
   location.
5. Extract `LinkSpan-Shipwright-9.2.3-Upstream-78dc6d970-Win64-overlay.zip`
   into that same folder and confirm file replacement.
6. Place `.zip` or `.shipmod` mods directly in `mods` and run `soh.exe`.

The mod package must have `manifest.toml` at its root. A native mod can load
its own DLL declared in the manifest; it therefore has the same access to
the process and the same crash risk as a traditional native mod.
Core extensions use `kind = "core_extension"`, `load_phase = "pre_game"` and
provider ABI 1.1. They load before regular mods and can publish versioned
services for other ZIPs without rebuilding Shipwright.

Native mods that query `linkspan.oot.engine` or `linkspan.oot.movement`
compare their `LINKSPAN_OOT_LAYOUT_ID` with the host's and are rejected when it
differs. Since `develop` changed headers in `soh/include`, DLLs built for
previous hosts must be rebuilt against this build's SDK. The host's id
is in `linkspan-overlay.json`, in the `linkSpan.ootLayoutId` field.

## Build the host

The host builds the Link-Span core from `LINKSPAN_SDK_SOURCE_DIR`, which
defaults to the `extern/ship-lua` submodule. Since `OOT-UPSTREAM-001`, the submodule
has pointed to the core used by the OoT hosts: commit `4244f7d` on the
`agent/NATIVE-001-provider-runtime` branch of `BaiterYamato/link-span` (`ship-lua.git`
is that repository's former name). A clone with submodules builds without cache
adjustments. Only point `LINKSPAN_SDK_SOURCE_DIR` to another folder, such as the
`NATIVE-001` worktree, when developing the core itself: another checkout may have different
package discovery, a different provider ABI and a different layout id.

The root `CMakeLists.txt` prefixes Link-Span's miniz symbols with `shiplua_`:
it generates `build/x64/ship-lua/shiplua_miniz_prefix.h` and injects it with `/FI`. Torch in
`develop` embeds another miniz, 9.1.15, with the same C names and earlier on the
link line. Without the prefix, `soh.exe` linked Link-Span to that copy; the host opened
normally, but loaded 0 ZIP mods, without any rejection lines.

The same file declares tinyxml2 before Torch. Torch downloads tinyxml2
10.0.0 with `OVERRIDE_FIND_PACKAGE`, and libultraship links that copy. vcpkg
also installs tinyxml2 (11.0.0), and its include path comes before the downloaded one in
Torch, libultraship and soh: all compiled with header 11 and linked against
library 10. Between the two versions, `DynArray` changed from `int` to `size_t`, and the
layout of `XMLPrinter` and `XMLDocument` changed. With
`gDeveloperTools.ResourceLogging` enabled, `XMLPrinter::CStr()` returned the
text itself as a pointer, and the host crashed on the first scene load. The
declaration reuses the vcpkg package, and configuration fails if vcpkg has
the header without the CMake package.

```powershell
git submodule update --init --recursive
cmake -S . -B build/x64   # plus the options in docs/BUILDING.md
cmake --build build/x64 --config Release --target soh -- /m:1
```

`LINKSPAN_OOT_LAYOUT_ID` is written to `build/x64/native-sdk/oot_layout_id.h` and changes
with the core source. The calculation uses the header bytes: with
`core.autocrlf=true`, the same core commit has CRLF in one checkout and LF
in another, changing the id. Build native mods against the
`build/x64/native-sdk/Release/OotNativeSdk.cmake` from the same build used in the
overlay.

## Generate the overlay

The script runs in Windows PowerShell 5.1 and PowerShell 7:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/package-linkspan-overlay.ps1 `
  -HostExecutable x64/Release/soh.exe `
  -HostResources build/x64/soh/soh.o2r `
  -ExtractorAssets soh/assets/yml `
  -ControllerDatabase build/x64/gamecontrollerdb.txt `
  -LayoutIdHeader build/x64/native-sdk/oot_layout_id.h `
  -HostBase "HarbourMasters/Shipwright develop 78dc6d970 + Link-Span core 4244f7d" `
  -ExampleMod build/distribution/Dynamic-Movement-Remake-0.2.8-layout-16ff0d5a.zip `
  -OutputPath build/distribution/LinkSpan-Shipwright-9.2.3-Upstream-78dc6d970-Win64-overlay.zip
```

The script creates the ZIP through a unique temporary directory, checks that the SDK's layout id
is embedded in the executable, generates `linkspan-overlay.json` and
`checksums.sha256`, writes ZIP entries with `/`, and neither reads nor packages
`oot.o2r`. `-ExampleMod` accepts multiple packages.

## Validate the overlay

`tools/package-linkspan-mod.ps1` packages the `mod` folder generated by a native
mod's CMake: `manifest.toml` at the root and entries with `/`.

`tools/test-linkspan-overlay.ps1` performs a clean validation:

1. copies an official installation that already has `oot.o2r`;
2. extracts the overlay on top of it;
3. checks `checksums.sha256` and verifies that user files are preserved
   byte for byte;
4. with `-ResourceLogging`, enables `gDeveloperTools.ResourceLogging` in the copy;
5. runs the game in a session of the `soh-runtime-playtest` skill;
6. fails if the log does not show the expected number of loaded mods, if
   it records a `CrashHandler` exception or if, with `-ResourceLogging`, there are no
   XML scene commands.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/test-linkspan-overlay.ps1 `
  -Overlay build/distribution/LinkSpan-Shipwright-9.2.3-Upstream-78dc6d970-Win64-overlay.zip `
  -Base <clean 9.2.3 install with oot.o2r> `
  -Smoke <new folder for the smoke test> `
  -Evidence <pasta de evidências> `
  -PlaytestScripts <scripts da skill soh-runtime-playtest> `
  -ExpectedMods 1 -ResourceLogging -NoCapture
```

The mod count catches linking regressions: with the wrong miniz, the host
opens and passes ROM-free tests, but loads 0 mods. `-ResourceLogging` catches
the tinyxml2 version mismatch: the host loads the mods and crashes on the first scene
load.
