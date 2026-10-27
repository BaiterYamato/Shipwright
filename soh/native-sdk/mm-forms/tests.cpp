#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shiplua/native/ship_native_abi.h>
#include "oot_player_body.h"
#include "oot_engine.h"
#include "oot_colliders.h"
#include "oot_camera.h"
#include "oot_render.h"
#include "mm_audio.h"
#include "oot_resources.h"
#include "z64.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace {
std::map<std::string, ShipNativeFunction> functions;
int hookCount = 0, removedHooks = 0, failHook = 0;
const char* absentService = nullptr;
const char* absentSymbol = nullptr;
void* playState = nullptr;
ShipOotPlayerBodyV1 body{};
ShipOotMovementV3 movement{};
ShipOotCollidersV2 colliders{};
ShipOotSkeletonsV2 skeletons{};
ShipOotCameraV2 camera{};
ShipOotRenderV3 render{};
ShipOotEngineV1 engine{};
ShipOotResourcesV1 resources{};
ShipMmAudioV1 audio{};
ShipNativeStatus SHIP_NATIVE_CALL Unsupported() {
    return SHIP_NATIVE_UNSUPPORTED;
}
void* SHIP_NATIVE_CALL NoPlay() {
    return nullptr;
}
ShipNativeStatus SHIP_NATIVE_CALL RegisterFunction(void*, const char* name, ShipNativeFunction fn, void*) {
    functions[name] = fn;
    return SHIP_NATIVE_OK;
}
const void* SHIP_NATIVE_CALL Service(void*, const char* name, uint32_t version, uint32_t size) {
    if (absentService && std::strcmp(name, absentService) == 0)
        return nullptr;
    const struct {
        const char* name;
        uint32_t version, size;
        const void* table;
    } services[] = {
        { LINKSPAN_OOT_PLAYER_BODY_SERVICE, 1, sizeof(body), &body },
        { LINKSPAN_OOT_MOVEMENT_SERVICE, 3, sizeof(movement), &movement },
        { LINKSPAN_OOT_COLLIDERS_SERVICE, 2, sizeof(colliders), &colliders },
        { LINKSPAN_OOT_SKELETONS_SERVICE, 2, sizeof(skeletons), &skeletons },
        { LINKSPAN_OOT_CAMERA_SERVICE, 2, sizeof(camera), &camera },
        { LINKSPAN_OOT_RENDER_SERVICE, 3, sizeof(render), &render },
        { LINKSPAN_OOT_ENGINE_SERVICE, 1, sizeof(engine), &engine },
        { LINKSPAN_OOT_RESOURCES_SERVICE, 1, sizeof(resources), &resources },
        { LINKSPAN_MM_AUDIO_SERVICE, 1, sizeof(audio), &audio },
    };
    for (const auto& service : services)
        if (std::strcmp(name, service.name) == 0 && version == service.version && size <= service.size)
            return service.table;
    return nullptr;
}
ShipNativeStatus SHIP_NATIVE_CALL Resolve(void*, const char* name, uintptr_t* address) {
    *address = 0;
    if (absentSymbol && std::strcmp(name, absentSymbol) == 0)
        return SHIP_NATIVE_UNSUPPORTED;
    *address = reinterpret_cast<uintptr_t>(&Unsupported);
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Hook(void*, const ShipNativeHookSpec* spec, uint64_t* handle) {
    if (!spec || spec->version != 1 || !spec->callback || spec->size < sizeof(*spec))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (failHook && hookCount == failHook)
        return SHIP_NATIVE_LIMIT;
    *handle = ++hookCount;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Unhook(void*, uint64_t) {
    ++removedHooks;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Write(void* target, const char* value, uint32_t length) {
    static_cast<std::string*>(target)->append(value, length);
    return SHIP_NATIVE_OK;
}
void Tables() {
    body.size = sizeof(body);
    movement.size = sizeof(movement);
    colliders.size = sizeof(colliders);
    skeletons.size = sizeof(skeletons);
    camera.size = sizeof(camera);
    render.size = sizeof(render);
    audio.size = sizeof(audio);
    engine.size = sizeof(engine);
    resources.size = sizeof(resources);
    engine.layout_id = MM_FORMS_LAYOUT_ID;
    engine.play_state_size = sizeof(PlayState);
    engine.player_size = sizeof(Player);
    engine.save_context_size = sizeof(SaveContext);
    engine.get_play_state = NoPlay;
    body.layout_id = movement.layout_id = colliders.layout_id = skeletons.layout_id = camera.layout_id =
        MM_FORMS_LAYOUT_ID;
    // Nenhuma destas funções é chamada sem gameplay; endereços verificam só a negociação das tabelas.
#define STUB(table, field) table.field = reinterpret_cast<decltype(table.field)>(&Unsupported)
    STUB(body, set_body);
    STUB(body, get_state);
    STUB(body, acquire_control);
    STUB(body, release_control);
    STUB(body, start_animation);
    STUB(body, animation_info);
    STUB(body, set_root_motion);
    STUB(body, set_segment);
    STUB(movement, get_action_state);
    STUB(movement, acquire_action);
    STUB(movement, release_action);
    STUB(movement, restore_player_animation);
    STUB(colliders, acquire_player_quads);
    STUB(colliders, release_player_quads);
    STUB(colliders, submit_player_quad);
    STUB(colliders, disable_player_quad);
    STUB(colliders, read_player_quad_hits);
    STUB(skeletons, create_ex);
    STUB(skeletons, destroy);
    STUB(skeletons, update);
    STUB(skeletons, draw_ex);
    STUB(skeletons, play_ex);
    STUB(camera, acquire_ex);
    STUB(camera, release);
    STUB(camera, is_owned);
    STUB(camera, get_view_ex);
    STUB(camera, set_view_ex);
    STUB(render, draw_display_list);
    STUB(render, matrix_push);
    STUB(render, matrix_pop);
    STUB(render, matrix_scale);
    STUB(render, interpolation_begin);
    STUB(render, interpolation_end);
    STUB(resources, has_file);
    STUB(audio, play_sample);
    STUB(audio, play_sfx_id);
    STUB(audio, set_voice_map);
    STUB(audio, clear_voice_map);
#undef STUB
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll)
        return 2;
    auto query = reinterpret_cast<ShipNativeQueryFn>(GetProcAddress(dll, "ShipNative_Query"));
    if (!query)
        return 3;
    const auto* descriptor = query();
    if (!descriptor || descriptor->size < sizeof(*descriptor) || descriptor->abi_major != 1 ||
        descriptor->abi_minor != 4)
        return 4;
    void* instance = reinterpret_cast<void*>(1);
    if (descriptor->init(nullptr, &instance) != SHIP_NATIVE_UNSUPPORTED || instance != nullptr)
        return 5;
    descriptor->shutdown(nullptr);
    ShipNativeRuntime runtime{};
    runtime.size = sizeof(runtime);
    runtime.abi_major = 1;
    runtime.abi_minor = 3;
    instance = reinterpret_cast<void*>(1);
    if (descriptor->init(&runtime, &instance) != SHIP_NATIVE_UNSUPPORTED || instance != nullptr)
        return 6;
    descriptor->shutdown(nullptr);
    runtime.abi_minor = 4;
    runtime.get_service = Service;
    runtime.resolve_symbol = Resolve;
    runtime.register_function = RegisterFunction;
    runtime.register_hook = Hook;
    runtime.unregister_hook = Unhook;
    Tables();
    for (const char* service :
         { LINKSPAN_OOT_PLAYER_BODY_SERVICE, LINKSPAN_OOT_MOVEMENT_SERVICE, LINKSPAN_OOT_COLLIDERS_SERVICE,
           LINKSPAN_OOT_SKELETONS_SERVICE, LINKSPAN_OOT_CAMERA_SERVICE, LINKSPAN_OOT_RENDER_SERVICE,
           LINKSPAN_MM_AUDIO_SERVICE, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_RESOURCES_SERVICE }) {
        absentService = service;
        if (descriptor->init(&runtime, &instance) != SHIP_NATIVE_UNSUPPORTED || instance)
            return 7;
        descriptor->shutdown(instance);
    }
    absentService = nullptr;
    camera.layout_id = "different";
    if (descriptor->init(&runtime, &instance) != SHIP_NATIVE_UNSUPPORTED || instance)
        return 8;
    descriptor->shutdown(instance);
    Tables();
    for (const char* symbol : { "Math_SinS", "ResourceMgr_LoadAnimByName" }) {
        absentSymbol = symbol;
        if (descriptor->init(&runtime, &instance) != SHIP_NATIVE_UNSUPPORTED || instance)
            return 9;
        descriptor->shutdown(instance);
    }
    absentSymbol = nullptr;
    failHook = 2;
    hookCount = removedHooks = 0;
    if (descriptor->init(&runtime, &instance) != SHIP_NATIVE_LIMIT || instance)
        return 10;
    descriptor->shutdown(instance);
    if (removedHooks != 2)
        return 11;
    failHook = 0;
    for (int iteration = 0; iteration < 2; ++iteration) {
        hookCount = removedHooks = 0;
        if (descriptor->init(&runtime, &instance) != SHIP_NATIVE_OK || !instance || hookCount != 6 ||
            functions.size() != 6)
            return 12;
        std::string result;
        if (functions.at("status")(nullptr, "{}", 2, Write, &result) != SHIP_NATIVE_OK ||
            result.find("\"active\":false") == std::string::npos)
            return 13;
        const char invalid[] = "{\"skeleton\":\"missing\",\"anims\":{}}";
        if (functions.at("configure")(nullptr, invalid, sizeof(invalid) - 1, Write, &result) !=
            SHIP_NATIVE_INVALID_ARGUMENT)
            return 14;
        const char spec[] = "{\"skeleton\":\"missing\",\"anims\":{\"idle\":\"missing\"}}";
        if (functions.at("configure")(nullptr, spec, sizeof(spec) - 1, Write, &result) != SHIP_NATIVE_OK)
            return 15;
        const char enable[] = "{\"enabled\":true}";
        if (functions.at("set_enabled")(nullptr, enable, sizeof(enable) - 1, Write, &result) != SHIP_NATIVE_UNSUPPORTED)
            return 16;
        descriptor->shutdown(instance);
        if (removedHooks != 6)
            return 17;
    }
    FreeLibrary(dll);
    std::puts("MM Forms: DLL/ABI, serviços, layouts, símbolos, rollback parcial, configure sem assets e reload OK.");
    return 0;
}
