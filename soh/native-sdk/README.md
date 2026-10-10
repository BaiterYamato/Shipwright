# OoT native SDK

This directory contains example native mods for Shipwright OoT. A native
mod is a DLL and a `manifest.toml` loaded by Link-Span. It receives
C tables at init; it does not link against `soh`, `shiplua` or Lua.

## Generate the SDK from the same host

Configure and build the host first. The build generates:

```text
<build-do-host>/native-sdk/<config>/OotNativeSdk.cmake
<build-do-host>/native-sdk/oot_layout_id.h
```

`OotNativeSdk.cmake` exports the host's includes and definitions. Pass the file
from the same configuration as the executable that will load the DLL:

```powershell
$sdk = (Resolve-Path .\build\x64\native-sdk\Release\OotNativeSdk.cmake).Path
$hostExe = (Resolve-Path .\x64\Release\soh.exe).Path   # $host is reserved in PowerShell
cmake -S soh/native-sdk/example -B build/dmr -DOOT_NATIVE_SDK=$sdk -DHOST_EXECUTABLE=$hostExe
cmake --build build/dmr --config Release
```

The example's output is `build/dmr/mod/`: `manifest.toml`, `main.lua`,
`provider/` and any assets the example copies. Use `example` (DMR) as a template for
CMake, init and service checks. The code demos (hooks, item, save, view,
world, familiar, escape, crash, Shovel, CEL rendering and the Unbound demos) live in
[link-span-examples](https://github.com/BaiterYamato/link-span-examples) (`oot-native/`), with packaging scripts in `tools/`.

## Layout id

The host calculates `LINKSPAN_OOT_LAYOUT_ID` from the public headers,
platform, compiler and flags. A provider that casts pointers to host
types must compare this id and the relevant `sizeof` values before using the
service table. `native-sdk/example/provider.cpp` shows the engine and
movement checks.

After updating the host executable, public headers, compiler, platform or
flags:

1. reconfigure/build the host to regenerate the SDK;
2. use the new `OotNativeSdk.cmake` to rebuild the mod;
3. recreate the package; do not mix DLLs, layout ids and executables from different builds.

## Package and install

The manifest and `main.lua` go at the ZIP root. The DLL goes at the path declared
in `[provider]`, usually `provider/<nome>.dll`. For a `mod` directory
already assembled by CMake:

```powershell
.\tools\package-linkspan-mod.ps1 -Source build\dmr\mod -OutputPath build\dmr\Meu-Mod-0.1.0.zip
```

Do not use `Compress-Archive` or `ZipFile.CreateFromDirectory` in Windows PowerShell 5.1: they write
entries with `\` and the loader cannot find the manifest. The script creates entries with `/`, in a fixed order and with
a fixed timestamp (`tools/linkspan-zip.ps1`), so the same content produces the same ZIP.

Copy the `.shipmod` to `mods/` next to the same `soh.exe` used for the build and launch
the game. Confirm in the log that the package was loaded; a rejection reports the cause.
After changing the package or executable, close the host, replace the entire file
and perform the same log check.

## Services and hooks

The `oot_*.h` headers are the public reference for versions, struct sizes,
threading and ownership. The main services cover registry, randomizer,
actor-models, lights, render, scenes, save namespaces and resources. Request each
service through `runtime->get_service`, with the correct name, version and `sizeof`, and treat
a null/`UNSUPPORTED` return as an unavailable feature.

Use `oot_hooks.h` for hooks. `oot.actor.init` only observes an already initialized
actor; `oot.play.draw_end` occurs in the rendering context at the end of the frame.
Pointers received in a payload are valid only during the callback.

## Escape hatch

The escape hatch requires core extension ABI 1.3 and the executable's fingerprint in
the manifest. The host can activate init patches in a batch and keeps the DLL in
quarantine if it cannot remove all patches during unload. Do not depend on
a patch during init itself and do not install a patch during shutdown.
