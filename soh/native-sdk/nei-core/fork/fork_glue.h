// Cola entre o coremod linkspan.nei e o código de itens do fork NEI (fork_glue.cpp).
#pragma once

#include <string>

#include <shiplua/native/ship_native_abi.h>

namespace LinkSpanNei {

class Registry;

// Chamados na thread do jogo, no init e no shutdown do coremod. Falha deixa o fork desligado (ForkStatus).
void StartFork(const ShipNativeRuntime* runtime, Registry* registry);
void StopFork();
const std::string& ForkStatus();
// Teste em jogo: posse do item do fork pelo id namespaced e equipado no C esquerdo.
ShipNativeStatus GiveForkItem(const char* id);

} // namespace LinkSpanNei
