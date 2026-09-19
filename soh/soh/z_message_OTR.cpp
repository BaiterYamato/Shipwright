// SOH [Unbound] Message tables are owned, growable and hash-indexed; mods can add message ids.
// Ported from the Unbound fork (fd236b9) without its JSON text loader: reading text/<lang>/messages.json is the
// Unbound framework's job (coremod). The host keeps the tables and exposes OTRMessage_Set/Remove.
#include "z_message_OTR.h"

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>

#include "soh/resource/type/Text.h"
#include <message_data_static.h>

#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" MessageTableEntry* sNesMessageEntryTablePtr;
extern "C" MessageTableEntry* sGerMessageEntryTablePtr;
extern "C" MessageTableEntry* sFraMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;
extern "C" MessageTableEntry* sStaffMessageEntryTablePtr;
extern "C" char* _message_0xFFFC_nes;

namespace {

constexpr uint16_t kTerminatorId = 0xFFFF;
constexpr char kMessageEnd = '\x02';

struct LanguageSpec {
    MessageLanguage language;
    const char* folder;      // archive folder, also the override/ subfolder
    const char* baseFile;    // primary base resource
    const char* altBaseFile; // fallback base resource (NTSC english), may be null
    uint16_t firstId;        // expected first id of the base table
};

const LanguageSpec kLanguages[MSG_LANGUAGE_COUNT] = {
    { MSG_NES, "text/nes_message_data_static", "text/nes_message_data_static/nes_message_data_static",
      "text/nes_message_data_static/ntsc_nes_message_data_static", 0x0001 },
    { MSG_GER, "text/ger_message_data_static", "text/ger_message_data_static/ger_message_data_static", nullptr,
      0x0001 },
    { MSG_FRA, "text/fra_message_data_static", "text/fra_message_data_static/fra_message_data_static", nullptr,
      0x0001 },
    { MSG_JPN, "text/jpn_message_data_static", "text/jpn_message_data_static/jpn_message_data_static", nullptr,
      0x0001 },
    { MSG_STAFF, "text/staff_message_data_static", "text/staff_message_data_static/staff_message_data_static",
      nullptr, 0x0500 },
};

/**
 * One language's message table. Owns the message bytes (deque keeps c_str() stable across growth), keeps the
 * C-visible entry array in base order with additions appended before the terminator, and a hash index for lookup.
 */
struct MessageTable {
    std::deque<std::string> storage;
    std::vector<MessageTableEntry> entries; // terminator is the last entry once loaded
    std::unordered_map<uint16_t, size_t> index;
    bool loaded = false;

    void Set(uint16_t id, uint8_t typePos, std::string bytes) {
        if (id == kTerminatorId) {
            return;
        }
        if (bytes.empty() || bytes.back() != kMessageEnd) {
            bytes.push_back(kMessageEnd);
        }
        storage.push_back(std::move(bytes));
        const std::string& owned = storage.back();

        MessageTableEntry entry;
        entry.textId = id;
        entry.typePos = typePos;
        entry.segment = owned.c_str();
        entry.msgSize = static_cast<uint32_t>(owned.size());

        auto it = index.find(id);
        if (it != index.end()) {
            entries[it->second] = entry;
            return;
        }
        // Keep the terminator last: insert the new entry before it.
        const size_t at = loaded ? entries.size() - 1 : entries.size();
        entries.insert(entries.begin() + at, entry);
        index[id] = at;
        if (loaded) {
            index[kTerminatorId] = entries.size() - 1;
        }
    }

    void Set(const SOH::MessageEntry& msg) {
        Set(msg.id, (uint8_t)((msg.textboxType << 4) | msg.textboxYPos), msg.msg);
    }

    bool Remove(uint16_t id) {
        auto it = index.find(id);
        if (id == kTerminatorId || it == index.end()) {
            return false;
        }
        entries.erase(entries.begin() + it->second);
        index.clear();
        for (size_t i = 0; i < entries.size(); i++) {
            index[entries[i].textId] = i;
        }
        return true;
    }

    MessageTableEntry* Find(uint16_t id) {
        auto it = index.find(id);
        return it == index.end() || id == kTerminatorId ? nullptr : &entries[it->second];
    }

    void Clear() {
        storage.clear();
        entries.clear();
        index.clear();
        loaded = false;
    }

    void Finalize() {
        MessageTableEntry terminator = { kTerminatorId, 0, nullptr, 0 };
        entries.push_back(terminator);
        index[kTerminatorId] = entries.size() - 1;
        loaded = true;
    }
};

std::array<MessageTable, MSG_LANGUAGE_COUNT> sTables;
bool sInitialized = false;

std::shared_ptr<SOH::Text> LoadTextResource(const std::string& path) {
    return std::static_pointer_cast<SOH::Text>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path));
}

bool LoadBase(MessageTable& table, const LanguageSpec& spec) {
    auto file = LoadTextResource(spec.baseFile);
    if (file == nullptr && spec.altBaseFile != nullptr) {
        file = LoadTextResource(spec.altBaseFile);
    }
    if (file == nullptr) {
        return false;
    }
    for (const auto& msg : file->messages) {
        table.Set(msg);
    }
    if (table.entries.empty() || table.entries[0].textId != spec.firstId) {
        SPDLOG_WARN("[Unbound] {} does not start at message {:#06x}", spec.baseFile, spec.firstId);
    }
    return true;
}

