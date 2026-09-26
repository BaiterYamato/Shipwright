#pragma once

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "oot_randomizer.h"

namespace ShipLuaHost {

// Item de mod oferecido ao randomizer: nome do linkspan.oot.items e cópias no pool.
struct OotRandoOffer {
    std::string name;
    uint8_t copies = 1;
};

void InitializeOotNativeRando(std::thread::id ownerThread = std::this_thread::get_id());
// Esquece ofertas e a seed (testes e troca de host).
void ResetOotNativeRando();
const ShipOotRandomizerV1& GetOotNativeRandomizerService();

// Ofertas cujo item ainda está no registro, em ordem de nome: a mesma seed com os mesmos mods dá o mesmo pool.
// Esta e as funções de seed abaixo valem em qualquer thread (a geração roda fora da thread do jogo).
std::vector<OotRandoOffer> GetOotRandoOffers();

// Itens de mod da seed atual; o índice é o deslocamento na faixa RG_LINKSPAN_ITEM_*. A geração grava a lista, o
// spoiler e o arquivo a restauram.
void SetOotRandoSeedItems(const std::vector<std::string>& names);
std::vector<std::string> GetOotRandoSeedItems();
// Índice do nome na seed atual, acrescentando-o se faltar (leitura de spoiler); -1 com a faixa cheia.
int32_t AddOotRandoSeedItem(const std::string& name);

} // namespace ShipLuaHost
