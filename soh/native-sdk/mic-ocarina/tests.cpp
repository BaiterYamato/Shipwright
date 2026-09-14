#include "pitch_matcher.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

constexpr double kPi = 3.14159265358979323846;

struct Tone {
    const char* name;
    double hz;
};

// Janela sintética com a fundamental e os harmônicos dados: seno puro para assobio, série
// decrescente para voz.
void Synthesize(float* window, double hz, const float* harmonics, int harmonicCount) {
    for (int i = 0; i < MicOcarina::kWindowSize; ++i) {
        double sample = 0.0;
        for (int h = 0; h < harmonicCount; ++h) {
            sample += harmonics[h] * std::sin(2.0 * kPi * hz * (h + 1) * i / MicOcarina::kSampleRate);
        }
        window[i] = static_cast<float>(0.3 * sample);
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

    // Padrões como o host 78dc6d970 entrega: índices de botão A, C-baixo, C-direita, C-esquerda e C-cima.
    const MicOcarina::SongPattern ootSongs[] = {
        { 0, 6, { 0, 4, 3, 2, 3, 2 } },  { 1, 8, { 1, 0, 1, 0, 2, 1, 2, 1 } }, { 2, 5, { 0, 1, 2, 2, 3 } },
        { 3, 6, { 0, 1, 0, 2, 1, 0 } },  { 4, 7, { 3, 2, 2, 0, 3, 2, 1 } },    { 5, 6, { 4, 2, 4, 2, 3, 4 } },
        { 6, 6, { 1, 2, 3, 1, 2, 3 } },  { 7, 6, { 4, 3, 2, 4, 3, 2 } },       { 8, 6, { 3, 4, 2, 3, 4, 2 } },
        { 9, 6, { 2, 1, 4, 2, 1, 4 } },  { 10, 6, { 2, 0, 1, 2, 0, 1 } },      { 11, 6, { 0, 1, 4, 0, 1, 4 } },
    };

    // Zelda's Lullaby cantarolada no teste de 14/09 (telemetria do Mic de diagnóstico): os dois Lá saíram
    // partidos em duas notas, -2277/-2200 e -2374/-2322. Sem a junção, a frase ficava a 239 cents da música.
    const float lullabySplit[] = { -1992.0f, -1744.0f, -2277.0f, -2200.0f, -1995.0f, -1740.0f, -2374.0f };
    const auto lullaby = MicOcarina::RankSuffixes(lullabySplit, 7, ootSongs, 12, 0x07FF);
    Check(lullaby.song == 8 && MicOcarina::IsConfident(lullaby, 120.0f, 40.0f),
          "nota partida por vibrato não deve impedir a Zelda's Lullaby");

    const float lullabyBothSplit[] = { -1992.0f, -1744.0f, -2277.0f, -2200.0f,
                                       -1995.0f, -1740.0f, -2374.0f, -2322.0f };
    const auto lullabyFull = MicOcarina::RankSuffixes(lullabyBothSplit, 8, ootSongs, 12, 0x07FF);
    Check(lullabyFull.song == 8 && MicOcarina::IsConfident(lullabyFull, 120.0f, 40.0f),
          "as duas notas partidas da Zelda's Lullaby devem ser juntadas");

    // A junção é hipótese alternativa: repetições legítimas, como o Lá-Lá da Serenade, continuam valendo.
    const float serenade[] = { -1500.0f, -1200.0f, -800.0f, -800.0f, -600.0f };
    const auto repeated = MicOcarina::RankSuffixes(serenade, 5, ootSongs, 12, 0x07FF);
    Check(repeated.song == 2 && MicOcarina::IsConfident(repeated, 120.0f, 40.0f),
          "a repetição legítima da Serenade deve continuar reconhecida");

    // Detector: no teste de 14/09, assobio em D6, E6, F6, G6 e A6 aparecia uma oitava abaixo.
    std::array<float, MicOcarina::kWindowSize> window{};
    std::array<float, MicOcarina::kTauMax + 2> difference{};
    std::array<float, MicOcarina::kTauMax + 2> cmndf{};
    const auto detects = [&](const Tone& tone, const float* harmonics, int harmonicCount) {
        Synthesize(window.data(), tone.hz, harmonics, harmonicCount);
        const auto estimate = MicOcarina::EstimatePitch(window.data(), difference.data(), cmndf.data());
        const double cents = estimate.hz > 0.0f ? 1200.0 * std::log2(estimate.hz / tone.hz) : 1.0e6;
        std::printf("%s %.2f Hz -> %.2f Hz (%+.1f cents)\n", tone.name, tone.hz, estimate.hz, cents);
        return std::fabs(cents) < 20.0;
    };
    const Tone whistles[] = { { "C6", 1046.50 }, { "D6", 1174.66 }, { "E6", 1318.51 }, { "F6", 1396.91 },
                              { "G6", 1567.98 }, { "A6", 1760.00 }, { "B6", 1975.53 }, { "C7", 2093.00 } };
    const float pure[] = { 1.0f };
    bool whistlesOk = true;
    for (const auto& tone : whistles) {
        whistlesOk = detects(tone, pure, 1) && whistlesOk;
    }
    Check(whistlesOk, "assobio de C6 a C7 deve sair na oitava certa");

    // Voz cantarolada tem harmônicos fortes, e o teto maior não pode fazer a nota subir de oitava.
    const Tone voices[] = { { "A2", 110.00 }, { "D3", 146.83 }, { "A3", 220.00 }, { "A4", 440.00 }, { "D5", 587.33 } };
    const float hum[] = { 1.0f, 0.6f, 0.4f, 0.25f, 0.15f };
    bool voicesOk = true;
    for (const auto& tone : voices) {
        voicesOk = detects(tone, hum, 5) && voicesOk;
    }
    Check(voicesOk, "voz cantarolada deve manter a fundamental");

    std::cout << "mic ocarina: matcher e detector ok\n";
    return 0;
}
