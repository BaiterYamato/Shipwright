#pragma once
#include "z64math.h"
struct PlayState;
struct Player;
#ifdef __cplusplus
extern "C" {
#endif
// Somente no thread do jogo, dentro de Player_DrawImpl/Matrix_*.
void HatPhysics_BeginDraw(struct PlayState* play, void* player, int gameplayPath);
void HatPhysics_EndDraw(void);
void HatPhysics_OnLimb(struct PlayState* play, struct Player* player, int limb);
// Retorna 1 após consumir pos/rot e compor a matriz; 0 não altera nada.
int HatPhysics_Apply(struct PlayState* play, struct Player* player, Vec3f* pos, Vec3s* rot);
#ifdef __cplusplus
}
#endif
