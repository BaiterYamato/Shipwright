#include <stdlib.h>
#include <string.h>
#include "oot_item_table.h"
#include "oot_engine.h"
#include "oot_item_source_layout.h"
#include "z64.h"

typedef struct OotItemSource {
    const ShipOotEngineV1* engine;
    const LinkSpanAttributesV1* attributes;
    uint64_t handle;
    uint8_t registered;
} OotItemSource;

static ShipNativeStatus SHIP_NATIVE_CALL EmitItems(void* user, LinkSpanAttrEmitFn emit, void* sink) {
    OotItemSource* source = (OotItemSource*)user;
    const SaveContext* save = (const SaveContext*)source->engine->get_save_context();
    const Player* player;
    OotItemSnapshot snapshot = { 0 };
    if (save == NULL)
        return SHIP_NATIVE_OK;
    snapshot.equips_equipment = save->equips.equipment;
    snapshot.inventory_equipment = save->inventory.equipment;
    snapshot.upgrades = save->inventory.upgrades;
    memcpy(snapshot.items, save->inventory.items, sizeof(snapshot.items));
    memcpy(snapshot.button_items, save->equips.buttonItems, sizeof(snapshot.button_items));
    player = (const Player*)source->engine->get_player();
    if (player != NULL)
        snapshot.current_mask = player->currentMask;
    return OotItemSource_Emit(&snapshot, emit, sink);
}

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    const ShipOotEngineV1* engine;
    const LinkSpanAttributesV1* attributes;
    OotItemSource* source;
    ShipNativeStatus status;
    if (output == NULL)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    *output = NULL;
    if (runtime == NULL || runtime->size < sizeof(ShipNativeRuntime) || runtime->abi_major != SHIP_NATIVE_ABI_MAJOR ||
        runtime->abi_minor < 4u || runtime->get_service == NULL)
        return SHIP_NATIVE_UNSUPPORTED;
    engine = (const ShipOotEngineV1*)runtime->get_service(runtime->context, LINKSPAN_OOT_ENGINE_SERVICE,
                                                          LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1));
    if (engine == NULL || engine->size < sizeof(ShipOotEngineV1) || engine->layout_id == NULL ||
        strcmp(engine->layout_id, OOT_ITEM_SOURCE_LAYOUT_ID) != 0 || engine->save_context_size != sizeof(SaveContext) ||
        engine->player_size != sizeof(Player) || engine->get_save_context == NULL || engine->get_player == NULL)
        return SHIP_NATIVE_UNSUPPORTED;
    attributes = (const LinkSpanAttributesV1*)runtime->get_service(
        runtime->context, LINKSPAN_ATTRIBUTES_SERVICE, LINKSPAN_ATTRIBUTES_VERSION, sizeof(LinkSpanAttributesV1));
    if (attributes == NULL || attributes->size < sizeof(LinkSpanAttributesV1) ||
        attributes->register_item_source == NULL || attributes->unregister_item_source == NULL)
        return SHIP_NATIVE_UNSUPPORTED;
    source = (OotItemSource*)calloc(1, sizeof(*source));
    if (source == NULL)
        return SHIP_NATIVE_FAILURE;
    *output = source; /* O host chama Shutdown inclusive após init parcial. */
    source->engine = engine;
    source->attributes = attributes;
    status = attributes->register_item_source(attributes->context, "oot", EmitItems, source, &source->handle);
    if (status == SHIP_NATIVE_OK)
        source->registered = 1;
    return status;
}

static void SHIP_NATIVE_CALL Shutdown(void* instance) {
    OotItemSource* source = (OotItemSource*)instance;
    if (source == NULL)
        return;
    if (source->registered)
        source->attributes->unregister_item_source(source->attributes->context, source->handle);
    free(source);
}

SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor = { sizeof(ShipNativeDescriptor), 1u, 4u, Init, Shutdown };
    return &descriptor;
}
