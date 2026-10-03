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

// Includes the original I5 photo; colour/combo synchronization is separate.
const NeiSaveField* NeiSave_Fields(uint32_t* count);
void* NeiSave_Data(void);
void NeiSave_Reset(void);
// Migrações que o NeiSave_Load do fork aplica depois de ler os campos (tabela de ecos do Trirod v2).
void NeiSave_AfterLoad(void);
// Restores the saved flame into the fork's live item state; defined in player_unit.c.
void NeiLantern_RestoreFire(uint8_t fireType);

#ifdef __cplusplus
}
#endif

#endif
