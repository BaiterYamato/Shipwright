/* Peças que o kaleido do fork (NEI-003) pede e que este soh.exe não entrega pelo escape hatch.
 *
 * Duas famílias, por motivos diferentes:
 *
 * 1. Os três OTRGet*Edge são funções de uma linha do OTRGlobals.cpp. O LTCG do host as inlinou em
 *    todos os usos e elas sumiram do soh.symbols, então não há endereço para resolver. O que elas
 *    fazem cabe aqui: é a mesma conta sobre o OTRGetAspectRatio(), esse sim resolvido. Copiar a
 *    conta vale mais do que um stub, porque o resultado posiciona o HUD em tela larga.
 *
 * 2. Os ExtButton_* são a ponte do fork para equipar item de id u16 num botão C. O fork guarda o id
 *    real em `gSaveContext.ship.extButtons`, um campo que ele acrescentou ao z64save.h — e mexer nos
 *    headers do host mudaria o layout id da rodada. A DLL fica com a própria tabela: o marcador
 *    ITEM_EXT_BUTTON continua indo para o buttonItems do host, que é o que o jogo lê, e o id real
 *    vive aqui. Consequência conhecida: esse vínculo não entra no save e o código do próprio host
 *    não enxerga o id real, só o marcador. */
#include "z64.h"
#include "macros.h"

extern SaveContext gSaveContext;

/* Resolvido no soh.exe (gen_imports.py). */
float OTRGetAspectRatio(void);

float OTRGetDimensionFromLeftEdge(float v) {
    return (SCREEN_WIDTH / 2 - SCREEN_HEIGHT / 2 * OTRGetAspectRatio() + (v));
}

float OTRGetDimensionFromRightEdge(float v) {
    return (SCREEN_WIDTH / 2 + SCREEN_HEIGHT / 2 * OTRGetAspectRatio() - (SCREEN_WIDTH - v));
}

int16_t OTRGetRectDimensionFromRightEdge(float v) {
    float x = OTRGetDimensionFromRightEdge(v);
    int16_t truncado = (int16_t)x;
    return (x > (float)truncado) ? (int16_t)(truncado + 1) : truncado; /* ceilf sem a CRT */
}

/* Um id por botão do jogo (B, três C e quatro do D-pad), no mesmo índice do buttonItems. */
static u16 sExtButtonItems[ARRAY_COUNT(gSaveContext.equips.buttonItems)];

u16 ExtButton_GetItem(s32 btn) {
    if (btn < 0 || btn >= (s32)ARRAY_COUNT(sExtButtonItems)) {
        return ITEM_NONE;
    }
    if (gSaveContext.equips.buttonItems[btn] == ITEM_EXT_BUTTON) {
        return sExtButtonItems[btn];
    }
    return (u16)gSaveContext.equips.buttonItems[btn];
}

void ExtButton_SetItem(s32 btn, u16 extId) {
    if (btn < 0 || btn >= (s32)ARRAY_COUNT(sExtButtonItems)) {
        return;
    }
    gSaveContext.equips.buttonItems[btn] = ITEM_EXT_BUTTON;
    sExtButtonItems[btn] = extId;
}

void ExtButton_ClearItem(s32 btn) {
    if (btn < 0 || btn >= (s32)ARRAY_COUNT(sExtButtonItems)) {
        return;
    }
    gSaveContext.equips.buttonItems[btn] = ITEM_NONE;
    sExtButtonItems[btn] = 0;
}
