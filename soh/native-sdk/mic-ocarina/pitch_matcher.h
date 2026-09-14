#pragma once

#include <cstdint>

namespace MicOcarina {

constexpr int kMaxSongs = 16;
constexpr int kMaxPhraseNotes = 8;
// Diferença abaixo da qual duas notas vizinhas contam como a mesma nota relativa: todo
// intervalo real entre notas da ocarina tem pelo menos 200 cents.
constexpr float kContourDeadbandCents = 90.0f;

// Detector YIN: 48 kHz, janela de 2048 amostras e soma das diferenças na primeira metade.
constexpr int kSampleRate = 48000;
constexpr int kWindowSize = 2048;
constexpr int kIntegrationSize = kWindowSize / 2;
// Faixa de período: de 80 Hz, abaixo da voz cantarolada, a 2400 Hz, acima do C7 de um assobio
// agudo. Com o teto de 1000 Hz do protótipo, o período de D6 e das notas acima ficava fora da
// busca, e o primeiro vale aceito era o dobro dele: a nota saía uma oitava abaixo.
constexpr int kTauMin = kSampleRate / 2400;
constexpr int kTauMax = kSampleRate / 80;
constexpr float kYinThreshold = 0.15f;

struct PitchEstimate {
    float hz = 0.0f;
    float clarity = 0.0f;
};

struct SongPattern {
    std::uint8_t song = 0;
    std::uint8_t length = 0;
    std::uint8_t buttons[kMaxPhraseNotes]{};
};

struct MatchResult {
    int song = -1;
    float errorCents = 1.0e9f;
    float runnerUpErrorCents = 1.0e9f;
};

// window precisa de kIntegrationSize + kTauMax amostras, e difference e cmndf, de kTauMax + 2
// posições de trabalho. Sem vale abaixo do limiar dentro da faixa, devolve hz = 0.
PitchEstimate EstimatePitch(const float* window, float* difference, float* cmndf);
float PhraseError(const float* cents, int count, const SongPattern& pattern);
int BuildMergedPhrase(const float* cents, int count, float* merged);
MatchResult RankPhrase(const float* cents, int count, const SongPattern* patterns, int patternCount,
                       std::uint16_t availableFlags);
MatchResult RankSuffixes(const float* cents, int count, const SongPattern* patterns, int patternCount,
                         std::uint16_t availableFlags);
bool IsConfident(const MatchResult& result, float maxErrorCents, float minimumMarginCents);

} // namespace MicOcarina
