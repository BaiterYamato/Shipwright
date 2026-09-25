// Ponte entre o código C do fork NEI e a cola C++ do coremod (fork_glue.cpp). Sem tipos do jogo: a cola
// C++ inclui este header sem os headers do SoH.
#ifndef LINKSPAN_NEI_FORK_ITEMS_H
#define LINKSPAN_NEI_FORK_ITEMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// O que o fork diz de um item (linha do sNeiItems[] e tabela de modelos), para o registro linkspan.nei.items.
typedef struct NeiForkItemInfo {
    uint8_t logicalId;      // id do item no fork (ITEM_* de nei_compat.h)
    uint8_t slot;           // slot da página do NEI (24..47), ou 0xFF sem célula
    uint8_t age;            // LINKSPAN_OOT_ITEM_AGE_* (0 adulto, 1 criança, 9 qualquer)
    const char* icon;       // textura do ícone sem __OTR__, ou NULL
    const char* message;    // texto do get-item em inglês, com os códigos do fork, ou NULL
    const char* modelPath;  // display list do get-item sem __OTR__, ou NULL
    float modelScale;
    uint8_t modelLayer;     // NEI_MODEL_LAYER_* (fork_models.h)
    const char* component;  // componente de assets de que o item depende ("core", "expansion.ssbb"...)
} NeiForkItemInfo;

// Ids lógicos dos itens do fork que o coremod liga ao registro. Devolve quantos escreveu.
uint32_t NeiFork_ListItems(uint8_t* out, uint32_t capacity);
// 0 quando o fork não conhece o item.
int NeiFork_DescribeItem(uint8_t logicalId, NeiForkItemInfo* info);
// 0 para os itens que rodam sem os assets do fork (não desenham modelo dele: Roc's Feather e Roc's Cape).
int NeiFork_NeedsAssets(uint8_t logicalId);
// Nome do item que o fork não escreve no texto do get-item, ou NULL.
const char* NeiFork_FallbackName(uint8_t logicalId);

void NeiFork_MapItem(uint8_t runtimeId, uint8_t logicalId);
void NeiFork_ClearItems(void);
uint8_t NeiFork_ToLogicalItem(uint8_t runtimeId);
uint8_t NeiFork_ToRuntimeItem(uint8_t logicalId);

// fork/inventory_unit.c: posse do item na página do NEI (gNeiSave), a que o kaleido do fork mostra.
void NeiInv_ReceiveItem(uint8_t logicalId);
void NeiInv_PlaceItem(uint8_t logicalId);
void NeiInv_RemoveItem(uint8_t logicalId);
int NeiInv_HasItem(uint8_t logicalId);

// Código do fork chamado pela cola (Player* e PlayState* opacos aqui).
void CustomItems_Update(void* player, void* play);
void CustomItems_OverrideDraw(void* player, void* play);

// nei_host_imports.c (gen_imports.py): 0 = ok, senão índice+1 do nome que falhou em *failed.
typedef int (*NeiHostResolveFn)(void* context, const char* name, uintptr_t* address);
int nei_host_resolve(NeiHostResolveFn resolve, void* context, const char** failed);
extern const char nei_host_symbols_sha256[];

// assets.cpp: 1 quando o componente de assets está montado.
int NeiAssets_CoreMounted(void);
int NeiAssets_ComponentMounted(const char* component);

#ifdef __cplusplus
}
#endif

#endif
