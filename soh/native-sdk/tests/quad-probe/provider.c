#include "oot_colliders.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include "probe_layout.h"
#include "z64.h"
#include <math.h>
#include <shiplua/native/ship_native_abi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OWNER "linkspan.test.quad_probe"
#define QUEUE_SIZE 12288u

/* Headers/layout do SDK, somente leitura de Player. Nao cria collider
 * substituto, nao escreve no Player, nao resolve simbolos privados e nao retem
 * Actor*. */
typedef struct Probe {
    const ShipOotEngineV1* engine;
    const ShipOotMovementV3* movement;
    const ShipOotCollidersV2* colliders;
    uint64_t action, lease, epoch, due, lastResolved;
    unsigned armed, live, done, cycle, submitted, reads, hits, actors, suppressed, observed, violations;
    unsigned finishing, errors, destroy, overflow, samples, waitingLogged;
    char mode[16];
    char queue[QUEUE_SIZE];
    size_t used;
} Probe;

static void Record(Probe* p, const char* format, ...) {
    va_list args;
    int count;
    if (p->used >= sizeof(p->queue) - 2) {
        ++p->overflow;
        return;
    }
    va_start(args, format);
    count = vsnprintf(p->queue + p->used, sizeof(p->queue) - p->used, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= sizeof(p->queue) - p->used - 1) {
        ++p->overflow;
        p->queue[p->used] = 0;
        return;
    }
    p->used += (size_t)count;
    p->queue[p->used++] = '\n';
    p->queue[p->used] = 0;
}

static ShipNativeStatus Read(Probe* p, const char* phase) {
    ShipOotPlayerQuadHitsV2 h = { 0 };
    ShipNativeStatus status;
    h.size = sizeof(h);
    status = p->colliders->read_player_quad_hits(p->lease, SHIP_OOT_QUAD_ATTACK_0, &h);
    Record(p,
           "quad-sample cycle=%u phase=%s status=%u epoch=%llu resolved=%llu "
           "at_hit=%u actor=%u oc_hit=%u",
           p->cycle, phase, status, (unsigned long long)h.scene_epoch, (unsigned long long)h.resolved_tick,
           h.hits.at_hit, h.hits.at_actor != NULL, h.hits.oc_hit);
    if (status == SHIP_NATIVE_OK && strcmp(phase, "frame") == 0) {
        ++p->reads;
        if (!h.resolved_tick || h.resolved_tick <= p->lastResolved || h.scene_epoch != p->epoch || h.hits.oc_hit ||
            h.hits.oc_actor)
            ++p->errors;
        p->lastResolved = h.resolved_tick;
        if (h.hits.at_hit) {
            ++p->hits;
            if (h.hits.at_actor)
                ++p->actors;
        }
    }
    return status;
}

static ShipOotPlayerQuadSpecV2 Quad(const Player* player) {
    ShipOotPlayerQuadSpecV2 q = { 0 };
    /* Mesmo Quad() do teste: a-b numa aresta, c-d na oposta; c do lado de a.
     * O teste usa dano2; a tarefa26 pede dano1, adotado explicitamente aqui. */
    static const float local[4][3] = { { -20, 60, 50 }, { 20, 60, 50 }, { -20, 10, 15 }, { 20, 10, 15 } };
    const double yaw = (double)player->actor.shape.rot.y * (3.14159265358979323846 / 32768.0);
    const float sn = (float)sin(yaw), cs = (float)cos(yaw);
    unsigned i;
    q.size = sizeof(q);
    q.col_type = COLTYPE_NONE;
    q.at_flags = AT_ON | AT_TYPE_PLAYER;
    q.elem_type = ELEMTYPE_UNK2;
    q.touch_dmg_flags = 0x00000100u;
    q.touch_damage = 1;
    q.bump_dmg_flags = 0xFFCFFFFFu;
    q.touch_flags = TOUCH_ON | TOUCH_SFX_NORMAL;
    for (i = 0; i < 4; ++i) {
        q.vertices[i][0] = player->actor.world.pos.x + local[i][0] * cs + local[i][2] * sn;
        q.vertices[i][1] = player->actor.world.pos.y + local[i][1];
        q.vertices[i][2] = player->actor.world.pos.z - local[i][0] * sn + local[i][2] * cs;
        q.defense_matrix[i * 5] = 1;
    }
    return q;
}

