#include "OotNativeAudioMix.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "oot_audio.h"

namespace ShipLuaHost {
namespace {
struct MixHook {
    uint64_t handle = 0;
    uint32_t mode = 0;
    uint32_t phase = 0;
    ShipNativeHookFn callback = nullptr;
    void* user = nullptr;
    std::atomic<uint32_t> failures{ 0 };
    std::atomic<bool> disabled{ false };
};

struct MixList {
    explicit MixList(std::size_t size) : hooks(new MixHook[size]), count(size) {
    }
    std::unique_ptr<MixHook[]> hooks;
    std::size_t count;
};

ShipLua::NativeHookRegistry* gRegistry = nullptr;
uint64_t gPoint = 0;
std::atomic<MixList*> gPublished{ nullptr };
std::atomic<uint32_t> gInFlight{ 0 };
std::atomic<bool> gFailuresPending{ false };
std::atomic<bool> gWaitingForReaders{ false };
// Thread do jogo: hooks desligados na thread de áudio cuja lista saiu sem levá-los (reconstrução que falhou ou
// remoção). O Pump desliga no registro; handle que já saiu dele é recusado sem efeito.
std::vector<uint64_t> gDisabledOrphans;

// Retira a lista antes de esperar leitores. Só depois da quiescência copia os estados por handle.
// Ordem sequencial (padrão): o leitor incrementa gInFlight antes de ler gPublished.
void Publish(MixList* next) noexcept {
    MixList* previous = gPublished.exchange(nullptr);
    if (gInFlight.load() != 0) {
        gWaitingForReaders.store(true);
        while (gInFlight.load() != 0)
            std::this_thread::yield();
        gWaitingForReaders.store(false);
    }
    for (std::size_t old = 0; previous && old < previous->count; ++old) {
        const auto& before = previous->hooks[old];
        bool carried = false;
        for (std::size_t index = 0; next && index < next->count; ++index) {
            auto& hook = next->hooks[index];
            if (hook.handle != before.handle)
                continue;
            hook.failures.store(before.failures.load());
            hook.disabled.store(before.disabled.load());
            carried = true;
            break;
        }
        // Revisão do Codex: sem isto, uma reconstrução que lança (Publish(nullptr)) apagaria o desligamento.
        if (!carried && before.disabled.load()) {
            try {
                gDisabledOrphans.push_back(before.handle);
            } catch (...) {}
        }
    }
    delete previous;
    gPublished.store(next);
}

void Rebuild(uint64_t point) {
    if (!gRegistry)
        return;
    try {
        const auto entries = gRegistry->Snapshot(point);
        if (entries.empty()) {
            Publish(nullptr);
            return;
        }
        auto list = std::make_unique<MixList>(entries.size());
        for (std::size_t index = 0; index < entries.size(); ++index) {
            auto& hook = list->hooks[index];
            hook.handle = entries[index].handle;
            hook.mode = entries[index].mode;
            hook.phase = entries[index].phase;
            hook.callback = entries[index].callback;
            hook.user = entries[index].user;
        }
        Publish(list.release());
    } catch (...) {
        // Snapshot e alocações podem falhar durante unregister/unload. Não deixar callbacks antigos publicados.
        Publish(nullptr);
        throw; // NativeHookRegistry::Notify contém e registra a falha.
    }
}

struct InFlight {
    InFlight() {
        gInFlight.fetch_add(1);
    }
    ~InFlight() {
        gInFlight.fetch_sub(1);
    }
};
} // namespace

uint64_t InstallOotAudioMix(ShipLua::NativeHookRegistry& registry) {
    const ShipNativeHookPointSpec spec{ sizeof(spec), LINKSPAN_OOT_HOOK_AUDIO_MIX, LINKSPAN_OOT_HOOK_AUDIO_MIX_VERSION,
                                        sizeof(ShipOotAudioMixHookV1),
                                        SHIP_NATIVE_HOOK_OBSERVE | SHIP_NATIVE_HOOK_TRANSFORM };
    uint64_t point = 0;
    if (registry.DeclarePoint("", spec, &point) != SHIP_NATIVE_OK)
        return 0;
    gRegistry = &registry;
    gPoint = point;
    if (registry.SetPointListener("", point, Rebuild) != SHIP_NATIVE_OK) {
        gRegistry = nullptr;
        gPoint = 0;
        return 0;
    }
    return point;
}

void ResetOotAudioMix() {
    if (gRegistry && gPoint)
        gRegistry->SetPointListener("", gPoint, {});
    Publish(nullptr);
    gRegistry = nullptr;
    gPoint = 0;
    gFailuresPending.store(false);
    gDisabledOrphans.clear();
}

bool OotAudioMixWaitingForReaders() {
    return gWaitingForReaders.load();
}

bool OotAudioMixActive() {
    return gPublished.load() != nullptr;
}

bool MixOotAudio(int16_t* samples, uint32_t frames) {
    if (!samples || frames == 0 || frames > LINKSPAN_OOT_AUDIO_MAX_FRAMES)
        return false;
    const InFlight guard;
    MixList* list = gPublished.load();
    if (!list)
        return false;
    static thread_local int16_t backup[LINKSPAN_OOT_AUDIO_MAX_FRAMES * LINKSPAN_OOT_AUDIO_CHANNELS];
    const std::size_t bytes = std::size_t(frames) * LINKSPAN_OOT_AUDIO_CHANNELS * sizeof(int16_t);
    ShipOotAudioMixHookV1 payload{};
    ShipNativeHookCall call{};
    call.size = sizeof(call);
    call.payload_size = sizeof(payload);
    call.payload = &payload;
    call.point = LINKSPAN_OOT_HOOK_AUDIO_MIX;
    call.point_version = LINKSPAN_OOT_HOOK_AUDIO_MIX_VERSION;
    auto run = [&](MixHook& hook) {
        // Payload novo a cada callback: um hook não troca o buffer nem o tamanho do seguinte.
        payload = { sizeof(payload), LINKSPAN_OOT_AUDIO_SAMPLE_RATE, LINKSPAN_OOT_AUDIO_CHANNELS, frames, samples };
        call.mode = hook.mode;
        call.phase = hook.phase;
        ShipNativeStatus status;
        try {
            status = hook.callback(hook.user, &call);
        } catch (...) { status = SHIP_NATIVE_FAILURE; }
        if (status == SHIP_NATIVE_OK) {
            hook.failures.store(0);
        } else if (status != SHIP_NATIVE_UNSUPPORTED &&
                   hook.failures.fetch_add(1) + 1 >= ShipLua::NativeHookRegistry::kMaxFailures) {
            hook.disabled.store(true);
            gFailuresPending.store(true);
        }
        return status;
    };
    auto observe = [&](uint32_t phase) {
        for (std::size_t index = 0; index < list->count; ++index) {
            auto& hook = list->hooks[index];
            if (hook.mode == SHIP_NATIVE_HOOK_OBSERVE && hook.phase == phase && !hook.disabled.load())
                run(hook);
        }
    };
    observe(SHIP_NATIVE_HOOK_BEFORE);
    for (std::size_t index = 0; index < list->count; ++index) {
        auto& hook = list->hooks[index];
        if (hook.mode != SHIP_NATIVE_HOOK_TRANSFORM || hook.disabled.load())
            continue;
        std::memcpy(backup, samples, bytes);
        if (run(hook) != SHIP_NATIVE_OK)
            std::memcpy(samples, backup, bytes);
    }
    observe(SHIP_NATIVE_HOOK_AFTER);
    return true;
}

void PumpOotAudioMix() {
    if (!gFailuresPending.exchange(false) || !gRegistry)
        return;
    std::vector<uint64_t> failed;
    failed.swap(gDisabledOrphans);
    if (const MixList* list = gPublished.load()) {
        for (std::size_t index = 0; index < list->count; ++index) {
            if (list->hooks[index].disabled.load())
                failed.push_back(list->hooks[index].handle);
        }
    }
    for (const auto handle : failed)
        gRegistry->Disable("", handle, "três falhas seguidas na thread de áudio");
}
} // namespace ShipLuaHost
