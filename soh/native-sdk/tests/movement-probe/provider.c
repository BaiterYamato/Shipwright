#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shiplua/native/ship_native_abi.h>
#include "oot_camera.h"
#include "oot_engine.h"
#include "oot_hooks.h"

/* Sem ponteiros do jogo retidos, detours, escrita em actionFunc ou headers privados. */
typedef struct Probe {
    const ShipOotMovementV3* movement;
    const ShipOotCameraV2* camera;
    uint64_t action, cameraToken, epochBefore, epochAfter;
    unsigned armed, transition, started, done, ticks, callbacks, suppressed, pass, executed;
    unsigned suppressionViolation, suppressionObserved, suppressInput, input, errors, destroy, liveBeforeDestroy;
    unsigned pending, disposition;
    ShipNativeStatus leaseBefore, leaseAfter, releaseAction, releaseCamera;
    int rollSeen;
} Probe;

static ShipNativeStatus SHIP_NATIVE_CALL Action(void* user, const ShipOotPlayerActionTickV1* tick,
                                                uint32_t* disposition) {
    Probe* p = (Probe*)user;
    if (!tick || tick->size < sizeof(*tick) || !disposition)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    ++p->callbacks;
    *disposition = (p->transition || p->ticks < 60) ? SHIP_OOT_ACTION_SUPPRESS : SHIP_OOT_ACTION_PASS;
    p->pending = 1;
    p->disposition = *disposition;
    if (*disposition == SHIP_OOT_ACTION_SUPPRESS)
        ++p->suppressed;
    else
        ++p->pass;
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL AfterAction(void* user, const ShipNativeHookCall* call) {
    Probe* p = (Probe*)user;
    const ShipOotPlayerActionHookV1* at = (const ShipOotPlayerActionHookV1*)call->payload;
    if (!p->started || p->done)
        return SHIP_NATIVE_OK;
    if (p->pending && p->disposition == SHIP_OOT_ACTION_SUPPRESS) {
        ++p->suppressionObserved;
        if (at->executed)
            ++p->suppressionViolation;
        if (p->movement->get_stick_x(0) || p->movement->get_stick_y(0))
            ++p->suppressInput;
    }
    if (!p->transition && p->ticks >= 60 && at->executed)
        ++p->executed;
    if (p->movement->get_stick_x(0) || p->movement->get_stick_y(0))
        ++p->input;
    p->pending = 0;
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Update(void* user, const ShipNativeHookCall* call) {
    Probe* p = (Probe*)user;
    ShipOotPlayerActionStateV1 action = { 0 };
    ShipOotCameraLeaseV2 lease = { 0 };
    ShipOotCameraViewV2 view = { 0 };
    (void)call;
    action.size = sizeof(action);
    lease.size = sizeof(lease);
    view.size = sizeof(view);
    if (p->done || !p->armed)
        return SHIP_NATIVE_OK;
    if (!p->started) {
        ShipOotPlayerActionSpecV1 spec = { sizeof(spec), 0, Action, p };
        if (p->movement->get_action_state(&action) != SHIP_NATIVE_OK || (action.flags & SHIP_OOT_ACTION_STATE_BLOCKED))
            return SHIP_NATIVE_OK;
        if (p->movement->acquire_action("linkspan.test.movement_probe", &spec, &p->action) != SHIP_NATIVE_OK)
            return SHIP_NATIVE_OK;
        if (p->camera->acquire_ex("linkspan.test.movement_probe", &p->cameraToken, &lease) != SHIP_NATIVE_OK) {
            p->movement->release_action(p->action);
            p->action = 0;
            return SHIP_NATIVE_OK;
        }
        p->leaseBefore = p->camera->get_lease(p->cameraToken, &lease);
        p->epochBefore = action.scene_epoch;
        if (p->camera->get_view_ex(&view) != SHIP_NATIVE_OK)
            ++p->errors;
        else {
            view.roll = 0x1000;
            if (p->camera->set_view_ex(p->cameraToken, &view) != SHIP_NATIVE_OK)
                ++p->errors;
        }
        p->started = 1;
        return SHIP_NATIVE_OK;
    }
    ++p->ticks;
    if (p->destroy) {
        (void)p->movement->get_action_state(&action);
        p->epochAfter = action.scene_epoch;
        p->leaseAfter = p->camera->get_lease(p->cameraToken, &lease);
        p->releaseCamera = p->camera->release(p->cameraToken);
        p->releaseAction = p->movement->release_action(p->action);
        p->done = 1;
        return SHIP_NATIVE_OK;
    }
    if (p->ticks == 30 || p->ticks == 90) {
        if (p->camera->get_view_ex(&view) != SHIP_NATIVE_OK)
            ++p->errors;
        else
            p->rollSeen = view.roll;
    }
    if (!p->transition && p->ticks >= 120) {
        p->releaseAction = p->movement->release_action(p->action);
        p->releaseCamera = p->camera->release(p->cameraToken);
        p->leaseAfter = p->camera->get_lease(p->cameraToken, &lease);
        p->done = 1;
    }
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL BeforeDestroy(void* user, const ShipNativeHookCall* call) {
    Probe* p = (Probe*)user;
    ShipOotCameraLeaseV2 lease = { 0 };
    ShipOotPlayerActionStateV1 state = { 0 };
    (void)call;
    lease.size = sizeof(lease);
    state.size = sizeof(state);
    if (!p->started || p->done)
        return SHIP_NATIVE_OK;
    /* Epoch ja avancou antes dos observers; a posse viva precisa ser amostrada no update anterior. */
    ++p->destroy;
    (void)p->movement->get_action_state(&state);
    p->epochAfter = state.scene_epoch;
    p->leaseAfter = p->camera->get_lease(p->cameraToken, &lease);
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL BeforePlay(void* user, const ShipNativeHookCall* call) {
    Probe* p = (Probe*)user;
    ShipOotCameraLeaseV2 lease = { 0 };
    ShipOotPlayerActionStateV1 state = { 0 };
    (void)call;
    lease.size = sizeof(lease);
    state.size = sizeof(state);
    if (p->transition && p->started && !p->done && !p->destroy) {
        if (p->camera->get_lease(p->cameraToken, &lease) == SHIP_NATIVE_OK && lease.owned &&
            p->movement->get_action_state(&state) == SHIP_NATIVE_OK && state.owned)
            p->liveBeforeDestroy = 1;
    }
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Arm(void* user, const char* request, uint32_t length, ShipNativeWriteFn write,
                                             void* writer) {
    Probe* p = (Probe*)user;
    const char* message = "movement-probe armed";
    if (p->armed)
        return SHIP_NATIVE_LIMIT;
    if (length == 10 && memcmp(request, "transition", 10) == 0)
        p->transition = 1;
    else if (!(length == 6 && memcmp(request, "normal", 6) == 0))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    p->armed = 1;
    return write(writer, message, (uint32_t)strlen(message));
}

static ShipNativeStatus SHIP_NATIVE_CALL Report(void* user, const char* request, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    Probe* p = (Probe*)user;
    char report[768];
    int n;
    (void)request;
    (void)length;
    n = snprintf(report, sizeof(report),
                 "movement-probe run=%s armed=%u started=%u done=%u ticks=%u callbacks=%u suppressed=%u pass=%u "
                 "executed=%u suppression_violation=%u suppression_observed=%u suppress_input=%u input=%u errors=%u "
                 "lease_before=%u lease_after=%u "
                 "release_action=%u release_camera=%u roll=%d destroy=%u live_before_destroy=%u epoch_before=%llu "
                 "epoch_after=%llu",
                 p->transition ? "transition" : "normal", p->armed, p->started, p->done, p->ticks, p->callbacks,
                 p->suppressed, p->pass, p->executed, p->suppressionViolation, p->suppressionObserved, p->suppressInput,
                 p->input, p->errors, p->leaseBefore, p->leaseAfter, p->releaseAction, p->releaseCamera, p->rollSeen,
                 p->destroy, p->liveBeforeDestroy, (unsigned long long)p->epochBefore,
                 (unsigned long long)p->epochAfter);
    if (n < 0 || n >= (int)sizeof(report))
        return SHIP_NATIVE_FAILURE;
    return write(writer, report, (uint32_t)n);
}

static ShipNativeStatus Hook(const ShipNativeRuntime* rt, Probe* p, const char* point, uint32_t size, uint32_t phase,
                             ShipNativeHookFn fn) {
    ShipNativeHookSpec spec = { sizeof(spec), point, 1, size, SHIP_NATIVE_HOOK_OBSERVE, phase, 0, fn, p };
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
    p->leaseBefore = p->leaseAfter = p->releaseAction = p->releaseCamera = SHIP_NATIVE_FAILURE;
    p->movement =
        (const ShipOotMovementV3*)rt->get_service(rt->context, LINKSPAN_OOT_MOVEMENT_SERVICE, 3, sizeof(*p->movement));
    p->camera =
        (const ShipOotCameraV2*)rt->get_service(rt->context, LINKSPAN_OOT_CAMERA_SERVICE, 2, sizeof(*p->camera));
    if (!p->movement || !p->camera || !p->movement->layout_id || !p->camera->layout_id ||
        strcmp(p->movement->layout_id, p->camera->layout_id) != 0)
        return SHIP_NATIVE_UNSUPPORTED;
    status = Hook(rt, p, LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END, sizeof(ShipOotPlayerTickHookV1), SHIP_NATIVE_HOOK_AFTER,
                  Update);
    if (status == SHIP_NATIVE_OK)
        status = Hook(rt, p, LINKSPAN_OOT_HOOK_PLAYER_ACTION, sizeof(ShipOotPlayerActionHookV1), SHIP_NATIVE_HOOK_AFTER,
                      AfterAction);
    if (status == SHIP_NATIVE_OK)
        status =
            Hook(rt, p, LINKSPAN_OOT_HOOK_PLAY_UPDATE, sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_BEFORE, BeforePlay);
    if (status == SHIP_NATIVE_OK)
        status = Hook(rt, p, LINKSPAN_OOT_HOOK_PLAY_DESTROY, sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_AFTER,
                      BeforeDestroy);
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
    if (p->cameraToken && p->camera)
        p->camera->release(p->cameraToken);
    if (p->action && p->movement)
        p->movement->release_action(p->action);
    free(p);
}

SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor = { sizeof(descriptor), 1, 4, Init, Shutdown };
    return &descriptor;
}
