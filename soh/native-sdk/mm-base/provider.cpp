#include "mm_audio.h"
#include "bindings.h"
#include "mount.h"
#include "mmaudio/MmSfxPlayer.h"
#include "mmaudio/MmSoundFont.h"
#include "oot_audio.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include <cstring>
#include <string>

namespace {
const ShipOotResourcesV4* gResources = nullptr;
uint64_t gMount = 0;
bool gLoader = false;
bool gMapActive = false;
uint32_t gBase = 0, gCount = 0;
int32_t gOffset = 0;
ShipOotMountInfoV1 gInfo{ sizeof(gInfo) };

ShipNativeStatus SHIP_NATIVE_CALL PlaySample(const char* path) {
    if (!path || std::strncmp(path, "mm/audio/samples/", 17) || std::strstr(path, "..") || std::strchr(path, '\\'))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!gMount || !gLoader)
        return SHIP_NATIVE_UNSUPPORTED;
    return ShipLua::MmAudio_PlaySampleOneShot(path) ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}
ShipNativeStatus SHIP_NATIVE_CALL PlayFont(uint32_t font, uint32_t index) {
    if (font > 40 || index > 4095)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!gMount || !gLoader)
        return SHIP_NATIVE_UNSUPPORTED;
    return ShipLua::MmAudio_PlayFontSfx(static_cast<int>(font), static_cast<int>(index)) ? SHIP_NATIVE_OK
                                                                                         : SHIP_NATIVE_UNSUPPORTED;
}
ShipNativeStatus SHIP_NATIVE_CALL PlaySfx(uint32_t id) {
    if (id > 65535)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!gMount || !gLoader)
        return SHIP_NATIVE_UNSUPPORTED;
    return ShipLua::MmAudio_PlayVoiceSfx(static_cast<int>(id)) ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}
ShipNativeStatus SHIP_NATIVE_CALL SetMap(uint32_t base, uint32_t count, int32_t offset) {
    if (!count || count > 65536 || base > 65535 || base + count > 65536 || int64_t(base) + offset < 0 ||
        int64_t(base) + count - 1 + offset > 65535)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    gBase = base;
    gCount = count;
    gOffset = offset;
    gMapActive = true;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL ClearMap() {
    gMapActive = false;
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Query(uint32_t font, uint32_t index, ShipMmSfxEntryV1* out, uint32_t capacity,
                                        uint32_t* total, uint32_t* written) {
    if (total)
        *total = 0;
    if (written)
        *written = 0;
    if (!total || !written || font > 40 || (!out && capacity))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!gMount || !gLoader)
        return SHIP_NATIVE_UNSUPPORTED;
    return ShipLua::MmAudio_QuerySfxTable(font, index, out, capacity, total, written);
}
ShipNativeStatus SHIP_NATIVE_CALL Status(ShipMmAudioStatusV1* out) {
    if (!out || out->size < sizeof(*out))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    *out = { sizeof(*out),           gMount != 0,           gLoader, ShipLua::MmAudio_HasPending(), gMapActive, 0,
             gInfo.aliases_accepted, gInfo.aliases_rejected };
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Mix(void*, const ShipNativeHookCall* call) {
    if (!call || call->payload_size < sizeof(ShipOotAudioMixHookV1) || !call->payload)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    auto* audio = static_cast<ShipOotAudioMixHookV1*>(call->payload);
    if (audio->size < sizeof(*audio) || audio->sample_rate != 32000 || audio->channels != 2 || !audio->samples ||
        audio->frames > LINKSPAN_OOT_AUDIO_MAX_FRAMES)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    ShipLua::MmAudio_MixInto(audio->samples, audio->frames);
    // LACUNA: o sequenciador legado aloca/carrega no render; não entra neste hook.
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Voice(void*, const ShipNativeHookCall* call) {
    if (!call || !call->payload || call->payload_size < sizeof(ShipOotPlayerVoiceHookV1))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    auto* voice = static_cast<ShipOotPlayerVoiceHookV1*>(call->payload);
    if (voice->size < sizeof(*voice))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    if (!voice->consumed && gMapActive && voice->sfx >= gBase && voice->sfx < gBase + gCount &&
        PlaySfx(static_cast<uint32_t>(int64_t(voice->sfx) + gOffset)) == SHIP_NATIVE_OK)
        voice->consumed = 1;
    return SHIP_NATIVE_OK;
}
const ShipMmAudioV1 gService{ sizeof(gService), PlaySample, PlayFont, PlaySfx, SetMap, ClearMap, Query, Status };

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    if (output)
        *output = nullptr;
    if (!runtime || !output || runtime->size < sizeof(*runtime) || runtime->abi_major != 1 || runtime->abi_minor < 4 ||
        !runtime->get_service || !runtime->register_service || !runtime->register_hook || !runtime->resolve_symbol)
        return SHIP_NATIVE_UNSUPPORTED;
    const auto* engine = static_cast<const ShipOotEngineV1*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, 1, sizeof(ShipOotEngineV1)));
    if (!engine || engine->size < sizeof(*engine) || !engine->layout_id ||
        std::strcmp(engine->layout_id, MM_BASE_LAYOUT_ID))
        return SHIP_NATIVE_UNSUPPORTED;
    gResources = static_cast<const ShipOotResourcesV4*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, 4, sizeof(ShipOotResourcesV4)));
    if (!gResources || gResources->size < sizeof(*gResources) || !gResources->mount_archive_prefixed ||
        !gResources->unmount_archive)
        return SHIP_NATIVE_UNSUPPORTED;
    *output = const_cast<ShipMmAudioV1*>(&gService); // Cleanup também em init parcial.
    gLoader = MmBase::BindResources(runtime);
    const auto mounted = MmBase::Mount(gResources, &gMount, &gInfo);
    if (mounted != SHIP_NATIVE_OK && mounted != SHIP_NATIVE_UNSUPPORTED)
        return mounted;
    const ShipNativeHookSpec hooks[] = {
        { sizeof(ShipNativeHookSpec), LINKSPAN_OOT_HOOK_AUDIO_MIX, 1, sizeof(ShipOotAudioMixHookV1),
          SHIP_NATIVE_HOOK_TRANSFORM, 0, 0, Mix, nullptr },
        { sizeof(ShipNativeHookSpec), LINKSPAN_OOT_HOOK_PLAYER_VOICE_SFX, 1, sizeof(ShipOotPlayerVoiceHookV1),
          SHIP_NATIVE_HOOK_TRANSFORM, 0, 0, Voice, nullptr }
    };
    for (const auto& hook : hooks) {
        uint64_t handle = 0;
        const auto result = runtime->register_hook(runtime->context, &hook, &handle);
        if (result != SHIP_NATIVE_OK)
            return result;
    }
    return runtime->register_service(runtime->context, LINKSPAN_MM_AUDIO_SERVICE, 1, sizeof(gService), &gService);
}
void SHIP_NATIVE_CALL Shutdown(void* instance) {
    if (!instance)
        return;
    // O core já retirou os hooks e drenou a thread de áudio antes deste ponto.
    ClearMap();
    ShipLua::MmAudio_Clear();
    if (gMount && gResources)
        gResources->unmount_archive(gMount);
    gMount = 0;
    gResources = nullptr;
    gLoader = false;
    gInfo = { sizeof(gInfo) };
    MmBase::ClearBindings();
}
} // namespace
extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{ sizeof(descriptor), 1, 4, Init, Shutdown };
    return &descriptor;
}
