#include "soh/OotActorHealth.h"

#include <iostream>
#include <limits>

#ifdef OOT_ACTOR_HEALTH_GAME_TESTS
#include "z64.h"
#include <shiplua/runtime/Result.h>
extern "C" void LinkSpan_ActorHealthInit(Actor* actor);
extern "C" void LinkSpan_ActorHealthDestroy(Actor* actor);
extern "C" int LinkSpan_ActorApplyDamage(Actor* actor);
extern "C" void LinkSpan_ActorSettleDamage(Actor* actor);
bool LinkSpan_ActorHealthInfo(Actor* actor, uint32_t* health, uint32_t* maxHealth, bool* extended);
float LinkSpan_ExtendedDamage(PlayState* play, Actor* target, float damage) noexcept;
ShipLua::Result<bool> LinkSpan_SetActorMaxHealth(Actor* actor, uint32_t maxHealth);
#endif

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
    const OotHealthCommit first = OotHealthApply(health);
    Check(first.vanillaHealth == 1 && first.amount == 435 && health.hp == 100 && health.pending == 0,
          "apply deve confirmar os 435 acumulados e zerar pendente");
    Check(OotHealthAddDebit(health, 600) == 1 && health.pending == 100, "debito deve saturar na vida restante");
    Check(OotHealthAddDebit(health, std::numeric_limits<uint32_t>::max()) == 0 && health.pending == 100,
          "debito saturado nao deve acumular nem baixar novamente o espelho");
    const OotHealthCommit last = OotHealthApply(health);
    Check(last.vanillaHealth == 0 && last.amount == 100 && health.hp == 0 && health.pending == 0,
          "apply letal deve consumir somente a vida restante");
    Check(OotHealthAddDebit(health, 1) == 0 && health.pending == 0, "ator morto nao deve acumular debito");
    const OotHealthCommit empty = OotHealthApply(health);
    Check(empty.vanillaHealth == 0 && empty.amount == 0, "apply repetido nao deve consumir vida");
}

struct VanillaDamage {
    uint8_t health;
    uint8_t damage = 0;
};

uint8_t AddElement(ExtendedHealth& health, VanillaDamage& vanilla, uint32_t debit, uint32_t coop = 1) {
    const bool hadHeadroom = health.headroom;
    const uint8_t delta = OotHealthAddDebit(health, debit, coop);
    if (!hadHeadroom && health.headroom) {
        vanilla.health = static_cast<uint8_t>(OotHealthMirror(health, health.hp) + 1);
    }
    vanilla.damage = static_cast<uint8_t>(vanilla.damage + delta);
    return delta;
}

void ConsumeManually(VanillaDamage& vanilla) {
    vanilla.health = vanilla.damage >= vanilla.health ? 0 : static_cast<uint8_t>(vanilla.health - vanilla.damage);
}

void TestHeadroomAccepted() {
    ExtendedHealth health = OotHealthEnable(4, 4, 46);
    VanillaDamage vanilla{ 4 };
    Check(AddElement(health, vanilla, 9) == 1 && vanilla.health == 5 && health.headroom && health.pending == 9,
          "golpe subgranular deve abrir uma folga sem consumir hp");
    ConsumeManually(vanilla);
    uint32_t amount = 999;
    Check(vanilla.health == 4 &&
              OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed && amount == 9 &&
              health.hp == 37 && health.pending == 0 && !health.headroom,
          "volta ao espelho deve confirmar debito subgranular e devolver amount para damaged");
    Check(OotHealthSettleAfterUpdate(health, 4, amount) == OotHealthSettle::None && amount == 0,
          "settle repetido nao deve emitir o mesmo dano");

    vanilla = { 4 };
    Check(AddElement(health, vanilla, 1) == 1 && vanilla.health == 5, "novo frame deve poder abrir nova folga");
    const auto applied = OotHealthApply(health);
    Check(applied.amount == 1 && applied.vanillaHealth == 4 && health.hp == 36 && !health.headroom,
          "apply deve confirmar golpe subgranular e limpar folga");
}

