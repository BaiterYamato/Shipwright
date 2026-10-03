// Parser do snapshot de configuração: padrões do fork, faixas, chaves desconhecidas e locale.
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "settings.h"
#include "play_stamp.h"

namespace {
int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FALHOU: %s\n", message);
        ++failures;
    }
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}
} // namespace

int main() {
    WWStyle::Settings defaults;
    Check(defaults.celEnabled && !defaults.shadowsEnabled && !defaults.skyEnabled && Near(defaults.rampCenter, 0.5f) &&
              defaults.shadowMaxDistance == 550 && defaults.starCount == 1000,
          "padrões do fork");

    WWStyle::Settings s;
    auto result = WWStyle::ParseSettings("cel.enabled=0\nshadows.enabled=1\r\ncel.ramp_center=0.25\n"
                                         "shadows.max_distance=1200\n# comentario\n\nsky.stars.count=20\n",
                                         s);
    Check(result.applied == 5 && result.unknown == 0 && result.invalid == 0, "cinco chaves válidas");
    Check(!s.celEnabled && s.shadowsEnabled && Near(s.rampCenter, 0.25f) && s.shadowMaxDistance == 1200,
          "valores aplicados");
    Check(s.starCount == 50, "valor abaixo da faixa é limitado ao mínimo");

    result = WWStyle::ParseSettings("cel.ramp_center=abc\nnova.chave=1\nsem_igual\nshadows.enabled=talvez\n", s);
    Check(result.applied == 0 && result.unknown == 1 && result.invalid == 3, "malformados e desconhecida");
    Check(Near(s.rampCenter, 0.25f) && s.shadowsEnabled, "malformado mantém o valor anterior");

    result = WWStyle::ParseSettings("cel.highlight_intensity=9\nshadows.edge_softness=1.6\ncel.transition_time=nan\n",
                                    s);
    Check(Near(s.highlightIntensity, 2.0f) && s.shadowEdgeSoftness == 2 && result.invalid == 1,
          "máximo, arredondamento de inteiro e NaN");

    // O SoH troca o locale numérico para pt-BR; o parser não pode ler a vírgula nem parar no ponto.
    if (std::setlocale(LC_NUMERIC, "pt_BR.UTF-8") || std::setlocale(LC_NUMERIC, "Portuguese_Brazil.1252")) {
        WWStyle::Settings local;
        WWStyle::ParseSettings("cel.ramp_softness=0.05\n", local);
        Check(Near(local.rampSoftness, 0.05f), "locale pt-BR não muda o ponto decimal");
        std::setlocale(LC_NUMERIC, "C");
    }

    int firstPlay = 0;
    int secondPlay = 0;
    WWStyle::PlayStamp stamp;
    Check(WWStyle::ObservePlay(stamp, &firstPlay, 1, 100), "primeira carga de cena");
    Check(!WWStyle::ObservePlay(stamp, &firstPlay, 1, 101), "continuação da mesma cena");
    Check(WWStyle::ObservePlay(stamp, &firstPlay, 2, 102), "nova cena no mesmo endereço");
    Check(WWStyle::ObservePlay(stamp, &firstPlay, 2, 0), "mesma cena recarregada no mesmo endereço");
    Check(WWStyle::ObservePlay(stamp, &secondPlay, 2, 1), "novo PlayState");
    Check(WWStyle::ObservePlay(stamp, nullptr, -1, 0), "saída do jogo");
    Check(!WWStyle::ObservePlay(stamp, nullptr, -1, 0), "continua fora do jogo");

    if (failures == 0) std::puts("linkspan_ww_style_tests: ok");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
