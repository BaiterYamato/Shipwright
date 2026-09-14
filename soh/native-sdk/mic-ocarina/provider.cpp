#include "pitch_matcher.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>

#include <SDL.h>
#include <libultraship/libultra/controller.h>
#include <shiplua/native/ship_native_abi.h>

#include "oot_engine.h"
#include "oot_ocarina.h"

namespace {

using MicOcarina::kSampleRate;
using MicOcarina::kTauMax;
using MicOcarina::kWindowSize;
using MicOcarina::PitchEstimate;
constexpr int kHopSize = 512;
constexpr int kRingSize = 1 << 15;
constexpr int kMedianSize = 5;
constexpr int kReleaseHops = 5;
constexpr int kReanchorHops = 90;
constexpr int kPhraseEndHops = 40;
constexpr float kDipRatio = 0.45f;
constexpr float kRecoverRatio = 0.70f;
constexpr float kJumpCents = 90.0f;
constexpr int kJumpHops = 3;
constexpr int kAttackHops = 3;
constexpr int kMinNoteHops = 9;
constexpr float kGlideCents = 60.0f;
constexpr float kFloorFallRate = 0.25f;
constexpr float kFloorRiseRate = 0.002f;
constexpr float kGateCloseRatio = 0.5f;
constexpr float kNoiseMargin = 4.0f;
constexpr float kAbsoluteGate = 0.0056f;
constexpr float kClarityMin = 0.55f;
constexpr float kMaxPhraseErrorCents = 120.0f;
constexpr float kMatchMarginCents = 40.0f;
constexpr float kPhraseEndMaxErrorCents = 180.0f;

// Rolo de notas do HUD, o mesmo do protótipo MicOcarina: janela de seis segundos
// com até 24 notas, cada uma na altura relativa à primeira nota aceita da frase.
constexpr float kHopSeconds = static_cast<float>(kHopSize) / kSampleRate;
constexpr int kMeterSegmentMax = 24;
constexpr float kMeterWindowSeconds = 6.0f;
constexpr float kMeterSemitoneRange = 12.5f;

// SDL usa posições Xbox. No Switch Pro, o botão físico B aparece como SDL A.
constexpr std::uint8_t kNintendoB = 0;
constexpr std::uint8_t kControllerStart = 6;
constexpr std::uint32_t PhysicalButton(std::uint8_t button) { return std::uint32_t{ 1 } << button; }

struct NoteTracker {
    float centsHistory[kMedianSize]{};
    int historyCount = 0;
    float phraseCents[MicOcarina::kMaxPhraseNotes]{};
    int phraseCount = 0;
    float anchorCents = 0.0f;
    bool hasAnchor = false;
    float noteCents = 0.0f;
    bool noteActive = false;
    bool noteInPhrase = false;
    int noteHops = 0;
    float holdStartCents = 0.0f;
    float notePeakRms = 0.0f;
    bool sawDip = false;
    int silentHops = 0;
    int deviationHops = 0;
    float pendingCents = 0.0f;
    float pendingRms = 0.0f;
    int pendingHops = 0;
    float noiseFloor = 0.0f;
};

struct MeterSegment {
    float startSeconds = 0.0f;
    float endSeconds = 0.0f;
    float cents = 0.0f;
    bool accepted = false;
};

struct MeterHistory {
    MeterSegment segments[kMeterSegmentMax]{};
    int count = 0;
    float nowSeconds = 0.0f;
    float anchorCents = 0.0f;
    bool hasAnchor = false;
};

struct Capture {
    std::array<float, kRingSize> ring{};
    std::atomic<std::uint32_t> ringWrite{ 0 };
    std::uint32_t ringRead = 0;
    std::array<float, kTauMax + 2> difference{};
    std::array<float, kTauMax + 2> cmndf{};
    MicOcarina::SongPattern songs[MicOcarina::kMaxSongs]{};
    int songCount = 0;
    std::atomic<std::uint16_t> availableFlags{ 0 };
    std::atomic<int> matchedSong{ -1 };
    std::atomic<std::uint32_t> matchSequence{ 0 };
    std::atomic<std::uint32_t> capturedSamples{ 0 };
    std::atomic<float> telemetryRms{ 0.0f };
    std::atomic<float> telemetryGate{ kAbsoluteGate };
    std::atomic<float> telemetryHz{ 0.0f };
    std::atomic<float> telemetryClarity{ 0.0f };
    std::atomic<int> telemetryTone{ 0 };
    std::atomic<int> telemetryPhraseCount{ 0 };
    NoteTracker tracker{};
    // Escrito pelo worker a cada hop e copiado inteiro pela thread do jogo. O worker é
    // uma thread comum, não o callback do dispositivo, então um mutex basta.
    std::mutex meterMutex;
    MeterHistory meter{};
    SDL_AudioDeviceID device = 0;
    bool audioSubsystemReady = false;
    std::thread worker;
    std::atomic<bool> workerRunning{ false };
    char lastError[192]{};

