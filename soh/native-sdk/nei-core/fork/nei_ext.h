/* Campos que o fork NEI acrescentou a structs de atores do host (NEI-HOST-001).
 *
 * O fork pôs campos novos no Player, no EnMThunder e no EnButte. Pôr os campos nos headers do host mudaria o
 * layout id e o tamanho dos atores; aqui cada ator ganha um bloco lateral na DLL, guardado pela guarda de atores
 * (actor_guard.c), zerado quando o ator nasce e liberado quando ele sai das listas. O sync.py reescreve
 * `x->campo` nas funções do host copiadas para a DLL (extracted/) para `NEI_EXT(Tipo, x)->campo`. */
#pragma once

#include "z64.h"

typedef struct NeiExt_Player {
    u8 ivanFloating;
} NeiExt_Player;

typedef struct NeiExt_EnMThunder {
    Actor* homingTarget; /* NULL = voa reto; mira do raio do FD */
    u8 isGerudoCone;
    u8 coneArmed;
    u8 coneWait;
    s16 coneYaw;
} NeiExt_EnMThunder;

typedef struct NeiExt_EnButte {
    u8 netForced;
} NeiExt_EnButte;

/* Bloco lateral do ator (zerado na primeira leitura); nunca NULL. */
void* NeiExt_Get(const void* actor, unsigned size);

#define NEI_EXT(type, actor) ((NeiExt_##type*)NeiExt_Get((actor), sizeof(NeiExt_##type)))
