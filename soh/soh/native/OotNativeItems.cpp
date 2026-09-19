#include "OotNativeItems.h"

#include <array>
#include <cmath>
#include <cstring>
#include <set>
#include <string_view>

namespace ShipLuaHost {
namespace {

constexpr uint8_t kNoItem = 0xFF;
constexpr std::string_view kOtrPrefix = "__OTR__";

struct ItemsState {
    std::thread::id ownerThread;
    OotItemsBridge bridge;
    std::array<bool, 256> used{};
    std::array<OotItemRecord, 256> items{};
    std::map<std::string, uint8_t, std::less<>> itemsByName;
    std::map<int32_t, OotActorTypeRecord> actorTypes;
    std::map<std::string, int32_t, std::less<>> actorTypesByName;
    // Caminhos "__OTR__..." entregues ao jogo; nós de std::set não mudam de endereço.
    std::set<std::string, std::less<>> paths;
    bool actorDrawActive = false;
    uint32_t actorCallbackDepth = 0;
};

ItemsState& State() {
    static ItemsState state;
    return state;
}

bool OnOwnerThread() {
    return State().ownerThread == std::this_thread::get_id();
}

bool ValidName(const char* text, size_t maxLength) {
    if (!text) {
        return false;
    }
    const std::string_view name(text, strnlen(text, maxLength + 1));
    if (name.empty() || name.size() > maxLength || name.front() == '.' || name.back() == '.' ||
        name.find('.') == std::string_view::npos) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

// Caminho do resource manager: não vazio, sem prefixo __OTR__ nem "..".
bool ValidPath(const char* text, size_t maxLength) {
    if (!text) {
        return false;
    }
    const std::string_view path(text, strnlen(text, maxLength + 1));
    return !path.empty() && path.size() <= maxLength && path.substr(0, kOtrPrefix.size()) != kOtrPrefix &&
           path.find("..") == std::string_view::npos && path.front() != '/';
}

const char* InternPath(std::string_view path) {
    std::string full(kOtrPrefix);
    full.append(path);
    return State().paths.emplace(std::move(full)).first->c_str();
}

bool ValidButton(uint8_t button) {
    return button >= LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT && button <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT;
}

void ClearButtonsWith(uint8_t item) {
    const auto& bridge = State().bridge;
    if (!bridge.getButtonItem || !bridge.setButtonItem) {
        return;
    }
    for (uint8_t button = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT; ++button) {
        if (bridge.getButtonItem(button) == item) {
            bridge.setButtonItem(button, kNoItem);
        }
    }
}

ShipNativeStatus SHIP_NATIVE_CALL RegisterItem(const ShipOotItemSpecV1* spec, uint8_t* item) {
    if (!OnOwnerThread() || !spec || spec->size < sizeof(ShipOotItemSpecV1) || !item ||
        !ValidName(spec->name, LINKSPAN_OOT_ITEMS_MAX_NAME) ||
        !ValidPath(spec->icon_path, LINKSPAN_OOT_ITEMS_MAX_PATH) ||
        (spec->age != LINKSPAN_OOT_ITEM_AGE_ADULT && spec->age != LINKSPAN_OOT_ITEM_AGE_CHILD &&
         spec->age != LINKSPAN_OOT_ITEM_AGE_ANY)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *item = kNoItem;
    try {
        auto& state = State();
        if (state.itemsByName.find(spec->name) != state.itemsByName.end()) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        uint32_t id = LINKSPAN_OOT_ITEMS_FIRST_ID;
        while (id <= LINKSPAN_OOT_ITEMS_LAST_ID && state.used[id]) {
            ++id;
        }
        if (id > LINKSPAN_OOT_ITEMS_LAST_ID) {
            return SHIP_NATIVE_LIMIT;
        }
        OotItemRecord record;
        record.name = spec->name;
        record.use = spec->use;
        record.user = spec->user;
        record.age = spec->age;
        const char* icon = InternPath(spec->icon_path);
        state.itemsByName.emplace(record.name, static_cast<uint8_t>(id));
        state.items[id] = std::move(record);
        state.used[id] = true;
        if (state.bridge.setItemVisual) {
            state.bridge.setItemVisual(static_cast<uint8_t>(id), icon, spec->age);
        }
        *item = static_cast<uint8_t>(id);
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL UnregisterItem(uint8_t item) {
    if (!OnOwnerThread() || !IsOotSyntheticItemId(item)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    if (!state.used[item]) {
        return SHIP_NATIVE_FAILURE;
    }
    ClearButtonsWith(item);
    if (state.bridge.setItemVisual) {
        state.bridge.setItemVisual(item, nullptr, LINKSPAN_OOT_ITEM_AGE_ANY);
    }
    state.itemsByName.erase(state.items[item].name);
    state.items[item] = OotItemRecord{};
    state.used[item] = false;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL FindItem(const char* name, uint8_t* item) {
    if (!OnOwnerThread() || !ValidName(name, LINKSPAN_OOT_ITEMS_MAX_NAME) || !item) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    const auto found = state.itemsByName.find(name);
    if (found == state.itemsByName.end()) {
        *item = kNoItem;
        return SHIP_NATIVE_FAILURE;
    }
    *item = found->second;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL GetButtonItem(uint8_t button, uint8_t* item) {
    if (!OnOwnerThread() || !ValidButton(button) || !item) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!bridge.getButtonItem) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    *item = bridge.getButtonItem(button);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL SetButtonItem(uint8_t button, uint8_t item) {
    if (!OnOwnerThread() || !ValidButton(button) || (item != kNoItem && !FindOotItem(item))) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!bridge.setButtonItem) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    bridge.setButtonItem(button, item);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL RegisterActorType(const ShipOotActorTypeSpecV1* spec, int16_t* actorId) {
    if (!OnOwnerThread() || !spec || spec->size < sizeof(ShipOotActorTypeSpecV1) || !actorId ||
        !ValidName(spec->name, LINKSPAN_OOT_ACTORS_MAX_NAME) || spec->instance_size == 0) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *actorId = -1;
    try {
        auto& state = State();
        if (state.actorCallbackDepth) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        const auto named = state.actorTypesByName.find(spec->name);
        if (named != state.actorTypesByName.end() && state.actorTypes[named->second].active) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        if (named == state.actorTypesByName.end() && state.actorTypes.size() >= LINKSPAN_OOT_ACTORS_MAX_TYPES) {
            return SHIP_NATIVE_LIMIT;
        }
        if (!state.bridge.addActorType) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
        const int32_t id = state.bridge.addActorType(spec->name, *spec);
        if (id < 0 || id > INT16_MAX) {
            return SHIP_NATIVE_FAILURE;
        }
        auto& record = state.actorTypes[id];
        record.name = spec->name;
        record.spec = *spec;
        record.spec.name = nullptr;
        record.active = true;
        state.actorTypesByName[record.name] = id;
        *actorId = static_cast<int16_t>(id);
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL UnregisterActorType(int16_t actorId) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    const auto found = state.actorTypes.find(actorId);
    if (found == state.actorTypes.end() || !found->second.active) {
        return SHIP_NATIVE_FAILURE;
    }
    found->second.active = false;
    if (state.bridge.killActors) {
        state.bridge.killActors(actorId, found->second.spec.category);
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL FindActorType(const char* name, int16_t* actorId) {
    if (!OnOwnerThread() || !ValidName(name, LINKSPAN_OOT_ACTORS_MAX_NAME) || !actorId) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    const auto found = state.actorTypesByName.find(name);
    if (found == state.actorTypesByName.end() || !state.actorTypes[found->second].active) {
        *actorId = -1;
        return SHIP_NATIVE_FAILURE;
    }
    *actorId = static_cast<int16_t>(found->second);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL DrawDisplayList(void* play, const char* path, uint8_t translucent) {
    if (!OnOwnerThread() || !play || !ValidPath(path, LINKSPAN_OOT_ACTORS_MAX_PATH) || translucent > 1) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    if (!state.actorDrawActive || !state.bridge.drawDisplayList) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    try {
        return state.bridge.drawDisplayList(play, InternPath(path), translucent);
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL SetGetItem(uint8_t item, const ShipOotGetItemSpecV1* spec) {
    if (!OnOwnerThread() || !spec || spec->size < sizeof(ShipOotGetItemSpecV1) || !FindOotItem(item) ||
        !ValidPath(spec->model_path, LINKSPAN_OOT_ITEMS_MAX_PATH) || spec->model_layer > LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT ||
        !std::isfinite(spec->model_scale) || spec->model_scale <= 0.0f || spec->model_scale > 100.0f || !spec->message) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const std::string_view message(spec->message, strnlen(spec->message, LINKSPAN_OOT_ITEMS_MAX_MESSAGE + 1));
    if (message.empty() || message.size() > LINKSPAN_OOT_ITEMS_MAX_MESSAGE) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    for (const char c : message) {
        if (static_cast<unsigned char>(c) >= 0x80) {
            return SHIP_NATIVE_INVALID_ARGUMENT; // a fonte do jogo não tem esses glifos
        }
    }
    try {
        auto& record = State().items[item];
        std::string copy(message);
        record.modelPath = InternPath(spec->model_path);
        record.message = std::move(copy);
        record.modelLayer = spec->model_layer;
        record.modelScale = spec->model_scale;
        record.receive = spec->receive;
        record.receiveUser = spec->user;
        record.hasGetItem = true;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL GiveItem(uint8_t item) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto* record = FindOotItem(item);
    if (!record || !record->hasGetItem) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    return bridge.giveItem ? bridge.giveItem(item) : SHIP_NATIVE_UNSUPPORTED;
}

ShipNativeStatus SHIP_NATIVE_CALL SetItemIcon(uint8_t item, const char* iconPath) {
    auto* record = OnOwnerThread() ? const_cast<OotItemRecord*>(FindOotItem(item)) : nullptr;
    if (!record || !ValidPath(iconPath, LINKSPAN_OOT_ITEMS_MAX_PATH)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        auto& state = State();
        if (state.bridge.setItemVisual) {
            state.bridge.setItemVisual(item, InternPath(iconPath), record->age);
        }
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL SetItemAmmo(uint8_t item, uint16_t count, uint16_t full) {
    auto* record = OnOwnerThread() ? const_cast<OotItemRecord*>(FindOotItem(item)) : nullptr;
    if (!record || (count != LINKSPAN_OOT_ITEMS_NO_AMMO && count > LINKSPAN_OOT_ITEMS_MAX_AMMO) ||
        full > LINKSPAN_OOT_ITEMS_MAX_AMMO) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    record->ammo = count;
    record->ammoFull = full;
    return SHIP_NATIVE_OK;
}

uint8_t SHIP_NATIVE_CALL GetLanguage() {
    const auto& bridge = State().bridge;
    return OnOwnerThread() && bridge.getLanguage ? bridge.getLanguage() : LINKSPAN_OOT_LANGUAGE_ENGLISH;
}

const ShipOotItemsV1 itemsV1{ sizeof(ShipOotItemsV1), RegisterItem, UnregisterItem, FindItem, GetButtonItem,
                              SetButtonItem };
const ShipOotItemsV2 itemsV2{ sizeof(ShipOotItemsV2), RegisterItem, UnregisterItem, FindItem, GetButtonItem,
                              SetButtonItem, SetGetItem, GiveItem };
const ShipOotItemsV3 itemsV3{ sizeof(ShipOotItemsV3), RegisterItem, UnregisterItem, FindItem, GetButtonItem,
                              SetButtonItem, SetGetItem, GiveItem, SetItemIcon, SetItemAmmo, GetLanguage };
const ShipOotActorsV1 actorsV1{ sizeof(ShipOotActorsV1), RegisterActorType, UnregisterActorType, FindActorType,
                                DrawDisplayList };

} // namespace

void SetOotItemsBridge(const OotItemsBridge& bridge) {
    State().bridge = bridge;
}

void InitializeOotNativeItems(std::thread::id ownerThread) {
    ResetOotNativeItems();
    State().ownerThread = ownerThread;
}

void ResetOotNativeItems() {
    auto& state = State();
    for (uint32_t id = LINKSPAN_OOT_ITEMS_FIRST_ID; id <= LINKSPAN_OOT_ITEMS_LAST_ID; ++id) {
        if (state.used[id] && state.bridge.setItemVisual) {
            state.bridge.setItemVisual(static_cast<uint8_t>(id), nullptr, LINKSPAN_OOT_ITEM_AGE_ANY);
        }
        state.used[id] = false;
        state.items[id] = OotItemRecord{};
    }
    state.itemsByName.clear();
    for (auto& [id, record] : state.actorTypes) {
        record.active = false;
    }
    state.actorDrawActive = false;
    state.actorCallbackDepth = 0;
}

const ShipOotItemsV1& GetOotNativeItemsService() {
    return itemsV1;
}

const ShipOotItemsV2& GetOotNativeItemsServiceV2() {
    return itemsV2;
}

const ShipOotItemsV3& GetOotNativeItemsServiceV3() {
    return itemsV3;
}

const ShipOotActorsV1& GetOotNativeActorsService() {
    return actorsV1;
}

bool IsOotSyntheticItemId(uint32_t item) {
    return item >= LINKSPAN_OOT_ITEMS_FIRST_ID && item <= LINKSPAN_OOT_ITEMS_LAST_ID;
}

const OotItemRecord* FindOotItem(uint8_t item) {
    const auto& state = State();
    return IsOotSyntheticItemId(item) && state.used[item] ? &state.items[item] : nullptr;
}

const OotActorTypeRecord* FindOotActorType(int32_t actorId) {
    const auto& state = State();
    const auto found = state.actorTypes.find(actorId);
    return found == state.actorTypes.end() ? nullptr : &found->second;
}

void SetOotActorDrawActive(bool active) {
    State().actorDrawActive = active;
}

void EnterOotActorCallback() {
    ++State().actorCallbackDepth;
}

void LeaveOotActorCallback() {
    --State().actorCallbackDepth;
}

std::map<uint8_t, std::string> ExportOotItemButtons() {
    std::map<uint8_t, std::string> buttons;
    const auto& bridge = State().bridge;
    if (!bridge.getButtonItem) {
        return buttons;
    }
    for (uint8_t button = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT; ++button) {
        if (const auto* record = FindOotItem(bridge.getButtonItem(button))) {
            buttons[button] = record->name;
        }
    }
    return buttons;
}

void RestoreOotItemButtons(const std::map<uint8_t, std::string>& buttons) {
    auto& state = State();
    if (!state.bridge.getButtonItem || !state.bridge.setButtonItem) {
        return;
    }
    for (uint8_t button = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT; ++button) {
        uint8_t item = kNoItem;
        const auto saved = buttons.find(button);
        if (saved != buttons.end()) {
            const auto named = state.itemsByName.find(saved->second);
            if (named != state.itemsByName.end()) {
                item = named->second;
            }
        } else if (!IsOotSyntheticItemId(state.bridge.getButtonItem(button))) {
            continue;
        }
        state.bridge.setButtonItem(button, item);
    }
}

} // namespace ShipLuaHost
