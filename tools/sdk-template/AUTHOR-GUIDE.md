**English** · [Português (Brasil)](AUTHOR-GUIDE.pt-BR.md)

# Build and package a native OoT mod

## 1. Select the matching SDK

Use Windows x64, CMake 3.26+, Python 3.11+ for the bundled CLI validation fallback, and the **exact MSVC compiler version in `compiler.version`** in [linkspan-sdk.json](linkspan-sdk.json). Use an x64 Developer PowerShell. The package config rejects another compiler version, a non-MSVC compiler, or a non-Windows/x64 target. Examples use C++20 and Release `/MT` (`MSVC_RUNTIME_LIBRARY MultiThreaded`). Preserve the SDK compile definitions and options.

Read `ootLayoutId`, `nativeProviderAbi`, `hostSha256`, `configuration` and `compiler` in [linkspan-sdk.json](linkspan-sdk.json). Keep [checksums.sha256](checksums.sha256) with the SDK. Build outside the host checkout and game installation. The SDK ZIP is a development kit; only your resulting mod package goes in `mods/`.

## 2. Compile the compatibility probe

```powershell
$sdk = (Resolve-Path .\LinkSpan-OoT-SDK).Path
cmake -S "$sdk/examples/provider-example" -B .\probe-build -A x64 "-DCMAKE_PREFIX_PATH=$sdk"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build .\probe-build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
```

Use [examples/provider-example/provider.cpp](examples/provider-example/provider.cpp) as the smallest starting point. Its [CMakeLists.txt](examples/provider-example/CMakeLists.txt) is:

```cmake
cmake_minimum_required(VERSION 3.26)
project(LinkSpanProviderExample LANGUAGES CXX)
find_package(LinkSpanOotSdk CONFIG REQUIRED)
add_library(linkspan_sdk_probe SHARED provider.cpp)
target_link_libraries(linkspan_sdk_probe PRIVATE LinkSpan::OotSdk)
set_target_properties(linkspan_sdk_probe PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" upper)
    set_target_properties(linkspan_sdk_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/mod/provider")
endforeach()
configure_file(manifest.toml "${CMAKE_BINARY_DIR}/mod/manifest.toml" COPYONLY)
configure_file(main.lua "${CMAKE_BINARY_DIR}/mod/main.lua" COPYONLY)
```

`find_package` locates [LinkSpanOotSdkConfig.cmake](lib/cmake/LinkSpanOotSdk/LinkSpanOotSdkConfig.cmake). `LinkSpan::OotSdk` is an INTERFACE target: includes, compile definitions, C++20 and compiler options, with no host import library. It exports `/utf-8`, `/Zc:preprocessor`, `/fp:precise` and `/fp:except-`. Standard C++/Windows libraries remain the toolchain’s responsibility. Do not link `soh`, the core, Lua or libultraship. For older projects, [OotNativeSdk.cmake](OotNativeSdk.cmake) includes the same relocatable configuration.

## 3. Give your mod its own identity

Copy [examples/provider-example/](examples/provider-example/README.md) into your own project. Rename the CMake target, manifest ID, display name, DLL path and output package together. Keep the manifest and Lua entrypoint at the mod root; put the DLL at the relative path declared by `[provider].win64`. The probe manifest is:

```toml
id = "example.linkspan_sdk_probe"
name = "Link-Span SDK Probe"
version = "0.1.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.2"
win64 = "provider/linkspan_sdk_probe.dll"
```

The manifest’s Lua API range and provider ABI are separate version contracts. The probe manifest requests ABI 1.2; its descriptor uses the current header macros (1.3). Preserve this checked example or select the manifest ABI required by the functions you use. The [main.lua](examples/provider-example/main.lua) entrypoint requires `ship` without changing gameplay.

## 4. Validate the runtime and engine before using pointers

The probe exports `ShipNative_Query` with C linkage and `SHIP_NATIVE_CALL`, then queries the engine by name, version and minimum size. It checks the full layout ID and each host structure size:

