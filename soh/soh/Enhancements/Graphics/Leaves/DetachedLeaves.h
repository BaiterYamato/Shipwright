#pragma once

struct PlayState;
#ifdef __cplusplus
extern "C" {
#endif

// Retorna 1 quando a emissão foi assumida pelo pool; 0 preserva o caminho vanilla.
// pos aponta para três floats. scale é a escala X do arbusto.
int DetachedLeaves_OnBushCut(struct PlayState* play, const float* pos, float scale);
// Intercepta somente WOOD_LEAF_* após normalizar actor.params; yaw em radianos.
int DetachedLeaves_OnTreeLeaf(struct PlayState* play, const float* pos, float yaw, int yellow);
void DetachedLeaves_Tick(struct PlayState* play);
void DetachedLeaves_Draw(struct PlayState* play);
void DetachedLeaves_Reset(void);

#ifdef __cplusplus
}
#endif
