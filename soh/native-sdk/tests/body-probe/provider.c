#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shiplua/native/ship_native_abi.h>
#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_player_body.h"

#define OWNER "linkspan.test.body_probe"
#define OTHER "linkspan.test.body_probe.other"
#define WAIT_ANIM "objects/gameplay_keep/gPlayerAnim_link_normal_wait"
#define WALK_ANIM "objects/gameplay_keep/gPlayerAnim_link_normal_walk"
#define HIT_ANIM "objects/gameplay_keep/gPlayerAnim_link_fighter_normal_kiru"

/* Somente servicos publicos. Nenhum Actor/Player/SkelAnime e implementado ou escrito aqui. */
typedef struct Probe {
    const ShipOotMovementV3* movement;
    const ShipOotPlayerBodyV1* body;
    uint64_t control, action, generation, firstGeneration, detachedGeneration, oldControl;
    uint64_t epochBefore, epochAfter, lastSerial, attackBaseSerial, attackSerial;
    float lastFrame;
    unsigned armed, transition, done, attached, detached, pendingDetach, pendingAttach;
    unsigned initialEmpty, uncontrolledAttach, attachCount, detachCount, reattachCount, reacquired;
    unsigned controls, controlReleasePreservedBody, staleControlDenied, staleGenerationDenied;
    unsigned otherSetDenied, otherControlDenied, secondControlDenied, otherRootDenied, staleRootDenied;
    unsigned bodyTicks, draws, limbs, animationAdvanced, walkRequests, idleRequests;
    unsigned actionTicks, attacks, finished, aborted, rootAccepted, idleRestored;
    unsigned attackOn, attackTick, lastAttackTick, destroys, sceneInactive, detachedEmpty, errors;
    unsigned luaRejected, lastOwned, stopAttacks, ocarinaPending, ocarinaWindow, ocarinaTicks, ocarinaAdvanced;
    unsigned detachedFrames, detachedCallbackViolation, postSceneTicks, postSceneDraws, postSceneAdvanced;
    ShipNativeStatus ocarinaStatus;
} Probe;

