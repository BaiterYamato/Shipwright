#include "bindings.h"
#include "ShipLuaPuppet.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "oot_layout_id.h"

namespace ShipLuaHost {
PuppetBindings gHost;

bool StatueAvailable() {
    // Serviço da tarefa A: só presença; esta DLL não interpreta a tabela de áudio.
    return gHost.runtime &&
           gHost.runtime->get_service(gHost.runtime->context, "linkspan.mm.audio", 1u, sizeof(uint32_t)) &&
           gHost.resources->has_file("mm/objects/gameplay_keep/gElegyShellHumanDL");
}
} // namespace ShipLuaHost

namespace {
using namespace ShipLuaHost;

struct Provider {
    ShipNativeRuntime runtime{};
    int16_t child = -1;
    int16_t statue = -1;
};

// Uma instância ativa por DLL: o loader não deve sobrepor registros do mesmo mod.
Provider* sActive = nullptr;

template <typename T> bool Resolve(const ShipNativeRuntime* runtime, const char* name, T& pointer) {
    uintptr_t address = 0;
    if (runtime->resolve_symbol(runtime->context, name, &address) != SHIP_NATIVE_OK || !address) {
        std::fprintf(stderr, "Bonecos: símbolo indisponível: %s\n", name);
        return false;
    }
    pointer = reinterpret_cast<T>(address);
    return true;
}

bool Bind(const ShipNativeRuntime* runtime) {
    gHost.runtime = runtime;
    gHost.engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    gHost.actors = static_cast<const ShipOotActorsV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ACTORS_SERVICE, LINKSPAN_OOT_ACTORS_VERSION, sizeof(ShipOotActorsV1)));
    gHost.resources = static_cast<const ShipOotResourcesV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION, sizeof(ShipOotResourcesV1)));
    if (!gHost.engine || !gHost.actors || !gHost.resources || !gHost.engine->layout_id ||
        std::strcmp(gHost.engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) != 0 ||
        gHost.engine->play_state_size != sizeof(PlayState) || gHost.engine->player_size != sizeof(Player) ||
        gHost.engine->save_context_size != sizeof(SaveContext) || !gHost.engine->kill_actor ||
        !gHost.actors->register_actor_type || !gHost.actors->unregister_actor_type ||
        !gHost.actors->draw_display_list || !gHost.resources->has_file) {
        return false;
    }
#define BIND(name)                            \
    if (!Resolve(runtime, #name, gHost.name)) \
    return false
    BIND(Actor_SetScale);
    BIND(DmaMgr_SendRequest1);
    BIND(Gfx_SetupDL_25Opa);
    BIND(LinkAnimation_Update);
    BIND(Player_DrawImpl);
    BIND(SkelAnime_InitLink);
    BIND(func_80093C80);
    BIND(gSPSegment);
    BIND(gLinkObjectIds);
    BIND(gObjectTable);
    BIND(gPlayerSkelHeaders);
    BIND(gSegments);
    // Fora do catálogo público: o manifesto exige o SHA do exe.
    BIND(Player_OverrideLimbDrawPause);
    BIND(FrameInterpolation_RecordOpenChild);
    BIND(FrameInterpolation_RecordCloseChild);
#undef BIND
    return true;
}

void SHIP_NATIVE_CALL InitActor(void* user, void* actor, void* play) {
    try {
        const bool statue = user != nullptr;
        if (!(statue ? ShipLuaPuppet_AttachStatue(actor) : ShipLuaPuppet_Attach(actor, play))) {
            gHost.engine->kill_actor(actor);
        }
    } catch (...) { gHost.engine->kill_actor(actor); }
}

void SHIP_NATIVE_CALL DestroyActor(void*, void* actor, void*) {
    ShipLuaPuppet_HandleActorDestroy(actor);
}

void SHIP_NATIVE_CALL UpdateActor(void*, void* actor, void* play) {
    ShipLuaPuppetUpdate(static_cast<Actor*>(actor), static_cast<PlayState*>(play));
}

void SHIP_NATIVE_CALL DrawActor(void* user, void* actor, void* play) {
    if (user) {
        ShipLuaStatueDraw(static_cast<Actor*>(actor), static_cast<PlayState*>(play));
    } else {
        ShipLuaPuppetDraw(static_cast<Actor*>(actor), static_cast<PlayState*>(play));
    }
}

ShipNativeStatus RegisterType(const char* name, void* user, int16_t* id) {
    ShipOotActorTypeSpecV1 spec{};
    spec.size = sizeof(spec);
    spec.name = name;
    spec.category = ACTORCAT_PROP;
    spec.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    spec.object_id = OBJECT_GAMEPLAY_KEEP;
    spec.instance_size = sizeof(Actor);
    spec.init = InitActor;
    spec.destroy = DestroyActor;
    spec.update = UpdateActor;
    spec.draw = DrawActor;
    spec.user = user;
    return gHost.actors->register_actor_type(&spec, id);
}

ShipNativeStatus SHIP_NATIVE_CALL ActorIds(void* user, const char*, uint32_t, ShipNativeWriteFn write, void* writer) {
    const auto* state = static_cast<Provider*>(user);
    if (!state || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    char result[160];
    const int count = std::snprintf(result, sizeof(result), "{\"oot.link_child_puppet\":%d,\"oot.mm_elegy_statue\":%d}",
                                    state->child, state->statue);
    return write(writer, result, static_cast<uint32_t>(count));
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* state = static_cast<Provider*>(instance);
    if (!state || state != sActive) {
        return;
    }
    // unregister mata as instâncias e retira callbacks; o destroy não volta ao mod.
    if (gHost.actors) {
        if (state->statue >= 0) {
            gHost.actors->unregister_actor_type(state->statue);
        }
        if (state->child >= 0) {
            gHost.actors->unregister_actor_type(state->child);
        }
    }
    ShipLuaPuppet_Reset();
    gHost = {};
    sActive = nullptr;
    delete state;
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    if (!output) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *output = nullptr;
    if (!runtime || runtime->size < sizeof(*runtime) || runtime->abi_major != SHIP_NATIVE_ABI_MAJOR ||
        runtime->abi_minor < 4 || !runtime->get_service || !runtime->resolve_symbol || !runtime->register_function) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (sActive) {
        return SHIP_NATIVE_LIMIT;
    }
    auto* state = new (std::nothrow) Provider;
    if (!state) {
        return SHIP_NATIVE_FAILURE;
    }
    state->runtime = *runtime;
    sActive = state;
    *output = state;
    if (!Bind(&state->runtime)) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    ShipNativeStatus status = RegisterType("oot.link_child_puppet", nullptr, &state->child);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    status = RegisterType("oot.mm_elegy_statue", state, &state->statue);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    return runtime->register_function(runtime->context, "actor_ids", ActorIds, state);
}
} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{ sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 4u, Init,
                                                  Shutdown };
    return &descriptor;
}
