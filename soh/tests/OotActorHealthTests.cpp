#include "soh/OotActorHealth.h"

#include <iostream>
#include <limits>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

using namespace ShipLuaHost;

void TestMirror() {
    const ExtendedHealth health = OotHealthEnable(4, 4, 535);
    Check(OotHealthMirror(health, 535) == 4, "espelho de 535 deve ser 4");
    Check(OotHealthMirror(health, 400) == 3, "espelho de 400 deve ser 3");
    Check(OotHealthMirror(health, 134) == 2, "espelho de 134 deve ser 2");
    Check(OotHealthMirror(health, 133) == 1, "espelho de 133 deve ser 1");
    Check(OotHealthMirror(health, 1) == 1, "vida positiva deve ter espelho positivo");
    Check(OotHealthMirror(health, 0) == 0, "vida zerada deve ter espelho zero");
    Check(OotHealthMirror(health, std::numeric_limits<uint32_t>::max()) == 4, "espelho nunca deve passar de V");
}

void TestEnable() {
    const ExtendedHealth full = OotHealthEnable(4, 4, 535);
    Check(full.max == 535 && full.hp == 535 && full.vanillaMax == 4 && full.pending == 0,
          "ativacao cheia deve registrar max, hp, V e pendente");
    const ExtendedHealth half = OotHealthEnable(4, 2, 535);
    Check(half.hp == 267 && OotHealthMirror(half, half.hp) == 2, "ativacao parcial deve arredondar hp para baixo");
    Check(OotHealthEnable(4, 1, 535).hp == 133, "ativacao com vida 1 deve produzir hp 133");
    Check(OotHealthEnable(4, 0, 535).hp == 0, "ativacao nao deve ressuscitar o ator");
    const ExtendedHealth minimum = OotHealthEnable(255, 1, 1);
    Check(minimum.hp == 1 && OotHealthMirror(minimum, minimum.hp) == 255,
          "ativacao deve preservar vida positiva mesmo com max menor que V");
}

void TestDebitAndApply() {
    ExtendedHealth health = OotHealthEnable(4, 4, 535);
    Check(OotHealthAddDebit(health, 135) == 1, "primeiro debito deve baixar o espelho de 4 para 3");
    Check(OotHealthAddDebit(health, 300) == 2, "segundo debito deve baixar o espelho de 3 para 1");
    Check(health.pending == 435 && health.hp == 535, "funil deve acumular sem confirmar hp");
    const OotHealthCommit first = OotHealthApply(health, 1);
    Check(first.vanillaHealth == 1 && first.amount == 435 && health.hp == 100 && health.pending == 0,
          "apply deve confirmar os 435 acumulados e zerar pendente");
    Check(OotHealthAddDebit(health, 600) == 1 && health.pending == 100, "debito deve saturar na vida restante");
    Check(OotHealthAddDebit(health, std::numeric_limits<uint32_t>::max()) == 0 && health.pending == 100,
          "debito saturado nao deve acumular nem baixar novamente o espelho");
    const OotHealthCommit last = OotHealthApply(health, 1);
    Check(last.vanillaHealth == 0 && last.amount == 100 && health.hp == 0 && health.pending == 0,
          "apply letal deve consumir somente a vida restante");
    Check(OotHealthAddDebit(health, 1) == 0 && health.pending == 0, "ator morto nao deve acumular debito");
    const OotHealthCommit empty = OotHealthApply(health, 1);
    Check(empty.vanillaHealth == 0 && empty.amount == 0, "apply repetido nao deve consumir vida");
}

void TestSmallHit() {
    ExtendedHealth health = OotHealthEnable(4, 4, 535);
    Check(OotHealthAddDebit(health, 1) == 0 && health.pending == 1, "golpe pequeno nao deve mudar o espelho");
    uint32_t amount = 999;
    Check(OotHealthSettleAfterUpdate(health, 4, amount) == OotHealthSettle::Discarded && amount == 0 &&
              health.hp == 535 && health.pending == 0,
          "settle deve descartar golpe sem consumo visivel e zerar amount");
    OotHealthAddDebit(health, 1);
    const OotHealthCommit hit = OotHealthApply(health, 1);
    Check(hit.vanillaHealth == 4 && hit.amount == 1 && health.hp == 534 && health.pending == 0,
          "apply deve confirmar golpe pequeno mesmo sem mudar o espelho");
}