    static void AudioCallback(void* user, Uint8* stream, int length) {
        auto& capture = *static_cast<Capture*>(user);
        const float* samples = reinterpret_cast<const float*>(stream);
        const int count = length / static_cast<int>(sizeof(float));
        const std::uint32_t write = capture.ringWrite.load(std::memory_order_relaxed);
        for (int i = 0; i < count; ++i) {
            capture.ring[(write + static_cast<std::uint32_t>(i)) & (kRingSize - 1)] = samples[i];
        }
        capture.ringWrite.store(write + static_cast<std::uint32_t>(count), std::memory_order_release);
        capture.capturedSamples.fetch_add(static_cast<std::uint32_t>(count), std::memory_order_relaxed);
    }

    float Rms(const float* window) const {
        float sum = 0.0f;
        for (int i = 0; i < kWindowSize; ++i) {
            sum += window[i] * window[i];
        }
        return std::sqrt(sum / static_cast<float>(kWindowSize));
    }

    PitchEstimate EstimatePitch(const float* window) {
        return MicOcarina::EstimatePitch(window, difference.data(), cmndf.data());
    }

    static float Median(const float* values, int count) {
        float sorted[kMedianSize]{};
        std::copy(values, values + count, sorted);
        std::sort(sorted, sorted + count);
        return sorted[count / 2];
    }

    void ResetMeter() {
        std::lock_guard<std::mutex> lock(meterMutex);
        meter = MeterHistory{};
    }

    void ClearMeter() {
        std::lock_guard<std::mutex> lock(meterMutex);
        meter.count = 0;
        meter.hasAnchor = false;
    }

    void BeginMeterSegment(float cents) {
        std::lock_guard<std::mutex> lock(meterMutex);
        if (meter.count >= kMeterSegmentMax) {
            std::copy(meter.segments + 1, meter.segments + kMeterSegmentMax, meter.segments);
            meter.count = kMeterSegmentMax - 1;
        }
        meter.segments[meter.count++] = { meter.nowSeconds, meter.nowSeconds, cents, false };
    }

    void AcceptMeterSegment(float cents) {
        std::lock_guard<std::mutex> lock(meterMutex);
        if (meter.count == 0) {
            return;
        }
        MeterSegment& live = meter.segments[meter.count - 1];
        live.cents = cents;
        live.accepted = true;
        meter.anchorCents = tracker.anchorCents;
        meter.hasAnchor = tracker.hasAnchor;
    }

    // Uma vez por hop, silêncio incluído: o rolo continua andando e a barra da nota
    // sustentada continua crescendo.
    void AdvanceMeter(bool noteHeld) {
        std::lock_guard<std::mutex> lock(meterMutex);
        meter.nowSeconds += kHopSeconds;
        if (noteHeld && meter.count > 0) {
            meter.segments[meter.count - 1].endSeconds = meter.nowSeconds;
        }
    }

    MeterHistory SnapshotMeter() {
        std::lock_guard<std::mutex> lock(meterMutex);
        return meter;
    }

    void PublishMatch(float maxError, float margin) {
        const auto ranked = MicOcarina::RankSuffixes(tracker.phraseCents, tracker.phraseCount, songs, songCount,
                                                      availableFlags.load(std::memory_order_relaxed));
        if (!MicOcarina::IsConfident(ranked, maxError, margin)) {
            return;
        }
        matchedSong.store(ranked.song, std::memory_order_relaxed);
        matchSequence.fetch_add(1, std::memory_order_release);
        tracker.phraseCount = 0;
    }

