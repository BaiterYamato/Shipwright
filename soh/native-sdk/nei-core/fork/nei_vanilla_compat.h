// OOT-VANILLA-001 no código do host que a DLL copia (funções mescladas pelo overlay.py): a cópia usa os mesmos
// helpers do host (LinkSpan_S16F, LINKSPAN_BGCHECK_Y_MIN...). A chave gLinkSpanEngineExtended vem por ponteiro,
// como as outras variáveis do host: gen_imports.py resolve nei_host_gLinkSpanEngineExtended pelo soh.symbols.
// Incluído por /FI (fork_cpp_compat.h); a guarda do header do host impede segunda inclusão.
#pragma once

#define gLinkSpanEngineExtended (*nei_host_gLinkSpanEngineExtended)
#include "code/linkspan_vanilla.h"
