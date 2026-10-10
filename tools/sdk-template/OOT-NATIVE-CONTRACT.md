**English** · [Português (Brasil)](OOT-NATIVE-CONTRACT.pt-BR.md)

# OoT native provider contract

## Boundary and versioning

The provider boundary is the versioned C ABI in [ship_native_abi.h](include/shiplua/native/ship_native_abi.h). This header declares `SHIP_NATIVE_ABI_MAJOR = 1u` and `SHIP_NATIVE_ABI_MINOR = 3u`. It does not expose a stable C++ ABI or require linking host/core/Lua import libraries. The OoT package still requires the exact MSVC/x64 layout and compile settings because mods can convert opaque pointers to host types. Provider ABI, service versions, Lua API range and layout ID are separate checks.

Export `ShipNative_Query` with C linkage, `SHIP_NATIVE_EXPORT` and `SHIP_NATIVE_CALL` (`__cdecl` on Windows). It returns a `const ShipNativeDescriptor*` with `size`, `abi_major`, `abi_minor`, `init` and `shutdown`. Keep the descriptor valid, as in the probe’s static descriptor. Query, DLL constructors and `DllMain` are already native execution; this ABI is not a sandbox.

## Initialization and service lookup

`init(const ShipNativeRuntime*, void**)` runs per instance, even if the OS reuses a mapped library. Validate runtime size and ABI before accessing fields. Initialize your instance output so partial cleanup is safe. `shutdown(void*)` can follow partial init and must accept a null instance.

`get_service(context, name, version, minimum_size)` returns null for an absent/incompatible service. The table belongs to its host/provider and may be retained until this provider shuts down; do not call it afterward. Request the precise version and `sizeof` of that version’s table, check its `size` and the members you will call. Set `size` on versioned argument/result structs. Header presence does not prove a service is available in the running host or that its supplying coremod is installed.

`register_function` is init-only and gives a mod-local name to a native request/response function. `register_service` is init-only publication by a core extension (ABI 1.1); its table must live until shutdown. Providers do not expose pointers through Lua. Request/writer pointers are call-scoped; `ShipNativeWriteFn` copies bytes without taking ownership.

## Layout ID and structure sizes

[oot_layout_id.h](include/oot_layout_id.h) defines `LINKSPAN_OOT_LAYOUT_ID`; [linkspan-sdk.json](linkspan-sdk.json) records the full value as `ootLayoutId`. Do not use only the eight-character filename suffix as a compatibility check.

Before converting engine pointers, compare `ShipOotEngineV1.layout_id` with the full macro and compare `play_state_size`, `player_size` and `save_context_size` with `sizeof(PlayState)`, `sizeof(Player)` and `sizeof(SaveContext)` from the bundled [host headers](include/host/include/z64.h). Movement tables also carry `layout_id`. See the [probe implementation](examples/provider-example/provider.cpp).

The current `oot-native-v2` recipe hashes the ordered public header set under host `soh/include`, host `soh/soh/native` and core `include/shiplua/native`, plus pointer size, compiler ID/version, generator platform and C++/Release flags. Header CRLF is normalized to LF before hashing. The builder verifies the existing generated ID against those inputs rather than generating a replacement to hide drift. Read the actual bundle values; this document fixes no particular release hash. Rebuild and repackage against a newly matching SDK when these compatibility inputs change.

## Thread, pointer and buffer rules

OoT gameplay services and native hooks require the game thread unless a header explicitly states another context. Engine pointers do not survive frames/scenes. Hook payloads and enumeration pointers are callback-scoped. A service table’s longer lifetime does not extend the lifetime of its results. Never infer ownership from `void*`; consult each header. Caller output buffers remain caller-owned. Use null/zero-capacity size queries only where the specific function documents them.

**Resource exception:** `ShipOotJsonTranscodeFn` in [oot_resources.h](include/oot-native/oot_resources.h) runs on the resource-loading thread, potentially from a thread pool. Protect provider state; do not call game-thread-only services there. Its header explicitly allows reading layers with `read_file_layers` for transcoding. The writer and callback arguments remain call-scoped.

**Draw scope:** render matrix/list operations and skeleton drawing require an allowed draw scope. Native display lists and referenced geometry must remain valid through the end of the frame. Lights’ `LightInfo` identity pointers are for comparison, not direct writes. Text-table edits must be outside textbox display. Follow the individual [service rules](SERVICES.md).

## Hooks and lifecycle

ABI 1.2 adds `register_hook`, `unregister_hook`, `declare_hook_point` and `dispatch_hook`. Hook point names, versions, allowed modes and payloads come from [oot_hooks.h](include/oot-native/oot_hooks.h). It defines hook points, not an `oot.hooks` service table.

Exactly one of `SHIP_NATIVE_HOOK_OBSERVE`, `SHIP_NATIVE_HOOK_TRANSFORM` or `SHIP_NATIVE_HOOK_REPLACE` is selected. OBSERVE must not write the payload. Execution order is BEFORE observers, transform chain, replace/original, AFTER observers. Larger priority runs first, ties follow registration order. Only one REPLACE owns a point. `call_original` is non-null only for REPLACE and may be called at most once. `original_context` is opaque. Three consecutive failure returns disable a hook; `UNSUPPORTED` is a refusal rather than a failure. A failed transform restores the payload, but nested pointed-to storage may need explicit restoration: the room-actors header requires failed transforms to leave `entries` intact.

Core extensions declare points only in init; only their owner dispatches them. The loader removes a mod’s hooks before shutdown. Framework consumers unload before their service provider according to dependencies. Declare your framework dependency and undo your own gameplay/CVar mutations. Unregister the actor/item/scene types and release service handles you own as specified by the headers.

## ABI 1.3 and executable fingerprints

`get_host_fingerprint` is available to any provider: it copies the executable SHA-256 as 64 hexadecimal bytes without a NUL terminator. Null output and zero capacity query its size. `resolve_symbol`, `install_patch` and `remove_patch` require a core extension with manifest ABI `"1.3"` whose `[provider].host_fingerprints` contains the running executable hash; otherwise they return `UNSUPPORTED`. A layout match alone is insufficient.

Symbol names are a unique function name or `file.c!Function` for repeated static names. Missing/ambiguous names are `UNSUPPORTED`. Patches target the executable, return an original trampoline and reject a second patch at the same address with `LIMIT`. Host symbols must correspond to that exact executable. Init patches activate together after init returns: do not depend on them during init or install patches in shutdown. The loader removes patches before shutdown; a removal failure can quarantine the DLL to avoid unloading code still in use. No executable or symbols file is included in the SDK.

## Status codes and build settings

The ABI defines `SHIP_NATIVE_OK = 0u`, `SHIP_NATIVE_INVALID_ARGUMENT = 1u`, `SHIP_NATIVE_UNSUPPORTED = 2u`, `SHIP_NATIVE_FAILURE = 3u` and `SHIP_NATIVE_LIMIT = 4u`. Their precise conditions depend on each operation.

Use the Release package config and matching MSVC Windows x64 toolchain. Keep C++20, `/MT`, `/utf-8`, `/Zc:preprocessor`, `/fp:precise`, `/fp:except-` and the exported definitions. See [AUTHOR-GUIDE.md](AUTHOR-GUIDE.md) for a checked build/manifest example and [SERVICES.md](SERVICES.md) for every declared service/version. These documents describe the source contract; they do not certify a compiled DLL or a gameplay session.
