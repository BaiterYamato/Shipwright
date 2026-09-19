#include "registry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <nlohmann/json.hpp>

namespace LinkSpanNei {
namespace {

constexpr uint8_t kNoItem = 0xFF;

bool ValidId(const char* text) {
    if (!text) {
        return false;
    }
    const std::string_view id(text, strnlen(text, LINKSPAN_NEI_MAX_ID + 1));
    if (id.empty() || id.size() > LINKSPAN_NEI_MAX_ID || id.front() == '.' || id.back() == '.' ||
        id.find('.') == std::string_view::npos) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
               c == '-';
    });
}

bool ValidPath(const char* text) {
    if (!text) {
        return false;
    }
    const std::string_view path(text, strnlen(text, LINKSPAN_OOT_ITEMS_MAX_PATH + 1));
    return !path.empty() && path.size() <= LINKSPAN_OOT_ITEMS_MAX_PATH && path.rfind("__OTR__", 0) != 0 &&
           path.find("..") == std::string_view::npos && path.front() != '/';
}

// Texto ASCII visível (a fonte do jogo não tem acentos) de até `limit` bytes; NULL/vazio = "".
bool CopyAscii(const char* text, size_t limit, std::string& out) {
    out.clear();
    if (!text || !*text) {
        return true;
    }
    const std::string_view view(text, strnlen(text, limit + 1));
    if (view.size() > limit) {
        return false;
    }
    for (const char c : view) {
        if (static_cast<unsigned char>(c) >= 0x80 || static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    out.assign(view);
    return true;
}

// Idiomas faltantes caem no inglês, que é obrigatório.
bool CopyLocalized(const char* const* texts, size_t limit, std::array<std::string, LINKSPAN_NEI_LANGUAGES>& out) {
    for (uint32_t language = 0; language < LINKSPAN_NEI_LANGUAGES; ++language) {
        if (!CopyAscii(texts[language], limit, out[language])) {
            return false;
        }
    }
    if (out[0].empty()) {
        return false;
    }
    for (auto& text : out) {
        if (text.empty()) {
            text = out[0];
        }
    }
    return true;
}

bool ValidButton(uint8_t button) {
    return button >= LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT && button <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT;
}

} // namespace

void Registry::Attach(const ShipOotItemsV3* items, const ShipOotSaveV1* save, uint64_t saveHandle) {
    mItems = items;
    mSave = save;
    mSaveHandle = saveHandle;
}

void Registry::Detach() {
    if (mItems) {
        for (auto& [handle, item] : mDefined) {
            if (item->runtime != kNoItem) {
                mItems->unregister_item(item->runtime);
            }
        }
    }
    mDefined.clear();
    mById.clear();
    mStates.clear();
    mItems = nullptr;
    mSave = nullptr;
    mSaveHandle = 0;
    mDirty = false;
}

Registry::Item* Registry::Lookup(uint64_t handle) const {
    const auto found = mDefined.find(handle);
    return found == mDefined.end() ? nullptr : found->second.get();
}

SavedState& Registry::StateOf(const Item& item) {
    return mStates[item.id];
}

const SavedState* Registry::FindState(const Item& item) const {
    const auto found = mStates.find(item.id);
    return found == mStates.end() ? nullptr : &found->second;
}

uint16_t Registry::MaxCount(const Item& item) const {
    const SavedState* state = FindState(item);
    const size_t level = state ? std::min<size_t>(state->level, item.levels.size() - 1) : 0;
    return item.levels[level].maxCount;
}

uint8_t Registry::ButtonOf(const Item& item) const {
    if (!mItems || item.runtime == kNoItem) {
        return LINKSPAN_NEI_NO_BUTTON;
    }
    for (uint8_t button = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT; ++button) {
        uint8_t current = kNoItem;
        if (mItems->get_button_item(button, &current) == SHIP_NATIVE_OK && current == item.runtime) {
            return button;
        }
    }
    return LINKSPAN_NEI_NO_BUTTON;
}

void Registry::Refresh(Item& item) {
    SavedState& state = StateOf(item);
    state.level = static_cast<uint8_t>(std::min<size_t>(state.level, item.levels.size() - 1));
    const Level& level = item.levels[state.level];
    state.count = std::min<uint16_t>(state.count, level.maxCount);
    if (!mItems || item.runtime == kNoItem) {
        return;
    }
    mItems->set_item_icon(item.runtime, level.icon.c_str());
    if (level.maxCount) {
        mItems->set_item_ammo(item.runtime, state.count, level.maxCount);
    } else {
        mItems->set_item_ammo(item.runtime, LINKSPAN_OOT_ITEMS_NO_AMMO, 0);
    }
    if (!state.owned) {
        for (uint8_t button = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT;
             ++button) {
            uint8_t current = kNoItem;
            if (mItems->get_button_item(button, &current) == SHIP_NATIVE_OK && current == item.runtime) {
                mItems->set_button_item(button, kNoItem);
            }
        }
    }
}

void Registry::MarkDirty() {
    mDirty = true;
    Flush();
}

std::string Registry::SerializeForTests() const {
    nlohmann::json items = nlohmann::json::object();
    for (const auto& [id, state] : mStates) {
        // Estado zerado não precisa ir para o arquivo.
        if (!state.owned && !state.count && !state.level) {
            continue;
        }
        items[id] = { { "owned", state.owned }, { "count", state.count }, { "level", state.level } };
    }
    return nlohmann::json{ { "items", std::move(items) } }.dump();
}

void Registry::Flush() {
    // Sem arquivo carregado (tela de título) não há onde gravar; o próximo load reaplica o arquivo.
    if (!mDirty || !mSave || !mSaveHandle || mSave->get_slot() < 0) {
        return;
    }
    try {
        const std::string json = SerializeForTests();
        if (mSave->write(mSaveHandle, json.data(), static_cast<uint32_t>(json.size())) == SHIP_NATIVE_OK) {
            mDirty = false;
        }
    } catch (...) {
    }
}

void Registry::OnSaveLoaded() {
    ++mLoads;
    mStates.clear();
    mDirty = false;
    if (mSave && mSaveHandle) {
        uint32_t size = 0;
        mSave->read(mSaveHandle, nullptr, 0, &size);
        if (size) {
            std::string text(size, '\0');
            if (mSave->read(mSaveHandle, text.data(), size, &size) == SHIP_NATIVE_OK) {
                text.resize(size);
                const auto doc = nlohmann::json::parse(text, nullptr, false);
                if (doc.is_object() && doc.contains("items") && doc["items"].is_object()) {
                    for (const auto& [id, value] : doc["items"].items()) {
                        if (!value.is_object()) {
                            continue;
                        }
                        SavedState state;
                        state.owned = value.value("owned", false);
                        state.count = static_cast<uint16_t>(
                            std::clamp<int64_t>(value.value("count", int64_t{ 0 }), 0, LINKSPAN_NEI_MAX_COUNT));
                        state.level = static_cast<uint8_t>(
                            std::clamp<int64_t>(value.value("level", int64_t{ 0 }), 0, LINKSPAN_NEI_MAX_LEVELS - 1));
                        mStates[id] = state;
                    }
                }
            }
        }
    }
    for (auto& [handle, item] : mDefined) {
        Refresh(*item);
    }
}

ShipNativeStatus Registry::Define(const NeiItemDefinitionV1* definition, uint64_t* handle) {
    if (!definition || definition->size < sizeof(NeiItemDefinitionV1) || !handle || !ValidId(definition->id) ||
        !ValidPath(definition->model_path) || definition->model_layer > LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT ||
        !std::isfinite(definition->model_scale) || definition->model_scale <= 0.0f ||
        definition->model_scale > 100.0f || definition->level_count == 0 ||
        definition->level_count > LINKSPAN_NEI_MAX_LEVELS || !definition->levels ||
        definition->give_count > LINKSPAN_NEI_MAX_COUNT ||
        (definition->age != LINKSPAN_OOT_ITEM_AGE_ADULT && definition->age != LINKSPAN_OOT_ITEM_AGE_CHILD &&
         definition->age != LINKSPAN_OOT_ITEM_AGE_ANY)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *handle = 0;
    if (!mItems) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    try {
        if (mById.contains(definition->id)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        auto item = std::make_unique<Item>();
        item->owner = this;
        item->id = definition->id;
        item->age = definition->age;
        item->model = definition->model_path;
        item->modelLayer = definition->model_layer;
        item->modelScale = definition->model_scale;
        if (!CopyLocalized(definition->get_messages, LINKSPAN_OOT_ITEMS_MAX_MESSAGE, item->messages)) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
        for (uint32_t i = 0; i < definition->level_count; ++i) {
            const NeiItemLevelV1& source = definition->levels[i];
            Level level;
            if (source.size < sizeof(NeiItemLevelV1) || !ValidPath(source.icon_path) ||
                source.max_count > LINKSPAN_NEI_MAX_COUNT ||
                !CopyLocalized(source.names, LINKSPAN_NEI_MAX_NAME, level.names)) {
                return SHIP_NATIVE_INVALID_ARGUMENT;
            }
            level.icon = source.icon_path;
            level.maxCount = source.max_count;
            item->levels.push_back(std::move(level));
        }
        item->giveCount = definition->give_count;
        item->use = definition->use;
        item->received = definition->received;
        item->user = definition->user;

        const SavedState* saved = FindState(*item);
        const size_t level = saved ? std::min<size_t>(saved->level, item->levels.size() - 1) : 0;
        const ShipOotItemSpecV1 spec{ sizeof(ShipOotItemSpecV1), item->id.c_str(), item->levels[level].icon.c_str(),
                                      item->age, UseTrampoline, item.get() };
        const ShipNativeStatus status = mItems->register_item(&spec, &item->runtime);
        if (status != SHIP_NATIVE_OK) {
            return status;
        }
        const uint64_t id = mNextHandle++;
        item->handle = id;
        Item& stored = *item;
        mById.emplace(stored.id, id);
        mDefined.emplace(id, std::move(item));
        Refresh(stored);
        *handle = id;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus Registry::Remove(uint64_t handle) {
    Item* item = Lookup(handle);
    if (!item) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (mItems && item->runtime != kNoItem) {
        mItems->unregister_item(item->runtime);
    }
    mById.erase(item->id);
    mDefined.erase(handle);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::Find(const char* id, uint64_t* handle) const {
    if (!ValidId(id) || !handle) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto found = mById.find(std::string_view(id));
    *handle = found == mById.end() ? 0 : found->second;
    return found == mById.end() ? SHIP_NATIVE_UNSUPPORTED : SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::List(NeiItemVisitFn visit, void* user) const {
    if (!visit) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    for (const auto& [id, handle] : mById) {
        const ShipNativeStatus status = visit(user, handle, id.c_str());
        if (status != SHIP_NATIVE_OK) {
            return status;
        }
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::GetState(uint64_t handle, NeiItemStateV1* out) const {
    const Item* item = Lookup(handle);
    if (!item || !out || out->size < sizeof(NeiItemStateV1)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const SavedState* state = FindState(*item);
    out->owned = state && state->owned ? 1 : 0;
    out->level = state ? state->level : 0;
    out->count = state ? state->count : 0;
    out->max_count = MaxCount(*item);
    out->runtime_id = item->runtime;
    out->button = ButtonOf(*item);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::Give(uint64_t handle) {
    Item* item = Lookup(handle);
    if (!item) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!mItems) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    uint8_t language = mItems->get_language();
    if (language >= LINKSPAN_NEI_LANGUAGES) {
        language = LINKSPAN_OOT_LANGUAGE_ENGLISH;
    }
    const ShipOotGetItemSpecV1 spec{ sizeof(ShipOotGetItemSpecV1),
                                     item->model.c_str(),
                                     item->modelLayer,
                                     item->modelScale,
                                     item->messages[language].c_str(),
                                     ReceiveTrampoline,
                                     item };
    const ShipNativeStatus status = mItems->set_get_item(item->runtime, &spec);
    return status == SHIP_NATIVE_OK ? mItems->give_item(item->runtime) : status;
}

ShipNativeStatus Registry::Grant(uint64_t handle) {
    Item* item = Lookup(handle);
    if (!item) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    SavedState& state = StateOf(*item);
    state.owned = true;
    state.count = static_cast<uint16_t>(std::min<uint32_t>(state.count + item->giveCount, MaxCount(*item)));
    Refresh(*item);
    MarkDirty();
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::Revoke(uint64_t handle) {
    Item* item = Lookup(handle);
    if (!item) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    SavedState& state = StateOf(*item);
    state.owned = false;
    state.count = 0;
    Refresh(*item);
    MarkDirty();
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::SetCount(uint64_t handle, uint16_t count) {
    Item* item = Lookup(handle);
    if (!item) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    StateOf(*item).count = std::min(count, MaxCount(*item));
    Refresh(*item);
    MarkDirty();
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::AddCount(uint64_t handle, int32_t delta, uint16_t* result) {
    Item* item = Lookup(handle);
    if (!item) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    SavedState& state = StateOf(*item);
    const int64_t next = static_cast<int64_t>(state.count) + delta;
    if (next < 0) {
        if (result) {
            *result = state.count;
        }
        return SHIP_NATIVE_LIMIT;
    }
    state.count = static_cast<uint16_t>(std::min<int64_t>(next, MaxCount(*item)));
    if (result) {
        *result = state.count;
    }
    Refresh(*item);
    MarkDirty();
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::SetLevel(uint64_t handle, uint8_t level) {
    Item* item = Lookup(handle);
    if (!item || level >= item->levels.size()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    StateOf(*item).level = level;
    Refresh(*item);
    MarkDirty();
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::Equip(uint64_t handle, uint8_t button) {
    Item* item = Lookup(handle);
    if (!item || !ValidButton(button)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const SavedState* state = FindState(*item);
    if (!state || !state->owned) {
        return SHIP_NATIVE_LIMIT;
    }
    if (!mItems) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    for (uint8_t other = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; other <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT; ++other) {
        uint8_t current = kNoItem;
        if (other != button && mItems->get_button_item(other, &current) == SHIP_NATIVE_OK &&
            current == item->runtime) {
            mItems->set_button_item(other, kNoItem);
        }
    }
    return mItems->set_button_item(button, item->runtime);
}

ShipNativeStatus Registry::Unequip(uint64_t handle) {
    Item* item = Lookup(handle);
    if (!item) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!mItems) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    for (uint8_t button = LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT; button <= LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT; ++button) {
        uint8_t current = kNoItem;
        if (mItems->get_button_item(button, &current) == SHIP_NATIVE_OK && current == item->runtime) {
            mItems->set_button_item(button, kNoItem);
        }
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Registry::GetName(uint64_t handle, uint8_t language, char* output, uint32_t capacity,
                                   uint32_t* outputSize) const {
    const Item* item = Lookup(handle);
    if (!item || language >= LINKSPAN_NEI_LANGUAGES || !outputSize || (!output && capacity)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const SavedState* state = FindState(*item);
    const size_t level = state ? std::min<size_t>(state->level, item->levels.size() - 1) : 0;
    const std::string& name = item->levels[level].names[language];
    *outputSize = static_cast<uint32_t>(name.size());
    if (!output && capacity == 0) {
        return SHIP_NATIVE_OK;
    }
    if (capacity < name.size()) {
        return SHIP_NATIVE_LIMIT;
    }
    std::memcpy(output, name.data(), name.size());
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Registry::UseTrampoline(void* user, uint8_t, uint8_t button) {
    auto* item = static_cast<Item*>(user);
    Registry& self = *item->owner;
    const SavedState* state = self.FindState(*item);
    // Botão restaurado de um save sem posse, ou sem munição: nada acontece, como no vanilla.
    if (!state || !state->owned || (self.MaxCount(*item) && state->count == 0) || !item->use) {
        return SHIP_NATIVE_OK;
    }
    ++self.mUses;
    return item->use(item->user, item->handle, button);
}

ShipNativeStatus SHIP_NATIVE_CALL Registry::ReceiveTrampoline(void* user, uint8_t) {
    auto* item = static_cast<Item*>(user);
    Registry& self = *item->owner;
    ++self.mReceived;
    self.Grant(item->handle);
    return item->received ? item->received(item->user, item->handle) : SHIP_NATIVE_OK;
}

std::string Registry::Stats() const {
    size_t owned = 0;
    for (const auto& [id, state] : mStates) {
        owned += state.owned ? 1 : 0;
    }
    char text[160];
    std::snprintf(text, sizeof(text), "definidos=%zu possuidos=%zu loads=%u recebidos=%u usos=%u", mDefined.size(),
                  owned, mLoads, mReceived, mUses);
    return text;
}

} // namespace LinkSpanNei
