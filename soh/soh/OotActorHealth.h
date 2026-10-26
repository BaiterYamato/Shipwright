#pragma once

#include <cstdint>

namespace ShipLuaHost {

// RFC 0031: conta pura da vida estendida; o host lê e grava a vida u8 do jogo.
struct ExtendedHealth {
    uint32_t max = 0;       // n de set_max_health, 1..65535
    uint32_t hp = 0;        // vida real, 0..max
    uint8_t vanillaMax = 0; // V: vida vanilla registrada no init, >= 1
    uint32_t pending = 0;   // débito ainda não confirmado, <= hp
};

// Zero se hp == 0; senão max(1, ceil(V * hp / max)), limitado a V.
uint8_t OotHealthMirror(const ExtendedHealth& health, uint32_t hp);

// Primeira ativação: floor(max * current / V), no mínimo 1 se current > 0.
ExtendedHealth OotHealthEnable(uint8_t vanillaMax, uint8_t current, uint32_t max);

// Preserva a proporção da vida e zera o pendente; não ressuscita o ator.
void OotHealthRescale(ExtendedHealth& health, uint32_t newMax);

// Acumula até hp e devolve a queda incremental do espelho para colChkInfo.damage.
uint8_t OotHealthAddDebit(ExtendedHealth& health, uint32_t damage);

struct OotHealthCommit {
    uint8_t vanillaHealth; // o que gravar em colChkInfo.health
    uint32_t amount;       // o que saiu da vida real
};

// Actor_ApplyDamage: confirma min(pending * coopMultiplier, hp) e zera o pendente.
OotHealthCommit OotHealthApply(ExtendedHealth& health, uint32_t coopMultiplier);

enum class OotHealthSettle { None, Committed, Discarded, Rescaled };

// Reconcilia a vida vanilla no fim do update, antes do reset de dano.
// Confirma consumo manual visível; descarta dano ignorado; reescala mudanças externas.
OotHealthSettle OotHealthSettleAfterUpdate(ExtendedHealth& health, uint8_t vanillaNow, uint32_t& amount);

} // namespace ShipLuaHost
