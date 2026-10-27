#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "game_bindings.h"
#include "MmGoronForm.h"
#include "forms_quads.h"
#include "oot_hooks.h"
#include "oot_engine.h"
#include "oot_player_body.h"
#include "oot_colliders.h"
#include "oot_camera.h"
#include "oot_render.h"
#include "mm_audio.h"

namespace MMForms {
namespace {
constexpr char kOwner[] = "linkspan.mm.forms";
const ShipNativeRuntime* runtime = nullptr;
const ShipOotPlayerBodyV1* body = nullptr;
const ShipOotMovementV3* movement = nullptr;
const ShipOotCollidersV2* colliders = nullptr;
const ShipOotSkeletonsV2* skeletons = nullptr;
const ShipOotCameraV2* camera = nullptr;
const ShipOotRenderV3* render = nullptr;
const ShipMmAudioV1* audio = nullptr;
uint64_t control = 0, action = 0, quads = 0, shield = 0;
uint64_t bodyGeneration = 0;
std::vector<uint64_t> hookHandles;
bool requested = false, configured = false, active = false;
bool shieldWasActive = false;
bool doorWasActive = false, chestWasActive = false;
std::string loopAnimation;
ShipOotAnimationPlayV2 loopPlay{};
bool transitionMaskOwned = false;
uint8_t previousMask = PLAYER_MASK_NONE;
std::string currentAnimation;
std::string fallbackAnimation;
ShipOotBodyStateV1 bodyState{};
nlohmann::json config;

bool StartAnimation(const char* name, float speed, bool once, bool reverse) {
    if (!control || !name)
        return false;
    ShipOotAnimationPlayV2 play{};
    play.size = sizeof(play);
    play.speed = speed;
    play.mode = once ? LINKSPAN_OOT_ANIM_ONCE : LINKSPAN_OOT_ANIM_LOOP;
    play.reverse = reverse;
    const auto status =
        body->start_animation(control, name, &play, once ? SHIP_OOT_ANIM_OVERRIDE : SHIP_OOT_ANIM_FALLBACK);
    if (status == SHIP_NATIVE_OK && !once) {
        loopAnimation = name;
        loopPlay = play;
    }
    if (status == SHIP_NATIVE_OK)
        currentAnimation = name;
    return status == SHIP_NATIVE_OK;
}
bool HasAnimation(const char* name) {
    ShipOotAnimationInfoV2 info{};
    info.size = sizeof(info);
    return control && name && body->animation_info(control, name, &info) == SHIP_NATIVE_OK && info.present &&
           info.loaded;
}
float CurrentFrame() {
    return bodyState.animation.current_frame;
}
float AnimationLastFrame(const char* name) {
    ShipOotAnimationInfoV2 info{};
    info.size = sizeof(info);
    return control && body->animation_info(control, name, &info) == SHIP_NATIVE_OK ? info.asset_last_frame : 0;
}
void PlaySample(const char* path) {
    if (path)
        audio->play_sample(path);
}
void SetInstrument(uint8_t id) {
    AudioOcarina_SetInstrument(id);
}

void EmitScaledDisplayList(PlayState* play, const char* path, float x, float y, float z) {
    if (!path || render->matrix_push() != SHIP_NATIVE_OK)
        return;
    render->matrix_scale(x, y, z);
    render->draw_display_list(play, std::strncmp(path, "__OTR__", 7) == 0 ? path + 7 : path,
                              LINKSPAN_OOT_RENDER_OPAQUE);
    render->matrix_pop();
}

#include "content.inc"

void ReleaseQuads() {
    if (quads)
        colliders->release_player_quads(quads);
    quads = 0;
}
void ClearAction() {
    if (control)
        body->set_root_motion(control, 0, SHIP_OOT_ROOT_DISABLED);
    ReleaseQuads();
    if (action)
        movement->release_action(action);
    action = 0;
}
void Stop(PlayState* play, bool forgetRequest, bool keepTransition = false) {
    if (forgetRequest)
        requested = false;
    Player* player = play ? GET_PLAYER(play) : nullptr;
    // A restauração precisa ocorrer enquanto o Player e o Play ainda existem.
    ShipLua::MmForm::ReleaseBody(player);
    CustomBodyWaterVoidReset(player);
    if (!keepTransition) {
        MaskTransitionClearForcedMask(player);
        MaskTransitionReset(player);
    }
    ClearAction();
    if (control)
        body->release_control(control);
    control = 0;
    if (shield)
        skeletons->destroy(shield);
    shield = 0;
    if (active)
        body->set_body(kOwner, nullptr);
    if (active)
        audio->clear_voice_map();
    active = false;
    bodyGeneration = 0;
    currentAnimation.clear();
    fallbackAnimation.clear();
    shieldWasActive = false;
    doorWasActive = chestWasActive = false;
    loopAnimation.clear();
    ShipLua::MmForm::Reset();
}

ShipNativeStatus SHIP_NATIVE_CALL ActionTick(void*, const ShipOotPlayerActionTickV1* tick, uint32_t* disposition) {
    if (!tick || tick->size < sizeof(*tick) || !disposition)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    *disposition = SHIP_OOT_ACTION_PASS;
    if (!active && gMaskTransitionPhase == MaskTransitionPhase::None)
        return SHIP_NATIVE_OK;
    Player* player = static_cast<Player*>(tick->player);
    PlayState* play = static_cast<PlayState*>(tick->play_state);
    if (!player || !play)
        return SHIP_NATIVE_UNSUPPORTED;
    if (gMaskTransitionPhase != MaskTransitionPhase::None) {
        *disposition = SHIP_OOT_ACTION_SUPPRESS;
        return SHIP_NATIVE_OK;
    }
    bodyState = {};
    bodyState.size = sizeof(bodyState);
    if (!control || body->get_state(&bodyState) != SHIP_NATIVE_OK || !bodyState.active)
        return SHIP_NATIVE_OK;
    ShipLua::MmForm::SetEntryBlocked(gCustomBodyWaterVoidPhase != CustomBodyWaterVoidPhase::None ||
                                     (!tick->vanilla_allowed && !ShipLua::MmForm::WantsPlayerActionPaused()));
    ShipLua::MmForm::SetOotRollActive((tick->state.flags & SHIP_OOT_ACTION_STATE_ROLL) != 0);
    ShipLua::MmForm::Update(player, play, bodyState.animation.finished_previous_update != 0);
    if (ShipLua::MmForm::WantsPlayerActionPaused())
        *disposition = SHIP_OOT_ACTION_SUPPRESS;
    return SHIP_NATIVE_OK;
}

bool AcquireAction() {
    if (action) {
        ShipOotPlayerQuadHitsV2 hits{};
        hits.size = sizeof(hits);
        if (quads && colliders->read_player_quad_hits(quads, SHIP_OOT_QUAD_ATTACK_0, &hits) == SHIP_NATIVE_OK)
            return true;
        ClearAction();
    }
    ShipOotPlayerActionSpecV1 spec{};
    spec.size = sizeof(spec);
    spec.callback = ActionTick;
    if (movement->acquire_action(kOwner, &spec, &action) != SHIP_NATIVE_OK)
        return false;
    if (colliders->acquire_player_quads(kOwner, action, SHIP_OOT_QUAD_MASK_ATTACK | SHIP_OOT_QUAD_MASK_DEFENSE,
                                        &quads) != SHIP_NATIVE_OK) {
        ClearAction();
        return false;
    }
    return true;
}

void SubmitShield(Player* player) {
    const s16 yaw = player->actor.shape.rot.y;
    const float sy = Math_SinS(yaw), cy = Math_CosS(yaw);
    const Vec3f& pos = player->actor.world.pos;
    const float cx = pos.x + sy * 12, cz = pos.z + cy * 12;
    ShipOotPlayerQuadSpecV2 spec{};
    spec.size = sizeof(spec);
    const float vertices[4][3] = {
        { cx - cy * 25, pos.y + 75, cz + sy * 25 },
        { cx + cy * 25, pos.y + 75, cz - sy * 25 },
        { cx + cy * 25, pos.y - 10, cz - sy * 25 },
        { cx - cy * 25, pos.y - 10, cz + sy * 25 },
    };
    std::memcpy(spec.vertices, vertices, sizeof(vertices));
    spec.col_type = player->currentShield == PLAYER_SHIELD_DEKU ? COLTYPE_WOOD : COLTYPE_METAL;
    spec.at_flags = player->shieldQuad.base.atFlags;
    spec.ac_flags = player->shieldQuad.base.acFlags;
    spec.elem_type = player->shieldQuad.info.elemType;
    spec.touch_flags = player->shieldQuad.info.toucherFlags;
    spec.touch_dmg_flags = player->shieldQuad.info.toucher.dmgFlags;
    spec.touch_damage = player->shieldQuad.info.toucher.damage;
    spec.touch_effect = player->shieldQuad.info.toucher.effect;
    spec.bump_flags = player->shieldQuad.info.bumperFlags;
    spec.bump_dmg_flags = player->shieldQuad.info.bumper.dmgFlags;
    spec.bump_effect = player->shieldQuad.info.bumper.effect;
    spec.bump_defense = player->shieldQuad.info.bumper.defense;
    spec.defending = 1;
    MtxF matrix{};
    SkinMatrix_SetTranslateRotateYXZScale(&matrix, 1, 1, 1, 0, yaw + 0x8000, 0, 0, 0, 0);
    std::memcpy(spec.defense_matrix, &matrix, sizeof(matrix));
    colliders->submit_player_quad(quads, SHIP_OOT_QUAD_DEFENSE, &spec);
}

ShipNativeStatus SHIP_NATIVE_CALL BodyTick(void*, const ShipOotBodyTickV1* tick) {
    if (!tick || tick->size < sizeof(*tick))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!active || tick->state.body_generation != bodyGeneration)
        return SHIP_NATIVE_UNSUPPORTED;
    Player* player = static_cast<Player*>(tick->player);
    PlayState* play = static_cast<PlayState*>(tick->play_state);
    bodyState = tick->state;
    gCustomBodySkelAnime.curFrame = tick->state.animation.current_frame;
    gCustomBodyCurrentAnim = currentAnimation;
    gCustomBodyPunchHitActive = ShipLua::MmForm::PunchHitActive();
    CustomBodyUpdateWaterVoid(player, play);
    if (action && quads) {
        body->set_root_motion(control, action,
                              ShipLua::MmForm::UsesRootMotion() ? SHIP_OOT_ROOT_XZ : SHIP_OOT_ROOT_DISABLED);
        if (ShipLua::MmForm::IsShielding() && shield) {
            if (!shieldWasActive) {
                ShipOotAnimationPlayV2 animation{};
                animation.size = sizeof(animation);
                animation.speed = 1;
                animation.mode = LINKSPAN_OOT_ANIM_LOOP;
                skeletons->play_ex(shield, config["shield"]["animation"].get_ref<const std::string&>().c_str(),
                                   &animation);
            }
            uint8_t finished = 0;
            skeletons->update(shield, &finished);
            SubmitShield(player);
        } else
            colliders->disable_player_quad(quads, SHIP_OOT_QUAD_DEFENSE);
        shieldWasActive = ShipLua::MmForm::IsShielding();
    }
    const char* wanted = ShipLua::MmForm::DesiredAnimation();
    const bool climbing = (player->stateFlags1 & PLAYER_STATE1_CLIMBING_LADDER) != 0;
    const int stick = play->state.input[0].rel.stick_y;
    const bool rightStep = (player->av2.actionVar2 & 1) != 0;
    if (climbing) {
        // LACUNA: player-body V1 não altera playSpeed de um loop em curso; falta conferir a cadência analógica.
        wanted = player->av2.actionVar2 < 0 ? (player->av2.actionVar2 == -4 ? "climb_start_b" : "climb_start_a")
                 : stick > 0                ? (rightStep ? "climb_up_r" : "climb_up_l")
                 : stick < 0                ? (rightStep ? "climb_down_r" : "climb_down_l")
                                            : "climb_wait";
    } else if (ShipLua::MmForm::GakkiPhase() && !loopAnimation.empty()) {
        wanted = loopAnimation.c_str();
    }
    ShipOotPlayerActionStateV1 actionState{};
    actionState.size = sizeof(actionState);
    const bool door = movement->get_action_state(&actionState) == SHIP_NATIVE_OK &&
                      (actionState.flags & SHIP_OOT_ACTION_STATE_DOOR) && player->doorType == PLAYER_DOORTYPE_HANDLE;
    const bool chest = (player->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) && player->interactRangeActor &&
                       player->interactRangeActor->id == ACTOR_EN_BOX;
    if (door && !doorWasActive)
        StartAnimation(player->doorDirection < 0 ? "door_open_left" : "door_open_right", 1, true, false);
    if (chest && !chestWasActive)
        StartAnimation("chest_open", 1, true, false);
    doorWasActive = door;
    chestWasActive = chest;
    if (wanted && HasAnimation(wanted)) {
        ShipOotAnimationPlayV2 animation{};
        animation.size = sizeof(animation);
        animation.speed = 1;
        animation.mode = LINKSPAN_OOT_ANIM_LOOP;
        if (ShipLua::MmForm::GakkiPhase() && loopAnimation == wanted)
            animation = loopPlay;
        if (config.contains("reverse_anims"))
            animation.reverse = config["reverse_anims"].value(wanted, false);
        if (body->start_animation(control, wanted, &animation, SHIP_OOT_ANIM_FALLBACK) == SHIP_NATIVE_OK)
            fallbackAnimation = wanted;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL PostLimb(void*, const ShipOotSkeletonLimbV2* limb) {
    if (!limb || limb->size < sizeof(*limb))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    CustomBodyPostLimbDraw(static_cast<PlayState*>(limb->play_state), limb->limb, nullptr, nullptr, limb->actor);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL BodyDraw(void*, const ShipOotBodyDrawV1* draw, uint8_t* drawDefault) {
    if (!draw || draw->size < sizeof(*draw) || !drawDefault)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    *drawDefault = 1;
    if (!active)
        return SHIP_NATIVE_OK;
    auto* play = static_cast<PlayState*>(draw->play_state);
    auto* player = static_cast<Player*>(draw->player);
    auto path = [&](const char* name) -> const char* {
        auto it = gCustomBodySpec.modelsPrefixed.find(name);
        return it == gCustomBodySpec.modelsPrefixed.end() ? nullptr : it->second.c_str();
    };
    const bool waterBall = gCustomBodyWaterVoidPhase == CustomBodyWaterVoidPhase::Ball;
    if ((ShipLua::MmForm::IsBallBody() || waterBall) && path("roll")) {
        // A matriz da F2 já contém o offset do corpo ereto.
        Matrix_Translate(0, (config.value("roll_offset", 12.0f) - config.value("ground_offset", -30.0f)) / 0.01f, 0,
                         MTXMODE_APPLY);
        Matrix_Scale(1.15f, 1.15f, 1.15f, MTXMODE_APPLY);
        CustomBodyDrawRoll(play, path("roll"),
                           !waterBall && ShipLua::MmForm::RollSpikesActive() ? path("roll_spikes") : nullptr,
                           waterBall ? nullptr : path("roll_energy_1"), waterBall ? nullptr : path("roll_energy_2"),
                           waterBall ? 0 : ShipLua::MmForm::RollChargeLevel(), !waterBall);
        *drawDefault = 0;
    } else if (ShipLua::MmForm::IsShielding() && shield) {
        skeletons->draw_ex(play, draw->actor, shield);
        *drawDefault = 0;
    }
    (void)player;
    return SHIP_NATIVE_OK;
}

bool AttachControl() {
    ShipOotBodyStateV1 state{};
    state.size = sizeof(state);
    if (body->get_state(&state) != SHIP_NATIVE_OK || !state.active)
        return false;
    if (control && state.body_generation == bodyGeneration)
        return true;
    control = 0;
    ClearAction();
    ShipOotBodyControllerV1 controller{};
    controller.size = sizeof(controller);
    controller.tick = BodyTick;
    controller.post_limb = PostLimb;
    controller.draw = BodyDraw;
    if (body->acquire_control(kOwner, state.body_generation, &controller, &control) != SHIP_NATIVE_OK)
        return false;
    bodyGeneration = state.body_generation;
    bodyState = state;
    ShipLua::MmForm::Services services{};
    services.startAnimation = StartAnimation;
    services.hasAnimation = HasAnimation;
    services.currentFrame = CurrentFrame;
    services.animationLastFrame = AnimationLastFrame;
    services.playMmSample = PlaySample;
    services.setOcarinaInstrument = SetInstrument;
    ShipLua::MmForm::Init(services);
    ShipLua::MmForm::SetBallEnabled(config.value("ball", true));
    ShipLua::MmForm::SetPunchEnabled(HasAnimation("punch_a"));
    ShipLua::MmForm::SetShieldEnabled(shield != 0);
    return AcquireAction();
}

ShipNativeStatus Enable() {
    if (!configured || !gPlayState)
        return SHIP_NATIVE_UNSUPPORTED;
    if (active)
        return AttachControl() ? SHIP_NATIVE_OK : SHIP_NATIVE_LIMIT;
    for (const auto& [name, path] : gCustomBodySpec.modelsPrefixed)
        if (!ResourceMgr_FileExists(path.c_str()))
            return SHIP_NATIVE_UNSUPPORTED;
    std::vector<ShipOotBodyAnimationV1> animations;
    std::vector<ShipOotBodySegmentV1> segments;
    const auto& anims = config.at("anims");
    for (auto it = anims.begin(); it != anims.end(); ++it) {
        ShipOotBodyAnimationV1 anim{};
        anim.size = sizeof(anim);
        anim.name = it.key().c_str();
        anim.path = it.value().get_ref<const std::string&>().c_str();
        if (config.contains("reverse_anims"))
            anim.reverse_default = config["reverse_anims"].value(it.key(), false);
        animations.push_back(anim);
    }
    if (config.contains("segments")) {
        for (auto it = config["segments"].begin(); it != config["segments"].end(); ++it) {
            ShipOotBodySegmentV1 segment{};
            segment.size = sizeof(segment);
            segment.segment = static_cast<uint32_t>(std::stoul(it.key()));
            segment.resource_path = it.value().get_ref<const std::string&>().c_str();
            segments.push_back(segment);
        }
    }
    ShipOotBodySpecV1 spec{};
    spec.size = sizeof(spec);
    spec.skeleton.size = sizeof(spec.skeleton);
    spec.skeleton.format = SHIP_OOT_SKELETON_LINK;
    spec.skeleton.skeleton_path = config.at("skeleton").get_ref<const std::string&>().c_str();
    const std::string defaultName = config.value("default_anim", std::string("idle"));
    spec.skeleton.initial_animation_path = anims.at(defaultName).get_ref<const std::string&>().c_str();
    const int16_t base[] = { -57, 3377, 0 };
    std::memcpy(spec.skeleton.base_translation, base, sizeof(base));
    spec.default_animation = defaultName.c_str();
    spec.animations = animations.data();
    spec.animation_count = static_cast<uint32_t>(animations.size());
    spec.segments = segments.data();
    spec.segment_count = static_cast<uint32_t>(segments.size());
    spec.ground_offset = config.value("ground_offset", -30.0f);
    auto status = body->set_body(kOwner, &spec);
    if (status != SHIP_NATIVE_OK)
        return status;
    active = true;
    if (config.contains("shield")) {
        ShipOotSkeletonSpecV2 shieldSpec{};
        shieldSpec.size = sizeof(shieldSpec);
        shieldSpec.format = SHIP_OOT_SKELETON_STANDARD;
        shieldSpec.skeleton_path = config["shield"].at("skeleton").get_ref<const std::string&>().c_str();
        shieldSpec.initial_animation_path = config["shield"].at("animation").get_ref<const std::string&>().c_str();
        status = skeletons->create_ex(kOwner, &shieldSpec, &shield);
        if (status != SHIP_NATIVE_OK) {
            Stop(gPlayState, false);
            return status;
        }
    }
    if (!AttachControl()) {
        Stop(gPlayState, false);
        return SHIP_NATIVE_LIMIT;
    }
    status = audio->set_voice_map(0x6800, 0x100, 0xC0);
    if (status != SHIP_NATIVE_OK) {
        Stop(gPlayState, false);
        return status;
    }
    MaskTransitionBeginReveal(GET_PLAYER(gPlayState));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Hook(void*, const ShipNativeHookCall* call) {
    if (!call || call->size < sizeof(*call) || !call->payload)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    const std::string point = call->point;
    if (point == LINKSPAN_OOT_HOOK_PLAY_DESTROY) {
        auto* event = static_cast<ShipOotPlayHookV1*>(call->payload);
        Stop(static_cast<PlayState*>(event->play_state), false);
    } else if (point == LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END) {
        auto* event = static_cast<ShipOotPlayerTickHookV1*>(call->payload);
        auto* player = static_cast<Player*>(event->player);
        auto* play = static_cast<PlayState*>(event->play_state);
        if (gMaskTransitionPhase != MaskTransitionPhase::None) {
            MaskTransitionUpdate(player);
            CutsceneUpdate(play);
            if (gMaskTransitionPhase == MaskTransitionPhase::None && !active) {
                MaskTransitionClearForcedMask(player);
                ClearAction();
            }
        }
        if (requested && !active)
            Enable();
        if (active) {
            AttachControl();
            AcquireAction();
        }
    } else if (point == LINKSPAN_OOT_HOOK_PLAYER_LEDGE_GRAB_ALLOWED) {
        if (active && config.value("block_ledge_grab", true))
            static_cast<ShipOotPlayerGateHookV1*>(call->payload)->allowed = 0;
    } else if (point == LINKSPAN_OOT_HOOK_PLAYER_MASK_DRAW) {
        if (gMaskTransitionPhase == MaskTransitionPhase::None && call->call_original)
            return call->call_original(call);
    } else if (point == LINKSPAN_OOT_HOOK_PLAYER_LIMB_DRAW) {
        auto* event = static_cast<ShipOotPlayerLimbHookV1*>(call->payload);
        auto* play = static_cast<PlayState*>(event->play_state);
        auto* player = static_cast<Player*>(event->actor);
        if (event->limb == PLAYER_LIMB_L_HAND)
            DrawMaskTransitionHandImpl(play, player);
        if (event->limb == PLAYER_LIMB_HEAD)
            DrawMaskTransitionHeadImpl(play, player);
    } else if (point == LINKSPAN_OOT_HOOK_PLAY_DRAW_END) {
        auto* event = static_cast<ShipOotPlayHookV1*>(call->payload);
        DrawMaskTransitionFlash(static_cast<PlayState*>(event->play_state));
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Reply(const nlohmann::json& value, ShipNativeWriteFn write, void* writer) {
    if (!write)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    const auto data = value.dump();
    return write(writer, data.data(), static_cast<uint32_t>(data.size()));
}
ShipNativeStatus SHIP_NATIVE_CALL Status(void*, const char*, uint32_t, ShipNativeWriteFn write, void* writer) {
    return Reply({ { "shell", true },
                   { "configured", configured },
                   { "active", active },
                   { "action", ShipLua::MmForm::CurrentActionName() },
                   { "gameplay_validated", false } },
                 write, writer);
}
ShipNativeStatus SHIP_NATIVE_CALL Configure(void*, const char* request, uint32_t length, ShipNativeWriteFn write,
                                            void* writer) {
    if (!request || length > SHIP_NATIVE_MAX_BYTES || active)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    try {
        auto spec = nlohmann::json::parse(request, request + length);
        if (!spec.is_object() || !spec.at("skeleton").is_string() || !spec.at("anims").is_object() ||
            spec["anims"].empty() || spec["anims"].size() > 256)
            return SHIP_NATIVE_INVALID_ARGUMENT;
        for (const auto& path : spec["anims"])
            if (!path.is_string())
                return SHIP_NATIVE_INVALID_ARGUMENT;
        const auto defaultName = spec.value("default_anim", std::string("idle"));
        if (!spec["anims"].contains(defaultName))
            return SHIP_NATIVE_INVALID_ARGUMENT;
        for (const char* field : { "ball", "water_void", "block_ledge_grab" })
            if (spec.contains(field) && !spec[field].is_boolean())
                return SHIP_NATIVE_INVALID_ARGUMENT;
        for (const char* field : { "ground_offset", "roll_offset" }) {
            if (!spec.contains(field))
                continue;
            if (!spec[field].is_number())
                return SHIP_NATIVE_INVALID_ARGUMENT;
            const float offset = spec[field].get<float>();
            if (!std::isfinite(offset) || std::fabs(offset) > 10000)
                return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        if (spec.contains("segments")) {
            if (!spec["segments"].is_object() || spec["segments"].size() > 15)
                return SHIP_NATIVE_INVALID_ARGUMENT;
            for (auto it = spec["segments"].begin(); it != spec["segments"].end(); ++it) {
                if (!it.value().is_string() || it.key().empty() ||
                    it.key().find_first_not_of("0123456789") != std::string::npos)
                    return SHIP_NATIVE_INVALID_ARGUMENT;
                const auto number = std::stoul(it.key());
                if (number < 1 || number > 15)
                    return SHIP_NATIVE_INVALID_ARGUMENT;
            }
        }
        if (spec.contains("reverse_anims")) {
            if (!spec["reverse_anims"].is_object())
                return SHIP_NATIVE_INVALID_ARGUMENT;
            for (auto it = spec["reverse_anims"].begin(); it != spec["reverse_anims"].end(); ++it)
                if (!it.value().is_boolean() || !spec["anims"].contains(it.key()))
                    return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        if (spec.contains("shield") && (!spec["shield"].is_object() || !spec["shield"].at("skeleton").is_string() ||
                                        !spec["shield"].at("animation").is_string()))
            return SHIP_NATIVE_INVALID_ARGUMENT;
        FormsSpec staged;
        staged.waterVoid = spec.value("water_void", true);
        for (auto it = spec["anims"].begin(); it != spec["anims"].end(); ++it)
            staged.animsPrefixed[it.key()] = "__OTR__" + it.value().get<std::string>();
        if (spec.contains("models")) {
            if (!spec["models"].is_object())
                return SHIP_NATIVE_INVALID_ARGUMENT;
            for (auto it = spec["models"].begin(); it != spec["models"].end(); ++it)
                staged.modelsPrefixed[it.key()] = "__OTR__" + it.value().get<std::string>();
        }
        config = std::move(spec);
        gCustomBodySpec = std::move(staged);
        configured = true;
        return Reply({ { "configured", true } }, write, writer);
    } catch (...) { return SHIP_NATIVE_INVALID_ARGUMENT; }
}
ShipNativeStatus SHIP_NATIVE_CALL SetEnabled(void*, const char* request, uint32_t length, ShipNativeWriteFn write,
                                             void* writer) {
    try {
        if (!request || length > SHIP_NATIVE_MAX_BYTES)
            return SHIP_NATIVE_INVALID_ARGUMENT;
        const bool enabled = nlohmann::json::parse(request, request + length).at("enabled").get<bool>();
        if (!enabled) {
            const bool reveal = gMaskTransitionPhase == MaskTransitionPhase::Conceal && gMaskTransitionRemoving;
            Stop(gPlayState, true, reveal);
            if (reveal) {
                MaskTransitionBeginReveal(GET_PLAYER(gPlayState));
                AcquireAction();
            }
            return Reply({ { "active", false } }, write, writer);
        }
        const auto status = Enable();
        requested = status == SHIP_NATIVE_OK;
        if (status != SHIP_NATIVE_OK)
            return status;
        return Reply({ { "active", true } }, write, writer);
    } catch (...) {
        Stop(gPlayState, true);
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
}
ShipNativeStatus SHIP_NATIVE_CALL MaskOn(void*, const char*, uint32_t, ShipNativeWriteFn write, void* writer) {
    auto* play = gPlayState;
    auto* player = play ? GET_PLAYER(play) : nullptr;
    if (!player || active || gMaskTransitionPhase != MaskTransitionPhase::None ||
        !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) || std::fabs(player->linearVelocity) >= 0.5f ||
        player->csAction != 0 || play->msgCtx.msgMode != MSGMODE_NONE ||
        (player->stateFlags1 &
         (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_GETTING_ITEM |
          PLAYER_STATE1_TALKING | PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_IN_WATER)))
        return SHIP_NATIVE_UNSUPPORTED;
    ShipOotPlayerActionStateV1 state{};
    state.size = sizeof(state);
    if (movement->get_action_state(&state) != SHIP_NATIVE_OK || !(state.flags & SHIP_OOT_ACTION_STATE_IDLE))
        return SHIP_NATIVE_UNSUPPORTED;
    auto* animation = ResourceMgr_LoadAnimByName(kMaskOnAnimationPath);
    if (!animation || !ResourceMgr_FileExists(kMmGoronMaskDListPath) ||
        !ResourceMgr_LoadAnimByName(kMaskHensinAnimationPath))
        return SHIP_NATIVE_UNSUPPORTED;
    if (!AcquireAction())
        return SHIP_NATIVE_LIMIT;
    previousMask = player->currentMask;
    transitionMaskOwned = true;
    player->currentMask = PLAYER_MASK_GORON;
    LinkAnimation_Change(play, &player->skelAnime,
                         reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(kMaskOnAnimationPath)), 1, 0,
                         Animation_GetLastFrame(reinterpret_cast<LinkAnimationHeader*>(animation)), ANIMMODE_ONCE, 0);
    const int frames = MaskTransitionStart(player, play);
    return Reply({ { "frames", frames } }, write, writer);
}

ShipNativeStatus SHIP_NATIVE_CALL PlayBodyAnimation(void*, const char* request, uint32_t length,
                                                    ShipNativeWriteFn write, void* writer) {
    if (!request || length > SHIP_NATIVE_MAX_BYTES || !active || !gPlayState)
        return SHIP_NATIVE_UNSUPPORTED;
    try {
        const auto args = nlohmann::json::parse(request, request + length);
        const auto name = args.at("name").get<std::string>();
        const auto mode = args.value("mode", std::string("once"));
        const float speed = args.value("speed", 1.0f);
        if (!std::isfinite(speed) || speed <= 0 || speed > 4 ||
            (mode != "once" && mode != "loop" && mode != "reverse_once"))
            return SHIP_NATIVE_INVALID_ARGUMENT;
        if (!StartAnimation(name.c_str(), speed, mode != "loop", mode == "reverse_once"))
            return SHIP_NATIVE_UNSUPPORTED;
        int frames = 0;
        if (name == "mask_off" && mode == "once") {
            frames = static_cast<int>(std::ceil(AnimationLastFrame(name.c_str()) / speed)) + 1;
            MaskTransitionStartRemoval(GET_PLAYER(gPlayState), frames);
        }
        return Reply({ { "frames", frames }, { "playing", true } }, write, writer);
    } catch (...) { return SHIP_NATIVE_INVALID_ARGUMENT; }
}

ShipNativeStatus SHIP_NATIVE_CALL SetSegment(void*, const char* request, uint32_t length, ShipNativeWriteFn write,
                                             void* writer) {
    if (!control || !request || length > SHIP_NATIVE_MAX_BYTES)
        return SHIP_NATIVE_UNSUPPORTED;
    try {
        const auto args = nlohmann::json::parse(request, request + length);
        const uint32_t segment = args.at("segment").get<uint32_t>();
        const auto path = args.at("path").get<std::string>();
        const auto status = body->set_segment(control, segment, path.c_str());
        if (status != SHIP_NATIVE_OK)
            return status;
        return Reply({ { "applied", true } }, write, writer);
    } catch (...) { return SHIP_NATIVE_INVALID_ARGUMENT; }
}

template <class T> bool Service(const char* name, uint32_t version, const T*& out) {
    out = static_cast<const T*>(runtime->get_service(runtime->context, name, version, sizeof(T)));
    return out && out->size >= sizeof(T);
}
template <ShipNativeFunction Function>
ShipNativeStatus SHIP_NATIVE_CALL SafeNative(void* user, const char* request, uint32_t length, ShipNativeWriteFn write,
                                             void* writer) {
    try {
        return Function(user, request, length, write, writer);
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}
ShipNativeStatus SHIP_NATIVE_CALL SafeHook(void* user, const ShipNativeHookCall* call) {
    try {
        return Hook(user, call);
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}
ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* rt, void** instance) {
    if (!instance)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    *instance = nullptr;
    if (!rt || rt->size < sizeof(*rt) || rt->abi_major != 1 || rt->abi_minor < 4 || !rt->get_service ||
        !rt->register_function || !rt->register_hook || !rt->unregister_hook || !rt->resolve_symbol)
        return SHIP_NATIVE_UNSUPPORTED;
    if (runtime)
        return SHIP_NATIVE_LIMIT;
    runtime = rt;
    if (!Service(LINKSPAN_OOT_PLAYER_BODY_SERVICE, 1, body) || !Service(LINKSPAN_OOT_MOVEMENT_SERVICE, 3, movement) ||
        !Service(LINKSPAN_OOT_COLLIDERS_SERVICE, 2, colliders) ||
        !Service(LINKSPAN_OOT_SKELETONS_SERVICE, 2, skeletons) || !Service(LINKSPAN_OOT_CAMERA_SERVICE, 2, camera) ||
        !Service(LINKSPAN_OOT_RENDER_SERVICE, 3, render) || !Service(LINKSPAN_MM_AUDIO_SERVICE, 1, audio))
        return SHIP_NATIVE_UNSUPPORTED;
    for (const char* layout :
         { body->layout_id, movement->layout_id, colliders->layout_id, skeletons->layout_id, camera->layout_id })
        if (!layout || std::strcmp(layout, MM_FORMS_LAYOUT_ID) != 0)
            return SHIP_NATIVE_UNSUPPORTED;
    if (!body->set_body || !body->get_state || !body->acquire_control || !body->release_control ||
        !body->start_animation || !body->animation_info || !body->set_root_motion || !body->set_segment ||
        !movement->get_action_state || !movement->acquire_action || !movement->release_action ||
        !movement->restore_player_animation || !colliders->acquire_player_quads || !colliders->release_player_quads ||
        !colliders->submit_player_quad || !colliders->disable_player_quad || !colliders->read_player_quad_hits ||
        !skeletons->create_ex || !skeletons->destroy || !skeletons->update || !skeletons->draw_ex ||
        !skeletons->play_ex || !camera->acquire_ex || !camera->release || !camera->is_owned || !camera->get_view_ex ||
        !camera->set_view_ex || !render->draw_display_list || !render->matrix_push || !render->matrix_pop ||
        !render->matrix_scale || !audio->play_sample || !audio->play_sfx_id || !audio->set_voice_map ||
        !audio->clear_voice_map)
        return SHIP_NATIVE_UNSUPPORTED;
    if (!MMFormsGame::Bind(rt))
        return SHIP_NATIVE_UNSUPPORTED;
    const struct {
        const char* name;
        ShipNativeFunction fn;
    } functions[] = {
        { "status", SafeNative<Status> },
        { "configure", SafeNative<Configure> },
        { "set_enabled", SafeNative<SetEnabled> },
        { "play_mask_on_animation", SafeNative<MaskOn> },
        { "play_body_animation", SafeNative<PlayBodyAnimation> },
        { "set_body_segment", SafeNative<SetSegment> },
    };
    for (const auto& fn : functions)
        if (rt->register_function(rt->context, fn.name, fn.fn, nullptr) != SHIP_NATIVE_OK)
            return SHIP_NATIVE_FAILURE;
    const struct {
        const char* point;
        uint32_t size, mode, phase;
    } hooks[] = {
        { LINKSPAN_OOT_HOOK_PLAYER_UPDATE_END, sizeof(ShipOotPlayerTickHookV1), SHIP_NATIVE_HOOK_OBSERVE,
          SHIP_NATIVE_HOOK_AFTER },
        { LINKSPAN_OOT_HOOK_PLAY_DESTROY, sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE,
          SHIP_NATIVE_HOOK_BEFORE },
        { LINKSPAN_OOT_HOOK_PLAYER_LIMB_DRAW, sizeof(ShipOotPlayerLimbHookV1), SHIP_NATIVE_HOOK_OBSERVE,
          SHIP_NATIVE_HOOK_AFTER },
        { LINKSPAN_OOT_HOOK_PLAYER_MASK_DRAW, sizeof(ShipOotPlayerMaskDrawHookV1), SHIP_NATIVE_HOOK_REPLACE,
          SHIP_NATIVE_HOOK_BEFORE },
        { LINKSPAN_OOT_HOOK_PLAYER_LEDGE_GRAB_ALLOWED, sizeof(ShipOotPlayerGateHookV1), SHIP_NATIVE_HOOK_TRANSFORM,
          SHIP_NATIVE_HOOK_BEFORE },
        { LINKSPAN_OOT_HOOK_PLAY_DRAW_END, sizeof(ShipOotPlayHookV1), SHIP_NATIVE_HOOK_OBSERVE,
          SHIP_NATIVE_HOOK_AFTER },
    };
    for (const auto& hook : hooks) {
        ShipNativeHookSpec spec{};
        spec.size = sizeof(spec);
        spec.point = hook.point;
        spec.version = 1;
        spec.minimum_payload_size = hook.size;
        spec.mode = hook.mode;
        spec.phase = hook.phase;
        spec.callback = SafeHook;
        uint64_t handle = 0;
        const auto status = rt->register_hook(rt->context, &spec, &handle);
        if (status != SHIP_NATIVE_OK)
            return status;
        hookHandles.push_back(handle);
    }
    *instance = const_cast<ShipNativeRuntime*>(rt);
    return SHIP_NATIVE_OK;
}
void SHIP_NATIVE_CALL Shutdown(void*) {
    if (!runtime)
        return;
    if (body && movement && colliders && skeletons && camera && audio && MMFormsGame::Ready())
        Stop(gPlayState, true);
    for (auto it = hookHandles.rbegin(); it != hookHandles.rend(); ++it)
        runtime->unregister_hook(runtime->context, *it);
    hookHandles.clear();
    configured = false;
    config = {};
    gCustomBodySpec = {};
    MMFormsGame::Reset();
    runtime = nullptr;
    body = nullptr;
    movement = nullptr;
    colliders = nullptr;
    skeletons = nullptr;
    camera = nullptr;
    render = nullptr;
    audio = nullptr;
}
} // namespace

void DisablePunchQuads() {
    if (!quads)
        return;
    colliders->disable_player_quad(quads, SHIP_OOT_QUAD_ATTACK_0);
    colliders->disable_player_quad(quads, SHIP_OOT_QUAD_ATTACK_1);
}
void SubmitPunchQuad(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d, unsigned damage) {
    if (!quads)
        return;
    ShipOotPlayerQuadSpecV2 spec{};
    spec.size = sizeof(spec);
    const Vec3f points[] = { a, b, c, d };
    std::memcpy(spec.vertices, points, sizeof(points));
    spec.col_type = COLTYPE_NONE;
    spec.at_flags = AT_ON | AT_TYPE_PLAYER;
    spec.elem_type = ELEMTYPE_UNK0;
    spec.touch_dmg_flags = DMG_HAMMER_SWING;
    spec.touch_damage = static_cast<uint8_t>(damage);
    spec.touch_flags = TOUCH_ON | TOUCH_NEAREST;
    colliders->submit_player_quad(quads, SHIP_OOT_QUAD_ATTACK_0, &spec);
    colliders->disable_player_quad(quads, SHIP_OOT_QUAD_ATTACK_1);
}
} // namespace MMForms

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{ sizeof(descriptor), 1, 4, MMForms::Init, MMForms::Shutdown };
    return &descriptor;
}
