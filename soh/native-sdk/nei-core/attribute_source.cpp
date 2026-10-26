#include "attribute_source.h"

namespace LinkSpanNei {
namespace {

struct EmitContext {
    const Registry& registry;
    LinkSpanAttrEmitFn emit;
    void* sink;
};

ShipNativeStatus SHIP_NATIVE_CALL VisitActiveItem(void* user, uint64_t item, const char* id) {
    auto& context = *static_cast<EmitContext*>(user);
    try {
        NeiItemStateV1 state{ sizeof(NeiItemStateV1) };
        const auto status = context.registry.GetState(item, &state);
        if (status != SHIP_NATIVE_OK || !state.owned) {
            return status;
        }
        const uint8_t when =
            state.button != LINKSPAN_NEI_NO_BUTTON ? LINKSPAN_ATTR_WHEN_EQUIPPED : LINKSPAN_ATTR_WHEN_OWNED;
        return context.emit(context.sink, id, when);
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

} // namespace

ShipNativeStatus EmitActiveItems(const Registry& registry, LinkSpanAttrEmitFn emit, void* sink) {
    if (!emit) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    EmitContext context{ registry, emit, sink };
    return registry.List(VisitActiveItem, &context);
}

} // namespace LinkSpanNei