void TestHeadroomIgnored() {
    ExtendedHealth health = OotHealthEnable(4, 4, 46);
    VanillaDamage vanilla{ 4 };
    AddElement(health, vanilla, 9);
    uint32_t amount = 999;
    Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Discarded && amount == 0 &&
              health.hp == 46 && health.pending == 0 && !health.headroom && OotHealthMirror(health, health.hp) == 4,
          "vida em espelho mais 1 deve descartar debito e mandar restaurar espelho");
    Check(OotHealthApply(health).amount == 0, "apply apos descarte nao deve consumir o golpe ignorado");

    vanilla = { 4 };
    AddElement(health, vanilla, 9);
    AddElement(health, vanilla, 9);
    Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Discarded && health.hp == 46 &&
              amount == 0,
          "ignorar varios elementos deve descartar inclusive a folga anterior ao degrau real");
}

void TestBabaSixHits() {
    ExtendedHealth health = OotHealthEnable(4, 4, 46);
    const uint32_t expectedHp[] = { 37, 28, 19, 10, 1, 0 };
    const uint8_t expectedMirror[] = { 4, 3, 2, 1, 1, 0 };
    for (int hit = 0; hit < 6; ++hit) {
        VanillaDamage vanilla{ OotHealthMirror(health, health.hp) };
        Check(AddElement(health, vanilla, 9) == 1, "Baba deve receber dano u8 1 nos seis golpes");
        ConsumeManually(vanilla);
        uint32_t amount = 0;
        Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed &&
                  health.hp == expectedHp[hit] && vanilla.health == expectedMirror[hit] &&
                  amount == (hit < 5 ? 9u : 1u) && health.pending == 0 && !health.headroom,
              "Baba deve percorrer 46, 37, 28, 19, 10, 1, 0 e confirmar cada golpe");
    }
}

void TestKeeseThreeHits() {
    for (bool manual : { false, true }) {
        ExtendedHealth health = OotHealthEnable(1, 1, 9);
        const uint32_t expectedHp[] = { 5, 1, 0 };
        const uint32_t expectedAmount[] = { 4, 4, 1 };
        for (int hit = 0; hit < 3; ++hit) {
            VanillaDamage vanilla{ OotHealthMirror(health, health.hp) };
            Check(AddElement(health, vanilla, 4) == 1, "Keese deve receber sinal de dano nos tres golpes");
            Check(vanilla.health == (hit < 2 ? 2 : 1), "folga deve proteger Keese somente nos golpes nao letais");
            uint32_t amount = 0;
            if (manual) {
                ConsumeManually(vanilla);
                Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed,
                      "Keese manual deve confirmar cada golpe");
            } else {
                const auto applied = OotHealthApply(health);
                amount = applied.amount;
                vanilla.health = applied.vanillaHealth;
            }
            Check(health.hp == expectedHp[hit] && amount == expectedAmount[hit] && health.pending == 0 &&
                      !health.headroom && vanilla.health == (hit < 2 ? 1 : 0),
                  "Keese deve percorrer 9, 5, 1, 0 sem morrer antes do terceiro golpe");
        }
    }
}

void TestMultipleElements() {
    ExtendedHealth health = OotHealthEnable(4, 4, 46);
    VanillaDamage vanilla{ 4 };
    Check(AddElement(health, vanilla, 3) == 1 && AddElement(health, vanilla, 3) == 0 && vanilla.health == 5 &&
              vanilla.damage == 1 && health.pending == 6,
          "elementos subgranulares no mesmo frame nao devem empilhar folga");
    ConsumeManually(vanilla);
    uint32_t amount = 0;
    Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed && amount == 6 &&
              health.hp == 40,
          "uma folga deve confirmar a soma de varios elementos");

    health = OotHealthEnable(4, 4, 46);
    vanilla = { 4 };
    Check(AddElement(health, vanilla, 9) == 1 && AddElement(health, vanilla, 9) == 1 && vanilla.health == 5 &&
              vanilla.damage == 2 && health.pending == 18,
          "degrau real posterior deve somar somente sua queda, sem repetir folga");
    ConsumeManually(vanilla);
    Check(vanilla.health == 3 &&
              OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed &&
              amount == 18 && health.hp == 28,
          "folga seguida de degrau real deve confirmar hp 28");

    health = OotHealthEnable(4, 4, 46);
    vanilla = { 4 };
    Check(AddElement(health, vanilla, 13) == 1 && AddElement(health, vanilla, 1) == 1 && vanilla.health == 5 &&
              vanilla.damage == 2 && health.headroom,
          "folga depois de degrau real deve subir a vida somente uma vez");
    ConsumeManually(vanilla);
    Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed && health.hp == 32 &&
              amount == 14,
          "degrau real seguido de folga deve confirmar ambos os elementos");
}

