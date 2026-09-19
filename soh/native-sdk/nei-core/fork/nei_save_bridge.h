// Ponte C entre NeiSaveData do fork e a persistencia JSON do coremod.
#ifndef LINKSPAN_NEI_SAVE_BRIDGE_H
#define LINKSPAN_NEI_SAVE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NeiSaveField {
    const char* name;
    size_t offset;
    uint32_t elementSize;
    uint32_t count;
    uint8_t isSigned;
} NeiSaveField;

// A tabela preserva as chaves do SaveManager do fork; foto I5 fica deliberadamente fora.
const NeiSaveField* NeiSave_Fields(uint32_t* count);
void* NeiSave_Data(void);
void NeiSave_Reset(void);
// Migrações que o NeiSave_Load do fork aplica depois de ler os campos (tabela de ecos do Trirod v2).
void NeiSave_AfterLoad(void);

#ifdef __cplusplus
}
#endif

#endif