    // O tom da frase é marcado pela primeira nota sustentada o bastante, nunca por
    // uma candidata ou um ruído.
    void PushPhraseNote(float cents) {
        if (!tracker.hasAnchor) {
            tracker.anchorCents = cents;
            tracker.hasAnchor = true;
        }
        if (tracker.phraseCount >= MicOcarina::kMaxPhraseNotes) {
            std::memmove(tracker.phraseCents, &tracker.phraseCents[1],
                         (MicOcarina::kMaxPhraseNotes - 1) * sizeof(float));
            tracker.phraseCount = MicOcarina::kMaxPhraseNotes - 1;
        }
        tracker.phraseCents[tracker.phraseCount++] = cents;
    }

    void CommitNote(float cents, float rms) {
        BeginMeterSegment(cents);
        tracker.noteCents = cents;
        tracker.noteActive = true;
        tracker.noteInPhrase = false;
        tracker.noteHops = 0;
        tracker.holdStartCents = cents;
        tracker.notePeakRms = rms;
        tracker.sawDip = false;
        tracker.deviationHops = 0;
        tracker.pendingHops = 0;
    }

    void ProposeNote(float cents, float rms) {
        if (tracker.pendingHops > 0 && std::fabs(cents - tracker.pendingCents) > kJumpCents) {
            tracker.pendingHops = 0;
        }
        if (tracker.pendingHops == 0) {
            tracker.pendingCents = cents;
            tracker.pendingRms = rms;
        }
        tracker.pendingRms = std::max(tracker.pendingRms, rms);
        if (++tracker.pendingHops >= kAttackHops) {
            CommitNote(cents, tracker.pendingRms);
        }
    }

    void EndNote() {
        tracker.noteActive = false;
        tracker.historyCount = 0;
        tracker.pendingHops = 0;
    }

    void PublishTelemetry(float rms, float gate, const PitchEstimate& estimate, bool tone) {
        telemetryRms.store(rms, std::memory_order_relaxed);
        telemetryGate.store(gate, std::memory_order_relaxed);
        telemetryHz.store(estimate.hz, std::memory_order_relaxed);
        telemetryClarity.store(estimate.clarity, std::memory_order_relaxed);
        telemetryTone.store(tone ? 1 : 0, std::memory_order_relaxed);
        telemetryPhraseCount.store(tracker.phraseCount, std::memory_order_release);
    }

    void ProcessWindow(const float* window) {
        AdvanceMeter(tracker.noteActive);
        const float rms = Rms(window);
        if (rms < tracker.noiseFloor) {
            tracker.noiseFloor += kFloorFallRate * (rms - tracker.noiseFloor);
        } else if (!tracker.noteActive) {
            tracker.noiseFloor += kFloorRiseRate * (rms - tracker.noiseFloor);
        }
        const float openLevel = std::max(kAbsoluteGate, tracker.noiseFloor * kNoiseMargin);
        const float gate = tracker.noteActive ? openLevel * kGateCloseRatio : openLevel;
        const PitchEstimate estimate = rms > gate ? EstimatePitch(window) : PitchEstimate{};
        if (estimate.clarity <= kClarityMin || estimate.hz <= 0.0f) {
            ++tracker.silentHops;
            if (tracker.noteActive && tracker.silentHops >= kReleaseHops) {
                EndNote();
            }
            if (tracker.silentHops == kPhraseEndHops) {
                PublishMatch(kPhraseEndMaxErrorCents, 0.0f);
            }
            // Uma pausa longa encerra a frase: a próxima nota marca o tom de novo e o
            // rolo recomeça vazio.
            if (tracker.silentHops >= kReanchorHops) {
                tracker.phraseCount = 0;
                tracker.hasAnchor = false;
                ClearMeter();
            }
            PublishTelemetry(rms, gate, estimate, false);
            return;
        }
        tracker.silentHops = 0;

        const float cents = 1200.0f * std::log2(estimate.hz / 440.0f);
        if (tracker.historyCount < kMedianSize) {
            tracker.centsHistory[tracker.historyCount++] = cents;
        } else {
            std::memmove(tracker.centsHistory, &tracker.centsHistory[1], (kMedianSize - 1) * sizeof(float));
            tracker.centsHistory[kMedianSize - 1] = cents;
        }
        const float smoothed = Median(tracker.centsHistory, tracker.historyCount);
        if (!tracker.noteActive) {
            ProposeNote(smoothed, rms);
            PublishTelemetry(rms, gate, estimate, true);
            return;
        }

        tracker.notePeakRms = std::max(tracker.notePeakRms, rms);
        if (rms < kDipRatio * tracker.notePeakRms) {
            tracker.sawDip = true;
        }
        if (tracker.sawDip && rms > kRecoverRatio * tracker.notePeakRms) {
            CommitNote(smoothed, rms);
            PublishTelemetry(rms, gate, estimate, true);
            return;
        }
        if (std::fabs(smoothed - tracker.noteCents) > kJumpCents) {
            if (++tracker.deviationHops >= kJumpHops) {
                CommitNote(smoothed, rms);
            }
            PublishTelemetry(rms, gate, estimate, true);
            return;
        }
        tracker.deviationHops = 0;
        tracker.noteCents += 0.15f * (smoothed - tracker.noteCents);
        if (!tracker.noteInPhrase && std::fabs(smoothed - tracker.holdStartCents) > kGlideCents) {
            tracker.holdStartCents = smoothed;
            tracker.noteHops = 0;
        }
        if (!tracker.noteInPhrase && ++tracker.noteHops >= kMinNoteHops) {
            tracker.noteInPhrase = true;
            PushPhraseNote(tracker.noteCents);
            AcceptMeterSegment(tracker.noteCents);
            PublishMatch(kMaxPhraseErrorCents, kMatchMarginCents);
        }
        PublishTelemetry(rms, gate, estimate, true);
    }