void TestLethalDebit() {
    ExtendedHealth health = OotHealthEnable(4, 4, 46);
    VanillaDamage vanilla{ 4 };
    Check(AddElement(health, vanilla, 46) == 4 && !health.headroom && vanilla.health == 4,
          "golpe letal direto deve seguir o caminho sem folga");
    ConsumeManually(vanilla);
    uint32_t amount = 0;
    Check(vanilla.health == 0 && OotHealthSettleAfterUpdate(health, 0, amount) == OotHealthSettle::Committed &&
              amount == 46 && health.hp == 0,
          "golpe que zera hp deve confirmar morte real");

    health = OotHealthEnable(1, 1, 9);
    vanilla = { 1 };
    Check(AddElement(health, vanilla, 4) == 1 && AddElement(health, vanilla, 99) == 1 && vanilla.health == 2 &&
              vanilla.damage == 2 && health.pending == 9,
          "golpe letal apos folga deve consumir tambem a unidade temporaria");
    Check(AddElement(health, vanilla, 1) == 0 && vanilla.damage == 2, "letal saturado nao deve somar outro delta");
    ConsumeManually(vanilla);
    Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed && amount == 9 &&
              health.hp == 0 && !health.headroom,
          "varios elementos devem matar somente quando zerarem a vida real");
}

