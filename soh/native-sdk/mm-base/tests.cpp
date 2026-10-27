// Fixtures sintéticas: sem conteúdo de jogo, archive real ou runtime do host.
#include <z64.h>
#include "z64audio.h"
#include "mm_audio.h"
#include "mmaudio/MmSfxPlayer.h"
#include "mmaudio/MmSoundFont.h"
#include "bindings.h"
#include "mount.h"
#include "oot_audio.h"
#include "oot_engine.h"
#include "oot_hooks.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

static std::atomic<bool> gWatchMemory{ false };
static std::atomic<unsigned> gMemoryOps{ 0 };
void* operator new(size_t size) {
    if (gWatchMemory.load())
        ++gMemoryOps;
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (gWatchMemory.load())
        ++gMemoryOps;
    std::free(p);
}
void operator delete(void* p, size_t) noexcept {
    ::operator delete(p);
}
static unsigned checks = 0;
static void Check(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FALHA: %s\n", name);
        std::exit(1);
    }
}
static ShipNativeStatus SHIP_NATIVE_CALL Write(void* user, const char* data, uint32_t size) {
    auto& bytes = *static_cast<std::vector<uint8_t>*>(user);
    bytes.insert(bytes.end(), data, data + size);
    return SHIP_NATIVE_OK;
}
static void TestTransform() {
    std::vector<uint8_t> input(0x40 + 7 * 8, 0);
    std::memcpy(input.data() + 4, "TLDO", 4);
    input[0x43] = 0x20; // Comando de 16 bytes; payload não é opcode.
    input[0x4B] = 0x43;
    input[0x53] = 0x3D;
    input[0x5B] = 0x44;
    input[0x63] = 0x45;
    input[0x6B] = 0xDF;
    input[0x73] = 0x43; // Depois de ENDDL fica intacto.
    std::vector<uint8_t> output;
    Check(MmBase::Transform(nullptr, "objects/test", input.data(), input.size(), Write, &output) == SHIP_NATIVE_OK,
          "transform aceita DL sintética");
    Check(output.size() == input.size() && output[0x4B] == 0x43 && output[0x53] == 0 && output[0x5B] == 0x45 &&
              output[0x63] == 0x46 && output[0x73] == 0x43,
          "remapeia só comandos e respeita ENDDL");
    Check(input[0x53] == 0x3D, "transform não muda a entrada do host");
    input[4] = 'X';
    output.clear();
    Check(MmBase::Transform(nullptr, "other", input.data(), input.size(), Write, &output) == SHIP_NATIVE_OK &&
              output == input,
          "recurso sem assinatura passa intacto");
}
static void TestMixer() {
    int16_t coefficients[16]{};
    AdpcmBook book{};
    book.order = 2;
    book.npredictors = 1;
    book.book = coefficients;
    uint8_t encoded[9] = { 0, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 };
    SoundFontSample sample{};
    sample.codec = 0;
    sample.size = sizeof(encoded);
    sample.sampleAddr = encoded;
    sample.book = &book;
    Check(!ShipLua::MmAudio_HasPending(), "fila começa vazia");
    Check(ShipLua::MmAudio_PlayDecodedSample(&sample, 1), "decodifica VADPCM sintético");
    std::array<int16_t, 40> pcm{};
    pcm.fill(32767);
    gWatchMemory.store(true);
    ShipLua::MmAudio_MixInto(pcm.data(), 20);
    gWatchMemory.store(false);
    Check(gMemoryOps.load() == 0, "callback não aloca nem libera memória");
    Check(pcm[0] == 32767 && !ShipLua::MmAudio_HasPending(), "saturação e fim da voz");
    for (unsigned i = 0; i < 32; ++i)
        Check(ShipLua::MmAudio_PlayDecodedSample(&sample, 1), "slot aceita voz");
    Check(!ShipLua::MmAudio_PlayDecodedSample(&sample, 1), "fila cheia recusa sem sobrescrever");
    pcm.fill(0);
    ShipLua::MmAudio_MixInto(pcm.data(), 8);
    Check(pcm[0] == 32 && pcm[1] == 32 && ShipLua::MmAudio_HasPending(), "soma estéreo e mantém cursor");
    pcm.fill(0);
    ShipLua::MmAudio_MixInto(pcm.data(), 20);
    Check(pcm[0] == 32 && pcm[15] == 32 && pcm[16] == 0 && !ShipLua::MmAudio_HasPending(), "tail sem repetição");
    std::atomic<bool> done{ false };
    std::thread audio([&] {
        std::array<int16_t, 32> block{};
        while (!done.load() || ShipLua::MmAudio_HasPending()) {
            block.fill(0);
            ShipLua::MmAudio_MixInto(block.data(), 16);
        }
    });
    for (unsigned i = 0; i < 2000; ++i) {
        while (!ShipLua::MmAudio_PlayDecodedSample(&sample, 1))
            std::this_thread::yield();
    }
    done.store(true);
    audio.join();
    Check(!ShipLua::MmAudio_HasPending(), "produtor e consumidor drenam 2000 vozes");
    ShipLua::MmAudio_Clear();
}

