#include "pitch_matcher.h"

#include <cmath>

namespace MicOcarina {
namespace {

constexpr float kNoMatch = 1.0e9f;
constexpr int kScaleDegrees[5] = { 0, 3, 7, 9, 12 };

} // namespace

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
        const float error = PhraseError(&cents[count - pattern.length], pattern.length, pattern);
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