void TestHeadroomLimits() {
    ExtendedHealth health = OotHealthEnable(255, 255, 65535);
    Check(OotHealthAddDebit(health, 1) == 0 && !health.headroom && health.pending == 1,
          "espelho 255 nao deve abrir folga nem estourar o u8");
    uint32_t amount = 999;
    Check(OotHealthSettleAfterUpdate(health, 255, amount) == OotHealthSettle::Discarded && health.hp == 65535 &&
              amount == 0 && health.pending == 0,
          "sem espaco em 255 deve permanecer a limitacao anterior");
    health = OotHealthEnable(254, 254, 65535);
    VanillaDamage vanilla{ 254 };
    Check(AddElement(health, vanilla, 1) == 1 && vanilla.health == 255 && health.headroom,
          "espelho 254 deve admitir exatamente uma folga");
    OotHealthRescale(health, 100);
    Check(!health.headroom && health.pending == 0 && health.hp == 100, "rescale deve limpar a folga");

    health = OotHealthEnable(4, 4, 46);
    Check(OotHealthAddDebit(health, 0) == 0 && !health.headroom && health.pending == 0,
          "dano zero nao deve abrir folga");
    OotHealthAddDebit(health, 9);
    Check(OotHealthSettleAfterUpdate(health, 2, amount) == OotHealthSettle::Rescaled && health.hp == 23 &&
              amount == 0 && health.pending == 0 && !health.headroom,
          "mudanca externa deve continuar reescalando e limpar folga");

    health = OotHealthEnable(4, 4, 46);
    OotHealthAddDebit(health, 9);
    OotHealthAddDebit(health, 9);
    Check(OotHealthSettleAfterUpdate(health, 4, amount) == OotHealthSettle::Rescaled && health.hp == 46 &&
              amount == 0 && health.pending == 0 && !health.headroom,
          "consumo parcial inesperado deve reescalar sem vazar a folga para outro frame");
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
    for (bool manual : { false, true }) {
        ExtendedHealth health = OotHealthEnable(4, 4, 46);
        VanillaDamage vanilla{ 4 };
        Check(AddElement(health, vanilla, 13, 2) == 2 && vanilla.damage == 2 && health.pending == 26,
              "coop deve dobrar contribuicao antes de converter o espelho");
        uint32_t amount = 0;
        if (manual) {
            ConsumeManually(vanilla);
            Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed,
                  "coop manual deve confirmar sem reescalar");
        } else {
            const auto applied = OotHealthApply(health);
            amount = applied.amount;
            vanilla.health = applied.vanillaHealth;
        }
        Check(health.hp == 20 && amount == 26 && vanilla.health == 2 && health.pending == 0,
              "um elemento coop deve deixar 20 e emitir 26, sem multiplicar no apply");

        health = OotHealthEnable(10, 10, 100);
        vanilla = { 10 };
        Check(AddElement(health, vanilla, 10, 2) == 2 && AddElement(health, vanilla, 10, 2) == 2 &&
                  health.pending == 40 && vanilla.damage == 4,
              "dois elementos coop devem somar 20 mais 20, sem multiplicar o acumulador u8");
        if (manual) {
            ConsumeManually(vanilla);
            Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed,
                  "dois elementos coop manuais devem confirmar sem reescalar");
        } else {
            const auto applied = OotHealthApply(health);
            amount = applied.amount;
            vanilla.health = applied.vanillaHealth;
        }
        Check(health.hp == 60 && amount == 40 && vanilla.health == 6 && health.pending == 0,
              "dois elementos coop devem deixar 60 e emitir 40");
        Check(OotHealthApply(health).amount == 0 && health.hp == 60,
              "apply repetido nao deve consumir novamente o coop");
    }

    ExtendedHealth health = OotHealthEnable(1, 1, 9);
    VanillaDamage vanilla{ 1 };
    Check(AddElement(health, vanilla, 2, 2) == 1 && AddElement(health, vanilla, 2, 2) == 0 && health.pending == 8 &&
              vanilla.health == 2 && vanilla.damage == 1,
          "coop subgranular tambem deve compartilhar uma unica folga");
    ConsumeManually(vanilla);
    uint32_t amount = 0;
    Check(OotHealthSettleAfterUpdate(health, vanilla.health, amount) == OotHealthSettle::Committed && health.hp == 1 &&
              amount == 8,
          "coop com folga deve preservar a ultima unidade real");

    health = OotHealthEnable(4, 4, 535);
    Check(OotHealthAddDebit(health, std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()) == 4 &&
              health.pending == 535 && !health.headroom,
          "produto u32 por u32 deve saturar em hp sem overflow");
    const auto saturated = OotHealthApply(health);
    Check(saturated.amount == 535 && saturated.vanillaHealth == 0 && health.hp == 0,
          "coop letal deve consumir somente hp restante");

    health = OotHealthEnable(4, 4, 535);
    Check(OotHealthAddDebit(health, 135, 0) == 0 && health.pending == 0 && !health.headroom,
          "coop zero nao deve criar debito nem folga");
    Check(OotHealthApply(health).amount == 0 && health.hp == 535, "coop zero deve preservar vida no apply");
}