static SoundFont gFont{};
static void* SyntheticResource(const char* path) {
    return !std::strcmp(path, "mm/audio/fonts/Soundfont_0") ? &gFont : nullptr;
}
static ShipNativeStatus SHIP_NATIVE_CALL ResolveSynthetic(void*, const char* name, uintptr_t* address) {
    *address = !std::strcmp(name, "ResourceGetDataByName") ? reinterpret_cast<uintptr_t>(SyntheticResource) : 0;
    return *address ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}
static void TestQuery() {
    ShipNativeRuntime runtime{};
    runtime.resolve_symbol = ResolveSynthetic;
    Check(MmBase::BindResources(&runtime), "resolve carregador privado pela ABI");
    SoundFontSample sample{};
    sample.size = 18;
    sample.codec = 3;
    SoundFontSound sounds[2]{};
    sounds[0].sample = &sample;
    sounds[0].tuning = 1.25f;
    gFont.numSfx = 2;
    gFont.soundEffects = sounds;
    uint32_t total = 9, written = 9;
    Check(ShipLua::MmAudio_QuerySfxTable(0, 0, nullptr, 0, &total, &written) == SHIP_NATIVE_OK && total == 2 &&
              written == 0,
          "consulta tamanho da tabela");
    ShipMmSfxEntryV1 entries[2] = { { sizeof(ShipMmSfxEntryV1) }, { sizeof(ShipMmSfxEntryV1) } };
    Check(ShipLua::MmAudio_QuerySfxTable(0, 0, entries, 2, &total, &written) == SHIP_NATIVE_OK && written == 2 &&
              entries[0].sample_bytes == 18 && entries[0].codec == 3 && entries[0].tuning == 1.25f &&
              entries[0].resolved && !entries[1].resolved,
          "consulta metadados e slot vazio");
    Check(ShipLua::MmAudio_QuerySfxTable(0, 3, entries, 2, &total, &written) == SHIP_NATIVE_INVALID_ARGUMENT &&
              !written,
          "recusa pagina fora da tabela");
    entries[0].size = 0;
    Check(ShipLua::MmAudio_QuerySfxTable(0, 0, entries, 1, &total, &written) == SHIP_NATIVE_INVALID_ARGUMENT &&
              !written,
          "recusa saída de tamanho incompatível");
    MmBase::ClearBindings();
}

