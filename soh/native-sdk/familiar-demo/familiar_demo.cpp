// Demo do slice D4 (OOT-CORE-005B): item sintético com modelo animado e collider. Registra o item
// "linkspan-demo.keese-whistle" e o tipo de ator "linkspan-demo.keese-familiar". Em gameplay, se
// nenhum botão C tem o item, o Link o recebe por give_item e o receive o equipa. Apertar o botão
// solta um Keese (esqueleto e animação do object_firefly, via linkspan.oot.skeletons) que voa à
// frente do Link com um cilindro AT de espada Kokiri: corta arbustos e fere inimigos. Ao acertar,
// bate as asas mais rápido uma vez (animação ONCE) e some; sem acerto, some depois de 3 segundos.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "oot_actors.h"
#include "oot_colliders.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_items.h"
#include "oot_layout_id.h"
#include "oot_skeletons.h"
#include "z64.h"

namespace {

constexpr const char* ITEM_NAME = "linkspan-demo.keese-whistle";
constexpr const char* ACTOR_NAME = "linkspan-demo.keese-familiar";
constexpr const char* ICON = "textures/icon_item_static/gItemIconBoomerangTex";
constexpr const char* GET_MODEL = "objects/gameplay_keep/gHeartPieceInteriorDL";
constexpr const char* SKELETON = "objects/object_firefly/gKeeseSkeleton";
constexpr const char* FLY = "objects/object_firefly/gKeeseFlyAnim";
constexpr const char* MESSAGE = "You got the %rKeese Whistle%w!&Press it on a C button to send&a Keese familiar ahead.";
constexpr uint8_t NO_ITEM = 0xFF;
constexpr uint32_t GIVE_DELAY_FRAMES = 300;
constexpr int16_t LIFETIME = 90;
constexpr float SPEED = 7.0f;
constexpr double PI = 3.14159265358979323846;

struct Familiar {
    Actor actor;
    uint64_t skeleton;
    uint64_t collider;
    int16_t timer;
    uint8_t hit;
};

struct Demo {
    const ShipOotEngineV1* engine = nullptr;
    const ShipOotItemsV2* items = nullptr;
    const ShipOotActorsV1* actors = nullptr;
    const ShipOotSkeletonsV1* skeletons = nullptr;
    const ShipOotCollidersV1* colliders = nullptr;
    uint8_t item = NO_ITEM;
    int16_t actorId = -1;
    uint32_t uses = 0;
    uint32_t spawned = 0;
    uint32_t skeletonFailures = 0;
    uint32_t colliderFailures = 0;
    uint32_t draws = 0;
    uint32_t drawFailures = 0;
    uint32_t hits = 0;
    uint32_t onceFinished = 0;
    uint32_t destroyed = 0;
    int32_t lastHitActor = -1;
    float lastFrame = 0.0f;
    float lastEnd = 0.0f;
    char placement[64] = "nenhum";
    uint32_t gives = 0;
    uint32_t giveRefused = 0;
    uint32_t received = 0;
    uint32_t lastPlayFrames = 0;
    bool offered = false;
};

void SHIP_NATIVE_CALL FamiliarInit(void* user, void* actor, void*) {
    auto& demo = *static_cast<Demo*>(user);
    auto* familiar = static_cast<Familiar*>(actor);
    familiar->actor.scale = { 0.008f, 0.008f, 0.008f };
    familiar->timer = LIFETIME;
    familiar->hit = 0;
    familiar->skeleton = 0;
    familiar->collider = 0;
    familiar->actor.speedXZ = SPEED;
    if (demo.skeletons->create(SKELETON, FLY, &familiar->skeleton) != SHIP_NATIVE_OK) {
        ++demo.skeletonFailures;
    } else {
        demo.skeletons->play_animation(familiar->skeleton, FLY, 1.5f, LINKSPAN_OOT_ANIM_LOOP, 0.0f);
    }
    ShipOotCylinderSpecV1 spec{};
    spec.size = sizeof(spec);
    spec.actor = actor;
    spec.col_type = COLTYPE_NONE;
    spec.at_flags = AT_ON | AT_TYPE_PLAYER;
    spec.elem_type = ELEMTYPE_UNK2;
    spec.touch_dmg_flags = DMG_SLASH_KOKIRI;
    spec.touch_damage = 1;
    spec.touch_flags = TOUCH_ON | TOUCH_SFX_NORMAL;
    spec.radius = 18;
    spec.height = 30;
    spec.y_shift = -10;
    if (demo.colliders->create_cylinder(&spec, &familiar->collider) != SHIP_NATIVE_OK) {
        ++demo.colliderFailures;
    }
    ++demo.spawned;
}

void SHIP_NATIVE_CALL FamiliarDestroy(void* user, void* actor, void*) {
    auto& demo = *static_cast<Demo*>(user);
    auto* familiar = static_cast<Familiar*>(actor);
    if (familiar->skeleton) {
        demo.skeletons->destroy(familiar->skeleton);
        familiar->skeleton = 0;
    }
    if (familiar->collider) {
        demo.colliders->destroy(familiar->collider);
        familiar->collider = 0;
    }
    ++demo.destroyed;
}

void SHIP_NATIVE_CALL FamiliarUpdate(void* user, void* actor, void*) {
    auto& demo = *static_cast<Demo*>(user);
    auto* familiar = static_cast<Familiar*>(actor);
    if (familiar->skeleton) {
        uint8_t finished = 0;
        demo.skeletons->update(familiar->skeleton, &finished);
        demo.skeletons->get_frame(familiar->skeleton, &demo.lastFrame, &demo.lastEnd);
        if (finished && familiar->hit) {
            ++demo.onceFinished;
            demo.engine->kill_actor(actor);
            return;
        }
    }
    if (familiar->collider) {
        ShipOotColliderHitsV1 hits{ sizeof(ShipOotColliderHitsV1) };
        if (!familiar->hit && demo.colliders->read_hits(familiar->collider, &hits) == SHIP_NATIVE_OK && hits.at_hit) {
            ++demo.hits;
            familiar->hit = 1;
            familiar->actor.speedXZ = 0.0f;
            demo.lastHitActor = hits.at_actor ? static_cast<Actor*>(hits.at_actor)->id : -1;
            if (familiar->skeleton) {
                demo.skeletons->play_animation(familiar->skeleton, FLY, 3.0f, LINKSPAN_OOT_ANIM_ONCE, 4.0f);
            }
        }
        if (!familiar->hit) {
            demo.colliders->submit(familiar->collider);
        }
    }
    // Voa reto na direção em que nasceu, oscilando um pouco na altura.
    const double angle = familiar->actor.world.rot.y * (PI / 32768.0);
    familiar->actor.world.pos.x += static_cast<float>(std::sin(angle) * familiar->actor.speedXZ);
    familiar->actor.world.pos.z += static_cast<float>(std::cos(angle) * familiar->actor.speedXZ);
    familiar->actor.world.pos.y += static_cast<float>(std::sin(familiar->timer * 0.3) * 1.5);
    familiar->actor.shape.rot.y = familiar->actor.world.rot.y;
    if (--familiar->timer <= 0 && !familiar->hit) {
        demo.engine->kill_actor(actor);
    }
}

void SHIP_NATIVE_CALL FamiliarDraw(void* user, void* actor, void* play) {
    auto& demo = *static_cast<Demo*>(user);
    auto* familiar = static_cast<Familiar*>(actor);
    if (familiar->skeleton && demo.skeletons->draw(play, familiar->skeleton) == SHIP_NATIVE_OK) {
        ++demo.draws;
    } else {
        ++demo.drawFailures;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL UseWhistle(void* user, uint8_t, uint8_t) {
    auto& demo = *static_cast<Demo*>(user);
    auto* player = static_cast<Player*>(demo.engine->get_player());
    if (!player || demo.actorId < 0) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    ++demo.uses;
    const auto& pos = player->actor.world.pos;
    const int16_t yaw = player->actor.shape.rot.y;
    const double angle = yaw * (PI / 32768.0);
    const float x = pos.x + static_cast<float>(std::sin(angle) * 40.0);
    const float z = pos.z + static_cast<float>(std::cos(angle) * 40.0);
    return demo.engine->spawn_actor(demo.actorId, x, pos.y + 25.0f, z, 0, yaw, 0, 0) ? SHIP_NATIVE_OK
                                                                                     : SHIP_NATIVE_FAILURE;
}

uint8_t ButtonWith(const Demo& demo, uint8_t item) {
    for (uint8_t button = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT; ++button) {
        uint8_t current = NO_ITEM;
        demo.items->get_button_item(button, &current);
        if (current == item) {
            return button;
        }
    }
    return 0;
}

// Botão vazio; senão o primeiro com item vanilla, na ordem C-Down, C-Left, C-Right, para não
// tirar o item de outro mod de um botão.
uint8_t ChooseButton(const Demo& demo) {
    if (const uint8_t empty = ButtonWith(demo, NO_ITEM)) {
        return empty;
    }
    for (const uint8_t button : { LINKSPAN_OOT_ITEMS_BUTTON_C_DOWN, LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT,
                                  LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT }) {
        uint8_t current = NO_ITEM;
        demo.items->get_button_item(button, &current);
        if (current < LINKSPAN_OOT_ITEMS_FIRST_ID) {
            return button;
        }
    }
    return LINKSPAN_OOT_ITEMS_BUTTON_C_DOWN;
}

ShipNativeStatus SHIP_NATIVE_CALL OnReceive(void* user, uint8_t item) {
    auto& demo = *static_cast<Demo*>(user);
    ++demo.received;
    const uint8_t button = ChooseButton(demo);
    const ShipNativeStatus status = demo.items->set_button_item(button, item);
    std::snprintf(demo.placement, sizeof(demo.placement), "recebido no C%u status=%u", button, status);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL OnFrame(void* user, const ShipNativeHookCall* call) {
    auto& demo = *static_cast<Demo*>(user);
    const auto* play = static_cast<PlayState*>(static_cast<const ShipOotPlayHookV1*>(call->payload)->play_state);
    if (!demo.engine->get_player()) {
        return SHIP_NATIVE_OK;
    }
    if (play->state.frames < demo.lastPlayFrames) {
        demo.offered = false;
    }
    demo.lastPlayFrames = play->state.frames;
    if (demo.offered || play->state.frames < GIVE_DELAY_FRAMES || play->state.frames % 20 != 0) {
        return SHIP_NATIVE_OK;
    }
    if (const uint8_t button = ButtonWith(demo, demo.item)) {
        std::snprintf(demo.placement, sizeof(demo.placement), "restaurado no C%u", button);
        demo.offered = true;
        return SHIP_NATIVE_OK;
    }
    if (demo.items->give_item(demo.item) == SHIP_NATIVE_OK) {
        ++demo.gives;
        demo.offered = true;
    } else {
        ++demo.giveRefused;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Stats(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                        void* writer) {
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& demo = *static_cast<Demo*>(user);
    char text[384];
    const int count = std::snprintf(
        text, sizeof(text),
        "item=0x%02X actor=0x%X placement=%s gives=%u recusas=%u recebidos=%u uses=%u spawned=%u skelfail=%u "
        "colfail=%u draws=%u drawfail=%u hits=%u alvo=0x%X once=%u frame=%.1f/%.1f destroyed=%u",
        demo.item, static_cast<unsigned>(demo.actorId), demo.placement, demo.gives, demo.giveRefused, demo.received,
        demo.uses, demo.spawned, demo.skeletonFailures, demo.colliderFailures, demo.draws, demo.drawFailures,
        demo.hits, static_cast<unsigned>(demo.lastHitActor), demo.onceFinished, demo.lastFrame, demo.lastEnd,
        demo.destroyed);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(text)) {
        return SHIP_NATIVE_FAILURE;
    }
    return write(writer, text, static_cast<uint32_t>(count));
}

const void* Service(const ShipNativeRuntime* runtime, const char* name, uint32_t version, uint32_t size) {
    return runtime->get_service(runtime->context, name, version, size);
}

void Release(Demo* demo) {
    if (demo->items && demo->item != NO_ITEM) {
        demo->items->unregister_item(demo->item);
    }
    if (demo->actors && demo->actorId >= 0) {
        demo->actors->unregister_actor_type(demo->actorId);
    }
    delete demo;
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
    demo->items = static_cast<const ShipOotItemsV2*>(
        Service(runtime, LINKSPAN_OOT_ITEMS_SERVICE, LINKSPAN_OOT_ITEMS_VERSION_2, sizeof(ShipOotItemsV2)));
    demo->actors = static_cast<const ShipOotActorsV1*>(
        Service(runtime, LINKSPAN_OOT_ACTORS_SERVICE, LINKSPAN_OOT_ACTORS_VERSION, sizeof(ShipOotActorsV1)));
    demo->skeletons = static_cast<const ShipOotSkeletonsV1*>(Service(
        runtime, LINKSPAN_OOT_SKELETONS_SERVICE, LINKSPAN_OOT_SKELETONS_VERSION, sizeof(ShipOotSkeletonsV1)));
    demo->colliders = static_cast<const ShipOotCollidersV1*>(Service(
        runtime, LINKSPAN_OOT_COLLIDERS_SERVICE, LINKSPAN_OOT_COLLIDERS_VERSION, sizeof(ShipOotCollidersV1)));
    if (!demo->items || !demo->actors || !demo->skeletons || !demo->colliders) {
        delete demo;
        return SHIP_NATIVE_UNSUPPORTED;
    }

    const ShipOotActorTypeSpecV1 actorSpec{ sizeof(ShipOotActorTypeSpecV1),
                                            ACTOR_NAME,
                                            ACTORCAT_ITEMACTION,
                                            ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED,
                                            OBJECT_GAMEPLAY_KEEP,
                                            sizeof(Familiar),
                                            FamiliarInit,
                                            FamiliarDestroy,
                                            FamiliarUpdate,
                                            FamiliarDraw,
                                            demo };
    ShipNativeStatus status = demo->actors->register_actor_type(&actorSpec, &demo->actorId);
    if (status == SHIP_NATIVE_OK) {
        const ShipOotItemSpecV1 itemSpec{ sizeof(ShipOotItemSpecV1), ITEM_NAME, ICON, LINKSPAN_OOT_ITEM_AGE_ANY,
                                          UseWhistle, demo };
        status = demo->items->register_item(&itemSpec, &demo->item);
    }
    if (status == SHIP_NATIVE_OK) {
        const ShipOotGetItemSpecV1 getSpec{ sizeof(ShipOotGetItemSpecV1), GET_MODEL,
                                            LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT, 0.025f, MESSAGE, OnReceive, demo };
        status = demo->items->set_get_item(demo->item, &getSpec);
    }
    if (status == SHIP_NATIVE_OK) {
        const ShipNativeHookSpec spec{ sizeof(ShipNativeHookSpec), LINKSPAN_OOT_HOOK_PLAY_UPDATE,
                                       LINKSPAN_OOT_HOOKS_VERSION, sizeof(ShipOotPlayHookV1),
                                       SHIP_NATIVE_HOOK_OBSERVE, SHIP_NATIVE_HOOK_AFTER, 0, OnFrame, demo };
        uint64_t handle = 0;
        status = runtime->register_hook(runtime->context, &spec, &handle);
    }
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "stats", Stats, demo);
    }
    if (status != SHIP_NATIVE_OK) {
        Release(demo);
        return status;
    }
    *instance = demo;
    return SHIP_NATIVE_OK;
}

// Hooks já saíram; o unregister mata os familiares (o destroy do host não chama mais o mod) e o
// reset do host libera esqueletos e colliders que sobrarem.
void SHIP_NATIVE_CALL Shutdown(void* instance) {
    Release(static_cast<Demo*>(instance));
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{ sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                  Shutdown };
    return &descriptor;
}
