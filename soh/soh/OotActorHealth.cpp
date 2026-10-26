#include "OotActorHealth.h"

#include <algorithm>

namespace ShipLuaHost {

uint8_t OotHealthMirror(const ExtendedHealth& health, uint32_t hp) {
    if (hp == 0) {
        return 0;
    }
    const uint64_t scaled = static_cast<uint64_t>(health.vanillaMax) * hp;
    const uint64_t mirror = (scaled + health.max - 1) / health.max;
    return static_cast<uint8_t>(std::min<uint64_t>(health.vanillaMax, std::max<uint64_t>(1, mirror)));
}

ExtendedHealth OotHealthEnable(uint8_t vanillaMax, uint8_t current, uint32_t max) {
    ExtendedHealth health;
    health.max = max;
    health.vanillaMax = vanillaMax;
    if (current > 0) {
        health.hp = static_cast<uint32_t>(std::max<uint64_t>(1, static_cast<uint64_t>(max) * current / vanillaMax));
    }
    return health;
}

void OotHealthRescale(ExtendedHealth& health, uint32_t newMax) {
    if (health.hp > 0) {
        health.hp =
            static_cast<uint32_t>(std::max<uint64_t>(1, static_cast<uint64_t>(newMax) * health.hp / health.max));
    }
    health.max = newMax;
    health.pending = 0;
    health.headroom = false;
}

uint8_t OotHealthAddDebit(ExtendedHealth& health, uint32_t damage, uint32_t coopMultiplier) {
    const uint32_t remaining = health.hp - health.pending;
    const uint8_t before = OotHealthMirror(health, remaining);
    const uint64_t debit = static_cast<uint64_t>(damage) * coopMultiplier;
    const uint32_t added = static_cast<uint32_t>(std::min<uint64_t>(debit, remaining));
    health.pending += added;
    const uint8_t after = OotHealthMirror(health, health.hp - health.pending);
    uint8_t delta = static_cast<uint8_t>(before - after);
    if (health.allowHeadroom && added > 0 && after > 0 && before == after && !health.headroom &&
        OotHealthMirror(health, health.hp) < 255) {
        // O consumidor devolve a folga ao subtrair 1; hp continua sendo a autoridade.
        health.headroom = true;
        ++delta;
    }
    return delta;
}

OotHealthCommit OotHealthApply(ExtendedHealth& health) {
    if (!health.allowHeadroom &&
        OotHealthMirror(health, health.hp - health.pending) == OotHealthMirror(health, health.hp)) {
        // Sem sinal u8, um apply incondicional também descarta o golpe subgranular.
        health.pending = 0;
        return { OotHealthMirror(health, health.hp), 0 };
    }
    const uint32_t amount = std::min(health.pending, health.hp);
    health.hp -= amount;
    health.pending = 0;
    health.headroom = false;
    return { OotHealthMirror(health, health.hp), amount };
}

OotHealthSettle OotHealthSettleAfterUpdate(ExtendedHealth& health, uint8_t vanillaNow, uint32_t& amount) {
    amount = 0;
    const uint8_t expected = OotHealthMirror(health, health.hp);
    if (health.pending > 0) {
        const uint8_t debited = OotHealthMirror(health, health.hp - health.pending);
        if ((debited != expected || health.headroom) && vanillaNow == debited) {
            amount = health.pending;
            health.hp -= amount;
            health.pending = 0;
            health.headroom = false;
            return OotHealthSettle::Committed;
        }
        const uint8_t untouched = static_cast<uint8_t>(expected + (health.headroom ? 1 : 0));
        if (vanillaNow == untouched) {
            health.pending = 0;
            health.headroom = false;
            return OotHealthSettle::Discarded;
        }
    }
    if (health.pending > 0 || vanillaNow != expected) {
        health.hp = vanillaNow == 0 ? 0
                                    : static_cast<uint32_t>(std::max<uint64_t>(1, static_cast<uint64_t>(health.max) *
                                                                                      vanillaNow / health.vanillaMax));
        health.pending = 0;
        health.headroom = false;
        return OotHealthSettle::Rescaled;
    }
    return OotHealthSettle::None;
}

} // namespace ShipLuaHost