    void Run() {
        float window[kWindowSize]{};
        while (workerRunning.load(std::memory_order_acquire)) {
            const std::uint32_t write = ringWrite.load(std::memory_order_acquire);
            if (write - ringRead < kWindowSize) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
                continue;
            }
            if (write - ringRead > kRingSize / 2) {
                ringRead = write - kWindowSize;
            }
            for (int i = 0; i < kWindowSize; ++i) {
                window[i] = ring[(ringRead + static_cast<std::uint32_t>(i)) & (kRingSize - 1)];
            }
            ringRead += kHopSize;
            ProcessWindow(window);
        }
    }

    bool Open() {
        lastError[0] = '\0';
        if (device != 0) {
            return true;
        }
        if (!audioSubsystemReady) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                std::snprintf(lastError, sizeof(lastError), "SDL audio indisponível: %s", SDL_GetError());
                return false;
            }
            audioSubsystemReady = true;
        }
        SDL_AudioSpec wanted{};
        wanted.freq = kSampleRate;
        wanted.format = AUDIO_F32SYS;
        wanted.channels = 1;
        wanted.samples = kHopSize;
        wanted.callback = AudioCallback;
        wanted.userdata = this;
        SDL_AudioSpec obtained{};
        device = SDL_OpenAudioDevice(nullptr, 1, &wanted, &obtained, 0);
        if (device == 0) {
            std::snprintf(lastError, sizeof(lastError), "não foi possível abrir o microfone: %s", SDL_GetError());
            return false;
        }
        tracker = {};
        ResetMeter();
        matchedSong.store(-1, std::memory_order_relaxed);
        capturedSamples.store(0, std::memory_order_relaxed);
        telemetryRms.store(0.0f, std::memory_order_relaxed);
        telemetryGate.store(kAbsoluteGate, std::memory_order_relaxed);
        telemetryHz.store(0.0f, std::memory_order_relaxed);
        telemetryClarity.store(0.0f, std::memory_order_relaxed);
        telemetryTone.store(0, std::memory_order_relaxed);
        telemetryPhraseCount.store(0, std::memory_order_relaxed);
        ringRead = ringWrite.load(std::memory_order_acquire);
        workerRunning.store(true, std::memory_order_release);
        worker = std::thread([this] { Run(); });
        SDL_PauseAudioDevice(device, 0);
        return true;
    }

    void Close() {
        if (device == 0) {
            return;
        }
        SDL_PauseAudioDevice(device, 1);
        SDL_LockAudioDevice(device);
        SDL_UnlockAudioDevice(device);
        workerRunning.store(false, std::memory_order_release);
        if (worker.joinable()) {
            worker.join();
        }
        SDL_CloseAudioDevice(device);
        device = 0;
        tracker = {};
        ResetMeter();
        matchedSong.store(-1, std::memory_order_relaxed);
        capturedSamples.store(0, std::memory_order_relaxed);
        telemetryRms.store(0.0f, std::memory_order_relaxed);
        telemetryGate.store(kAbsoluteGate, std::memory_order_relaxed);
        telemetryHz.store(0.0f, std::memory_order_relaxed);
        telemetryClarity.store(0.0f, std::memory_order_relaxed);
        telemetryTone.store(0, std::memory_order_relaxed);
        telemetryPhraseCount.store(0, std::memory_order_relaxed);
    }

    // O SDL estático pertence à DLL: encerrar o subsistema antes do unload remove as
    // threads e os callbacks de dispositivo do driver de áudio que apontariam para
    // código já descarregado.
    void Shutdown() {
        Close();
        if (audioSubsystemReady) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audioSubsystemReady = false;
        }
    }
};