// override/<folder>/* : Text resources whose entries replace OR add ids (vanilla SoH could only replace)
void LoadOverrides(MessageTable& table, const LanguageSpec& spec) {
    auto files = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->ListFiles(
        std::string("override/") + spec.folder + "/*");
    if (files == nullptr) {
        return;
    }
    for (const auto& path : *files) {
        auto file = LoadTextResource(path);
        if (file == nullptr) {
            continue;
        }
        for (const auto& msg : file->messages) {
            table.Set(msg);
        }
    }
}

void PublishTables() {
    sNesMessageEntryTablePtr = sTables[MSG_NES].loaded ? sTables[MSG_NES].entries.data() : nullptr;
    sGerMessageEntryTablePtr = sTables[MSG_GER].loaded ? sTables[MSG_GER].entries.data() : nullptr;
    sFraMessageEntryTablePtr = sTables[MSG_FRA].loaded ? sTables[MSG_FRA].entries.data() : nullptr;
    sJpnMessageEntryTablePtr = sTables[MSG_JPN].loaded ? sTables[MSG_JPN].entries.data() : nullptr;
    sStaffMessageEntryTablePtr = sTables[MSG_STAFF].loaded ? sTables[MSG_STAFF].entries.data() : nullptr;

    MessageTableEntry* fffc = sTables[MSG_NES].loaded ? sTables[MSG_NES].Find(0xFFFC) : nullptr;
    _message_0xFFFC_nes = fffc != nullptr ? (char*)fffc->segment : nullptr;
}

} // namespace

extern "C" void OTRMessage_Init(void) {
    if (sInitialized) {
        return; // the tables are process-lifetime; a second pass would append a second terminator
    }
    sInitialized = true;

    // Per language: base Text resource + override/ resources, then publish.
    for (const auto& spec : kLanguages) {
        MessageTable& table = sTables[spec.language];
        if (LoadBase(table, spec)) {
            LoadOverrides(table, spec);
        }
    }
    for (auto& table : sTables) {
        if (!table.entries.empty()) {
            table.Finalize();
        }
    }
    PublishTables();
}

extern "C" MessageTableEntry* OTRMessage_Find(MessageTableEntry* table, u16 textId) {
    for (auto& t : sTables) {
        if (t.loaded && t.entries.data() == table) {
            return t.Find(textId);
        }
    }
    return nullptr;
}

extern "C" s32 OTRMessage_Set(s32 language, u16 textId, u8 typePos, const char* bytes, u32 size) {
    if (language < 0 || language >= MSG_LANGUAGE_COUNT || textId == kTerminatorId || (bytes == nullptr && size)) {
        return 0;
    }
    MessageTable& table = sTables[language];
    if (!table.loaded) {
        return 0;
    }
    table.Set(textId, typePos, std::string(bytes ? bytes : "", size));
    PublishTables();
    return 1;
}

extern "C" s32 OTRMessage_Remove(s32 language, u16 textId) {
    if (language < 0 || language >= MSG_LANGUAGE_COUNT || !sTables[language].loaded) {
        return 0;
    }
    const bool removed = sTables[language].Remove(textId);
    PublishTables();
    return removed ? 1 : 0;
}

extern "C" s32 OTRMessage_Clear(s32 language) {
    if (language < 0 || language >= MSG_LANGUAGE_COUNT || !sTables[language].loaded) {
        return 0;
    }
    MessageTable& table = sTables[language];
    table.Clear();
    table.Finalize();
    PublishTables();
    return 1;
}

extern "C" s32 OTRMessage_Reset(s32 language) {
    if (language < 0 || language >= MSG_LANGUAGE_COUNT || !sInitialized) {
        return 0;
    }
    const LanguageSpec& spec = kLanguages[language];
    MessageTable& table = sTables[spec.language];
    table.Clear();
    if (LoadBase(table, spec)) {
        LoadOverrides(table, spec);
    }
    if (!table.entries.empty()) {
        table.Finalize();
    }
    PublishTables();
    return table.loaded ? 1 : 0;
}

extern "C" s32 OTRMessage_ForEach(s32 language, OTRMessageVisitor visitor, void* user) {
    if (language < 0 || language >= MSG_LANGUAGE_COUNT || !visitor || !sTables[language].loaded) {
        return 0;
    }
    const MessageTable& table = sTables[language];
    std::vector<std::pair<uint16_t, size_t>> order;
    order.reserve(table.index.size());
    for (const auto& [id, position] : table.index) {
        if (id != kTerminatorId) {
            order.emplace_back(id, position);
        }
    }
    std::sort(order.begin(), order.end());
    for (const auto& [id, position] : order) {
        const MessageTableEntry& entry = table.entries[position];
        const s32 result = visitor(user, id, entry.typePos, entry.segment, entry.msgSize);
        if (result != 0) {
            return result;
        }
    }
    return 0;
}