static void Finish(Probe* p, unsigned revoked) {
    ShipNativeStatus releaseQuad, releaseAction, stale;
    if (strcmp(p->mode, "detach") == 0 && !revoked) {
        /* Prova do aviso F2d: soltar apenas a acao deve derrubar os quads. */
        releaseAction = p->movement->release_action(p->action);
        stale = Read(p, "after_action_release");
        releaseQuad = p->colliders->release_player_quads(p->lease);
    } else if (revoked) {
        stale = Read(p, "after_scene"); /* Antes de qualquer release: prova
                                           revogacao do host. */
        releaseQuad = p->colliders->release_player_quads(p->lease);
        releaseAction = p->movement->release_action(p->action);
    } else {
        releaseQuad = p->colliders->release_player_quads(p->lease);
        stale = Read(p, "after_quad_release");
        releaseAction = p->movement->release_action(p->action);
    }
    if (releaseQuad != SHIP_NATIVE_OK || releaseAction != SHIP_NATIVE_OK || stale != SHIP_NATIVE_UNSUPPORTED)
        ++p->errors;
    p->live = 0;
    p->done = 1;
    Record(p,
           "quad-finish cycle=%u run=%s submitted=%u reads=%u hits=%u actors=%u "
           "suppressed=%u observed=%u "
           "violations=%u release_quad=%u release_action=%u stale=%u destroy=%u "
           "errors=%u",
           p->cycle, p->mode, p->submitted, p->reads, p->hits, p->actors, p->suppressed, p->observed, p->violations,
           releaseQuad, releaseAction, stale, p->destroy, p->errors);
    /* Normal repete apos 100 ticks (~5s a 20fps); outros modos exigem nova
     * sessao. */
    if (strcmp(p->mode, "normal") != 0)
        p->armed = 0;
    p->action = p->lease = 0;
}

