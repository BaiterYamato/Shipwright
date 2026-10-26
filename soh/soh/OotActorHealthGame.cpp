// RFC 0031: vida por instância, somente na thread do jogo.
#include "OotActorHealth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include <shiplua/runtime/Result.h>
#include <libultraship/bridge.h>
#include "soh/cvar_prefixes.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
void ShipLua_EmitActorDamaged(Actor* actor, uint32_t amount);
}

namespace {
std::unordered_map<Actor*, uint8_t> gVanillaMax;
std::unordered_map<Actor*, ShipLuaHost::ExtendedHealth> gExtended;
} // namespace

extern "C" void LinkSpan_ActorHealthInit(Actor* actor) {
    if (actor == nullptr) {
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
        actor->colChkInfo.health = ShipLuaHost::OotHealthMirror(found->second, found->second.hp);
        return ShipLua::Result<bool>::ok(true);
    } catch (...) {
        return ShipLua::Result<bool>::err(ShipLua::ErrorCode::HostFailure, "cannot allocate extended actor health");
    }
}

float LinkSpan_ExtendedDamage(Actor* target, float damage, bool* handled) {
    if (handled != nullptr) {
        *handled = false;
    }
    if (gExtended.empty() || target == nullptr) {
        return damage;
    }
    const auto found = gExtended.find(target);
    if (found == gExtended.end()) {
        return damage;
    }
    if (handled != nullptr) {
        *handled = true;
    }
    // Trunca só depois do corte; NaN não chega ao cast inteiro.
    const float bounded = std::clamp(std::isnan(damage) ? 0.0f : damage, 0.0f, 65535.0f);
    return ShipLuaHost::OotHealthAddDebit(found->second, static_cast<uint32_t>(bounded));
}

extern "C" void LinkSpan_ActorDamaged(Actor* actor, uint32_t amount) {
    if (actor == nullptr || amount == 0) {
        return;
    }
    try {
        ShipLua_EmitActorDamaged(actor, amount);
    } catch (...) {
        // A conta já foi confirmada; falha de notificação não muda o dano.
    }
}

extern "C" int LinkSpan_ActorApplyDamage(Actor* actor) {
    if (gExtended.empty() || actor == nullptr) {
        return 0;
    }
    try {
        const auto found = gExtended.find(actor);
        if (found == gExtended.end()) {
            return 0;
        }
        Player* player = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
        const uint32_t coopMultiplier = CVarGetInteger(CVAR_ENHANCEMENT("IvanCoopModeEnabled"), 0) && player != nullptr
                                            ? player->ivanDamageMultiplier
                                            : 1;
        const auto committed = ShipLuaHost::OotHealthApply(found->second, coopMultiplier);
        actor->colChkInfo.health = committed.vanillaHealth;
        actor->colChkInfo.damage = 0;
        LinkSpan_ActorDamaged(actor, committed.amount);
        return 1;
    } catch (...) {
        // Ator estendido nunca cai no consumo vanilla depois de uma falha.
        return 1;
    }
}

extern "C" void LinkSpan_ActorSettleDamage(Actor* actor) {
    if (gExtended.empty() || actor == nullptr) {
        return;
    }
    try {
        const auto found = gExtended.find(actor);
        if (found == gExtended.end()) {
            return;
        }
        uint32_t amount = 0;
        const auto settled = ShipLuaHost::OotHealthSettleAfterUpdate(found->second, actor->colChkInfo.health, amount);
        if (settled == ShipLuaHost::OotHealthSettle::Committed || settled == ShipLuaHost::OotHealthSettle::Rescaled) {
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
