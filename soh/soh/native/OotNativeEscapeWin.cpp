// Escape hatch do host OoT no Windows x64 (RFC 0023): SHA-256 do soh.exe, soh.symbols ao lado do
// executável e desvios com MinHook. Tudo na thread do jogo, chamado pelo NativeProvider.
#if defined(_WIN32) && defined(_M_X64)
#include "OotNativeEscape.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>

#include <MinHook.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {

struct EscapeState {
    bool fingerprintLoaded = false;
    std::string fingerprint;
    bool symbolsLoaded = false;
    std::unique_ptr<ShipLuaHost::OotSymbolTable> symbols;
    bool minhookReady = false;
    uint64_t nextPatch = 1;
    std::map<uint64_t, LPVOID> patches;
};

EscapeState& State() {
    static EscapeState state;
    return state;
}

std::filesystem::path ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string Sha256File(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!input || BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return {};
    }
    std::string result;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<char> buffer(1 << 20);
        bool ok = true;
        while (ok && input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0) {
                ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) == 0;
            }
        }
        unsigned char digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
            static constexpr char hex[] = "0123456789abcdef";
            for (const unsigned char byte : digest) {
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 0xF]);
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

const std::string& Fingerprint() {
    auto& state = State();
    if (!state.fingerprintLoaded) {
        state.fingerprintLoaded = true;
        state.fingerprint = Sha256File(ExecutablePath());
    }
    return state.fingerprint;
}

const ShipLuaHost::OotSymbolTable* Symbols() {
    auto& state = State();
    if (state.symbolsLoaded) {
        return state.symbols.get();
    }
    state.symbolsLoaded = true;
    const auto path = ExecutablePath().parent_path() / "soh.symbols";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SPDLOG_WARN("Link-Span escape hatch: {} ausente; resolve_symbol indisponível", path.string());
        return nullptr;
    }
    std::ostringstream text;
    text << input.rdbuf();
    auto table = std::make_unique<ShipLuaHost::OotSymbolTable>();
    std::string error;
    if (!table->Parse(text.str(), error)) {
        SPDLOG_WARN("Link-Span escape hatch: {} inválido: {}", path.string(), error);
        return nullptr;
    }
    if (table->Fingerprint() != Fingerprint()) {
        SPDLOG_WARN("Link-Span escape hatch: {} é de outro executável ({})", path.string(), table->Fingerprint());
        return nullptr;
    }
    SPDLOG_INFO("Link-Span escape hatch: {} funções em {}", table->Size(), path.filename().string());
    state.symbols = std::move(table);
    return state.symbols.get();
}

bool InsideExecutable(uintptr_t address) {
    MODULEINFO info{};
    const HMODULE module = GetModuleHandleW(nullptr);
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) {
        return false;
    }
    const auto base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
    return address >= base && address < base + info.SizeOfImage;
}

ShipNativeStatus Resolve(const char* name, uintptr_t* address) {
    const auto* symbols = Symbols();
    if (!symbols) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    uint64_t rva = 0;
    const auto status = symbols->Resolve(name, rva);
    if (status == SHIP_NATIVE_OK) {
        *address = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + static_cast<uintptr_t>(rva);
    }
    return status;
}

ShipNativeStatus Install(uintptr_t target, void* detour, void** original, uint64_t* patch) {
    if (!InsideExecutable(target)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    if (!state.minhookReady) {
        const auto initialized = MH_Initialize();
        if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
            SPDLOG_ERROR("Link-Span escape hatch: MH_Initialize falhou ({})", MH_StatusToString(initialized));
            return SHIP_NATIVE_FAILURE;
        }
        state.minhookReady = true;
    }
    const auto created = MH_CreateHook(reinterpret_cast<LPVOID>(target), detour, reinterpret_cast<LPVOID*>(original));
    if (created == MH_ERROR_ALREADY_CREATED) {
        return SHIP_NATIVE_LIMIT;
    }
    if (created != MH_OK) {
        SPDLOG_WARN("Link-Span escape hatch: MH_CreateHook em {:#x} falhou ({})", target, MH_StatusToString(created));
        return created == MH_ERROR_UNSUPPORTED_FUNCTION ? SHIP_NATIVE_UNSUPPORTED : SHIP_NATIVE_FAILURE;
    }
    const auto enabled = MH_EnableHook(reinterpret_cast<LPVOID>(target));
    if (enabled != MH_OK) {
        MH_RemoveHook(reinterpret_cast<LPVOID>(target));
        *original = nullptr;
        SPDLOG_WARN("Link-Span escape hatch: MH_EnableHook em {:#x} falhou ({})", target, MH_StatusToString(enabled));
        return SHIP_NATIVE_FAILURE;
    }
    const auto id = state.nextPatch++;
    state.patches.emplace(id, reinterpret_cast<LPVOID>(target));
    *patch = id;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Remove(uint64_t patch) {
    auto& state = State();
    const auto found = state.patches.find(patch);
    if (found == state.patches.end()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    MH_DisableHook(found->second);
    const auto removed = MH_RemoveHook(found->second);
    state.patches.erase(found);
    return removed == MH_OK ? SHIP_NATIVE_OK : SHIP_NATIVE_FAILURE;
}

[[maybe_unused]] const bool kEscapeHatchBound = [] {
    ShipLuaHost::SetOotEscapeHatchFactory([] {
        auto hatch = std::make_shared<ShipLua::NativeEscapeHatch>();
        hatch->fingerprint = [] { return Fingerprint(); };
        hatch->resolve = Resolve;
        hatch->install = Install;
        hatch->remove = Remove;
        return hatch;
    });
    return true;
}();

} // namespace
#endif
