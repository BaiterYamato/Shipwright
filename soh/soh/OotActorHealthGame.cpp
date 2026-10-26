// RFC 0031: vida por instância, somente na thread do jogo.
#include "OotActorHealth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include <shiplua/runtime/Result.h>
#include <libultraship/bridge.h>
#include "soh/cvar_prefixes.h"
#include "../src/code/linkspan_vanilla.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
void ShipLua_EmitActorDamaged(Actor* actor, uint32_t amount);
}

namespace {
std::unordered_map<Actor*, uint8_t> gVanillaMax;
std::unordered_map<Actor*, ShipLuaHost::ExtendedHealth> gExtended;

// Esses consumidores comparam a vida anterior com limiares de fase.
constexpr int16_t kActorsWithoutHealthHeadroom[] = { ACTOR_EN_IK, ACTOR_EN_DH, ACTOR_EN_ZF };

bool AllowsHealthHeadroom(const Actor* actor) {
    return std::find(std::begin(kActorsWithoutHealthHeadroom), std::end(kActorsWithoutHealthHeadroom), actor->id) ==
           std::end(kActorsWithoutHealthHeadroom);
}
} // namespace

extern "C" void LinkSpan_ActorHealthInit(Actor* actor) {
    if (!LinkSpan_EngineExtended() || actor == nullptr) {
        return;
    }
    try {
        // Um endereço reaproveitado nunca herda a conta da instância anterior.
        gExtended.erase(actor);
        gVanillaMax.insert_or_assign(actor, actor->colChkInfo.health);
    } catch (...) {
        gVanillaMax.erase(actor);
        gExtended.erase(actor);
    }
}

extern "C" void LinkSpan_ActorHealthDestroy(Actor* actor) {
    if (!LinkSpan_EngineExtended() || actor == nullptr) {
        return;
    }
    try {
        gExtended.erase(actor);
        gVanillaMax.erase(actor);
    } catch (...) {
        // Nenhuma exceção atravessa o jogo C.
    }
}

void LinkSpan_ActorHealthReset() {
    gExtended.clear();
    gVanillaMax.clear();
}

bool LinkSpan_ActorHealthInfo(Actor* actor, uint32_t* health, uint32_t* maxHealth, bool* extended) {
    if (actor == nullptr || health == nullptr || maxHealth == nullptr || extended == nullptr) {
        return false;
    }
    *extended = false;
    if (actor->category == ACTORCAT_PLAYER) {
        *health = static_cast<uint32_t>(gSaveContext.health);
        *maxHealth = static_cast<uint32_t>(gSaveContext.healthCapacity);
        return true;
    }
    if (!LinkSpan_EngineExtended()) {
        *health = actor->colChkInfo.health;
        *maxHealth = 0;
        return true;
    }
    const auto found = gExtended.find(actor);
    if (found != gExtended.end()) {
        *health = found->second.hp;
        *maxHealth = found->second.max;
        *extended = true;
        return true;
    }
    const auto vanilla = gVanillaMax.find(actor);
    *health = actor->colChkInfo.health;
    // Sem init conhecido, não inventa um máximo a partir da vida restante.
    *maxHealth = vanilla != gVanillaMax.end() ? vanilla->second : 0;
    return true;
}

ShipLua::Result<bool> LinkSpan_SetActorMaxHealth(Actor* actor, uint32_t maxHealth) {
    if (actor == nullptr || maxHealth == 0 || maxHealth > 65535 || actor->category == ACTORCAT_PLAYER) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::InvalidArgument,
                                          "invalid actor or maximum health (Link is read-only)");
    }
    if (!LinkSpan_EngineExtended()) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::InvalidState, "extended engine is disabled");
    }
    const auto vanilla = gVanillaMax.find(actor);
    if (vanilla == gVanillaMax.end() || vanilla->second == 0) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::InvalidState, "actor had no health at initialization");
    }
    try {
        auto found = gExtended.find(actor);
        if (found == gExtended.end()) {
            const uint8_t current = std::min(actor->colChkInfo.health, vanilla->second);
            found = gExtended.emplace(actor, ShipLuaHost::OotHealthEnable(vanilla->second, current, maxHealth)).first;
        } else {
            ShipLuaHost::OotHealthRescale(found->second, maxHealth);
        }
        found->second.allowHeadroom = AllowsHealthHeadroom(actor);
        actor->colChkInfo.health = ShipLuaHost::OotHealthMirror(found->second, found->second.hp);
        return ShipLua::Result<bool>::ok(true);
    } catch (...) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::HostFailure, "cannot allocate extended actor health");
    }
}

