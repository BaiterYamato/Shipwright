#pragma once

#include <cstdint>

namespace ShipLuaHost {

// RFC 0031: conta pura da vida estendida; o host lê e grava a vida u8 do jogo.
struct ExtendedHealth {
    uint32_t max = 0;          // n de set_max_health, 1..65535
    uint32_t hp = 0;           // vida real, 0..max
    uint8_t vanillaMax = 0;    // V: vida vanilla registrada no init, >= 1
    uint32_t pending = 0;      // débito ainda não confirmado, <= hp
    bool headroom = false;     // uma unidade temporária acima do espelho, sem empilhar
    bool allowHeadroom = true; // falso para consumidores de limiares da vida anterior
};

// Zero se hp == 0; senão max(1, ceil(V * hp / max)), limitado a V.
uint8_t OotHealthMirror(const ExtendedHealth& health, uint32_t hp);

// Primeira ativação: floor(max * current / V), no mínimo 1 se current > 0.
ExtendedHealth OotHealthEnable(uint8_t vanillaMax, uint8_t current, uint32_t max);

// Preserva a proporção da vida e limpa pendente/folga; não ressuscita o ator.
void OotHealthRescale(ExtendedHealth& health, uint32_t newMax);

// Multiplica cada contribuição antes de acumular até hp; devolve o delta com a folga.
// Ao ligar headroom, o chamador sobe colChkInfo.health para mirror(hp) + 1 uma única vez.
uint8_t OotHealthAddDebit(ExtendedHealth& health, uint32_t damage, uint32_t coopMultiplier = 1);

struct OotHealthCommit {
    uint8_t vanillaHealth; // o que gravar em colChkInfo.health
    uint32_t amount;       // o que saiu da vida real
};

// Actor_ApplyDamage: confirma pending, limpa pendente/folga e preserva o dano vanilla.
OotHealthCommit OotHealthApply(ExtendedHealth& health);

enum class OotHealthSettle { None, Committed, Discarded, Rescaled };

// Reconcilia a vida vanilla no fim do update, antes do reset de dano.
// Confirma consumo manual visível; descarta dano ignorado; reescala mudanças externas.
OotHealthSettle OotHealthSettleAfterUpdate(ExtendedHealth& health, uint8_t vanillaNow, uint32_t& amount);

} // namespace ShipLuaHost
