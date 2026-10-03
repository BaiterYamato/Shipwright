// linkspan.oot.randomizer: itens do linkspan.oot.items que um mod oferece ao randomizer do SoH. Aqui fica só o
// estado (ofertas e itens da seed); a ligação com o randomizer está em OotNativeRandoGame.cpp.
#include "OotNativeRando.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

#include "OotNativeItems.h"

namespace ShipLuaHost {
namespace {

struct RandoState {
    std::thread::id ownerThread;
    // A geração da seed roda fora da thread do jogo.
    std::mutex mutex;
    // Chave: id runtime do item no momento da oferta; o nome é o que vale para a seed.
    std::map<uint8_t, OotRandoOffer> offers;
    std::vector<std::string> seedItems;
    OotRandoQueryBackend query;
};

RandoState& State() {
    static RandoState state;
    return state;
}

bool OnOwnerThread() {
    const auto& state = State();
    return state.ownerThread != std::thread::id{} && state.ownerThread == std::this_thread::get_id();
}

ShipNativeStatus SHIP_NATIVE_CALL AddItem(const ShipOotRandomizerItemSpecV1* spec) {
    if (!OnOwnerThread() || !spec || spec->size < sizeof(ShipOotRandomizerItemSpecV1) || spec->copies == 0 ||
        spec->copies > LINKSPAN_OOT_RANDOMIZER_MAX_COPIES) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const OotItemRecord* record = FindOotItem(spec->item);
    // Sem get-item a check não teria o que entregar.
    if (!record || !record->hasGetItem) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    std::lock_guard lock(State().mutex);
    auto& offers = State().offers;
    const auto found = offers.find(spec->item);
    if (found == offers.end() && offers.size() >= LINKSPAN_OOT_RANDOMIZER_MAX_ITEMS) {
        return SHIP_NATIVE_LIMIT;
    }
    offers[spec->item] = OotRandoOffer{ record->name, spec->copies };
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL RemoveItem(uint8_t item) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    std::lock_guard lock(State().mutex);
    return State().offers.erase(item) ? SHIP_NATIVE_OK : SHIP_NATIVE_FAILURE;
}

uint32_t SHIP_NATIVE_CALL SeedItemCount() {
    if (!OnOwnerThread()) {
        return 0;
    }
    std::lock_guard lock(State().mutex);
    return static_cast<uint32_t>(State().seedItems.size());
}

ShipNativeStatus SHIP_NATIVE_CALL SeedItemName(uint32_t index, char* name, uint32_t capacity) {
    if (!OnOwnerThread() || !name) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    std::lock_guard lock(State().mutex);
    const auto& items = State().seedItems;
    if (index >= items.size()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const std::string& value = items[index];
    if (value.size() + 1 > capacity) {
        return SHIP_NATIVE_LIMIT;
    }
    std::memcpy(name, value.c_str(), value.size() + 1);
    return SHIP_NATIVE_OK;
}

const ShipOotRandomizerV1 randomizerV1{ sizeof(ShipOotRandomizerV1), AddItem, RemoveItem, SeedItemCount,
                                        SeedItemName };

uint32_t SHIP_NATIVE_CALL ItemCount() {
    return OnOwnerThread() && State().query.itemCount ? State().query.itemCount() : 0;
}
ShipNativeStatus SHIP_NATIVE_CALL ItemInfo(uint32_t item, ShipOotRandomizerItemInfoV2* info) {
    if (!OnOwnerThread() || !info || info->size < sizeof(*info)) return SHIP_NATIVE_INVALID_ARGUMENT;
    return State().query.itemInfo ? State().query.itemInfo(item, info) : SHIP_NATIVE_UNSUPPORTED;
}
ShipNativeStatus SHIP_NATIVE_CALL FindUncollected(uint32_t item, ShipOotRandomizerHintV2* hint) {
    if (!OnOwnerThread() || !hint || hint->size < sizeof(*hint)) return SHIP_NATIVE_INVALID_ARGUMENT;
    return State().query.findUncollected ? State().query.findUncollected(item, hint) : SHIP_NATIVE_UNSUPPORTED;
}
const ShipOotRandomizerV2 randomizerV2{sizeof(randomizerV2), ItemCount, ItemInfo, FindUncollected};

} // namespace

void InitializeOotNativeRando(std::thread::id ownerThread) {
    State().ownerThread = ownerThread;
}

void ResetOotNativeRando() {
    std::lock_guard lock(State().mutex);
    State().offers.clear();
    State().seedItems.clear();
}

const ShipOotRandomizerV1& GetOotNativeRandomizerService() {
    return randomizerV1;
}
const ShipOotRandomizerV2& GetOotNativeRandomizerServiceV2() { return randomizerV2; }
void BindOotRandoQueryBackend(const OotRandoQueryBackend& backend) { State().query = backend; }

std::vector<OotRandoOffer> GetOotRandoOffers() {
    std::vector<OotRandoOffer> result;
    std::lock_guard lock(State().mutex);
    for (const auto& [item, offer] : State().offers) {
        // O item pode ter saído do registro (mod descarregado) ou o id ter sido reaproveitado por outro nome.
        const OotItemRecord* record = FindOotItem(item);
        if (record && record->name == offer.name && record->hasGetItem) {
            result.push_back(offer);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const OotRandoOffer& a, const OotRandoOffer& b) { return a.name < b.name; });
    return result;
}

void SetOotRandoSeedItems(const std::vector<std::string>& names) {
    std::lock_guard lock(State().mutex);
    State().seedItems = names;
    if (State().seedItems.size() > LINKSPAN_OOT_RANDOMIZER_MAX_ITEMS) {
        State().seedItems.resize(LINKSPAN_OOT_RANDOMIZER_MAX_ITEMS);
    }
}

std::vector<std::string> GetOotRandoSeedItems() {
    std::lock_guard lock(State().mutex);
    return State().seedItems;
}

int32_t AddOotRandoSeedItem(const std::string& name) {
    std::lock_guard lock(State().mutex);
    auto& items = State().seedItems;
    const auto found = std::find(items.begin(), items.end(), name);
    if (found != items.end()) {
        return static_cast<int32_t>(found - items.begin());
    }
    if (items.size() >= LINKSPAN_OOT_RANDOMIZER_MAX_ITEMS) {
        return -1;
    }
    items.push_back(name);
    return static_cast<int32_t>(items.size() - 1);
}

} // namespace ShipLuaHost
