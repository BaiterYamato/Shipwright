// Desvios das funções do host que o fork NEI modificou (NEI-HOST-001, fork/overlay_table.h).
#pragma once

#include <string>

#include <shiplua/native/ship_native_abi.h>

namespace LinkSpanNei {

// Desvia cada função da tabela (nei_overlays.c) para a cópia do fork na DLL. Chamado na thread do jogo, depois
// que o escape hatch do fork resolveu os símbolos: as cópias chamam o host pelos mesmos thunks. Função que não
// resolve ou cujo desvio é recusado fica com a versão do host e entra na contagem do OverlayStatus.
void StartOverlays(const ShipNativeRuntime* runtime);
void StopOverlays();
const std::string& OverlayStatus();

} // namespace LinkSpanNei
