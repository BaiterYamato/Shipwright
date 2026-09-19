// Demo de linkspan.nei.items (NEI-002): define "linkspan-demo.heart-seeds", item com quantidade e um
// upgrade. Em gameplay, se o arquivo não tem o item, o Link o recebe por give_item; o received o equipa
// num botão C vazio (ou no C-Right). Cada uso gasta uma semente e solta um coração à frente do Link; no
// terceiro uso a bolsa sobe de nível (ícone e máximo novos). Posse, contador e nível voltam com o save.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "include/linkspan/nei/nei_items.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include "oot_items.h"
#include "oot_layout_id.h"
#include "z64.h"

namespace {

constexpr const char* ITEM_ID = "linkspan-demo.heart-seeds";
constexpr const char* MODEL = "objects/gameplay_keep/gHeartPieceInteriorDL";
constexpr uint32_t GIVE_DELAY_FRAMES = 240;
constexpr uint32_t UPGRADE_AFTER_USES = 3;

const NeiItemLevelV1 kLevels[] = {
    { sizeof(NeiItemLevelV1),
      "textures/icon_item_static/gItemIconDekuSeedsTex",
      { "Heart Seeds", "Herzsamen", "Graines-coeur" },
      10 },
    { sizeof(NeiItemLevelV1),
      "textures/icon_item_static/gItemIconMagicBeanTex",
      { "Big Heart Seeds", "Grosse Herzsamen", "Grandes graines" },
      30 },
};

struct Demo {
    const ShipOotEngineV1* engine = nullptr;
    const NeiItemsV1* nei = nullptr;
    const ShipOotItemsV1* items = nullptr;
    uint64_t item = 0;
    uint32_t gives = 0;
    uint32_t giveRefused = 0;
    uint32_t received = 0;
    uint32_t uses = 0;
    uint32_t hearts = 0;
    uint32_t lastPlayFrames = 0;
    bool offered = false;
    char placement[64] = "nenhum";
};

ShipNativeStatus SHIP_NATIVE_CALL Use(void* user, uint64_t item, uint8_t) {
    auto& demo = *static_cast<Demo*>(user);
    auto* player = static_cast<Player*>(demo.engine->get_player());
    if (!player) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (demo.nei->add_count(item, -1, nullptr) != SHIP_NATIVE_OK) {
        return SHIP_NATIVE_OK;
    }
    ++demo.uses;
    const auto& pos = player->actor.world.pos;
    const double angle = player->actor.shape.rot.y * (3.14159265358979323846 / 32768.0);
    const float x = pos.x + static_cast<float>(std::sin(angle) * 50.0);
    const float z = pos.z + static_cast<float>(std::cos(angle) * 50.0);
    if (demo.engine->spawn_actor(ACTOR_EN_ITEM00, x, pos.y + 20.0f, z, 0, 0, 0, ITEM00_HEART)) {
        ++demo.hearts;
    }
    NeiItemStateV1 state{ sizeof(NeiItemStateV1) };
    if (demo.uses == UPGRADE_AFTER_USES && demo.nei->get_state(item, &state) == SHIP_NATIVE_OK && state.level == 0) {
        demo.nei->set_level(item, 1);
        demo.nei->set_count(item, 30);
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Received(void* user, uint64_t item) {
    auto& demo = *static_cast<Demo*>(user);
    ++demo.received;
    // Botão C vazio primeiro; senão o C-Right (o item vanilla continua no inventário).
    uint8_t button = LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT;
    for (uint8_t candidate = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; candidate <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT;
         ++candidate) {
        uint8_t current = 0;
        if (demo.items->get_button_item(candidate, &current) == SHIP_NATIVE_OK && current == 0xFF) {
            button = candidate;
            break;
        }
    }
    const ShipNativeStatus status = demo.nei->equip(item, button);
    std::snprintf(demo.placement, sizeof(demo.placement), "recebido no C%u status=%u", button, status);
    return SHIP_NATIVE_OK;
}

// Em gameplay: oferece o item uma vez por cena se o arquivo ainda não o tem.
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
    NeiItemStateV1 state{ sizeof(NeiItemStateV1) };
    if (demo.nei->get_state(demo.item, &state) == SHIP_NATIVE_OK && state.owned) {
        std::snprintf(demo.placement, sizeof(demo.placement), "restaurado no C%u", state.button);
        demo.offered = true;
        return SHIP_NATIVE_OK;
    }
    if (demo.nei->give_item(demo.item) == SHIP_NATIVE_OK) {
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
    NeiItemStateV1 state{ sizeof(NeiItemStateV1) };
    demo.nei->get_state(demo.item, &state);
    char name[LINKSPAN_NEI_MAX_NAME + 1] = {};
    uint32_t nameSize = 0;
    demo.nei->get_name(demo.item, LINKSPAN_OOT_LANGUAGE_ENGLISH, name, LINKSPAN_NEI_MAX_NAME, &nameSize);
    char text[320];
    const int count = std::snprintf(
        text, sizeof(text),
        "item=0x%02X nome='%s' posse=%u nivel=%u qtd=%u/%u botao=C%u placement=%s gives=%u recusas=%u "
        "recebidos=%u usos=%u coracoes=%u",
        state.runtime_id, name, state.owned, state.level, state.count, state.max_count, state.button, demo.placement,
        demo.gives, demo.giveRefused, demo.received, demo.uses, demo.hearts);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(text)) {
        return SHIP_NATIVE_FAILURE;
    }
    return write(writer, text, static_cast<uint32_t>(count));
}

// Teste dos itens do fork NEI (fase F): posse do item pelo id namespaced e equipado no C esquerdo.
ShipNativeStatus SHIP_NATIVE_CALL ForkGive(void* user, const char* args, uint32_t length, ShipNativeWriteFn write,
                                           void* writer) {
    auto& demo = *static_cast<Demo*>(user);
    char id[LINKSPAN_NEI_MAX_ID + 1] = {};
    if (!args || length == 0 || length > LINKSPAN_NEI_MAX_ID || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    std::memcpy(id, args, length);
    uint64_t item = 0;
    ShipNativeStatus status = demo.nei->find_item(id, &item);
    if (status == SHIP_NATIVE_OK) {
        status = demo.nei->grant_item(item);
    }
    if (status == SHIP_NATIVE_OK) {
        status = demo.nei->equip(item, LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT);
    }
    char text[160];
    const int count = std::snprintf(text, sizeof(text), "%s: %s", id, status == SHIP_NATIVE_OK ? "no C esquerdo"
                                                                                                : "recusado");
    return write(writer, text, static_cast<uint32_t>(count));
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || !instance || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_minor < 2 ||
        !runtime->get_service || !runtime->register_function || !runtime->register_hook) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* demo = new (std::nothrow) Demo;
    if (!demo) {
        return SHIP_NATIVE_FAILURE;
    }
    demo->engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    demo->nei = static_cast<const NeiItemsV1*>(runtime->get_service(
        runtime->context, LINKSPAN_NEI_ITEMS_SERVICE, LINKSPAN_NEI_ITEMS_VERSION, sizeof(NeiItemsV1)));
    demo->items = static_cast<const ShipOotItemsV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ITEMS_SERVICE, LINKSPAN_OOT_ITEMS_VERSION, sizeof(ShipOotItemsV1)));
    if (!demo->engine || !demo->engine->layout_id || std::strcmp(demo->engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) ||
        !demo->nei || !demo->items) {
        delete demo;
        return SHIP_NATIVE_UNSUPPORTED;
    }
    NeiItemDefinitionV1 definition{};
    definition.size = sizeof(definition);
    definition.id = ITEM_ID;
    definition.age = LINKSPAN_OOT_ITEM_AGE_ANY;
    definition.model_path = MODEL;
    definition.model_layer = LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT;
    definition.model_scale = 0.025f;
    definition.get_messages[0] = "You got %rHeart Seeds%w!&Press them on a C button to&sprout a recovery heart.";
    definition.get_messages[1] = "Du erhaeltst %rHerzsamen%w!&Druecke sie auf einer C-Taste.";
    definition.get_messages[2] = "Vous obtenez des %rGraines-coeur%w!&Utilisez-les avec un bouton C.";
    definition.level_count = 2;
    definition.levels = kLevels;
    definition.give_count = 5;
    definition.use = Use;
    definition.received = Received;
    definition.user = demo;
    ShipNativeStatus status = demo->nei->define_item(&definition, &demo->item);
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
    if (status == SHIP_NATIVE_OK) {
        status = runtime->register_function(runtime->context, "fork_give", ForkGive, demo);
    }
    if (status != SHIP_NATIVE_OK) {
        if (demo->item) {
            demo->nei->remove_item(demo->item);
        }
        delete demo;
        return status;
    }
    *instance = demo;
    return SHIP_NATIVE_OK;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* demo = static_cast<Demo*>(instance);
    if (demo && demo->item) {
        demo->nei->remove_item(demo->item);
    }
    delete demo;
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{ sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                  Shutdown };
    return &descriptor;
}
