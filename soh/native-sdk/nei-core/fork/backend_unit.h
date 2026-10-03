#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t NeiEquipment_Catalog(char* out, uint32_t capacity);
int NeiEquipment_Toggle(unsigned type, unsigned index);
void NeiEquipment_Tick(void* player, void* play);
void NeiItems_PostUpdate(void* player, void* play, const void* input);
void NeiBottles_Project(void* play);
uint32_t NeiEquipment_Describe(char* out, uint32_t capacity);
int NeiEquipment_UsesShieldCombo(void);
int NeiBottles_Add(void);
int NeiBottles_ToggleBottomless(void);
uint32_t NeiBottles_Status(char* out, uint32_t capacity);
#ifdef __cplusplus
}
#endif