static const ShipMmAudioV1* gService = nullptr;
static ShipNativeHookSpec gMix{}, gVoice{};
static ShipOotEngineV1 gEngine{ sizeof(gEngine), MM_BASE_LAYOUT_ID };
static ShipOotResourcesV4 gResources{};
static unsigned gHooks = 0;
static ShipNativeStatus SHIP_NATIVE_CALL MountMock(const ShipOotPrefixedArchiveSpecV1*, uint64_t*,
                                                   ShipOotMountInfoV1*) {
    return SHIP_NATIVE_FAILURE; // Sem assets: esta chamada não pode acontecer.
}
static ShipNativeStatus SHIP_NATIVE_CALL UnmountMock(uint64_t) {
    return SHIP_NATIVE_OK;
}
static const void* SHIP_NATIVE_CALL GetService(void*, const char* name, uint32_t version, uint32_t size) {
    if (!std::strcmp(name, LINKSPAN_OOT_ENGINE_SERVICE) && version == 1 && size <= sizeof(gEngine))
        return &gEngine;
    if (!std::strcmp(name, LINKSPAN_OOT_RESOURCES_SERVICE) && version == 4 && size <= sizeof(gResources))
        return &gResources;
    return nullptr;
}
static ShipNativeStatus SHIP_NATIVE_CALL Register(void*, const char* name, uint32_t version, uint32_t size,
                                                  const void* table) {
    if (std::strcmp(name, LINKSPAN_MM_AUDIO_SERVICE) || version != 1 || size != sizeof(ShipMmAudioV1))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    gService = static_cast<const ShipMmAudioV1*>(table);
    return SHIP_NATIVE_OK;
}
static ShipNativeStatus SHIP_NATIVE_CALL Hook(void*, const ShipNativeHookSpec* spec, uint64_t* handle) {
    ++gHooks;
    if (!std::strcmp(spec->point, LINKSPAN_OOT_HOOK_AUDIO_MIX))
        gMix = *spec;
    if (!std::strcmp(spec->point, LINKSPAN_OOT_HOOK_PLAYER_VOICE_SFX))
        gVoice = *spec;
    *handle = gHooks;
    return SHIP_NATIVE_OK;
}
static ShipNativeStatus SHIP_NATIVE_CALL Resolve(void*, const char*, uintptr_t* address) {
    *address = 0;
    return SHIP_NATIVE_UNSUPPORTED;
}
static void TestDll(const char* path) {
    HMODULE library = LoadLibraryA(path);
    Check(library != nullptr, "carrega DLL real sem import libraries do host");
    auto query =
        reinterpret_cast<const ShipNativeDescriptor*(SHIP_NATIVE_CALL*)()>(GetProcAddress(library, "ShipNative_Query"));
    Check(query != nullptr, "exporta ShipNative_Query");
    const auto* descriptor = query();
    Check(descriptor->abi_minor == 4, "negocia ABI 1.4");
    ShipNativeRuntime runtime{};
    runtime.size = sizeof(runtime);
    runtime.abi_major = 1;
    runtime.abi_minor = 4;
    runtime.get_service = GetService;
    runtime.register_service = Register;
    runtime.register_hook = Hook;
    runtime.resolve_symbol = Resolve;
    gResources.size = sizeof(gResources);
    gResources.mount_archive_prefixed = MountMock;
    gResources.unmount_archive = UnmountMock;
    // Override exclusivo inexistente, sem sondar instalações do usuário.
    SetEnvironmentVariableA("SHIPLUA_MM_ROOT", "mm-base-fixture-ausente");
    for (unsigned pass = 0; pass < 2; ++pass) {
        void* instance = nullptr;
        Check(descriptor->init(&runtime, &instance) == SHIP_NATIVE_OK && instance && gService,
              "inicializa sem arquivo nem símbolo privado");
        ShipMmAudioStatusV1 status{ sizeof(status) };
        Check(gService->get_status(&status) == SHIP_NATIVE_OK && !status.archive_mounted && !status.loader_available &&
                  !status.voice_map_active && !status.sequence_ready,
              "status relata ausência sem alegar áudio pronto");
        Check(gService->set_voice_map(0x6800, 0x100, 0xC0) == SHIP_NATIVE_OK, "aceita mapa fornecido pelo consumidor");
        Check(gService->set_voice_map(65535, 2, 0) == SHIP_NATIVE_INVALID_ARGUMENT, "recusa mapa fora da faixa");
        Check(gService->play_sfx_id(0x68C0) == SHIP_NATIVE_UNSUPPORTED, "áudio ausente retorna unsupported");
        ShipOotPlayerVoiceHookV1 voice{ sizeof(voice) };
        voice.sfx = 0x6800;
        ShipNativeHookCall call{};
        call.payload = &voice;
        call.payload_size = sizeof(voice);
        Check(gVoice.callback(nullptr, &call) == SHIP_NATIVE_OK && !voice.consumed, "fallback mantém voz do OoT");
        Check(gService->clear_voice_map() == SHIP_NATIVE_OK, "consumidor limpa mapa");
        int16_t pcm[4] = { 1, 2, 3, 4 };
        ShipOotAudioMixHookV1 mix{ sizeof(mix), 32000, 2, 2, pcm };
        call.payload = &mix;
        call.payload_size = sizeof(mix);
        Check(gMix.callback(nullptr, &call) == SHIP_NATIVE_OK && pcm[0] == 1 && pcm[3] == 4,
              "hook sem vozes preserva buffer");
        descriptor->shutdown(instance); // Harness síncrono: callbacks já terminaram.
    }
    gEngine.layout_id = "layout-incompativel";
    void* instance = reinterpret_cast<void*>(1);
    Check(descriptor->init(&runtime, &instance) == SHIP_NATIVE_UNSUPPORTED && !instance, "recusa layout diferente");
    Check(FreeLibrary(library) != 0, "descarrega DLL real");
}
int main(int argc, char** argv) {
    Check(argc == 2, "caminho da DLL fornecido");
    TestTransform();
    TestMixer();
    TestQuery();
    TestDll(argv[1]);
    std::printf("MM Base: %u verificações OK (fixtures sintéticas; sem jogo).\n", checks);
}