static void Expect(Probe* p, int condition) {
    if (!condition)
        ++p->errors;
}
static ShipNativeStatus State(Probe* p, ShipOotBodyStateV1* state) {
    memset(state, 0, sizeof(*state));
    state->size = sizeof(*state);
    return p->body->get_state(state);
}
static ShipOotAnimationPlayV2 Play(uint8_t mode) {
    ShipOotAnimationPlayV2 play = { 0 };
    play.size = sizeof(play);
    play.speed = 1.0f;
    play.mode = mode;
    return play;
}
static ShipNativeStatus SHIP_NATIVE_CALL Action(void* user, const ShipOotPlayerActionTickV1* tick,
                                                uint32_t* disposition) {
    Probe* p = (Probe*)user;
    Expect(p, tick && tick->size >= sizeof(*tick));
    if (!disposition)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    ++p->actionTicks;
    *disposition = SHIP_OOT_ACTION_SUPPRESS;
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Tick(void* user, const ShipOotBodyTickV1* tick) {
    Probe* p = (Probe*)user;
    ShipOotPlayerActionStateV1 action = { 0 };
    ShipOotAnimationPlayV2 play;
    int moving;
    if (!tick || tick->size < sizeof(*tick))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    ++p->bodyTicks;
    if (p->detached)
        ++p->detachedCallbackViolation;
    if (p->reacquired)
        ++p->postSceneTicks;
    Expect(p, tick->control == p->control && tick->state.controlled && tick->state.active);
    if (tick->state.animation.animation_serial == p->lastSerial &&
        tick->state.animation.current_frame != p->lastFrame) {
        ++p->animationAdvanced;
        if (p->reacquired)
            ++p->postSceneAdvanced;
        if (p->ocarinaWindow)
            ++p->ocarinaAdvanced;
    }
    p->lastSerial = tick->state.animation.animation_serial;
    p->lastFrame = tick->state.animation.current_frame;
    if (p->ocarinaWindow) {
        --p->ocarinaWindow;
        ++p->ocarinaTicks;
    }
    action.size = sizeof(action);
    moving = p->movement->get_stick_x(0) || p->movement->get_stick_y(0);
    if (p->attackOn) {
        if (tick->state.animation.animation_serial != p->attackBaseSerial && !p->attackSerial)
            p->attackSerial = tick->state.animation.animation_serial;
        if (p->movement->get_action_state(&action) != SHIP_NATIVE_OK || !action.owned) {
            p->movement->release_action(p->action);
            p->action = 0;
            p->attackOn = 0;
            ++p->aborted;
        } else if (p->attackSerial && tick->state.animation.animation_serial == p->attackSerial &&
                   tick->state.animation.finished_previous_update) {
            const uint64_t staleAction = p->action;
            Expect(p, p->movement->release_action(p->action) == SHIP_NATIVE_OK);
            p->action = 0;
            p->attackOn = 0;
            ++p->finished;
            if (p->body->set_root_motion(p->control, staleAction, SHIP_OOT_ROOT_XZ) == SHIP_NATIVE_UNSUPPORTED)
                ++p->staleRootDenied;
            else
                ++p->errors;
            Expect(p, p->body->set_root_motion(p->control, 0, SHIP_OOT_ROOT_DISABLED) == SHIP_NATIVE_OK);
            action.size = sizeof(action);
            if (p->movement->get_action_state(&action) == SHIP_NATIVE_OK && !action.owned &&
                (action.flags & SHIP_OOT_ACTION_STATE_IDLE))
                ++p->idleRestored;
        }
    }
    if (!p->attackOn && !p->done && !p->stopAttacks && p->bodyTicks - p->lastAttackTick >= 100 && !moving) {
        const ShipOotPlayerActionSpecV1 spec = { sizeof(spec), 0, Action, p };
        action.size = sizeof(action);
        if (p->movement->get_action_state(&action) == SHIP_NATIVE_OK && (action.flags & SHIP_OOT_ACTION_STATE_IDLE) &&
            !action.owned) {
            if (!p->otherRootDenied) {
                uint64_t otherAction = 0;
                if (p->movement->acquire_action(OTHER, &spec, &otherAction) == SHIP_NATIVE_OK) {
                    Expect(p, p->body->set_root_motion(p->control, otherAction, SHIP_OOT_ROOT_XZ) ==
                                  SHIP_NATIVE_UNSUPPORTED);
                    if (!p->errors)
                        p->otherRootDenied = 1;
                    Expect(p, p->movement->release_action(otherAction) == SHIP_NATIVE_OK);
                }
            }
            if (p->movement->acquire_action(OWNER, &spec, &p->action) == SHIP_NATIVE_OK) {
                play = Play(LINKSPAN_OOT_ANIM_ONCE);
                Expect(p,
                       p->body->start_animation(p->control, "golpe", &play, SHIP_OOT_ANIM_OVERRIDE) == SHIP_NATIVE_OK);
                p->attackBaseSerial = tick->state.animation.animation_serial;
                p->attackSerial = 0;
                p->attackOn = 1;
                p->attackTick = p->bodyTicks;
                p->lastAttackTick = p->bodyTicks;
                ++p->attacks;
            }
        }
    }
    if (p->attackOn) {
        if (p->body->set_root_motion(p->control, p->action, SHIP_OOT_ROOT_XZ) == SHIP_NATIVE_OK)
            ++p->rootAccepted;
        else
            ++p->errors;
    } else {
        play = Play(LINKSPAN_OOT_ANIM_LOOP);
        Expect(p, p->body->start_animation(p->control, moving ? "andar" : "parado", &play, SHIP_OOT_ANIM_FALLBACK) ==
                      SHIP_NATIVE_OK);
        if (moving)
            ++p->walkRequests;
        else
            ++p->idleRequests;
    }
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Draw(void* user, const ShipOotBodyDrawV1* draw, uint8_t* defaultDraw) {
    Probe* p = (Probe*)user;
    if (!draw || !defaultDraw)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    Expect(p, draw->control == p->control && draw->state.active && draw->state.controlled);
    ++p->draws;
    if (p->detached)
        ++p->detachedCallbackViolation;
    if (p->reacquired)
        ++p->postSceneDraws;
    *defaultDraw = 1;
    return SHIP_NATIVE_OK;
}
static ShipNativeStatus SHIP_NATIVE_CALL Limb(void* user, const ShipOotSkeletonLimbV2* limb) {
    Probe* p = (Probe*)user;
    Expect(p, limb && limb->size >= sizeof(*limb));
    ++p->limbs;
    if (p->detached)
        ++p->detachedCallbackViolation;
    return SHIP_NATIVE_OK;
}
static ShipOotBodyControllerV1 Controller(Probe* p) {
    ShipOotBodyControllerV1 controller = { 0 };
    controller.size = sizeof(controller);
    controller.tick = Tick;
    controller.draw = Draw;
    controller.post_limb = Limb;
    controller.user = p;
    return controller;
}
static ShipNativeStatus Acquire(Probe* p, uint64_t generation) {
    ShipOotBodyControllerV1 controller = Controller(p);
    ShipNativeStatus status = p->body->acquire_control(OWNER, generation, &controller, &p->control);
    if (status == SHIP_NATIVE_OK)
        ++p->controls;
    return status;
}
static ShipNativeStatus Attach(Probe* p) {
    static const ShipOotBodyAnimationV1 animations[] = {
        { sizeof(ShipOotBodyAnimationV1), 0, "parado", WAIT_ANIM, 0, { 0 } },
        { sizeof(ShipOotBodyAnimationV1), 0, "andar", WALK_ANIM, 0, { 0 } },
        { sizeof(ShipOotBodyAnimationV1), 0, "golpe", HIT_ANIM, 0, { 0 } }
    };
    static const ShipOotBodySegmentV1 segments[] = {
        { sizeof(ShipOotBodySegmentV1), 8, "objects/object_link_boy/gLinkAdultEyesOpenTex" },
        { sizeof(ShipOotBodySegmentV1), 9, "objects/object_link_boy/gLinkAdultMouth1Tex" }
    };
    ShipOotBodySpecV1 spec = { 0 };
    ShipOotBodyStateV1 state;
    ShipOotBodyControllerV1 controller = Controller(p);
    ShipOotAnimationInfoV2 info = { 0 };
    uint64_t invalid = 9, firstControl;
    ShipNativeStatus status;
    spec.size = sizeof(spec);
    spec.skeleton.size = sizeof(spec.skeleton);
    spec.skeleton.format = SHIP_OOT_SKELETON_LINK;
    spec.skeleton.skeleton_path = "objects/object_link_boy/gLinkAdultSkel";
    spec.skeleton.initial_animation_path = WAIT_ANIM;
    spec.skeleton.base_translation[0] = -57;
    spec.skeleton.base_translation[1] = 3377;
    spec.default_animation = "parado";
    spec.animations = animations;
    spec.animation_count = 3;
    spec.segments = segments;
    spec.segment_count = 2;
    status = p->body->set_body(OWNER, &spec);
    if (status != SHIP_NATIVE_OK)
        return status;
    Expect(p, State(p, &state) == SHIP_NATIVE_OK && state.active && !state.controlled && state.body_generation);
    p->uncontrolledAttach = 1;
    p->generation = state.body_generation;
    if (!p->firstGeneration) {
        p->firstGeneration = state.body_generation;
        p->epochBefore = state.scene_epoch;
    }
    ++p->attachCount;
    if (p->body->set_body(OTHER, NULL) == SHIP_NATIVE_LIMIT)
        ++p->otherSetDenied;
    else
        ++p->errors;
    if (p->body->acquire_control(OTHER, state.body_generation, &controller, &invalid) == SHIP_NATIVE_LIMIT && !invalid)
        ++p->otherControlDenied;
    else
        ++p->errors;
    invalid = 9;
    if (p->body->acquire_control(OWNER, state.body_generation + 1, &controller, &invalid) == SHIP_NATIVE_UNSUPPORTED &&
        !invalid)
        ++p->staleGenerationDenied;
    else
        ++p->errors;
    if (p->detachedGeneration) {
        invalid = 9;
        if (p->body->acquire_control(OWNER, p->detachedGeneration, &controller, &invalid) == SHIP_NATIVE_UNSUPPORTED &&
            !invalid)
            ++p->staleGenerationDenied;
        else
            ++p->errors;
    }
    Expect(p, Acquire(p, state.body_generation) == SHIP_NATIVE_OK);
    firstControl = p->control;
    invalid = 9;
    if (p->body->acquire_control(OWNER, state.body_generation, &controller, &invalid) == SHIP_NATIVE_LIMIT && !invalid)
        ++p->secondControlDenied;
    else
        ++p->errors;
    Expect(p, p->body->release_control(firstControl) == SHIP_NATIVE_OK);
    if (State(p, &state) == SHIP_NATIVE_OK && state.active && !state.controlled)
        ++p->controlReleasePreservedBody;
    else
        ++p->errors;
    info.size = sizeof(info);
    if (p->body->animation_info(firstControl, "parado", &info) == SHIP_NATIVE_UNSUPPORTED)
        ++p->staleControlDenied;
    else
        ++p->errors;
    Expect(p, p->body->release_control(firstControl) == SHIP_NATIVE_OK);
    Expect(p, Acquire(p, state.body_generation) == SHIP_NATIVE_OK && p->control != firstControl);
    if (p->oldControl)
        Expect(p, p->control != p->oldControl);
    info.size = sizeof(info);
    Expect(p, p->body->animation_info(p->control, "golpe", &info) == SHIP_NATIVE_OK && info.loaded && info.present &&
                  info.asset_last_frame > 0);
    p->attached = 1;
    p->detached = 0;
    p->lastAttackTick = p->bodyTicks;
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Update(void* user, const ShipNativeHookCall* call) {
    Probe* p = (Probe*)user;
    ShipOotBodyStateV1 state;
    ShipOotPlayerActionStateV1 action = { 0 };
    ShipOotAnimationInfoV2 info = { 0 };
    (void)call;
    if (!p->armed)
        return SHIP_NATIVE_OK;
    if (p->detached)
        ++p->detachedFrames;
    if (!p->attachCount || p->pendingAttach) {
        action.size = sizeof(action);
        if (p->movement->get_action_state(&action) != SHIP_NATIVE_OK || (action.flags & SHIP_OOT_ACTION_STATE_BLOCKED))
            return SHIP_NATIVE_OK;
        if (State(p, &state) != SHIP_NATIVE_OK) {
            ++p->errors;
            return SHIP_NATIVE_OK;
        }
        if (!p->attachCount) {
            if (state.active || state.body_generation != 0) {
                ++p->errors;
                return SHIP_NATIVE_OK;
            }
            p->initialEmpty = 1;
        }
        if (Attach(p) == SHIP_NATIVE_OK) {
            if (p->pendingAttach) {
                Expect(p, p->generation > p->detachedGeneration);
                ++p->reattachCount;
            }
            p->pendingAttach = 0;
        }
        return SHIP_NATIVE_OK;
    }
    if (p->pendingDetach) {
        p->pendingDetach = 0;
        if (p->action)
            Expect(p, p->movement->release_action(p->action) == SHIP_NATIVE_OK);
        p->action = 0;
        p->attackOn = 0;
        p->detachedGeneration = p->generation;
        p->oldControl = p->control;
        Expect(p, p->body->set_body(OWNER, NULL) == SHIP_NATIVE_OK);
        if (State(p, &state) == SHIP_NATIVE_OK && !state.active && !state.controlled && state.body_generation == 0)
            ++p->detachedEmpty;
        else
            ++p->errors;
        info.size = sizeof(info);
        if (p->body->animation_info(p->oldControl, "parado", &info) == SHIP_NATIVE_UNSUPPORTED)
            ++p->staleControlDenied;
        else
            ++p->errors;
        Expect(p, p->body->release_control(p->oldControl) == SHIP_NATIVE_OK);
        p->control = 0;
        p->lastOwned = 0;
        p->attached = 0;
        p->detached = 1;
        ++p->detachCount;
    }
    if (p->attached && State(p, &state) == SHIP_NATIVE_OK) {
        if (state.active && state.body_generation != p->generation) {
            ShipOotBodyControllerV1 controller = Controller(p);
            uint64_t invalid = 9;
            Expect(p, p->destroys && state.body_generation > p->generation);
            info.size = sizeof(info);
            if (p->body->animation_info(p->control, "parado", &info) == SHIP_NATIVE_UNSUPPORTED)
                ++p->staleControlDenied;
            else
                ++p->errors;
            Expect(p, p->body->release_control(p->control) == SHIP_NATIVE_OK);
            if (p->body->acquire_control(OWNER, p->generation, &controller, &invalid) == SHIP_NATIVE_UNSUPPORTED &&
                !invalid)
                ++p->staleGenerationDenied;
            else
                ++p->errors;
            Expect(p, Acquire(p, state.body_generation) == SHIP_NATIVE_OK);
            if (p->action)
                Expect(p, p->movement->release_action(p->action) == SHIP_NATIVE_OK);
            p->action = 0;
            p->attackOn = 0;
            p->generation = state.body_generation;
            p->epochAfter = state.scene_epoch;
            ++p->reacquired;
            p->lastSerial = UINT64_MAX;
            p->lastAttackTick = p->bodyTicks;
        }
        if (State(p, &state) == SHIP_NATIVE_OK)
            p->lastOwned = state.active && state.controlled;
    }
    if (p->ocarinaPending) {
        p->ocarinaPending = 0;
        /* ITEM_OCARINA_TIME = 0x08, conferido em z64item.h; servico valida o uso real. */
        p->ocarinaStatus = p->movement->player_use_item_shortcut(0x08u);
        if (p->ocarinaStatus == SHIP_NATIVE_OK)
            p->ocarinaWindow = 60;
        else
            ++p->errors;
    }
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Destroy(void* user, const ShipNativeHookCall* call) {
    Probe* p = (Probe*)user;
    ShipOotBodyStateV1 state;
    (void)call;
    if (p->armed && p->attached) {
        ++p->destroys;
        p->lastOwned = 0;
        if (State(p, &state) == SHIP_NATIVE_OK && !state.active && !state.controlled)
            ++p->sceneInactive;
        else
            ++p->errors;
    }
    return SHIP_NATIVE_OK;
}
static int Request(const char* request, uint32_t length, const char* expected) {
    return length == strlen(expected) && memcmp(request, expected, length) == 0;
}
static ShipNativeStatus SHIP_NATIVE_CALL Command(void* user, const char* request, uint32_t length,
                                                 ShipNativeWriteFn write, void* writer) {
    Probe* p = (Probe*)user;
    const char* reply = "body-probe command accepted";
    if (Request(request, length, "normal") || Request(request, length, "transition")) {
        if (p->armed)
            return SHIP_NATIVE_LIMIT;
        p->armed = 1;
        p->transition = Request(request, length, "transition");
    } else if (Request(request, length, "detach") && p->attached)
        p->pendingDetach = 1;
    else if (Request(request, length, "reattach") && p->detached)
        p->pendingAttach = 1;
    else if (Request(request, length, "finish") && p->attached) {
        p->done = 1;
        p->stopAttacks = 1;
    } else if (Request(request, length, "ocarina") && p->attached) {
        p->stopAttacks = 1;
        p->ocarinaPending = 1;
    } else if (Request(request, length, "lua_false") && p->attached)
        ++p->luaRejected;
    else if (Request(request, length, "lua_true"))
        ++p->errors;
    else
        return SHIP_NATIVE_INVALID_ARGUMENT;
    return write(writer, reply, (uint32_t)strlen(reply));
}
static ShipNativeStatus SHIP_NATIVE_CALL Report(void* user, const char* request, uint32_t length,
                                                ShipNativeWriteFn write, void* writer) {
    Probe* p = (Probe*)user;
    char report[2048];
    int n;
    (void)request;
    (void)length;
    n = snprintf(
        report, sizeof(report),
        "body-probe run=%s armed=%u done=%u attached=%u detached=%u initial_empty=%u uncontrolled_attach=%u "
        "attach=%u detach=%u reattach=%u controls=%u control_release_preserved=%u stale_control=%u stale_generation=%u "
        "other_set=%u other_control=%u second_control=%u other_root=%u stale_root=%u body_ticks=%u draws=%u limbs=%u "
        "advanced=%u walk=%u idle=%u action_ticks=%u attacks=%u finished=%u aborted=%u root=%u idle_restored=%u "
        "destroys=%u scene_inactive=%u reacquired=%u detached_empty=%u errors=%u lua_rejected=%u active_control=%u "
        "detached_frames=%u detached_callback_violation=%u post_scene_ticks=%u post_scene_draws=%u "
        "post_scene_advanced=%u "
        "ocarina_status=%u ocarina_ticks=%u ocarina_advanced=%u first_generation=%llu generation=%llu "
        "epoch_before=%llu epoch_after=%llu",
        p->transition ? "transition" : "normal", p->armed, p->done, p->attached, p->detached, p->initialEmpty,
        p->uncontrolledAttach, p->attachCount, p->detachCount, p->reattachCount, p->controls,
        p->controlReleasePreservedBody, p->staleControlDenied, p->staleGenerationDenied, p->otherSetDenied,
        p->otherControlDenied, p->secondControlDenied, p->otherRootDenied, p->staleRootDenied, p->bodyTicks, p->draws,
        p->limbs, p->animationAdvanced, p->walkRequests, p->idleRequests, p->actionTicks, p->attacks, p->finished,
        p->aborted, p->rootAccepted, p->idleRestored, p->destroys, p->sceneInactive, p->reacquired, p->detachedEmpty,
        p->errors, p->luaRejected, p->lastOwned, p->detachedFrames, p->detachedCallbackViolation, p->postSceneTicks,
        p->postSceneDraws, p->postSceneAdvanced, p->ocarinaStatus, p->ocarinaTicks, p->ocarinaAdvanced,
        (unsigned long long)p->firstGeneration, (unsigned long long)p->generation, (unsigned long long)p->epochBefore,
        (unsigned long long)p->epochAfter);
    if (n < 0 || n >= (int)sizeof(report))
        return SHIP_NATIVE_FAILURE;
    return write(writer, report, (uint32_t)n);
}
static ShipNativeStatus Hook(const ShipNativeRuntime* rt, Probe* p, const char* point, uint32_t size,
                             ShipNativeHookFn callback) {
    ShipNativeHookSpec spec = { sizeof(spec),           point, 1,        size, SHIP_NATIVE_HOOK_OBSERVE,
                                SHIP_NATIVE_HOOK_AFTER, 0,     callback, p };
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
    p->ocarinaStatus = SHIP_NATIVE_FAILURE;
    p->body =
        (const ShipOotPlayerBodyV1*)rt->get_service(rt->context, LINKSPAN_OOT_PLAYER_BODY_SERVICE, 1, sizeof(*p->body));
    p->movement =
        (const ShipOotMovementV3*)rt->get_service(rt->context, LINKSPAN_OOT_MOVEMENT_SERVICE, 3, sizeof(*p->movement));
    if (!p->body || !p->movement || !p->body->layout_id || !p->movement->layout_id ||
        strcmp(p->body->layout_id, p->movement->layout_id) != 0)
        return SHIP_NATIVE_UNSUPPORTED;
    status = Hook(rt, p, LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END, sizeof(ShipOotPlayerTickHookV1), Update);
    if (status == SHIP_NATIVE_OK)
        status = Hook(rt, p, LINKSPAN_OOT_HOOK_PLAY_DESTROY, sizeof(ShipOotPlayHookV1), Destroy);
    if (status == SHIP_NATIVE_OK)
        status = rt->register_function(rt->context, "command", Command, p);
    if (status == SHIP_NATIVE_OK)
        status = rt->register_function(rt->context, "report", Report, p);
    return status;
}
static void SHIP_NATIVE_CALL Shutdown(void* instance) {
    Probe* p = (Probe*)instance;
    if (!p)
        return;
    /* O host drena dono/callbacks antes do shutdown; libera tokens idempotentes remanescentes. */
    if (p->action && p->movement)
        p->movement->release_action(p->action);
    if (p->control && p->body)
        p->body->release_control(p->control);
    free(p);
}
SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor = { sizeof(descriptor), 1, 4, Init, Shutdown };
    return &descriptor;
}
