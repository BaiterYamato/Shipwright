#include "z64.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <shiplua/runtime/Result.h>

extern "C" {
uint8_t gLinkSpanEngineExtended = 1;
SaveContext gSaveContext{};
int32_t TestCoop = 0;
uint32_t TestEvents = 0;
uint32_t TestAmount = 0;
int32_t CVarGetInteger(const char*, int32_t) {
    return TestCoop;
}
void ShipLua_EmitActorDamaged(Actor*, uint32_t amount) {
    ++TestEvents;
    TestAmount = amount;
}
void LinkSpan_ActorHealthInit(Actor*);
void LinkSpan_ActorHealthDestroy(Actor*);
void LinkSpan_ActorSettleDamage(Actor*);
int LinkSpan_ActorApplyDamage(Actor*);
int LinkSpan_ActorHasExtendedHealth(Actor*) noexcept;
}
float LinkSpan_ExtendedDamage(PlayState*, Actor*, float) noexcept;
bool LinkSpan_ActorHealthInfo(Actor*, uint32_t*, uint32_t*, bool*);
ShipLua::Result<bool> LinkSpan_SetActorMaxHealth(Actor*, uint32_t);

namespace {
bool tracking = false;
size_t allocations = 0;
void Require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAILED adapter: " << message << '\n';
        std::exit(1);
    }
}
void Info(Actor& actor, uint32_t expected) {
    uint32_t hp = 0, max = 0;
    bool extended = false;
    Require(LinkSpan_ActorHealthInfo(&actor, &hp, &max, &extended) && extended && hp == expected,
            "hp real no adaptador");
}
void Add(PlayState* play, Actor& actor, float damage) {
    actor.colChkInfo.damage += static_cast<uint8_t>(LinkSpan_ExtendedDamage(play, &actor, damage));
    // O ramo C multiplicaria somente alvos sem conta estendida.
    if (TestCoop && !LinkSpan_ActorHasExtendedHealth(&actor)) {
        actor.colChkInfo.damage *= GET_PLAYER(play)->ivanDamageMultiplier;
    }
}
void Consume(Actor& actor) {
    auto& info = actor.colChkInfo;
    info.health = info.damage >= info.health ? 0 : static_cast<uint8_t>(info.health - info.damage);
}
} // namespace

void* operator new(size_t size) {
    if (tracking) {
        ++allocations;
    }
    if (void* ptr = std::malloc(size ? size : 1)) {
        return ptr;
    }
    throw std::bad_alloc();
}
void operator delete(void* ptr) noexcept {
    std::free(ptr);
}
void operator delete(void* ptr, size_t) noexcept {
    std::free(ptr);
}

void RunAdapterChecks() {
    Actor actor{};
    actor.colChkInfo.health = 4;
    gLinkSpanEngineExtended = 0;
    tracking = true;
    for (int i = 0; i < 100; ++i) {
        LinkSpan_ActorHealthInit(&actor);
        LinkSpan_ActorSettleDamage(&actor);
        LinkSpan_ActorHealthDestroy(&actor);
        Require(LinkSpan_ExtendedDamage(nullptr, &actor, 9) == 9, "sem mods: dano intacto");
        Require(!LinkSpan_ActorHasExtendedHealth(&actor), "sem mods: sem conta");
        Require(!LinkSpan_ActorApplyDamage(&actor), "sem mods: apply vanilla");
    }
    tracking = false;
    Require(allocations == 0, "sem mods: zero alocacoes no caminho por ator");
    gLinkSpanEngineExtended = 1;
    uint32_t hp = 0, max = 99;
    bool extended = true;
    LinkSpan_ActorHealthInfo(&actor, &hp, &max, &extended);
    Require(!extended && max == 0, "init sem mods nao cadastrou vanillaMax");

    LinkSpan_ActorHealthInit(&actor);
    Require(LinkSpan_SetActorMaxHealth(&actor, 46).isOk(), "ativar conta");
    TestEvents = 0;
    Add(nullptr, actor, 9);
    Require(actor.colChkInfo.health == 5 && actor.colChkInfo.damage == 1, "folga real no adaptador");
    LinkSpan_ActorSettleDamage(&actor);
    Info(actor, 46);
    Require(actor.colChkInfo.health == 4 && TestEvents == 0, "ignorado restaura sem evento");
    actor.colChkInfo.damage = 0;
    Add(nullptr, actor, 9);
    Consume(actor);
    LinkSpan_ActorSettleDamage(&actor);
    Info(actor, 37);
    Require(actor.colChkInfo.health == 4 && TestEvents == 1 && TestAmount == 9, "aceito emite 9");
    LinkSpan_ActorSettleDamage(&actor);
    Require(TestEvents == 1, "settle idempotente");
    LinkSpan_ActorHealthDestroy(&actor);

    Player player{ 2 };
    PlayState play{ &player };
    TestCoop = 1;
    for (bool manual : { false, true }) {
        actor = {};
        actor.colChkInfo.health = 10;
        LinkSpan_ActorHealthInit(&actor);
        Require(LinkSpan_SetActorMaxHealth(&actor, 100).isOk(), "ativar coop");
        TestEvents = 0;
        Add(&play, actor, 10);
        Add(&play, actor, 10);
        Require(actor.colChkInfo.damage == 4, "coop aplicado por contribuicao");
        if (manual) {
            Consume(actor);
            LinkSpan_ActorSettleDamage(&actor);
        } else {
            Require(LinkSpan_ActorApplyDamage(&actor) == 1, "apply coop");
        }
        Info(actor, 60);
        Require(actor.colChkInfo.health == 6 && actor.colChkInfo.damage == 4 && TestEvents == 1 && TestAmount == 40,
                "coop emite 40 e preserva damage");
        LinkSpan_ActorHealthDestroy(&actor);
    }
    TestCoop = 0;
    std::cout << "Adaptador real com dubles: OK; sem mods: 0 alocacoes/100 ciclos\n";
}

#undef main
int HealthTestsMain();
int main() {
    RunAdapterChecks();
    return HealthTestsMain();
}