```cpp
#include <cstring>
#include "shiplua/native/ship_native_abi.h"
#include "oot_engine.h"
#include "oot_layout_id.h"
#include "z64.h"

static_assert(sizeof(void*) == 8, "Windows x64 required");

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!instance) return SHIP_NATIVE_FAILURE;
    *instance = nullptr;
    if (!runtime || runtime->size < sizeof(ShipNativeRuntime) || !runtime->get_service)
        return SHIP_NATIVE_UNSUPPORTED;
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION,
        sizeof(ShipOotEngineV1)));
    if (!engine || engine->size < sizeof(ShipOotEngineV1) || !engine->layout_id ||
        std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) != 0 ||
        engine->play_state_size != sizeof(PlayState) || engine->player_size != sizeof(Player) ||
        engine->save_context_size != sizeof(SaveContext)) return SHIP_NATIVE_UNSUPPORTED;
    // Only query the contract. No gameplay pointer is retained.
    return SHIP_NATIVE_OK;
}

static void SHIP_NATIVE_CALL Shutdown(void*) {}

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor = {
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, SHIP_NATIVE_ABI_MINOR, Init, Shutdown
    };
    return &descriptor;
}
```

Read [the ABI header](include/shiplua/native/ship_native_abi.h), [engine header](include/oot-native/oot_engine.h), [generated layout header](include/oot_layout_id.h), [native contract](OOT-NATIVE-CONTRACT.md) and [services](SERVICES.md) before extending it. A missing service is a null result, not a usable table. Set input/output `size` fields as required by the particular struct. Query gameplay pointers afresh and use them only in the documented thread and lifetime.

## 5. Inspect and package

Use the actual matching executable at `game/soh.exe` for this example:

```powershell
python "$sdk/tools/shipmod.py" validate .\probe-build\mod
if ($LASTEXITCODE -ne 0) { throw 'Validation failed' }
python "$sdk/tools/shipmod.py" doctor .\probe-build\mod --host .\game\soh.exe
if ($LASTEXITCODE -ne 0) { throw 'Doctor failed' }
& "$sdk/tools/package-linkspan-mod.ps1" -Source .\probe-build\mod -OutputPath .\SDK-Probe.shipmod
```

The ZIP must have `manifest.toml` at its root, not inside an extra parent directory. The bundled packager accepts `.zip` or `.shipmod`, rejects an existing output path and creates entries with `/`. Do not use `Compress-Archive` or `ZipFile.CreateFromDirectory` from Windows PowerShell 5.1 for this flow.

`validate` delegates to `shiplua_manifest_validator` when available; otherwise it uses basic structural checks. The C++ validator binary is not in the SDK. `doctor <mod> --host <exe>` inspects PE exports/imports, ABI and fingerprints without loading the DLL. Plain `doctor` targets a complete source repository. `shipmod.py` has no `pack` command: use the PowerShell script above. `test` requires an external runner through `SHIPLUA_MOD_TEST_RUNNER`; that binary is not included.

## 6. Install and verify in the host

Close the host before replacing packages. Back up the previous mod and saves/configuration affected by a schema change. Install only your mod ZIP/`.shipmod` in `mods/` beside the matching `soh.exe`. Confirm load and init in the log, exercise your feature, then verify scene changes, save/load and shutdown when relevant. Compilation and structural validation alone do not establish runtime behavior. For rollback, restore the previous package and any required save/configuration backup.

Keep borrowed gameplay and callback pointers out of persistent mod state. Release your registered resources and undo your own mutations on shutdown, including after partial init. Native DLLs execute in the host process; use providers from trusted sources.

## Frameworks and examples

Core extensions use `kind = "core_extension"` and `load_phase = "pre_game"`. Service publication requires ABI 1.1; native hooks require ABI 1.2. ABI 1.3 escape-hatch symbol/patch operations additionally require a core extension and exact executable `host_fingerprints`; see [the native contract](OOT-NATIVE-CONTRACT.md).

[DMR](examples/example/CMakeLists.txt) uses the SDK host hash in its generated manifest but omits HUD textures. [Heart Seeds](examples/heart-seeds/CMakeLists.txt) queries `linkspan.nei.items` and depends on installed `linkspan.nei` `>=0.3.0 <0.4.0`. Its generated manifest and CMake are part of this SDK, and the builder requires its two source files. Neither a header nor a build installs the NEI framework for you.

More demos: [link-span-examples](https://github.com/BaiterYamato/link-span-examples). Further reading: [core guide](https://github.com/BaiterYamato/link-span/blob/main/docs/native-mods.md), [host guide](https://github.com/BaiterYamato/Shipwright/blob/agent/OOT-UPSTREAM-001-78dc6d970-integration/docs/native-mods.md), [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
