#include "soh/mmaudio/MmSoundFont.h"

#include <string>

#include <spdlog/spdlog.h>
#include <z64.h>

#include "z64audio.h"

#include "soh/ResourceManagerHelpers.h"
#include "soh/mmaudio/MmSfxPlayer.h"

namespace ShipLua {

namespace {

// Mesmo prefixo que o MmCrossWorldArchive aplica ao montar o mm.o2r.
// Ver ShipLuaBootstrap.cpp (kMmNamespace).
constexpr const char* kMmNamespace = "mm/";

struct FontStats {
    int declared = 0; // referências de amostra que o font realmente declara
    int resolved = 0; // as que apontaram para dados de verdade
    uint32_t firstSampleSize = 0;
};

void CountSample(const SoundFontSound& sound, FontStats& stats) {
    stats.declared++;
    if (sound.sample == nullptr) {
        return;
    }
    stats.resolved++;
    if (stats.firstSampleSize == 0) {
        stats.firstSampleSize = sound.sample->size;
    }
}

FontStats CollectStats(const SoundFont* font) {
    FontStats stats;
    if (font == nullptr) {
        return stats;
    }

    for (uint32_t i = 0; i < font->numSfx; i++) {
        CountSample(font->soundEffects[i], stats);
    }

    for (uint8_t i = 0; i < font->numInstruments; i++) {
        const Instrument* instrument = font->instruments[i];
        if (instrument == nullptr) {
            continue;
        }
        CountSample(instrument->lowNotesSound, stats);
        CountSample(instrument->normalNotesSound, stats);
        CountSample(instrument->highNotesSound, stats);
    }

    for (uint8_t i = 0; i < font->numDrums; i++) {
        const Drum* drum = font->drums[i];
        if (drum == nullptr) {
            continue;
        }
        CountSample(drum->sound, stats);
    }

    return stats;
}

} // namespace

SoundFont* LoadMmSoundFont(const std::string& pathWithinMm) {
    // O factory deriva o namespace das amostras do caminho do próprio recurso,
    // então basta pedir pelo caminho prefixado — nada de estado a instalar.
    const std::string fullPath = kMmNamespace + pathWithinMm;
    return ResourceMgr_LoadAudioSoundFontByName(fullPath.c_str());
}

bool MmAudio_PlayFontSfx(int fontIndex, int sfxIndex) {
    if (fontIndex < 0 || sfxIndex < 0) {
        return false;
    }

    SoundFont* font = LoadMmSoundFont("audio/fonts/Soundfont_" + std::to_string(fontIndex));
    if (font == nullptr) {
        SPDLOG_WARN("ShipLua/mmaudio: Soundfont_{} do MM nÃ£o carregou", fontIndex);
        return false;
    }
    if (sfxIndex >= (int)font->numSfx) {
        SPDLOG_WARN("ShipLua/mmaudio: sfx {} fora da faixa do Soundfont_{} (numSfx={})", sfxIndex, fontIndex,
                    font->numSfx);
        return false;
    }

    SoundFontSample* sample = font->soundEffects[sfxIndex].sample;
    if (sample == nullptr) {
        SPDLOG_WARN("ShipLua/mmaudio: sfx {} do Soundfont_{} sem amostra", sfxIndex, fontIndex);
        return false;
    }

    return MmAudio_PlayDecodedSample(sample, font->soundEffects[sfxIndex].tuning);
}

bool MmAudio_PlayVoiceSfx(int sfxId) {
    // Mesma decomposição do MM: o banco é endereço, não dado.
    const int bank = (sfxId >> 12) & 0x7;
    const int index = sfxId & 0x3FF;

    // O banco 3 (voz) mora no Soundfont_0 junto com os demais SFX do jogo; se um
    // dia a correspondência banco->font mudar, é aqui que se ajusta.
    (void)bank;
    return MmAudio_PlayFontSfx(0, index);
}

void MmAudio_DumpSfxTable(int fontIndex, int first, int count) {
    SoundFont* font = LoadMmSoundFont("audio/fonts/Soundfont_" + std::to_string(fontIndex));
    if (font == nullptr) {
        return;
    }
    const int last = (first + count < (int)font->numSfx) ? first + count : (int)font->numSfx;
    SPDLOG_INFO("ShipLua/mmaudio: === sfx do Soundfont_{} ({}..{} de {}) ===", fontIndex, first, last - 1,
                font->numSfx);
    for (int i = first; i < last; i++) {
        SoundFontSample* sample = font->soundEffects[i].sample;
        if (sample == nullptr) {
            continue;
        }
        // size e duracao sao o que permite reconhecer o som: o clarao da
        // transformacao e curto, o grito e longo.
        SPDLOG_INFO("  sfx[{:3d}] {:6d}B ~{:4d}ms tuning={:.3f}", i, sample->size,
                    (sample->size / (sample->codec == CODEC_SMALL_ADPCM ? 5 : 9)) * 16 * 1000 / 32000,
                    font->soundEffects[i].tuning);
    }
}

void ProbeMmSoundFonts() {
    SoundFont* font = LoadMmSoundFont("audio/fonts/Soundfont_0");
    if (font == nullptr) {
        SPDLOG_WARN("ShipLua/mmaudio: 'mm/audio/fonts/Soundfont_0' n\xC3\xA3o carregou — "
                    "os assets de \xC3\xA1udio do MM est\xC3\xA3o indispon\xC3\xADveis");
        return;
    }

    // As entradas sem amostra NÃO são falhas: um instrumento que não define
    // variante grave ou aguda deixa esses slots nulos de propósito. Não dá para
    // distingui-las de uma falha olhando só a struct pronta — quem mede isso é o
    // factory, e ele reportou zero falhas de resolução na Fase 1.
    const FontStats stats = CollectStats(font);
    SPDLOG_INFO("ShipLua/mmaudio: Soundfont_0 do MM pronto — inst={} drums={} sfx={} | "
                "{} amostras carregadas de {} slots (o resto s\xC3\xA3o slots vazios do pr\xC3\xB3prio font) | "
                "1a amostra={}B",
                font->numInstruments, font->numDrums, font->numSfx, stats.resolved, stats.declared,
                stats.firstSampleSize);
}

} // namespace ShipLua