struct Mod {
    const ShipOotMovementV1* movement = nullptr;
    const ShipOotOcarinaV1* ocarina = nullptr;
    Capture capture;
    bool startWasDown = false;
    bool bWasDown = false;
    std::uint32_t lastMatchSequence = 0;
};

ShipNativeStatus Write(ShipNativeWriteFn write, void* writer, const char* text) {
    return write(writer, text, static_cast<std::uint32_t>(std::strlen(text)));
}

// Rolo para o Lua: novo:antigo:semitons:flags por nota. Tempos em ms desde o fim e o
// começo da nota, semitons x10 relativos ao tom da frase, flags 1 = aceita na frase e
// 2 = nota sustentada agora.
void FormatNoteRoll(const MeterHistory& meter, char* out, std::size_t capacity) {
    out[0] = '\0';
    int used = 0;
    for (int i = 0; i < meter.count; ++i) {
        if (used < 0 || used >= static_cast<int>(capacity) - 24) {
            break;
        }
        const MeterSegment& segment = meter.segments[i];
        const float newest = meter.nowSeconds - segment.endSeconds;
        if (newest >= kMeterWindowSeconds) {
            continue;
        }
        const float oldest = std::min(kMeterWindowSeconds, meter.nowSeconds - segment.startSeconds);
        const float semis = meter.hasAnchor ? std::clamp((segment.cents - meter.anchorCents) / 100.0f,
                                                         -kMeterSemitoneRange, kMeterSemitoneRange)
                                            : 0.0f;
        const bool live = i == meter.count - 1 && segment.endSeconds >= meter.nowSeconds;
        used += std::snprintf(out + used, capacity - static_cast<std::size_t>(used), "%s%d:%d:%ld:%d",
                              used > 0 ? "," : "", static_cast<int>(newest * 1000.0f),
                              static_cast<int>(oldest * 1000.0f), std::lround(semis * 10.0f),
                              (segment.accepted ? 1 : 0) | (live ? 2 : 0));
    }
}

bool LoadPatterns(Mod& mod) {
    mod.capture.songCount = std::min<int>(mod.ocarina->get_song_count(), MicOcarina::kMaxSongs);
    for (int i = 0; i < mod.capture.songCount; ++i) {
        auto& pattern = mod.capture.songs[i];
        pattern = {};
        pattern.song = static_cast<std::uint8_t>(i);
        std::uint32_t count = 0;
        if (mod.ocarina->get_song_pattern(pattern.song, pattern.buttons, MicOcarina::kMaxPhraseNotes, &count) !=
                SHIP_NATIVE_OK ||
            count < 2 || count > MicOcarina::kMaxPhraseNotes) {
            return false;
        }
        pattern.length = static_cast<std::uint8_t>(count);
    }
    return mod.capture.songCount > 0;
}

