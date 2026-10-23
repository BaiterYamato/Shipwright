#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shiplua/native/ship_native_abi.h>
#include "oot_engine.h"
#include "oot_hooks.h"
#include "probe_layout.h"

typedef struct DamageProbe {
    const ShipOotEngineV1* engine;
    FILE* log;
    unsigned long long tick;
    unsigned long long hits;
} DamageProbe;

static ShipNativeStatus SHIP_NATIVE_CALL Tick(void* user, const ShipNativeHookCall* call) {
    DamageProbe* probe = (DamageProbe*)user;
    (void)call;
    ++probe->tick;
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Transform(void* user, const ShipNativeHookCall* call) {
    DamageProbe* probe = (DamageProbe*)user;
    ShipOotActorDamageHookV1* payload = (ShipOotActorDamageHookV1*)call->payload;
    void* player = probe->engine->get_player();
    if (player != NULL && payload->target == player)
        payload->damage = 0.0f;
    else if (payload->attacker_is_player)
        payload->damage *= 2.0f;
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Observe(void* user, const ShipNativeHookCall* call) {
    DamageProbe* probe = (DamageProbe*)user;
    const ShipOotActorDamageHookV1* payload = (const ShipOotActorDamageHookV1*)call->payload;
    void* player = probe->engine->get_player();
    int count = fprintf(probe->log,
                        "hit=%llu tick=%llu target=%p attacker=%p target_player=%u attacker_is_player=%u "
                        "flags=0x%08x effect=%u base=%.9g damage=%.9g\n",
                        ++probe->hits, probe->tick, payload->target, payload->attacker,
                        (unsigned)(player != NULL && payload->target == player), (unsigned)payload->attacker_is_player,
                        (unsigned)payload->dmg_flags, (unsigned)payload->damage_effect, (double)payload->base_damage,
                        (double)payload->damage);
    return count >= 0 && fflush(probe->log) == 0 ? SHIP_NATIVE_OK : SHIP_NATIVE_FAILURE;
}

static ShipNativeStatus Register(const ShipNativeRuntime* runtime, const char* point, uint32_t size, uint32_t mode,
                                 uint32_t phase, ShipNativeHookFn callback, DamageProbe* probe) {
    ShipNativeHookSpec spec = {
        sizeof(spec), point, LINKSPAN_OOT_HOOKS_VERSION, size, mode, phase, 0, callback, probe
    };
    uint64_t handle = 0;
    return runtime->register_hook(runtime->context, &spec, &handle);
}

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    DamageProbe* probe;
    ShipNativeStatus status;
    *output = NULL;
    if (runtime == NULL || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_major != 1u ||
        runtime->abi_minor < 4u || runtime->get_service == NULL || runtime->register_hook == NULL)
        return SHIP_NATIVE_UNSUPPORTED;
    probe = (DamageProbe*)calloc(1, sizeof(*probe));
    if (probe == NULL)
        return SHIP_NATIVE_FAILURE;
    *output = probe;
    probe->engine = (const ShipOotEngineV1*)runtime->get_service(runtime->context, LINKSPAN_OOT_ENGINE_SERVICE,
                                                                 LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1));
    if (probe->engine == NULL || probe->engine->size < sizeof(ShipOotEngineV1) || probe->engine->layout_id == NULL ||
        strcmp(probe->engine->layout_id, DAMAGE_PROBE_LAYOUT_ID) != 0 || probe->engine->get_player == NULL)
        return SHIP_NATIVE_UNSUPPORTED;
    probe->log = fopen("damage-probe.log", "w");
    if (probe->log == NULL)
        return SHIP_NATIVE_FAILURE;
    fprintf(probe->log, "damage-probe abi=1.4 layout=%s\n", probe->engine->layout_id);
    fflush(probe->log);
    status = Register(runtime, LINKSPAN_OOT_HOOK_PLAY_UPDATE, sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE,
                      SHIP_NATIVE_HOOK_BEFORE, Tick, probe);
    if (status != SHIP_NATIVE_OK)
        return status;
    status = Register(runtime, LINKSPAN_OOT_HOOK_ACTOR_DAMAGE, sizeof(ShipOotActorDamageHookV1),
                      SHIP_NATIVE_HOOK_TRANSFORM, 0, Transform, probe);
    if (status != SHIP_NATIVE_OK)
        return status;
    return Register(runtime, LINKSPAN_OOT_HOOK_ACTOR_DAMAGE, sizeof(ShipOotActorDamageHookV1), SHIP_NATIVE_HOOK_OBSERVE,
                    SHIP_NATIVE_HOOK_AFTER, Observe, probe);
}

static void SHIP_NATIVE_CALL Shutdown(void* instance) {
    DamageProbe* probe = (DamageProbe*)instance;
    if (probe == NULL)
        return;
    if (probe->log != NULL) {
        fprintf(probe->log, "end ticks=%llu hits=%llu\n", probe->tick, probe->hits);
        fclose(probe->log);
    }
    free(probe);
}

SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor = { sizeof(ShipNativeDescriptor), 1u, 4u, Init, Shutdown };
    return &descriptor;
}
