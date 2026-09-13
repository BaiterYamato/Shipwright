#include "soh/mmaudio/MmSfxPlayer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include <spdlog/spdlog.h>
#include <z64.h>

#include "z64audio.h"

#include "soh/ResourceManagerHelpers.h"

namespace ShipLua {

namespace {

// VADPCM do N64, duas variantes — ambas produzem 16 amostras por quadro:
//   CODEC_ADPCM (0)       1 byte de cabeçalho + 8 bytes de nibbles de 4 bits
//   CODEC_SMALL_ADPCM (3) 1 byte de cabeçalho + 4 bytes de pares de 2 bits
// O mm.o2r usa as duas; GoronYawn, por exemplo, é a de 2 bits.
constexpr int kSamplesPerFrame = 16;
constexpr int kOrder = 2; // o VADPCM do N64 é sempre de ordem 2

int16_t Clamp16(int32_t value) {
    if (value < -0x8000) {
        return -0x8000;
    }
    if (value > 0x7FFF) {
        return 0x7FFF;
    }
    return static_cast<int16_t>(value);
}

// Espelha aADPCMdecImpl de soh/soh/mixer.c:183 — a implementação em software do
// microcódigo de áudio do N64 que o próprio SoH já usa. Reproduzida aqui em vez
// de reaproveitada porque aquela opera sobre o DMEM emulado do RSP, e aqui só
// queremos um decodificador direto de amostra para PCM.
std::vector<int16_t> DecodeVadpcm(const SoundFontSample* sample) {
    std::vector<int16_t> pcm;

    if (sample == nullptr || sample->sampleAddr == nullptr || sample->book == nullptr ||
        sample->book->book == nullptr) {
        return pcm;
    }
    const bool small = (sample->codec == CODEC_SMALL_ADPCM);
    if (sample->codec != CODEC_ADPCM && !small) {
        SPDLOG_WARN("ShipLua/mmaudio: codec {} n\xC3\xA3o suportado (s\xC3\xB3 CODEC_ADPCM e CODEC_SMALL_ADPCM)",
                    static_cast<uint32_t>(sample->codec));
        return pcm;
    }
    const int frameBytes = small ? 5 : 9;
    if (sample->book->order != kOrder || sample->book->npredictors <= 0) {
        SPDLOG_WARN("ShipLua/mmaudio: book inesperado (order={} npredictors={})", sample->book->order,
                    sample->book->npredictors);
        return pcm;
    }

    const uint8_t* in = sample->sampleAddr;
    const uint32_t frames = sample->size / frameBytes;
    pcm.reserve(static_cast<size_t>(frames) * kSamplesPerFrame);

    int16_t prev1 = 0;
    int16_t prev2 = 0;

    for (uint32_t frame = 0; frame < frames; frame++) {
        const uint8_t header = *in++;
        const int shift = header >> 4;
        const int tableIndex = header & 0xF;

        if (tableIndex >= sample->book->npredictors) {
            in += frameBytes - 1; // quadro corrompido: pula sem tentar prever
            continue;
        }

        const int16_t* table = sample->book->book + (tableIndex * kOrder * 8);
        const int16_t* row0 = table;
        const int16_t* row1 = table + 8;

        // Duas metades de 8 amostras; prev1/prev2 ficam fixos dentro de cada
        // metade — a recorrência dentro dela sai do somatório em k.
        for (int half = 0; half < 2; half++) {
            int32_t ins[8];
            if (small) {
                // 2 bits por amostra: 4 amostras por byte, 2 bytes por metade.
                // (x << 30) >> 30 estende o sinal do valor de 2 bits.
                for (int j = 0; j < 2; j++) {
                    const uint8_t byte = *in++;
                    ins[j * 4] = ((static_cast<int32_t>(byte >> 6) << 30) >> 30) << shift;
                    ins[j * 4 + 1] = ((static_cast<int32_t>((byte >> 4) & 0x3) << 30) >> 30) << shift;
                    ins[j * 4 + 2] = ((static_cast<int32_t>((byte >> 2) & 0x3) << 30) >> 30) << shift;
                    ins[j * 4 + 3] = ((static_cast<int32_t>(byte & 0x3) << 30) >> 30) << shift;
                }
            } else {
                // 4 bits por amostra: 2 amostras por byte, 4 bytes por metade.
                // (x << 28) >> 28 estende o sinal do valor de 4 bits.
                for (int j = 0; j < 4; j++) {
                    const uint8_t byte = *in++;
                    ins[j * 2] = ((static_cast<int32_t>(byte >> 4) << 28) >> 28) << shift;
                    ins[j * 2 + 1] = ((static_cast<int32_t>(byte & 0xF) << 28) >> 28) << shift;
                }
            }

            int16_t written[8];
            for (int j = 0; j < 8; j++) {
                int32_t acc = row0[j] * prev2 + row1[j] * prev1 + (ins[j] << 11);
                for (int k = 0; k < j; k++) {
                    acc += row1[(j - k) - 1] * ins[k];
                }
                acc >>= 11;
                written[j] = Clamp16(acc);
                pcm.push_back(written[j]);
            }

            prev2 = written[6];
            prev1 = written[7];
        }
    }

    return pcm;
}

struct OneShot {
    std::vector<int16_t> pcm;
    size_t cursor = 0;
};

// A thread do jogo empurra, a thread de áudio consome. Um mutex é aceitável para
// esta fase de prova; a Fase 3 troca por uma fila lock-free, que é o desenho que
// o plano descreve e o que o fork de referência usa.
std::mutex gMutex;
std::vector<OneShot> gActive;
std::atomic<bool> gHasPending{ false };

} // namespace

bool MmAudio_PlaySampleOneShot(const char* prefixedSamplePath) {
    SoundFontSample* sample = ResourceMgr_LoadAudioSample(prefixedSamplePath);
    if (sample == nullptr) {
        SPDLOG_WARN("ShipLua/mmaudio: amostra '{}' n\xC3\xA3o carregou", prefixedSamplePath);
        return false;
    }

    std::vector<int16_t> pcm = DecodeVadpcm(sample);
    if (pcm.empty()) {
        SPDLOG_WARN("ShipLua/mmaudio: amostra '{}' decodificou vazia (size={}B)", prefixedSamplePath, sample->size);
        return false;
    }

    // Pico e RMS dizem se o sinal decodificou com amplitude sã. Um pico na casa
    // das centenas (em vez de milhares) apontaria erro de `shift` no VADPCM:
    // audível só com fone, fácil de confundir com "não saiu som".
    int32_t peak = 0;
    int64_t sumSquares = 0;
    for (const int16_t value : pcm) {
        const int32_t magnitude = value < 0 ? -value : value;
        if (magnitude > peak) {
            peak = magnitude;
        }
        sumSquares += static_cast<int64_t>(value) * value;
    }
    const int32_t rms = pcm.empty() ? 0 : static_cast<int32_t>(std::sqrt(static_cast<double>(sumSquares) / pcm.size()));

    SPDLOG_INFO("ShipLua/mmaudio: tocando '{}' — {}B comprimidos -> {} amostras ({} ms a 32 kHz) | "
                "pico={} rms={} (cheio seria ~32767)",
                prefixedSamplePath, sample->size, pcm.size(), pcm.size() * 1000 / 32000, peak, rms);

    {
        std::lock_guard<std::mutex> lock(gMutex);
        gActive.push_back(OneShot{ std::move(pcm), 0 });
    }
    gHasPending.store(true, std::memory_order_release);
    return true;
}

bool MmAudio_PlayDecodedSample(void* soundFontSample, float tuning) {
    SoundFontSample* sample = reinterpret_cast<SoundFontSample*>(soundFontSample);
    if (sample == nullptr) {
        return false;
    }

    std::vector<int16_t> pcm = DecodeVadpcm(sample);
    if (pcm.empty()) {
        SPDLOG_WARN("ShipLua/mmaudio: amostra de soundfont decodificou vazia (size={}B codec={})", sample->size,
                    static_cast<uint32_t>(sample->codec));
        return false;
    }

    // O tuning do TunedSample reamostra: 1.0 é a taxa original. Reamostragem de
    // ordem zero, igual à do motor de sequência — suficiente para SFX curto.
    if (tuning > 0.01f && (tuning < 0.99f || tuning > 1.01f)) {
        const size_t outCount = static_cast<size_t>(pcm.size() / tuning);
        std::vector<int16_t> resampled;
        resampled.reserve(outCount);
        for (size_t i = 0; i < outCount; i++) {
            const size_t src = static_cast<size_t>(i * tuning);
            resampled.push_back(src < pcm.size() ? pcm[src] : 0);
        }
        pcm.swap(resampled);
    }

    SPDLOG_INFO("ShipLua/mmaudio: sfx de soundfont — {}B -> {} amostras ({} ms) tuning={:.3f}", sample->size,
                pcm.size(), pcm.size() * 1000 / 32000, tuning);

    {
        std::lock_guard<std::mutex> lock(gMutex);
        gActive.push_back(OneShot{ std::move(pcm), 0 });
    }
    gHasPending.store(true, std::memory_order_release);
    return true;
}

bool MmAudio_HasPending() {
    return gHasPending.load(std::memory_order_acquire);
}

void MmAudio_MixInto(int16_t* buffer, uint32_t frames) {
    if (buffer == nullptr || frames == 0 || !MmAudio_HasPending()) {
        return;
    }

    std::lock_guard<std::mutex> lock(gMutex);

    // Prova, uma vez só, que a thread de áudio realmente recebeu o material —
    // separa "enfileirou mas nunca misturou" de "misturou mas não se ouve".
    static bool sLoggedFirstMix = false;
    if (!sLoggedFirstMix && !gActive.empty()) {
        sLoggedFirstMix = true;
        SPDLOG_INFO("ShipLua/mmaudio: primeira mixagem na thread de \xC3\xA1udio — {} quadros, {} amostras pendentes",
                    frames, gActive.front().pcm.size());
    }

    for (auto& shot : gActive) {
        const size_t available = shot.pcm.size() - shot.cursor;
        const size_t count = (available < frames) ? available : frames;

        for (size_t i = 0; i < count; i++) {
            // Amostra mono do MM entra nos dois canais, em volume cheio. Somar
            // sobre o áudio do jogo pode saturar no clamp em picos, o que é
            // aceitável enquanto isto é uma prova; a mixagem de verdade
            // (headroom, ducking, volume por banco) vem na Fase 3.
            const int32_t value = shot.pcm[shot.cursor + i];
            buffer[i * 2] = Clamp16(buffer[i * 2] + value);
            buffer[i * 2 + 1] = Clamp16(buffer[i * 2 + 1] + value);
        }
        shot.cursor += count;
    }

    gActive.erase(std::remove_if(gActive.begin(), gActive.end(),
                                 [](const OneShot& shot) { return shot.cursor >= shot.pcm.size(); }),
                  gActive.end());

    gHasPending.store(!gActive.empty(), std::memory_order_release);
}

} // namespace ShipLua
