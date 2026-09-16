// Demo de linkspan.oot.items v2 e actors (OOT-CORE-003/003B): registra o item "linkspan-demo.orb-wand"
// e o tipo de ator "linkspan-demo.orb". Em gameplay, se nenhum botão C tem o item, o Link o recebe por
// give_item (levanta o item com a caixa de texto); o receive o equipa num botão C vazio (C-Left
// primeiro) ou no C-Right (o item vanilla continua no inventário). O host grava o botão pelo nome.
// Apertar o botão solta um orbe à frente do Link, que sobe girando com o modelo do coração.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "oot_actors.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_items.h"
#include "oot_layout_id.h"
#include "z64.h"

namespace {

constexpr const char* ITEM_NAME = "linkspan-demo.orb-wand";
constexpr const char* ACTOR_NAME = "linkspan-demo.orb";
constexpr const char* ICON = "textures/icon_item_static/gItemIconBottleFairyTex";
constexpr const char* MODEL = "objects/gameplay_keep/gHeartPieceInteriorDL";
constexpr int16_t LIFETIME = 40;
constexpr uint8_t NO_ITEM = 0xFF;
constexpr const char* MESSAGE = "You got the %rOrb Wand%w!&Press it on a C button to release&a floating heart orb.";
// Espera depois da cena carregar, para os A que abrem o arquivo não fecharem a caixa.
constexpr uint32_t GIVE_DELAY_FRAMES = 240;

struct Orb {
    Actor actor;
    int16_t timer;
};

struct Demo {
    const ShipOotEngineV1* engine = nullptr;
    const ShipOotItemsV2* items = nullptr;
    const ShipOotActorsV1* actors = nullptr;
    uint8_t item = NO_ITEM;
    int16_t actorId = -1;
    uint32_t uses = 0;
    uint32_t spawned = 0;
    uint32_t updates = 0;
    uint32_t draws = 0;
    uint32_t drawFailures = 0;
    uint32_t destroyed = 0;
    char placement[64] = "nenhum";
    uint32_t gives = 0;
    uint32_t giveRefused = 0;
    uint32_t received = 0;
    uint32_t lastPlayFrames = 0;
    bool offered = false;
};

void SHIP_NATIVE_CALL OrbInit(void* user, void* actor, void*) {
    auto* orb = static_cast<Orb*>(actor);
    orb->timer = LIFETIME;
    orb->actor.scale = { 0.03f, 0.03f, 0.03f };
    ++static_cast<Demo*>(user)->spawned;
}

void SHIP_NATIVE_CALL OrbDestroy(void* user, void*, void*) {
    ++static_cast<Demo*>(user)->destroyed;
}

void SHIP_NATIVE_CALL OrbUpdate(void* user, void* actor, void*) {
    auto& demo = *static_cast<Demo*>(user);
    auto* orb = static_cast<Orb*>(actor);
    ++demo.updates;
    orb->actor.world.pos.y += 2.0f;
    orb->actor.shape.rot.y += 0x0C00;
    if (--orb->timer <= 0) {
        demo.engine->kill_actor(&orb->actor);
    }
}

void SHIP_NATIVE_CALL OrbDraw(void* user, void*, void* play) {
    auto& demo = *static_cast<Demo*>(user);
    if (demo.actors->draw_display_list(play, MODEL, 1) == SHIP_NATIVE_OK) {
        ++demo.draws;
    } else {
        ++demo.drawFailures;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL UseWand(void* user, uint8_t, uint8_t) {
    auto& demo = *static_cast<Demo*>(user);
    auto* player = static_cast<Player*>(demo.engine->get_player());
    if (!player || demo.actorId < 0) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    ++demo.uses;
    const auto& pos = player->actor.world.pos;
    const int16_t yaw = player->actor.shape.rot.y;
    const double angle = yaw * (3.14159265358979323846 / 32768.0);
    const float x = pos.x + static_cast<float>(std::sin(angle) * 60.0);
    const float z = pos.z + static_cast<float>(std::cos(angle) * 60.0);
    return demo.engine->spawn_actor(demo.actorId, x, pos.y + 30.0f, z, 0, yaw, 0, 0) ? SHIP_NATIVE_OK
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

ShipNativeStatus SHIP_NATIVE_CALL OnReceive(void* user, uint8_t item) {
    auto& demo = *static_cast<Demo*>(user);
    ++demo.received;
    uint8_t button = ButtonWith(demo, NO_ITEM);
    if (!button) {
        button = LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT;
    }
    const ShipNativeStatus status = demo.items->set_button_item(button, item);
    std::snprintf(demo.placement, sizeof(demo.placement), "recebido no C%u status=%u", button, status);
    return SHIP_NATIVE_OK;
}

// Em gameplay: oferece o item uma vez por cena se nenhum botão C o tem.
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
    char text[256];
    const int count = std::snprintf(
        text, sizeof(text), "item=0x%02X actor=0x%X placement=%s gives=%u recusas=%u recebidos=%u uses=%u spawned=%u "
                            "draws=%u drawfail=%u destroyed=%u",
        demo.item, static_cast<unsigned>(demo.actorId), demo.placement, demo.gives, demo.giveRefused, demo.received,
        demo.uses, demo.spawned, demo.draws, demo.drawFailures, demo.destroyed);
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
    if (!demo->items || !demo->actors) {
        delete demo;
        return SHIP_NATIVE_UNSUPPORTED;
    }

    const ShipOotActorTypeSpecV1 actorSpec{ sizeof(ShipOotActorTypeSpecV1),
                                            ACTOR_NAME,
                                            ACTORCAT_ITEMACTION,
                                            ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED,
                                            OBJECT_GAMEPLAY_KEEP,
                                            sizeof(Orb),
                                            OrbInit,
                                            OrbDestroy,
                                            OrbUpdate,
                                            OrbDraw,
                                            demo };
    ShipNativeStatus status = demo->actors->register_actor_type(&actorSpec, &demo->actorId);
    if (status == SHIP_NATIVE_OK) {
        const ShipOotItemSpecV1 itemSpec{ sizeof(ShipOotItemSpecV1), ITEM_NAME, ICON, LINKSPAN_OOT_ITEM_AGE_ANY,
                                          UseWand, demo };
        status = demo->items->register_item(&itemSpec, &demo->item);
    }
    if (status == SHIP_NATIVE_OK) {
        const ShipOotGetItemSpecV1 getSpec{ sizeof(ShipOotGetItemSpecV1), MODEL, LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT,
                                            0.025f, MESSAGE, OnReceive, demo };
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

// Hooks já saíram; item e tipo de ator são do mod e saem aqui.
void SHIP_NATIVE_CALL Shutdown(void* instance) {
    Release(static_cast<Demo*>(instance));
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{ sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                  Shutdown };
    return &descriptor;
}
