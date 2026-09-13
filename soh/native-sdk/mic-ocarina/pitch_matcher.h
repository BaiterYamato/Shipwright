#pragma once

#include <cstdint>

namespace MicOcarina {

constexpr int kMaxSongs = 16;
constexpr int kMaxPhraseNotes = 8;

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

float PhraseError(const float* cents, int count, const SongPattern& pattern);
MatchResult RankPhrase(const float* cents, int count, const SongPattern* patterns, int patternCount,
                       std::uint16_t availableFlags);
MatchResult RankSuffixes(const float* cents, int count, const SongPattern* patterns, int patternCount,
                         std::uint16_t availableFlags);
bool IsConfident(const MatchResult& result, float maxErrorCents, float minimumMarginCents);

} // namespace MicOcarina
