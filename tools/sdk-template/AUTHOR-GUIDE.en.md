# Author guide — OoT

Use Python 3.11+, CMake 3.26+, Windows x64 and **the MSVC version recorded in
linkspan-sdk.json**. Select the matching toolset if needed. This SDK checks the
exact compiler version because the layout fingerprint depends on it. Examples
use C++20 and Release `/MT`; retain the SDK's compile definitions.

## Build outside the host checkout

Extract the SDK to your own directory. In an x64 Developer PowerShell:

```powershell
$sdk = (Resolve-Path .\LinkSpan-OoT-SDK).Path
cmake -S "$sdk/examples/provider-example" -B .\probe-build -A x64 "-DCMAKE_PREFIX_PATH=$sdk"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build .\probe-build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
python "$sdk/tools/shipmod.py" validate .\probe-build\mod
python "$sdk/tools/shipmod.py" doctor .\probe-build\mod --host .\game\soh.exe
& "$sdk/tools/package-linkspan-mod.ps1" -Source .\probe-build\mod -OutputPath .\SDK-Probe.shipmod
```

`validate` falls back to basic structural checks without the canonical C++
validator. `doctor <mod> --host <exe>` inspects PE exports/imports, ABI and host
fingerprints without loading the provider. Plain `doctor` targets a full source
repository and may report missing schemas/codegen in this subset. **This shipmod
version has no `pack` subcommand**; use the included PowerShell packager.
`shipmod test` requires an external C++ runner through `SHIPLUA_MOD_TEST_RUNNER`;
its binary is not included.

## Provider contract

See `OOT-NATIVE-CONTRACT.md`, `docs/writing-mods.en.md`, `docs/shipmod-cli.en.md`
and `docs/manifest-validator.en.md`. Native documentation is in Portuguese;
this guide gives the release-specific build and lifecycle summary.

A mod is a ZIP with root `manifest.toml` and the DLL path declared by
`[provider].win64`. Export `ShipNative_Query` with C linkage and
`SHIP_NATIVE_CALL`. Providers receive versioned C tables at init. Do not link
to the host, shiplua, Lua or libultraship. `LinkSpan::OotSdk` is an INTERFACE
target for headers/definitions only. Standard C++/Windows toolchain libraries
remain the compiler's responsibility.

Check service name, version, size, full layout ID and native structure sizes.
Gameplay pointers are callback/frame/scene scoped; do not retain stale pointers.
Core extensions load pre-game; shutdown occurs in reverse dependency order.
Escape hatch requires ABI 1.3, exact executable fingerprints and matching host
symbols. A matching layout alone is insufficient. Native DLLs run in process
and are not sandboxed: use trusted providers.

`examples/example` is DMR source without its assets; compilation is not proof
of a complete playable package. `shovel-demo` illustrates the NEI item service.
`item-demo` is source reference without a ready manifest. Heart-seeds is optional
when its source directory exists; inspect metadata. Prepare your own manifest
and resources before distributing any unfinished example.

## Install and verify

Close the host, back up packages and saves, then install only the mod ZIP in
`mods/`. Confirm successful load/init/shutdown on the matching host and keep its
SHA unchanged. For rollback restore the previous package and save/config backups
when schemas changed. The boot guard records disabled mods in
`mods/.shiplua-disabled` without deleting packages.

Verify external configure/link, PE exports/imports, validate/doctor/pack and then
actual runtime. The minimal probe is a draft compatibility check, not the full
native conformance package required by release §17. Publication licenses are
still unresolved; this bundle is for local review.