static ShipNativeStatus SHIP_NATIVE_CALL Action(void* user, const ShipOotPlayerActionTickV1* tick,
                                                uint32_t* disposition) {
    Probe* p = (Probe*)user;
    ShipOotPlayerQuadSpecV2 quad;
    ShipNativeStatus status;
    if (!tick || tick->size < sizeof(*tick) || !disposition || !tick->player)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    *disposition = SHIP_OOT_ACTION_SUPPRESS;
    ++p->suppressed;
    if (p->submitted && p->reads < p->submitted) {
        status = Read(p, "frame");
        if (status != SHIP_NATIVE_OK) {
            ++p->errors;
            p->finishing = 1;
            return SHIP_NATIVE_OK;
        }
    }
    if (p->submitted >= 10) {
        if (strcmp(p->mode, "normal") == 0 || strcmp(p->mode, "detach") == 0)
            p->finishing = 1;
        /* Em hold/transition: preserva lease, mas retira quad apos 10 submits.
         * O modo hold permite testar espada e fechamento com lease sem ataque. */
        else if (p->colliders->disable_player_quad(p->lease, SHIP_OOT_QUAD_ATTACK_0) != SHIP_NATIVE_OK)
            ++p->errors;
        return SHIP_NATIVE_OK;
    }
    quad = Quad((const Player*)tick->player);
    status = p->colliders->submit_player_quad(p->lease, SHIP_OOT_QUAD_ATTACK_0, &quad);
    Record(p,
           "quad-submit cycle=%u n=%u status=%u tick=%llu yaw=%d damage=%u "
           "mask=3 slot=0",
           p->cycle, p->submitted + 1, status, (unsigned long long)tick->state.tick,
           ((const Player*)tick->player)->actor.shape.rot.y, quad.touch_damage);
    if (status == SHIP_NATIVE_OK)
        ++p->submitted;
    else {
        ++p->errors;
        p->finishing = 1;
    }
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Update(void* user, const ShipNativeHookCall* call) {
    Probe* p = (Probe*)user;
    ShipOotPlayerActionStateV1 state = { 0 };
    ShipNativeStatus actionStatus, quadStatus;
    (void)call;
    if (!p->armed)
        return SHIP_NATIVE_OK;
    state.size = sizeof(state);
    actionStatus = p->movement->get_action_state(&state);
    if (p->live) {
        if (p->finishing) {
            p->finishing = 0;
            Finish(p, 0);
        } else if (actionStatus != SHIP_NATIVE_OK || !state.owned || state.scene_epoch != p->epoch) {
            ++p->errors; /* Revogacao inesperada: nao promover a transicao a sucesso.
                          */
            Record(p, "quad-revoked cycle=%u reason=unexpected state_status=%u owned=%u", p->cycle, actionStatus,
                   state.owned);
            Finish(p, 1);
        }
        return SHIP_NATIVE_OK;
    }
    if (actionStatus != SHIP_NATIVE_OK || !(state.flags & SHIP_OOT_ACTION_STATE_IDLE) || state.owned ||
        (state.flags & SHIP_OOT_ACTION_STATE_BLOCKED) || (p->due && state.tick < p->due))
        return SHIP_NATIVE_OK;
    {
        ShipOotPlayerActionSpecV1 spec = { sizeof(spec), 0, Action, p };
        actionStatus = p->movement->acquire_action(OWNER, &spec, &p->action);
        if (actionStatus != SHIP_NATIVE_OK)
            return SHIP_NATIVE_OK;
    }
    quadStatus = p->colliders->acquire_player_quads(OWNER, p->action, SHIP_OOT_QUAD_MASK_ATTACK, &p->lease);
    if (quadStatus != SHIP_NATIVE_OK) {
        (void)p->movement->release_action(p->action);
        p->action = 0;
        ++p->errors;
        Record(p, "quad-acquire status=%u", quadStatus);
        return SHIP_NATIVE_OK;
    }
    ++p->cycle;
    p->live = 1;
    p->done = p->submitted = p->reads = p->hits = p->actors = p->suppressed = p->observed = p->violations = 0;
    p->destroy = 0;
    p->lastResolved = 0;
    p->epoch = state.scene_epoch;
    p->due = state.tick + 100;
    Record(p,
           "quad-acquired cycle=%u run=%s action=1 lease=1 epoch=%llu tick=%llu "
           "mask=3",
           p->cycle, p->mode, (unsigned long long)p->epoch, (unsigned long long)state.tick);
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL AfterAction(void* user, const ShipNativeHookCall* call) {
    Probe* p = (Probe*)user;
    const ShipOotPlayerActionHookV1* h;
    if (!call || call->payload_size < sizeof(*h))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    h = (const ShipOotPlayerActionHookV1*)call->payload;
    if (p->live) {
        ++p->observed;
        if (h->executed)
            ++p->violations;
    }
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Destroy(void* user, const ShipNativeHookCall* call) {
    Probe* p = (Probe*)user;
    ShipOotPlayerActionStateV1 state = { 0 };
    (void)call;
    if (!p->live)
        return SHIP_NATIVE_OK;
    ++p->destroy;
    state.size = sizeof(state);
    (void)p->movement->get_action_state(&state);
    Record(p, "quad-scene cycle=%u epoch_before=%llu epoch_after=%llu", p->cycle, (unsigned long long)p->epoch,
           (unsigned long long)state.scene_epoch);
    if (strcmp(p->mode, "transition") != 0 || state.scene_epoch <= p->epoch)
        ++p->errors;
    Finish(p, 1);
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Arm(void* user, const char* request, uint32_t length, ShipNativeWriteFn write,
                                             void* writer) {
    Probe* p = (Probe*)user;
    if (p->armed || p->live)
        return SHIP_NATIVE_LIMIT;
    if (!request || length >= sizeof(p->mode))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    memcpy(p->mode, request, length);
    p->mode[length] = 0;
    if (strcmp(p->mode, "normal") && strcmp(p->mode, "detach") && strcmp(p->mode, "transition") &&
        strcmp(p->mode, "hold"))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    p->armed = 1;
    return write(writer, "quad-probe armed", 16);
}

static ShipNativeStatus SHIP_NATIVE_CALL Report(void* user, const char* request, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    Probe* p = (Probe*)user;
    ShipNativeStatus status;
    (void)request;
    (void)length;
    Record(p,
           "quad-probe run=%s armed=%u live=%u done=%u cycle=%u submitted=%u "
           "reads=%u errors=%u overflow=%u",
           p->mode[0] ? p->mode : "none", p->armed, p->live, p->done, p->cycle, p->submitted, p->reads, p->errors,
           p->overflow);
    status = write(writer, p->queue, (uint32_t)p->used);
    if (status == SHIP_NATIVE_OK) {
        p->used = 0;
        p->queue[0] = 0;
    }
    return status;
}

static ShipNativeStatus Hook(const ShipNativeRuntime* rt, Probe* p, const char* point, uint32_t size, uint32_t phase,
                             ShipNativeHookFn callback) {
    ShipNativeHookSpec spec = { sizeof(spec), point, 1, size, SHIP_NATIVE_HOOK_OBSERVE, phase, 0, callback, p };
    uint64_t handle = 0;
    return rt->register_hook(rt->context, &spec, &handle);
}

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* rt, void** instance) {
    Probe* p;
    ShipNativeStatus status;
    if (!rt || !instance || rt->size < sizeof(*rt) || rt->abi_major != 1 || rt->abi_minor < 4)
        return SHIP_NATIVE_UNSUPPORTED;
    p = (Probe*)calloc(1, sizeof(*p));
    if (!p)
        return SHIP_NATIVE_FAILURE;
    *instance = p;
    p->engine =
        (const ShipOotEngineV1*)rt->get_service(rt->context, LINKSPAN_OOT_ENGINE_SERVICE, 1, sizeof(*p->engine));
    p->movement =
        (const ShipOotMovementV3*)rt->get_service(rt->context, LINKSPAN_OOT_MOVEMENT_SERVICE, 3, sizeof(*p->movement));
    p->colliders = (const ShipOotCollidersV2*)rt->get_service(rt->context, LINKSPAN_OOT_COLLIDERS_SERVICE, 2,
                                                              sizeof(*p->colliders));
    if (!p->engine || !p->movement || !p->colliders || p->engine->player_size != sizeof(Player) ||
        !p->engine->layout_id || !p->movement->layout_id || !p->colliders->layout_id ||
        strcmp(p->engine->layout_id, QUAD_PROBE_LAYOUT_ID) || strcmp(p->movement->layout_id, QUAD_PROBE_LAYOUT_ID) ||
        strcmp(p->colliders->layout_id, QUAD_PROBE_LAYOUT_ID))
        return SHIP_NATIVE_UNSUPPORTED;
    status = Hook(rt, p, LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END, sizeof(ShipOotPlayerTickHookV1), SHIP_NATIVE_HOOK_AFTER,
                  Update);
    if (status == SHIP_NATIVE_OK)
        status = Hook(rt, p, LINKSPAN_OOT_HOOK_PLAYER_ACTION, sizeof(ShipOotPlayerActionHookV1), SHIP_NATIVE_HOOK_AFTER,
                      AfterAction);
    if (status == SHIP_NATIVE_OK)
        status =
            Hook(rt, p, LINKSPAN_OOT_HOOK_PLAY_DESTROY, sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_AFTER, Destroy);
    if (status == SHIP_NATIVE_OK)
        status = rt->register_function(rt->context, "arm", Arm, p);
    if (status == SHIP_NATIVE_OK)
        status = rt->register_function(rt->context, "report", Report, p);
    return status;
}

static void SHIP_NATIVE_CALL Shutdown(void* instance) {
    Probe* p = (Probe*)instance;
    if (!p)
        return;
    /* O host revoga a acao e os quads antes de shutdown; releases sao
     * idempotentes. Nao ha logger ABI depois do unload: validar pelo log do
     * loader e exitCode. */
    if (p->lease && p->colliders)
        (void)p->colliders->release_player_quads(p->lease);
    if (p->action && p->movement)
        (void)p->movement->release_action(p->action);
    free(p);
}

SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor = { sizeof(descriptor), 1, 4, Init, Shutdown };
    return &descriptor;
}
