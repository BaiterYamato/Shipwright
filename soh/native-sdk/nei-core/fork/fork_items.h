// Ponte entre o código C do fork NEI e a cola C++ do coremod (fork_glue.cpp). Sem tipos do jogo: a cola
// C++ inclui este header sem os headers do SoH.
#ifndef LINKSPAN_NEI_FORK_ITEMS_H
#define LINKSPAN_NEI_FORK_ITEMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NeiForkItem {
    uint8_t logicalId; // id do item no fork (ITEM_* de nei_compat.h)
    const char* id;    // id namespaced no linkspan.nei.items
    const char* name;  // nome em inglês
} NeiForkItem;

extern const NeiForkItem gNeiForkItems[];
extern const uint32_t gNeiForkItemCount;

void NeiFork_MapItem(uint8_t runtimeId, uint8_t logicalId);
void NeiFork_ClearItems(void);
uint8_t NeiFork_ToLogicalItem(uint8_t runtimeId);

// Código do fork chamado pela cola (Player* e PlayState* opacos aqui).
void CustomItems_Update(void* player, void* play);
void CustomItems_OverrideDraw(void* player, void* play);

// nei_host_imports.c (gen_imports.py): 0 = ok, senão índice+1 do nome que falhou em *failed.
typedef int (*NeiHostResolveFn)(void* context, const char* name, uintptr_t* address);
int nei_host_resolve(NeiHostResolveFn resolve, void* context, const char** failed);
extern const char nei_host_symbols_sha256[];

#ifdef __cplusplus
}
#endif

#endif