void TestManualConsumption() {
    ExtendedHealth health = OotHealthEnable(4, 4, 535);
    OotHealthAddDebit(health, 135);
    uint32_t amount = 999;
    Check(OotHealthSettleAfterUpdate(health, 3, amount) == OotHealthSettle::Committed && amount == 135 &&
              health.hp == 400 && health.pending == 0,
          "settle deve confirmar consumo manual visivel");
    Check(OotHealthSettleAfterUpdate(health, 3, amount) == OotHealthSettle::None && amount == 0 && health.hp == 400,
          "settle repetido nao deve confirmar duas vezes");

    health = OotHealthEnable(4, 4, 535);
    OotHealthAddDebit(health, 135);
    Check(OotHealthSettleAfterUpdate(health, 4, amount) == OotHealthSettle::Discarded && amount == 0 &&
              health.hp == 535 && health.pending == 0,
          "settle deve descartar tambem golpe visivel ignorado pelo ator");

    OotHealthAddDebit(health, 535);
    Check(OotHealthSettleAfterUpdate(health, 0, amount) == OotHealthSettle::Committed && amount == 535 &&
              health.hp == 0 && health.pending == 0,
          "settle deve confirmar consumo manual letal");
}

void TestExternalChanges() {
    ExtendedHealth health{ 535, 100, 4, 0 };
    uint32_t amount = 999;
    Check(OotHealthMirror(health, health.hp) == 1, "hp 100 deve ter espelho 1");
    Check(OotHealthSettleAfterUpdate(health, 4, amount) == OotHealthSettle::Rescaled && amount == 0 &&
              health.hp == 535 && health.pending == 0,
          "cura externa deve reescalar a vida real");
    amount = 999;
    Check(OotHealthSettleAfterUpdate(health, 4, amount) == OotHealthSettle::None && amount == 0 && health.hp == 535,
          "settle estavel deve retornar None e zerar amount");

    OotHealthAddDebit(health, 135);
    Check(OotHealthSettleAfterUpdate(health, 2, amount) == OotHealthSettle::Rescaled && amount == 0 &&
              health.hp == 267 && health.pending == 0,
          "vida inesperada deve reescalar por piso e limpar debito");
    Check(OotHealthSettleAfterUpdate(health, 0, amount) == OotHealthSettle::Rescaled && amount == 0 && health.hp == 0,
          "morte externa deve zerar hp");
    Check(OotHealthSettleAfterUpdate(health, 0, amount) == OotHealthSettle::None && amount == 0,
          "ator morto estavel deve retornar None");

    health = OotHealthEnable(255, 0, 1);
    Check(OotHealthSettleAfterUpdate(health, 1, amount) == OotHealthSettle::Rescaled && health.hp == 1 && amount == 0,
          "reescalonamento externo positivo deve preservar hp minimo 1");
}

void TestCooperativeDamage() {
    ExtendedHealth health = OotHealthEnable(4, 4, 535);
    OotHealthAddDebit(health, 100);
    const OotHealthCommit doubled = OotHealthApply(health, 2);
    Check(doubled.vanillaHealth == 3 && doubled.amount == 200 && health.hp == 335 && health.pending == 0,
          "cooperativo deve multiplicar o debito real");
    OotHealthAddDebit(health, 100);
    const OotHealthCommit saturated = OotHealthApply(health, std::numeric_limits<uint32_t>::max());
    Check(saturated.amount == 335 && saturated.vanillaHealth == 0 && health.hp == 0 && health.pending == 0,
          "multiplicador grande deve saturar sem overflow");

    health = OotHealthEnable(4, 4, 535);
    OotHealthAddDebit(health, 135);
    const OotHealthCommit ignored = OotHealthApply(health, 0);
    Check(ignored.amount == 0 && ignored.vanillaHealth == 4 && health.hp == 535 && health.pending == 0,
          "multiplicador zero deve limpar debito sem consumir vida");
}

void TestRescale() {
    ExtendedHealth health = OotHealthEnable(4, 4, 535);
    OotHealthAddDebit(health, 135);
    OotHealthRescale(health, 1070);
    Check(health.max == 1070 && health.hp == 1070 && health.pending == 0 && health.vanillaMax == 4,
          "reescalonamento deve preservar proporcao e V e descartar pendente");
    health = { 535, 267, 4, 100 };
    OotHealthRescale(health, 100);
    Check(health.max == 100 && health.hp == 49 && health.pending == 0, "reescalonamento deve arredondar para baixo");
    health = { 65535, 1, 4, 1 };
    OotHealthRescale(health, 1);
    Check(health.hp == 1 && health.pending == 0, "reescalonamento nao deve matar por arredondamento");
    health = OotHealthEnable(4, 0, 535);
    OotHealthRescale(health, 1070);
    Check(health.max == 1070 && health.hp == 0 && health.pending == 0, "reescalonamento nao deve ressuscitar");
    health = OotHealthEnable(255, 255, 65535);
    OotHealthRescale(health, 65535);
    Check(health.hp == 65535 && OotHealthMirror(health, health.hp) == 255, "limites maximos devem manter a vida");
}

} // namespace

int main() {
    TestMirror();
    TestEnable();
    TestDebitAndApply();
    TestSmallHit();
    TestManualConsumption();
    TestExternalChanges();
    TestCooperativeDamage();
    TestRescale();
    if (failures == 0) {
        std::cout << "OotActorHealthTests: OK\n";
    }
    return failures;
}
