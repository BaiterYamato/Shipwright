#pragma once

/* A colisão repõe WATER antes de aplicar a superfície de gelo. O estado de pin
 * do frame anterior pode ser falso na aterrissagem; efeitos precisam avaliar
 * o Player atual, preservando a água abaixo da superfície. */
static inline s32 NeiVisual_OnFrozenWater(Player* player) {
    return player != NULL && Seasons_WalksOnWater() &&
           !(player->stateFlags1 & PLAYER_STATE1_IN_WATER) &&
           (player->actor.bgCheckFlags & (BGCHECKFLAG_WATER | BGCHECKFLAG_WATER_TOUCH | BGCHECKFLAG_GROUND)) &&
           player->actor.yDistToWater >= -1.0f && player->actor.yDistToWater <= 20.0f;
}

/* O host pode ter embutido a chamada do efeito no update original. Filtre só
 * a ondulação junto aos pés sobre o gelo; outros atores e água normal seguem. */
static inline s32 NeiVisual_SuppressPlayerRipple(Player* player, const Vec3f* pos) {
    if (pos == NULL || !NeiVisual_OnFrozenWater(player)) return 0;
    f32 dx = pos->x - player->actor.world.pos.x;
    f32 dz = pos->z - player->actor.world.pos.z;
    f32 dy = pos->y - player->actor.world.pos.y;
    return dx * dx + dz * dz <= 900.0f && dy >= -20.0f && dy <= 20.0f;
}
