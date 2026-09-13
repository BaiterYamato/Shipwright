#include "pitch_matcher.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    const MicOcarina::SongPattern patterns[] = {
        { 0, 4, { 0, 4, 3, 2 } },
        { 1, 4, { 1, 0, 1, 0 } },
    };
    const float transposed[] = { 420.0f, 1620.0f, 1320.0f, 1120.0f };
    const auto match = MicOcarina::RankPhrase(transposed, 4, patterns, 2, 0x0003);
    Check(match.song == 0 && match.errorCents < 0.01f && MicOcarina::IsConfident(match, 120.0f, 40.0f),
          "o matcher deve reconhecer o contorno independentemente da tonalidade");

    const auto unavailable = MicOcarina::RankPhrase(transposed, 4, patterns, 2, 0x0002);
    Check(unavailable.song != 0, "músicas não aceitas pelo jogo devem ser ignoradas");

    const float noisy[] = { 420.0f, 1480.0f, 1500.0f, 700.0f };
    const auto rejected = MicOcarina::RankPhrase(noisy, 4, patterns, 2, 0x0003);
    Check(!MicOcarina::IsConfident(rejected, 120.0f, 40.0f), "contorno ruidoso deve ser recusado");

    const float prefixed[] = { -900.0f, 420.0f, 1620.0f, 1320.0f, 1120.0f };
    const auto suffix = MicOcarina::RankSuffixes(prefixed, 5, patterns, 2, 0x0003);
    Check(suffix.song == 0 && suffix.errorCents < 0.01f, "uma nota espúria anterior não deve estragar o sufixo");

    std::cout << "mic ocarina matcher: ok\n";
    return 0;
}
