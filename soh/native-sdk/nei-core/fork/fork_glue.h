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
// Teste em jogo: posse do item do fork pelo id namespaced e equipado no C esquerdo.
ShipNativeStatus GiveForkItem(const char* id);
// Aquisição de verdade (NEI-008): get-item do registro, com modelo e texto do fork. Aceita o id namespaced, o
// sufixo ("deku_leaf") ou o nome ("Deku Leaf"); `result` diz o que aconteceu.
ShipNativeStatus ReceiveForkItem(const std::string& query, std::string& result);
// "id(logico->runtime,posse,pagina) ..." dos itens do fork no registro, para prova em jogo.
std::string ListForkItems();

} // namespace LinkSpanNei