void TestThresholdConsumersWithoutHeadroom() {
    // Inclui o caso 50 do Iron Knuckle e os limiares estritos de Dead Hand/Zf.
    for (uint8_t mirror : { 50, 15, 14, 10, 9, 8, 4, 3, 2 }) {
        ExtendedHealth health = OotHealthEnable(50, mirror, 500);
        health.allowHeadroom = false;
        const auto initial = health.hp;
        for (int hit = 0; hit < 3; ++hit) {
            Check(OotHealthAddDebit(health, 1) == 0 && !health.headroom, "consumidor de limiar nao deve receber folga");
            const auto applied = OotHealthApply(health);
            Check(applied.amount == 0 && applied.vanillaHealth == mirror && health.hp == initial,
                  "apply incondicional deve descartar golpe subgranular sem cruzar limiar");
        }
        Check(OotHealthAddDebit(health, 1) == 0 && OotHealthAddDebit(health, 9) == 1,
              "varios elementos ainda podem atingir um degrau real sem folga");
        const auto applied = OotHealthApply(health);
        Check(applied.amount == 10 && applied.vanillaHealth == mirror - 1 && health.hp == initial - 10,
              "degrau real deve confirmar apenas uma travessia");
        OotHealthRescale(health, 1000);
        Check(!health.allowHeadroom, "rescale deve preservar a exclusao de folga");
    }
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

#ifdef OOT_ACTOR_HEALTH_GAME_TESTS
void TestActorThresholdAllowlist() {
    for (int16_t id : { ACTOR_EN_IK, ACTOR_EN_DH, ACTOR_EN_ZF }) {
        const uint8_t vanilla = id == ACTOR_EN_IK ? 30 : (id == ACTOR_EN_DH ? 14 : 6);
        for (uint8_t mirror : { 50, 15, 14, 10, 9, 8, 4, 3, 2 }) {
            if (mirror > vanilla) {
                continue;
            }
            Actor actor{};
            actor.id = id;
            actor.colChkInfo.health = vanilla;
            LinkSpan_ActorHealthInit(&actor);
            actor.colChkInfo.health = mirror;
            Check(LinkSpan_SetActorMaxHealth(&actor, vanilla * 10).isOk(), "ativar ator sem folga");
            for (int hit = 0; hit < 3; ++hit) {
                actor.colChkInfo.damage = static_cast<uint8_t>(LinkSpan_ExtendedDamage(nullptr, &actor, 1));
                const auto before = actor.colChkInfo.health;
                Check(before == mirror && actor.colChkInfo.damage == 0, "funil nao pode fabricar vida anterior");
                LinkSpan_ActorApplyDamage(&actor);
                uint32_t hp = 0, maximum = 0;
                bool extended = false;
                Check(LinkSpan_ActorHealthInfo(&actor, &hp, &maximum, &extended) && extended &&
                          hp == static_cast<uint32_t>(mirror) * 10 && actor.colChkInfo.health == before,
                      "sem folga: golpe subgranular nao deve contar nem repetir fase");
                LinkSpan_ActorSettleDamage(&actor);
            }
            actor.colChkInfo.damage = static_cast<uint8_t>(LinkSpan_ExtendedDamage(nullptr, &actor, 10));
            Check(actor.colChkInfo.damage == 1 && actor.colChkInfo.health == mirror,
                  "golpe visivel nao deve elevar a vida anterior");
            LinkSpan_ActorApplyDamage(&actor);
            Check(actor.colChkInfo.health == mirror - 1, "golpe visivel cruza um degrau real");
            LinkSpan_ActorHealthDestroy(&actor);
        }
    }
}

// Opcional: compila o adaptador real com dublês mínimos, sem executar o jogo.
void TestApplyKeepsCollisionDamage() {
    Actor actor{};
    actor.colChkInfo.health = 4;
    LinkSpan_ActorHealthInit(&actor);
    Check(LinkSpan_SetActorMaxHealth(&actor, 46).isOk(), "adaptador deve ativar a vida estendida");
    actor.colChkInfo.damage = static_cast<uint8_t>(LinkSpan_ExtendedDamage(nullptr, &actor, 9.0f));
    Check(actor.colChkInfo.health == 5 && actor.colChkInfo.damage == 1, "adaptador deve abrir folga e devolver dano 1");
    Check(LinkSpan_ActorApplyDamage(&actor) == 1 && actor.colChkInfo.health == 4 && actor.colChkInfo.damage == 1,
          "apply estendido deve preservar damage ate o reset vanilla");
    Check(LinkSpan_ActorApplyDamage(&actor) == 1 && actor.colChkInfo.health == 4 && actor.colChkInfo.damage == 1,
          "apply repetido deve preservar dano sem consumir hp novamente");
    LinkSpan_ActorHealthDestroy(&actor);
}
#endif

} // namespace

int main() {
    TestMirror();
    TestEnable();
    TestDebitAndApply();
    TestHeadroomAccepted();
    TestHeadroomIgnored();
    TestBabaSixHits();
    TestKeeseThreeHits();
    TestMultipleElements();
    TestLethalDebit();
    TestHeadroomLimits();
    TestManualConsumption();
    TestExternalChanges();
    TestCooperativeDamage();
    TestRescale();
    TestThresholdConsumersWithoutHeadroom();
#ifdef OOT_ACTOR_HEALTH_GAME_TESTS
    TestApplyKeepsCollisionDamage();
    TestActorThresholdAllowlist();
#endif
    if (failures == 0) {
        std::cout << "OotActorHealthTests: OK\n";
    }
    return failures;
}