extern "C" int LinkSpan_ActorHasExtendedHealth(Actor* actor) noexcept {
    if (!LinkSpan_EngineExtended() || actor == nullptr) {
        return 0;
    }
    try {
        return !gExtended.empty() && gExtended.find(actor) != gExtended.end();
    } catch (...) { return 0; }
}

float LinkSpan_ExtendedDamage(PlayState* play, Actor* target, float damage) noexcept {
    if (!LinkSpan_EngineExtended() || target == nullptr) {
        return damage;
    }
    try {
        if (gExtended.empty()) {
            return damage;
        }
        const auto found = gExtended.find(target);
        if (found == gExtended.end()) {
            return damage;
        }
        Player* player = play != nullptr ? GET_PLAYER(play) : nullptr;
        const uint32_t coopMultiplier = CVarGetInteger(CVAR_ENHANCEMENT("IvanCoopModeEnabled"), 0) && player != nullptr
                                            ? player->ivanDamageMultiplier
                                            : 1;
        // Trunca só depois do corte; coop é aplicado uma vez por contribuição.
        const float bounded = std::clamp(std::isnan(damage) ? 0.0f : damage, 0.0f, 65535.0f);
        const bool hadHeadroom = found->second.headroom;
        const uint8_t delta =
            ShipLuaHost::OotHealthAddDebit(found->second, static_cast<uint32_t>(bounded), coopMultiplier);
        if (!hadHeadroom && found->second.headroom) {
            target->colChkInfo.health =
                static_cast<uint8_t>(ShipLuaHost::OotHealthMirror(found->second, found->second.hp) + 1);
        }
        return delta;
    } catch (...) {
        // Nunca devolve débito real ao u8 após uma falha na conta estendida.
        return 0.0f;
    }
}

extern "C" void LinkSpan_ActorDamaged(Actor* actor, uint32_t amount) {
    if (!LinkSpan_EngineExtended() || actor == nullptr || amount == 0) {
        return;
    }
    try {
        ShipLua_EmitActorDamaged(actor, amount);
    } catch (...) {
        // A conta já foi confirmada; falha de notificação não muda o dano.
    }
}

extern "C" int LinkSpan_ActorApplyDamage(Actor* actor) {
    if (!LinkSpan_EngineExtended() || actor == nullptr || gExtended.empty()) {
        return 0;
    }
    try {
        const auto found = gExtended.find(actor);
        if (found == gExtended.end()) {
            return 0;
        }
        const auto committed = ShipLuaHost::OotHealthApply(found->second);
        actor->colChkInfo.health = committed.vanillaHealth;
        // damage pertence ao consumidor até CollisionCheck_ResetDamage.
        LinkSpan_ActorDamaged(actor, committed.amount);
        return 1;
    } catch (...) {
        // Ator estendido nunca cai no consumo vanilla depois de uma falha.
        return 1;
    }
}

extern "C" void LinkSpan_ActorSettleDamage(Actor* actor) {
    if (!LinkSpan_EngineExtended() || actor == nullptr || gExtended.empty()) {
        return;
    }
    try {
        const auto found = gExtended.find(actor);
        if (found == gExtended.end()) {
            return;
        }
        uint32_t amount = 0;
        const auto settled = ShipLuaHost::OotHealthSettleAfterUpdate(found->second, actor->colChkInfo.health, amount);
        if (settled != ShipLuaHost::OotHealthSettle::None) {
            // Cura acima de V satura em max, preservando hp <= max.
            found->second.hp = std::min(found->second.hp, found->second.max);
            actor->colChkInfo.health = ShipLuaHost::OotHealthMirror(found->second, found->second.hp);
        }
        if (settled == ShipLuaHost::OotHealthSettle::Committed) {
            LinkSpan_ActorDamaged(actor, amount);
        }
    } catch (...) {
        // O reset de dano vanilla continua no chamador.
    }
}
