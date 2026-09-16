#include "OotNativeEscape.h"

#include <charconv>

namespace ShipLuaHost {
namespace {

OotEscapeHatchFactory escapeFactory = nullptr;

std::string_view NextLine(std::string_view& text) {
    const auto end = text.find('\n');
    auto line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

bool Hex64(std::string_view text) {
    if (text.size() != 64) {
        return false;
    }
    for (const char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace

bool OotSymbolTable::Parse(std::string_view text, std::string& error) {
    mFingerprint.clear();
    mEntries.clear();
    mByName.clear();
    if (NextLine(text) != "linkspan-symbols 1") {
        error = "cabeçalho diferente de 'linkspan-symbols 1'";
        return false;
    }
    const auto sha = NextLine(text);
    if (sha.substr(0, 7) != "sha256 " || !Hex64(sha.substr(7))) {
        error = "linha sha256 inválida";
        return false;
    }
    mFingerprint = std::string(sha.substr(7));
    size_t lineNumber = 2;
    while (!text.empty()) {
        const auto line = NextLine(text);
        ++lineNumber;
        if (line.empty()) {
            continue;
        }
        const auto tab1 = line.find('\t');
        const auto tab2 = tab1 == std::string_view::npos ? tab1 : line.find('\t', tab1 + 1);
        const auto tab3 = tab2 == std::string_view::npos ? tab2 : line.find('\t', tab2 + 1);
        if (tab3 == std::string_view::npos) {
            error = "linha " + std::to_string(lineNumber) + " sem quatro campos";
            return false;
        }
        Entry entry;
        const auto rva = line.substr(0, tab1);
        const auto parsed = std::from_chars(rva.data(), rva.data() + rva.size(), entry.rva, 16);
        const auto flags = line.substr(tab1 + 1, tab2 - tab1 - 1);
        entry.file = std::string(line.substr(tab2 + 1, tab3 - tab2 - 1));
        entry.name = std::string(line.substr(tab3 + 1));
        if (parsed.ec != std::errc{} || parsed.ptr != rva.data() + rva.size() || entry.name.empty() ||
            (flags != "-" && flags != "folded")) {
            error = "linha " + std::to_string(lineNumber) + " malformada";
            return false;
        }
        entry.folded = flags == "folded";
        mByName.emplace(entry.name, mEntries.size());
        mEntries.push_back(std::move(entry));
    }
    return true;
}

ShipNativeStatus OotSymbolTable::Resolve(std::string_view name, uint64_t& rva) const {
    std::string_view file;
    const auto bang = name.find('!');
    if (bang != std::string_view::npos) {
        file = name.substr(0, bang);
        name = name.substr(bang + 1);
        if (file.empty() || name.empty()) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
    }
    const Entry* match = nullptr;
    size_t count = 0;
    const auto [first, last] = mByName.equal_range(name);
    for (auto item = first; item != last; ++item) {
        const auto& entry = mEntries[item->second];
        if (!file.empty() && entry.file != file) {
            continue;
        }
        ++count;
        match = &entry;
    }
    if (count != 1 || match->folded) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    rva = match->rva;
    return SHIP_NATIVE_OK;
}

void SetOotEscapeHatchFactory(OotEscapeHatchFactory factory) {
    escapeFactory = factory;
}

std::shared_ptr<ShipLua::NativeEscapeHatch> CreateOotEscapeHatch() {
    return escapeFactory ? escapeFactory() : nullptr;
}

} // namespace ShipLuaHost
