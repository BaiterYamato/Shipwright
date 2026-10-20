#include "OotNativePublicSymbols.h"

#include <cstring>

#include "oot_layout_id.h"

namespace ShipLuaHost {
namespace {
bool Sorted(const LinkSpanPublicSymbol* table, size_t count) {
    for (size_t index = 1; index < count; ++index) {
        if (std::strcmp(table[index - 1].name, table[index].name) >= 0)
            return false;
    }
    return true;
}
// Busca binária sem conferir a ordem: quem chama já conferiu.
const LinkSpanPublicSymbol* Search(const LinkSpanPublicSymbol* table, size_t count, const char* name) {
    if (!table || !name || !*name)
        return nullptr;
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        const int order = std::strcmp(table[middle].name, name);
        if (order == 0)
            return &table[middle];
        if (order < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return nullptr;
}
} // namespace

const LinkSpanPublicSymbol* FindOotPublicSymbol(const LinkSpanPublicSymbol* table, size_t count, const char* name) {
    return table && Sorted(table, count) ? Search(table, count, name) : nullptr;
}

std::shared_ptr<ShipLua::NativePublicSymbols> CreateOotPublicSymbols(std::string layoutId,
                                                                    const LinkSpanPublicSymbol* table, size_t count) {
    auto symbols = std::make_shared<ShipLua::NativePublicSymbols>();
    symbols->layoutId = std::move(layoutId);
    // A ordem é conferida uma vez aqui; a busca confia nela depois.
    const bool sorted = Sorted(table, count);
    symbols->resolve = [table, count, sorted](const char* name, uintptr_t* address) -> ShipNativeStatus {
        if (!name || !address)
            return SHIP_NATIVE_INVALID_ARGUMENT;
        *address = 0;
        const auto* entry = sorted ? Search(table, count, name) : nullptr;
        if (!entry || !entry->address)
            return SHIP_NATIVE_UNSUPPORTED;
        *address = reinterpret_cast<uintptr_t>(entry->address);
        return SHIP_NATIVE_OK;
    };
    return symbols;
}

std::shared_ptr<ShipLua::NativePublicSymbols> CreateOotPublicSymbols() {
    return CreateOotPublicSymbols(LINKSPAN_OOT_LAYOUT_ID, LinkSpan_PublicSymbols, LinkSpan_PublicSymbolCount);
}
} // namespace ShipLuaHost
