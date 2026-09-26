// Cola entre o coremod linkspan.nei e a página de itens do kaleido do fork NEI (NEI-003).
#pragma once

#include <string>

#include <shiplua/native/ship_native_abi.h>

namespace LinkSpanNei {

// Desvia os pontos do kaleido do host para a versão do fork, que enxerga os slots acima de 23.
// Chamado na thread do jogo, depois que o escape hatch do fork resolveu os símbolos: sem ele as
// funções copiadas não têm como chamar o host. Falha deixa o menu como o do host (KaleidoStatus).
void StartKaleido(const ShipNativeRuntime* runtime);
void StopKaleido();
const std::string& KaleidoStatus();

// Estado das páginas do inventário, para o stats.
std::string InventoryStatus();

} // namespace LinkSpanNei
