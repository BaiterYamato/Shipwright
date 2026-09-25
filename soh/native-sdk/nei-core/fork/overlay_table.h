/* Tabela de desvios das funções do host que o fork NEI modificou (NEI-HOST-001).
 *
 * O fork não acrescenta só funções novas: ele muda funções do host (Player_ActionToMeleeWeapon devolve 4 para as
 * rods, o Player_UseItem sabe dos itens novos...). Os fontes do host que o fork muda são compilados sem inline e
 * fora do LTCG (soh/hookable_sources.txt), então cada uma dessas funções tem endereço próprio no soh.exe e toda
 * chamada passa por ele. O overlay.py (via sync.py) copia para a DLL a versão do fork mesclada com a do host, e
 * cada arquivo extraído termina com uma tabela nei_overlay_<arquivo>[] {nome no soh.symbols, cópia na DLL}; o
 * fork/overlay_glue.cpp desvia cada endereço do host para a cópia. */
#pragma once

typedef struct NeiOverlayEntry {
    const char* lookup; /* "Nome" ou "arquivo.c!Nome" (static repetido) */
    void* detour;
} NeiOverlayEntry;
