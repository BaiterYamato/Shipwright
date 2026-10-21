#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

#include <shiplua/native/ship_native_abi.h>
#include "oot_audio.h"

struct AudioProbe {
    std::atomic<unsigned long long> frames{ 0 };
    std::atomic<unsigned long long> calls{ 0 };
    std::atomic<bool> mute{ false };
    std::atomic<int> peak{ 0 }; // maior |amostra| vista depois da mistura desde o último relatório
};

static ShipNativeStatus SHIP_NATIVE_CALL Observe(void* user, const ShipNativeHookCall* call) {
    auto* probe = static_cast<AudioProbe*>(user);
    const auto* payload = static_cast<const ShipOotAudioMixHookV1*>(call->payload);
    int peak = 0;
    for (uint32_t index = 0; index < payload->frames * payload->channels; ++index) {
        const int sample = payload->samples[index] < 0 ? -int(payload->samples[index]) : int(payload->samples[index]);
        if (sample > peak)
            peak = sample;
    }
    int seen = probe->peak.load();
    while (peak > seen && !probe->peak.compare_exchange_weak(seen, peak)) {}
    probe->frames.fetch_add(payload->frames);
    probe->calls.fetch_add(1);
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Transform(void* user, const ShipNativeHookCall* call) {
    const auto* probe = static_cast<const AudioProbe*>(user);
    const auto* payload = static_cast<const ShipOotAudioMixHookV1*>(call->payload);
    if (probe->mute.load())
        std::memset(payload->samples, 0, std::size_t(payload->frames) * payload->channels * sizeof(int16_t));
    return SHIP_NATIVE_OK;
}

static ShipNativeStatus SHIP_NATIVE_CALL Report(void* user, const char*, uint32_t, ShipNativeWriteFn write,
                                                void* writer) {
    auto* probe = static_cast<AudioProbe*>(user);
    char text[160];
    const int count = std::snprintf(text, sizeof(text), "audio-probe frames=%llu calls=%llu peak=%d",
                                    probe->frames.load(), probe->calls.load(), probe->peak.exchange(0));
    if (count < 0 || std::size_t(count) >= sizeof(text))
        return SHIP_NATIVE_FAILURE;
    return write(writer, text, uint32_t(count));
}

static ShipNativeStatus SHIP_NATIVE_CALL Mute(void* user, const char* request, uint32_t length, ShipNativeWriteFn write,
                                              void* writer) {
    if (!request || length != 1 || (request[0] != '0' && request[0] != '1'))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    static_cast<AudioProbe*>(user)->mute.store(request[0] == '1');
    return write(writer, "ok", 2);
}

static ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    if (!runtime || !output || !runtime->register_hook || !runtime->register_function)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    auto* probe = new (std::nothrow) AudioProbe;
    if (!probe)
        return SHIP_NATIVE_FAILURE;
    *output = probe; // Shutdown também recebe a instância em caso de init parcial.
    const ShipNativeHookSpec observe{ sizeof(ShipNativeHookSpec),
                                      LINKSPAN_OOT_HOOK_AUDIO_MIX,
                                      LINKSPAN_OOT_HOOK_AUDIO_MIX_VERSION,
                                      sizeof(ShipOotAudioMixHookV1),
                                      SHIP_NATIVE_HOOK_OBSERVE,
                                      SHIP_NATIVE_HOOK_AFTER,
                                      0,
                                      Observe,
                                      probe };
    const ShipNativeHookSpec transform{ sizeof(ShipNativeHookSpec),
                                        LINKSPAN_OOT_HOOK_AUDIO_MIX,
                                        LINKSPAN_OOT_HOOK_AUDIO_MIX_VERSION,
                                        sizeof(ShipOotAudioMixHookV1),
                                        SHIP_NATIVE_HOOK_TRANSFORM,
                                        0,
                                        0,
                                        Transform,
                                        probe };
    uint64_t handle = 0;
    auto status = runtime->register_hook(runtime->context, &observe, &handle);
    if (status != SHIP_NATIVE_OK)
        return status;
    status = runtime->register_hook(runtime->context, &transform, &handle);
    if (status != SHIP_NATIVE_OK)
        return status;
    status = runtime->register_function(runtime->context, "mute", Mute, probe);
    if (status != SHIP_NATIVE_OK)
        return status;
    return runtime->register_function(runtime->context, "report", Report, probe);
}

static void SHIP_NATIVE_CALL Shutdown(void* instance) {
    delete static_cast<AudioProbe*>(instance);
}

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query(void) {
    static const ShipNativeDescriptor descriptor{ sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 2u, Init,
                                                  Shutdown };
    return &descriptor;
}
