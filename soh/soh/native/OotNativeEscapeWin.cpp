// Escape hatch do host OoT no Windows x64 (RFC 0023): SHA-256 do soh.exe, soh.symbols ao lado do
// executável e desvios com MinHook. Tudo na thread do jogo, chamado pelo NativeProvider.
// A MinHook congela as threads do jogo a cada desvio ligado ou desligado (~60 ms). No lote que o core abre em
// volta do init e do unload de um provider (COREEXT-010), install e remove só enfileiram, e o commit aplica tudo
// com um MH_ApplyQueued: um congelamento por lote.
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

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {

struct Patch {
    LPVOID target = nullptr;
    std::string owner;
    bool created = false;
    bool acknowledged = false;
    // Falso enquanto o desvio, instalado num lote, espera o commit para ligar.
    bool active = false;
    bool pendingEnable = false;
    bool pendingRemoval = false;
};

struct EscapeState {
    bool fingerprintLoaded = false;
    std::string fingerprint;
    bool symbolsLoaded = false;
    std::unique_ptr<ShipLuaHost::OotSymbolTable> symbols;
    bool minhookReady = false;
    uint64_t nextPatch = 1;
    std::map<uint64_t, Patch> patches;
    // Lote aberto pelo core: desvios à espera de ligar e alvos ligados que só saem da MinHook no commit.
    int batchDepth = 0;
    std::string batchOwner;
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

void NeutralizeQueue(const Patch& patch) {
    const auto queued = patch.active ? MH_QueueEnableHook(patch.target) : MH_QueueDisableHook(patch.target);
    if (queued != MH_OK) {
        SPDLOG_ERROR("Link-Span escape hatch: fila de {:#x} não pôde ser neutralizada ({})",
                     reinterpret_cast<uintptr_t>(patch.target), MH_StatusToString(queued));
    }
}

ShipNativeStatus DrainPendingTarget(EscapeState& state, LPVOID target) {
    for (auto found = state.patches.begin(); found != state.patches.end();) {
        if (found->second.target != target || !found->second.pendingRemoval) {
            ++found;
            continue;
        }
        const auto removed = MH_RemoveHook(target);
        if (removed != MH_OK) {
            NeutralizeQueue(found->second);
            SPDLOG_ERROR("Link-Span escape hatch: remoção pendente em {:#x} falhou ({})",
                         reinterpret_cast<uintptr_t>(target), MH_StatusToString(removed));
            return SHIP_NATIVE_FAILURE;
        }
        if (found->second.acknowledged) {
            found->second.created = false;
            found->second.active = false;
            found->second.pendingEnable = false;
            found->second.pendingRemoval = false;
            ++found;
        } else {
            found = state.patches.erase(found);
        }
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Install(std::string_view owner, uintptr_t target, void* detour, void** original, uint64_t* patch) {
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
    const auto nativeTarget = reinterpret_cast<LPVOID>(target);
    if (state.batchDepth > 0 && state.batchOwner != owner) return SHIP_NATIVE_FAILURE;
    if (DrainPendingTarget(state, nativeTarget) != SHIP_NATIVE_OK) return SHIP_NATIVE_FAILURE;
    // Id nunca reaproveitado: um rollback que falha deixa a entrada retida no mapa com o id dela.
    const auto id = state.nextPatch++;
    try {
        state.patches.emplace(id, Patch{ nativeTarget, std::string(owner), false, false, false,
                                         state.batchDepth > 0, false });
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
    const auto created = MH_CreateHook(nativeTarget, detour, reinterpret_cast<LPVOID*>(original));
    if (created == MH_ERROR_ALREADY_CREATED) {
        state.patches.erase(id);
        return SHIP_NATIVE_LIMIT;
    }
    if (created != MH_OK) {
        state.patches.erase(id);
        SPDLOG_WARN("Link-Span escape hatch: MH_CreateHook em {:#x} falhou ({})", target, MH_StatusToString(created));
        return created == MH_ERROR_UNSUPPORTED_FUNCTION ? SHIP_NATIVE_UNSUPPORTED : SHIP_NATIVE_FAILURE;
    }
    state.patches.at(id).created = true;
    const bool batched = state.batchDepth > 0;
    const auto enabled = batched ? MH_QueueEnableHook(nativeTarget) : MH_EnableHook(nativeTarget);
    if (enabled != MH_OK) {
        const auto removed = MH_RemoveHook(nativeTarget);
        if (removed == MH_OK) {
            state.patches.erase(id);
        } else {
            auto& retained = state.patches.at(id);
            retained.pendingEnable = false;
            retained.pendingRemoval = true;
            NeutralizeQueue(retained);
            SPDLOG_ERROR("Link-Span escape hatch: rollback de {:#x} falhou ({})", target,
                         MH_StatusToString(removed));
        }
        *original = nullptr;
        SPDLOG_WARN("Link-Span escape hatch: {} em {:#x} falhou ({})", batched ? "MH_QueueEnableHook" : "MH_EnableHook",
                    target, MH_StatusToString(enabled));
        return SHIP_NATIVE_FAILURE;
    }
    auto& installed = state.patches.at(id);
    installed.active = !batched;
    installed.acknowledged = true;
    *patch = id;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus Remove(uint64_t patch) {
    auto& state = State();
    const auto found = state.patches.find(patch);
    if (found == state.patches.end()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (state.batchDepth > 0) {
        if (found->second.owner != state.batchOwner) return SHIP_NATIVE_INVALID_ARGUMENT;
        if (!found->second.created) {
            state.patches.erase(found);
            return SHIP_NATIVE_OK;
        }
        if (found->second.pendingRemoval) {
            return SHIP_NATIVE_OK;
        }
        if (found->second.active) {
            // Ligado: desliga no commit com o resto do lote e só então sai da MinHook.
            const auto queued = MH_QueueDisableHook(found->second.target);
            if (queued != MH_OK) {
                SPDLOG_ERROR("Link-Span escape hatch: MH_QueueDisableHook em {:#x} falhou ({})",
                             reinterpret_cast<uintptr_t>(found->second.target), MH_StatusToString(queued));
            }
            found->second.pendingEnable = false;
            found->second.pendingRemoval = true;
            return SHIP_NATIVE_OK;
        }
        // Ainda à espera de ligar (init que falhou): sai já, e MH_RemoveHook num desvio desligado não congela.
        const auto removed = MH_RemoveHook(found->second.target);
        if (removed == MH_OK) {
            state.patches.erase(found);
            return SHIP_NATIVE_OK;
        }
        const auto queued = MH_QueueDisableHook(found->second.target);
        found->second.pendingEnable = false;
        found->second.pendingRemoval = true;
        SPDLOG_ERROR("Link-Span escape hatch: MH_RemoveHook em {:#x} falhou ({})",
                     reinterpret_cast<uintptr_t>(found->second.target), MH_StatusToString(removed));
        if (queued != MH_OK) {
            SPDLOG_ERROR("Link-Span escape hatch: MH_QueueDisableHook em {:#x} falhou ({})",
                         reinterpret_cast<uintptr_t>(found->second.target), MH_StatusToString(queued));
        }
        return SHIP_NATIVE_OK;
    }
    if (!found->second.created) {
        state.patches.erase(found);
        return SHIP_NATIVE_OK;
    }
    const auto target = found->second.target;
    const auto removed = MH_RemoveHook(target);
    if (removed == MH_OK) {
        state.patches.erase(found);
        return SHIP_NATIVE_OK;
    }
    SPDLOG_ERROR("Link-Span escape hatch: MH_RemoveHook em {:#x} falhou ({})",
                 reinterpret_cast<uintptr_t>(target), MH_StatusToString(removed));
    return SHIP_NATIVE_FAILURE;
}

void BeginBatch(std::string_view owner) {
    auto& state = State();
    if (state.batchDepth > 0) {
        if (state.batchOwner != owner) throw std::runtime_error("nested escape batch has another owner");
        ++state.batchDepth;
        return;
    }
    state.batchOwner = owner;
    state.batchDepth = 1;
}

ShipNativeStatus CommitBatch() {
    auto& state = State();
    if (state.batchDepth <= 0) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (--state.batchDepth > 0) {
        return SHIP_NATIVE_OK;
    }
    const auto owner = std::move(state.batchOwner);
    size_t enableCount = 0;
    size_t removalCount = 0;
    for (const auto& [id, entry] : state.patches) {
        if (entry.owner != owner) continue;
        enableCount += entry.pendingEnable && !entry.pendingRemoval;
        removalCount += entry.pendingRemoval;
    }
    if (enableCount == 0 && removalCount == 0) return SHIP_NATIVE_OK;
    const auto started = std::chrono::steady_clock::now();
    const auto applied = MH_ApplyQueued();
    for (auto& [id, entry] : state.patches) {
        if (entry.owner != owner || !entry.pendingEnable || entry.pendingRemoval) continue;
        // Em falha parcial não sabemos até onde ApplyQueued chegou; tratar como ligado torna o rollback seguro.
        entry.active = true;
        if (applied == MH_OK) entry.pendingEnable = false;
    }
    // Desligados pelo ApplyQueued saem sem congelar; se ele falhou, MH_RemoveHook ainda tenta desligar e remover.
    size_t failedRemovals = 0;
    for (auto found = state.patches.begin(); found != state.patches.end();) {
        auto& entry = found->second;
        if (entry.owner != owner || !entry.pendingRemoval) {
            ++found;
            continue;
        }
        // O ApplyQueued que deu certo desligou o desvio; se a remoção falhar, a fila neutralizada o mantém desligado.
        if (applied == MH_OK) entry.active = false;
        const auto removed = MH_RemoveHook(entry.target);
        if (removed == MH_OK) {
            found = state.patches.erase(found);
        } else {
            ++failedRemovals;
            NeutralizeQueue(entry);
            SPDLOG_ERROR("Link-Span escape hatch: MH_RemoveHook em {:#x} falhou ({})",
                         reinterpret_cast<uintptr_t>(entry.target), MH_StatusToString(removed));
            ++found;
        }
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    if (applied == MH_OK) {
        SPDLOG_INFO("Link-Span escape hatch: lote com {} desvios ligados e {} removidos em {} ms", enableCount,
                    removalCount - failedRemovals, elapsed);
    } else {
        SPDLOG_ERROR("Link-Span escape hatch: MH_ApplyQueued falhou ({}) num lote com {} desvios a ligar e {} a remover",
                     MH_StatusToString(applied), enableCount, removalCount);
    }
    const bool enablesClean = enableCount == 0 || applied == MH_OK;
    return enablesClean && failedRemovals == 0 ? SHIP_NATIVE_OK : SHIP_NATIVE_FAILURE;
}

[[maybe_unused]] const bool kEscapeHatchBound = [] {
    ShipLuaHost::SetOotEscapeHatchFactory([] {
        auto hatch = std::make_shared<ShipLua::NativeEscapeHatch>();
        hatch->fingerprint = [] { return Fingerprint(); };
        hatch->resolve = Resolve;
        hatch->install = Install;
        hatch->remove = Remove;
        hatch->beginBatch = BeginBatch;
        hatch->commitBatch = CommitBatch;
        return hatch;
    });
    return true;
}();

} // namespace
#endif
