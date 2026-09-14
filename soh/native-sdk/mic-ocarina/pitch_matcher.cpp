#include "pitch_matcher.h"

#include <algorithm>
#include <cmath>

namespace MicOcarina {
namespace {

constexpr float kNoMatch = 1.0e9f;
constexpr int kScaleDegrees[5] = { 0, 3, 7, 9, 12 };

} // namespace

PitchEstimate EstimatePitch(const float* window, float* difference, float* cmndf) {
    if (!window || !difference || !cmndf) {
        return {};
    }
    for (int tau = 1; tau <= kTauMax; ++tau) {
        float sum = 0.0f;
        for (int i = 0; i < kIntegrationSize; ++i) {
            const float delta = window[i] - window[i + tau];
            sum += delta * delta;
        }
        difference[tau] = sum;
    }
    float running = 0.0f;
    cmndf[0] = 1.0f;
    for (int tau = 1; tau <= kTauMax; ++tau) {
        running += difference[tau];
        cmndf[tau] = running > 0.0f ? difference[tau] * static_cast<float>(tau) / running : 1.0f;
    }
    // Primeiro vale abaixo do limiar, e não o menor de todos: os múltiplos do período também
    // afundam, e ficar com o primeiro mantém a fundamental.
    int best = -1;
    for (int tau = kTauMin; tau < kTauMax; ++tau) {
        if (cmndf[tau] >= kYinThreshold) {
            continue;
        }
        while (tau + 1 < kTauMax && cmndf[tau + 1] < cmndf[tau]) {
            ++tau;
        }
        best = tau;
        break;
    }
    // Subindo já no início da faixa: o vale fica antes dela, acima de 2400 Hz, e qualquer
    // período lido aqui seria de outra nota.
    if (best < 0 || (best == kTauMin && cmndf[best - 1] < cmndf[best])) {
        return {};
    }
    const float previous = cmndf[best - 1];
    const float current = cmndf[best];
    const float next = cmndf[best + 1];
    const float denominator = previous - 2.0f * current + next;
    float period = static_cast<float>(best);
    if (denominator > 0.0f) {
        // Ajuste parabólico entre os vizinhos: o vale está a menos de uma amostra de best.
        period += std::clamp(0.5f * (previous - next) / denominator, -1.0f, 1.0f);
    }
    return PitchEstimate{ static_cast<float>(kSampleRate) / period, 1.0f - current };
}

float PhraseError(const float* cents, int count, const SongPattern& pattern) {
    if (!cents || count < 2 || pattern.length != count || count > kMaxPhraseNotes) {
        return kNoMatch;
    }

    float residual[kMaxPhraseNotes]{};
    float mean = 0.0f;
    for (int i = 0; i < count; ++i) {
        const int degree = pattern.buttons[i];
        if (degree < 0 || degree >= 5) {
            return kNoMatch;
        }
        residual[i] = cents[i] - 100.0f * static_cast<float>(kScaleDegrees[degree]);
        mean += residual[i];
    }
    mean /= static_cast<float>(count);

    float squared = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float error = residual[i] - mean;
        squared += error * error;
    }
    return std::sqrt(squared / static_cast<float>(count));
}

MatchResult RankPhrase(const float* cents, int count, const SongPattern* patterns, int patternCount,
                       std::uint16_t availableFlags) {
    MatchResult ranked;
    if (!cents || !patterns || patternCount < 0 || patternCount > kMaxSongs) {
        return ranked;
    }

    for (int i = 0; i < patternCount; ++i) {
        const SongPattern& pattern = patterns[i];
        if (pattern.song >= 16 || (availableFlags & (std::uint16_t{ 1 } << pattern.song)) == 0) {
            continue;
        }
        const float error = PhraseError(cents, count, pattern);
        if (error < ranked.errorCents) {
            ranked.runnerUpErrorCents = ranked.errorCents;
            ranked.errorCents = error;
            ranked.song = pattern.song;
        } else if (error < ranked.runnerUpErrorCents) {
            ranked.runnerUpErrorCents = error;
        }
    }
    return ranked;
}

// Vibrato ou respiração partem uma nota sustentada em duas. Juntar notas vizinhas mais
// próximas que um intervalo real recupera a frase, mas também apagaria as repetições
// legítimas da Serenade e do Nocturne; por isso a junção entra como hipótese
// alternativa, e não como filtro da frase.
int BuildMergedPhrase(const float* cents, int count, float* merged) {
    int write = 0;
    for (int read = 0; read < count; ++read) {
        if (write > 0 && std::fabs(cents[read] - merged[write - 1]) < kContourDeadbandCents) {
            continue;
        }
        merged[write++] = cents[read];
    }
    return write;
}

MatchResult RankSuffixes(const float* cents, int count, const SongPattern* patterns, int patternCount,
                         std::uint16_t availableFlags) {
    MatchResult ranked;
    if (!cents || !patterns || count < 0 || count > kMaxPhraseNotes || patternCount < 0 ||
        patternCount > kMaxSongs) {
        return ranked;
    }
    for (int i = 0; i < patternCount; ++i) {
        const SongPattern& pattern = patterns[i];
        if (pattern.song >= 16 || pattern.length < 2 || pattern.length > count ||
            (availableFlags & (std::uint16_t{ 1 } << pattern.song)) == 0) {
            continue;
        }
        // Duas hipóteses sobre as notas mais novas: como foram segmentadas, e uma janela
        // duas notas mais larga com as notas partidas juntadas.
        float error = PhraseError(&cents[count - pattern.length], pattern.length, pattern);
        const int widened = pattern.length + 2 <= count ? pattern.length + 2 : count;
        float merged[kMaxPhraseNotes]{};
        const int mergedCount = BuildMergedPhrase(&cents[count - widened], widened, merged);
        if (mergedCount >= pattern.length) {
            error = std::fmin(error, PhraseError(&merged[mergedCount - pattern.length], pattern.length, pattern));
        }
        if (error < ranked.errorCents) {
            ranked.runnerUpErrorCents = ranked.errorCents;
            ranked.errorCents = error;
            ranked.song = pattern.song;
        } else if (error < ranked.runnerUpErrorCents) {
            ranked.runnerUpErrorCents = error;
        }
    }
    return ranked;
}

bool IsConfident(const MatchResult& result, float maxErrorCents, float minimumMarginCents) {
    return result.song >= 0 && result.errorCents <= maxErrorCents &&
           (result.runnerUpErrorCents - result.errorCents) >= minimumMarginCents;
}

} // namespace MicOcarina
