// Demo de linkspan.oot.world/colliders (OOT-CORE-004): em gameplay, cria à frente do Link um alvo
// (tipo de ator do mod) apoiado no chão por raycast, com cilindro AC/OC. Golpes de espada fazem o
// alvo girar e contam acertos; encostar no alvo empurra o Link. A cada segundo registra chão, água,
// linha e parede à frente do Link.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "oot_actors.h"
#include "oot_colliders.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_layout_id.h"
#include "oot_render.h"
#include "oot_world.h"
#include "z64.h"

namespace {

constexpr const char* ACTOR_NAME = "linkspan-demo.target";
constexpr const char* MODEL = "objects/gameplay_keep/gHeartPieceInteriorDL";
constexpr double PI = 3.14159265358979323846;

struct Target {
    Actor actor;
    uint64_t collider;
    int16_t spin;
    int16_t cooldown;
};

struct Demo {
    const ShipOotEngineV1* engine = nullptr;
    const ShipOotActorsV1* actors = nullptr;
    const ShipOotRenderV1* render = nullptr;
    const ShipOotWorldV1* world = nullptr;
    const ShipOotCollidersV1* colliders = nullptr;
    int16_t actorId = -1;
    bool spawned = false;
    uint32_t lastPlayFrames = 0;
    uint32_t frames = 0;
    uint32_t hits = 0;
    uint32_t touches = 0;
    uint32_t knockbacks = 0;
    uint32_t colliderFailures = 0;
    uint32_t lastDmgFlags = 0;
    char probe[160] = "sem consulta";
};

int16_t YawTo(const Vec3f& from, const Vec3f& to) {
    return static_cast<int16_t>(std::atan2(to.x - from.x, to.z - from.z) * (32768.0 / PI));
}

void SHIP_NATIVE_CALL TargetInit(void* user, void* actor, void*) {
    auto& demo = *static_cast<Demo*>(user);
    auto* target = static_cast<Target*>(actor);
    target->actor.scale = { 0.06f, 0.06f, 0.06f };
    target->actor.colChkInfo.mass = MASS_IMMOVABLE;
    target->spin = 0;
    target->cooldown = 0;
    target->collider = 0;
    ShipOotWorldHitV1 floor{ sizeof(ShipOotWorldHitV1) };
    const auto& pos = target->actor.world.pos;
    if (demo.world->raycast_floor(pos.x, pos.y + 50.0f, pos.z, &floor) == SHIP_NATIVE_OK && floor.hit) {
        target->actor.world.pos.y = floor.pos[1];
        target->actor.home.pos.y = floor.pos[1];
    }
    ShipOotCylinderSpecV1 spec{};
    spec.size = sizeof(spec);
    spec.actor = actor;
    spec.col_type = COLTYPE_NONE;
    spec.ac_flags = AC_ON | AC_TYPE_PLAYER;
    spec.oc1_flags = OC1_ON | OC1_TYPE_ALL;
    spec.oc2_flags = OC2_TYPE_2;
    spec.elem_type = ELEMTYPE_UNK0;
    spec.bump_dmg_flags = 0xFFCFFFFF;
    spec.bump_flags = BUMP_ON;
    spec.oc_elem_flags = OCELEM_ON;
    spec.radius = 25;
    spec.height = 60;
    if (demo.colliders->create_cylinder(&spec, &target->collider) != SHIP_NATIVE_OK) {
        ++demo.colliderFailures;
    }
}

void SHIP_NATIVE_CALL TargetDestroy(void* user, void* actor, void*) {
    auto* target = static_cast<Target*>(actor);
    if (target->collider) {
        static_cast<Demo*>(user)->colliders->destroy(target->collider);
        target->collider = 0;
    }
}

void SHIP_NATIVE_CALL TargetUpdate(void* user, void* actor, void*) {
    auto& demo = *static_cast<Demo*>(user);
    auto* target = static_cast<Target*>(actor);
    if (!target->collider) {
        return;
    }
    ShipOotColliderHitsV1 hits{ sizeof(ShipOotColliderHitsV1) };
    if (demo.colliders->read_hits(target->collider, &hits) == SHIP_NATIVE_OK) {
        auto* player = static_cast<Player*>(demo.engine->get_player());
        if (hits.ac_hit) {
            ++demo.hits;
            demo.lastDmgFlags = hits.ac_dmg_flags;
            target->spin = 30;
        }
        if (hits.oc_hit && player && hits.oc_actor == &player->actor) {
            ++demo.touches;
            if (target->cooldown == 0) {
                const int16_t yaw = YawTo(target->actor.world.pos, player->actor.world.pos);
                if (demo.colliders->knockback_player(actor, 8.0f, yaw, 5.0f, 0, 0) == SHIP_NATIVE_OK) {
                    ++demo.knockbacks;
                }
                target->cooldown = 30;
            }
        }
    }
    if (target->cooldown > 0) {
        --target->cooldown;
    }
    if (target->spin > 0) {
        --target->spin;
        target->actor.shape.rot.y += 0x1800;
    } else {
        target->actor.shape.rot.y += 0x0200;
    }
    demo.colliders->submit(target->collider);
}

void SHIP_NATIVE_CALL TargetDraw(void* user, void*, void* play) {
    auto& demo = *static_cast<Demo*>(user);
    demo.render->matrix_translate(0.0f, 500.0f, 0.0f);
    demo.render->draw_display_list(play, MODEL, LINKSPAN_OOT_RENDER_TRANSLUCENT);
}

void Probe(Demo& demo, const Player& player) {
    const auto& pos = player.actor.world.pos;
    const double angle = player.actor.shape.rot.y * (PI / 32768.0);
    const float dx = static_cast<float>(std::sin(angle));
    const float dz = static_cast<float>(std::cos(angle));
    ShipOotWorldHitV1 floor{ sizeof(ShipOotWorldHitV1) };
    demo.world->raycast_floor(pos.x, pos.y + 20.0f, pos.z, &floor);
    float water = 0.0f;
    const bool inWater = demo.world->water_surface(pos.x, pos.z, &water) == SHIP_NATIVE_OK;
    const float eye[3]{ pos.x, pos.y + 30.0f, pos.z };
    const float far[3]{ pos.x + dx * 1000.0f, pos.y + 30.0f, pos.z + dz * 1000.0f };
    ShipOotWorldHitV1 line{ sizeof(ShipOotWorldHitV1) };
    demo.world->line_test(eye, far, LINKSPAN_OOT_WORLD_LINE_WALL, &line);
    const float lineDistance =
        line.hit ? std::sqrt((line.pos[0] - eye[0]) * (line.pos[0] - eye[0]) + (line.pos[2] - eye[2]) * (line.pos[2] - eye[2]))
                 : -1.0f;
    const float from[3]{ pos.x, pos.y, pos.z };
    const float step[3]{ pos.x + dx * 30.0f, pos.y, pos.z + dz * 30.0f };
    ShipOotWorldHitV1 wall{ sizeof(ShipOotWorldHitV1) };
    demo.world->wall_check(from, step, 20.0f, 26.0f, &wall);
    std::snprintf(demo.probe, sizeof(demo.probe),
                  "chao=%d y=%.1f n.y=%.2f tipo=%u agua=%s linha=%.0f parede30=%d", floor.hit, floor.pos[1],
                  floor.normal[1], floor.floor_type, inWater ? "sim" : "nao", lineDistance, wall.hit);
}

ShipNativeStatus SHIP_NATIVE_CALL OnFrame(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    const auto* payload = static_cast<const ShipOotPlayHookV1*>(call->payload);
    auto* player = static_cast<Player*>(demo.engine->get_player());
    if (!player) {
        return SHIP_NATIVE_OK;
    }
    // A PlayState nova pode reusar o endereço da anterior; o contador de frames dela recomeça.
    const auto* play = static_cast<PlayState*>(payload->play_state);
    if (play->state.frames < demo.lastPlayFrames) {
        demo.spawned = false;
    }
    demo.lastPlayFrames = play->state.frames;
    ++demo.frames;
    if (demo.frames % 20 == 0) {
        Probe(demo, *player);
    }
    if (!demo.spawned && play->state.frames > 40) {
        const auto& pos = player->actor.world.pos;
        const int16_t yaw = player->actor.shape.rot.y;
        const double angle = yaw * (PI / 32768.0);
        const float x = pos.x + static_cast<float>(std::sin(angle) * 55.0);
        const float z = pos.z + static_cast<float>(std::cos(angle) * 55.0);
        if (demo.engine->spawn_actor(demo.actorId, x, pos.y, z, 0, static_cast<int16_t>(yaw + 0x8000), 0, 0)) {
            demo.spawned = true;
        }
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Stats(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                        void* writer) {
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& demo = *static_cast<Demo*>(user);
    char text[320];
    const int count = std::snprintf(text, sizeof(text),
                                    "alvo=%s hits=%u dmg=0x%X toques=%u empurroes=%u colfail=%u | %s",
                                    demo.spawned ? "sim" : "nao", demo.hits, demo.lastDmgFlags, demo.touches,
                                    demo.knockbacks, demo.colliderFailures, demo.probe);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(text)) {
        return SHIP_NATIVE_FAILURE;
    }
    return write(writer, text, static_cast<uint32_t>(count));
}

const void* Service(const ShipNativeRuntime* runtime, const char* name, uint32_t version, uint32_t size) {
    return runtime->get_service(runtime->context, name, version, size);
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || !instance || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_minor < 2 ||
        !runtime->get_service || !runtime->register_function || !runtime->register_hook) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto* engine = static_cast<const ShipOotEngineV1*>(
        Service(runtime, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    if (!engine || !engine->layout_id || std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID)) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* demo = new (std::nothrow) Demo;
    if (!demo) {
        return SHIP_NATIVE_FAILURE;
    }
    demo->engine = engine;
    demo->actors = static_cast<const ShipOotActorsV1*>(
        Service(runtime, LINKSPAN_OOT_ACTORS_SERVICE, LINKSPAN_OOT_ACTORS_VERSION, sizeof(ShipOotActorsV1)));
    demo->render = static_cast<const ShipOotRenderV1*>(
        Service(runtime, LINKSPAN_OOT_RENDER_SERVICE, LINKSPAN_OOT_RENDER_VERSION, sizeof(ShipOotRenderV1)));
    demo->world = static_cast<const ShipOotWorldV1*>(
        Service(runtime, LINKSPAN_OOT_WORLD_SERVICE, LINKSPAN_OOT_WORLD_VERSION, sizeof(ShipOotWorldV1)));
    demo->colliders = static_cast<const ShipOotCollidersV1*>(Service(
        runtime, LINKSPAN_OOT_COLLIDERS_SERVICE, LINKSPAN_OOT_COLLIDERS_VERSION, sizeof(ShipOotCollidersV1)));
    if (!demo->actors || !demo->render || !demo->world || !demo->colliders) {
        delete demo;
        return SHIP_NATIVE_UNSUPPORTED;
    }
    *instance = demo;
    const ShipOotActorTypeSpecV1 actorSpec{ sizeof(ShipOotActorTypeSpecV1),
                                            ACTOR_NAME,
                                            ACTORCAT_PROP,
                                            ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED,
                                            OBJECT_GAMEPLAY_KEEP,
                                            sizeof(Target),
                                            TargetInit,
                                            TargetDestroy,
                                            TargetUpdate,
                                            TargetDraw,
                                            demo };
    ShipNativeStatus status = demo->actors->register_actor_type(&actorSpec, &demo->actorId);
    if (status == SHIP_NATIVE_OK) {
        const ShipNativeHookSpec spec{ sizeof(ShipNativeHookSpec), LINKSPAN_OOT_HOOK_PLAY_UPDATE,
                                       LINKSPAN_OOT_HOOKS_VERSION, sizeof(ShipOotPlayHookV1),
                                       SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_AFTER, 0, OnFrame, demo };
        uint64_t handle = 0;
        status = runtime->register_hook(runtime->context, &spec, &handle);
    }
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    return runtime->register_function(runtime->context, "stats", Stats, demo);
}

// Hooks já saíram; o unregister mata os alvos, cujo destroy do host não chama mais o mod,
// e o host libera os colliders no fim da cena ou no reset.
void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* demo = static_cast<Demo*>(instance);
    if (demo->actorId >= 0) {
        demo->actors->unregister_actor_type(demo->actorId);
    }
    delete demo;
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{ sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                  Shutdown };
    return &descriptor;
}
