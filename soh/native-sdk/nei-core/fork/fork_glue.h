// Cola entre o coremod linkspan.nei e o código de itens do fork NEI (fork_glue.cpp).
#pragma once

#include <string>

#include <shiplua/native/ship_native_abi.h>

namespace LinkSpanNei {

class Registry;

// Chamados na thread do jogo, no init e no shutdown do coremod. Falha deixa o fork desligado (ForkStatus).
void StartFork(const ShipNativeRuntime* runtime, Registry* registry);
void StopFork();
bool ForkActive();
const std::string& ForkStatus();
std::string ForkLightStatus();
// Atualiza as células do inventário logo após uma mudança pelo menu, inclusive com o jogo pausado.
void RefreshForkInventory();
} // namespace LinkSpanNei