ShipNativeStatus SHIP_NATIVE_CALL Status(void* user, const char*, std::uint32_t length, ShipNativeWriteFn write,
                                         void* writer) {
    if (!user || length != 0 || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    return Write(write, writer, "pronto; ocarina + Start ativa microfone; B encerra");
}

ShipNativeStatus SHIP_NATIVE_CALL Update(void* user, const char*, std::uint32_t length, ShipNativeWriteFn write,
                                         void* writer) {
    if (!user || length != 0 || !write) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& mod = *static_cast<Mod*>(user);
    const bool hasGamepad = mod.movement->has_gamepad(0) != 0;
    const std::uint32_t physical = hasGamepad ? mod.movement->get_gamepad_buttons(0) : 0;
    const std::uint16_t virtualButtons = mod.movement->get_input_current(0);
    const bool startDown = hasGamepad ? (physical & PhysicalButton(kControllerStart)) != 0
                                      : (virtualButtons & BTN_START) != 0;
    const bool bDown = hasGamepad ? (physical & PhysicalButton(kNintendoB)) != 0
                                  : (virtualButtons & BTN_B) != 0;
    const bool startPressed = startDown && !mod.startWasDown;
    const bool bPressed = bDown && !mod.bWasDown;
    mod.startWasDown = startDown;
    mod.bWasDown = bDown;

    if (!mod.ocarina->is_active()) {
        const bool wasCapturing = mod.capture.device != 0;
        mod.capture.Close();
        return Write(write, writer, wasCapturing ? "audio-input-off: ocarina guardada" : "aguardando ocarina");
    }

    if (mod.capture.device == 0) {
        if (!startPressed) {
            return Write(write, writer, "ocarina ativa; aperte Start para usar o microfone");
        }
        if (!LoadPatterns(mod)) {
            return Write(write, writer, "erro: padrões de música do OoT indisponíveis");
        }
        mod.capture.availableFlags.store(mod.ocarina->get_available_song_flags(), std::memory_order_relaxed);
        if (!mod.capture.Open()) {
            return Write(write, writer, mod.capture.lastError);
        }
        mod.lastMatchSequence = mod.capture.matchSequence.load(std::memory_order_acquire);
        return Write(write, writer, "audio-input-on");
    }

    if (bPressed) {
        mod.capture.Close();
        return Write(write, writer, "audio-input-off: B");
    }

    mod.capture.availableFlags.store(mod.ocarina->get_available_song_flags(), std::memory_order_relaxed);
    const std::uint32_t sequence = mod.capture.matchSequence.load(std::memory_order_acquire);
    if (sequence != mod.lastMatchSequence) {
        mod.lastMatchSequence = sequence;
        const int song = mod.capture.matchedSong.load(std::memory_order_relaxed);
        if (song >= 0 && mod.ocarina->submit_song(static_cast<std::uint8_t>(song)) == SHIP_NATIVE_OK) {
            char response[64];
            std::snprintf(response, sizeof(response), "música reconhecida: %d", song);
            mod.capture.Close();
            return Write(write, writer, response);
        }
    }
    const MeterHistory meter = mod.capture.SnapshotMeter();
    char roll[512];
    FormatNoteRoll(meter, roll, sizeof(roll));
    char response[1024];
    std::snprintf(response, sizeof(response),
                  "audio-input-on;rms=%.6f;gate=%.6f;hz=%.2f;clarity=%.3f;tone=%d;phrase=%d;samples=%u;anchor=%d;"
                  "roll=%s",
                  mod.capture.telemetryRms.load(std::memory_order_relaxed),
                  mod.capture.telemetryGate.load(std::memory_order_relaxed),
                  mod.capture.telemetryHz.load(std::memory_order_relaxed),
                  mod.capture.telemetryClarity.load(std::memory_order_relaxed),
                  mod.capture.telemetryTone.load(std::memory_order_relaxed),
                  mod.capture.telemetryPhraseCount.load(std::memory_order_acquire),
                  mod.capture.capturedSamples.load(std::memory_order_relaxed), meter.hasAnchor ? 1 : 0, roll);
    return Write(write, writer, response);
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || runtime->size < sizeof(ShipNativeRuntime) || !runtime->get_service || !runtime->register_function ||
        !instance) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *instance = nullptr;
    const auto* movement = static_cast<const ShipOotMovementV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_MOVEMENT_SERVICE, LINKSPAN_OOT_MOVEMENT_VERSION, sizeof(ShipOotMovementV1)));
    const auto* ocarina = static_cast<const ShipOotOcarinaV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_OCARINA_SERVICE, LINKSPAN_OOT_OCARINA_VERSION, sizeof(ShipOotOcarinaV1)));
    if (!movement || !ocarina || !movement->get_input_current || !movement->has_gamepad ||
        !movement->get_gamepad_buttons || !ocarina->is_active || !ocarina->get_available_song_flags ||
        !ocarina->get_song_count || !ocarina->get_song_pattern || !ocarina->submit_song) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* mod = new (std::nothrow) Mod;
    if (!mod) {
        return SHIP_NATIVE_FAILURE;
    }
    mod->movement = movement;
    mod->ocarina = ocarina;
    *instance = mod;
    if (runtime->register_function(runtime->context, "status", Status, mod) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "update", Update, mod) != SHIP_NATIVE_OK) {
        delete mod;
        *instance = nullptr;
        return SHIP_NATIVE_FAILURE;
    }
    return SHIP_NATIVE_OK;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* mod = static_cast<Mod*>(instance);
    if (mod) {
        mod->capture.Shutdown();
    }
    delete mod;
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, SHIP_NATIVE_ABI_MINOR, Init, Shutdown
    };
    return &descriptor;
}
