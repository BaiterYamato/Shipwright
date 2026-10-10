**English** · [Português (Brasil)](README.pt-BR.md)

# Link-Span OoT native SDK

Build a native mod independently of the host checkout. This kit supplies public headers, a relocatable CMake configuration, example sources and packaging tools for **Shipwright OoT on Windows x64 / Release**. Extract the SDK outside the playable installation; do not install the SDK ZIP in `mods/`.

## Read first

Start with the [author guide](AUTHOR-GUIDE.md), then the [native contract](OOT-NATIVE-CONTRACT.md) and [service reference](SERVICES.md). [linkspan-sdk.json](linkspan-sdk.json) identifies the matching release/build. No host executable, import libraries, ROM, local game asset archives or saves are included.

## ZIP contents

| Path | Contents |
| --- | --- |
| [linkspan-sdk.json](linkspan-sdk.json) | Release, full layout ID, host SHA-256, compiler, configuration and limitations. |
| [checksums.sha256](checksums.sha256) | SHA-256 of all other staged files, including metadata. |
| `include/host/include/`, `include/host/` | Host/game headers reached by the include dependency walk; not the entire source tree. |
| `include/oot-native/` | All `oot_*.h` seeds and their resolved dependencies. |
| `include/shiplua/native/` | The C provider ABI and dependencies reached from it. |
| [include/linkspan/nei/nei_items.h](include/linkspan/nei/nei_items.h) | NEI item service header and its dependencies. |
| `include/lus/` | libultraship headers reached as dependencies. |
| [include/oot_layout_id.h](include/oot_layout_id.h) | Generated full layout ID of the matching host build. |
| [lib/cmake/LinkSpanOotSdk/LinkSpanOotSdkConfig.cmake](lib/cmake/LinkSpanOotSdk/LinkSpanOotSdkConfig.cmake) | Relocatable Release package exporting `LinkSpan::OotSdk`. |
| [OotNativeSdk.cmake](OotNativeSdk.cmake) | Relocatable compatibility include for the package config. |
| [examples/provider-example/README.md](examples/provider-example/README.md) | Minimal probe: CMakeLists.txt, provider.cpp, manifest.toml, main.lua and README.md. |
| [examples/example/CMakeLists.txt](examples/example/CMakeLists.txt) | DMR top-level .cpp/.h/.lua/.toml/.md files plus a generated CMake file; HUD assets omitted. |
| [examples/heart-seeds/CMakeLists.txt](examples/heart-seeds/CMakeLists.txt) | nei_demo.cpp, main.lua and generated manifest.toml/CMakeLists.txt; requires NEI. |
| `docs/` | Copied writing-mods(.en), native-providers, shipmod-cli(.en), manifest-validator(.en), host-integration(.en), API/architecture Markdown, and oot-native-sdk-source.md. |
| `docs/rfcs/` | 0021-native-hooks.md, 0023-native-escape-hatch.md and 0024-native-crash-attribution.md. |
| `tools/` | shipmod.py, LinkSpanPackaging.psm1, package-linkspan-mod.ps1, linkspan-zip.ps1 and scan-protected-content.py. |
| [LICENSE](LICENSE), [NOTICE.md](NOTICE.md), [licenses/libultraship-LICENSE.txt](licenses/libultraship-LICENSE.txt) | CC0 dedication, component notices and libultraship MIT license. |

The SDK root also contains README.md/README.pt-BR.md, AUTHOR-GUIDE.md/AUTHOR-GUIDE.pt-BR.md, OOT-NATIVE-CONTRACT.md/OOT-NATIVE-CONTRACT.pt-BR.md and SERVICES.md/SERVICES.pt-BR.md. Header directories contain only the dependency closure selected by the builder.

## Requirements

Use Windows x64, CMake 3.26+, Python 3.11+ for the bundled CLI validation fallback, and the **exact MSVC compiler version in `compiler.version`** in [linkspan-sdk.json](linkspan-sdk.json). Use an x64 Developer PowerShell. The package config rejects another compiler version, a non-MSVC compiler, or a non-Windows/x64 target. Examples use C++20 and Release `/MT` (`MSVC_RUNTIME_LIBRARY MultiThreaded`). Preserve the SDK compile definitions and options.

## Quick build

Assume the extracted directory is named `LinkSpan-OoT-SDK`:

```powershell
$sdk = (Resolve-Path .\LinkSpan-OoT-SDK).Path
cmake -S "$sdk/examples/provider-example" -B .\probe-build -A x64 "-DCMAKE_PREFIX_PATH=$sdk"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build .\probe-build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
```

The probe output is `probe-build/mod/`. Continue with validation, packaging and runtime checks in the [author guide](AUTHOR-GUIDE.md). `LinkSpan::OotSdk` supplies headers/options; it does not link to the host.

## Examples and further reading

The probe checks engine layout and structure sizes without retaining gameplay pointers. DMR is a compilation example without its HUD textures; it is not a complete playable package. Heart Seeds is included by the current builder and its manifest requires `linkspan.nei` version `>=0.3.0 <0.4.0`. The builder fails if its source files are missing.

More examples live in [link-span-examples](https://github.com/BaiterYamato/link-span-examples) (`lua/`, `oot-native/`, `oot-native/unbound/`, `tools/`). See the [core modder guide](https://github.com/BaiterYamato/link-span/blob/main/docs/native-mods.md) and [Shipwright host guide](https://github.com/BaiterYamato/Shipwright/blob/agent/OOT-UPSTREAM-001-78dc6d970-integration/docs/native-mods.md).

## License

Link-Span’s own work is dedicated to the public domain under CC0 1.0: [LICENSE](LICENSE). This does not grant rights over every component. [NOTICE.md](NOTICE.md) identifies the host/game headers and the libultraship headers; the latter retain their [MIT license](licenses/libultraship-LICENSE.txt).
