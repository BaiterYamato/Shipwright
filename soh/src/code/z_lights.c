#include "global.h"
#include "z_light_list.h" // SOH [Unbound] Internal API; excluded from native layout id.

#include <string.h>

#include "objects/gameplay_keep/gameplay_keep.h"

#include "soh/frame_interpolation.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/savestate_serialize.h"
#include "linkspan_vanilla.h"

// SOH [Link-Span] hook TRANSFORM e chave de serviço; inativos por padrão.
void LinkSpan_PointLightColor(LightInfo* info, u8* r, u8* g, u8* b, s16 radius);
s32 LinkSpan_RenderHideVanillaPointGlow(void);

// SOH [Unbound] M18: keep the vanilla budget independent of scene/room lists.
#define LIGHTS_ACTOR_BUFFER_SIZE 32
#define LIGHTS_LIST_LIMIT 255
// SOH [Unbound] Scene + two full room lists; cumulative until scene init, as vanilla.
#define LIGHTS_ROOM_BUFFER_SIZE (3 * LIGHTS_LIST_LIMIT)
#define LIGHTS_BUFFER_SIZE (LIGHTS_ACTOR_BUFFER_SIZE + LIGHTS_ROOM_BUFFER_SIZE)

typedef struct {
    /* 0x000 */ s32 numOccupied;
    /* 0x004 */ s32 searchIndex;
    /* 0x008 */ LightNode buf[LIGHTS_BUFFER_SIZE];
    // SOH [Unbound] Saved together with nodes; scene/room lists share the list partition.
    s32 numRoomLights;
    s32 roomSearchIndex;
    s32 exhaustionWarned;
} LightsBuffer;

static LightsBuffer sLightsBuffer;

#define LIGHTS_SHIP_SAVESTATE_FIELDS(F) F(sLightsBuffer)
SHIP_SAVESTATE_DEFINE(Lights, LIGHTS_SHIP_SAVESTATE_FIELDS)

void Lights_PointSetInfo(LightInfo* info, f32 x, f32 y, f32 z, u8 r, u8 g, u8 b, s16 radius, s32 type) {
    info->type = type;
    // SOH [Link-Span] OOT-VANILLA-001: sem mod, a posição vira inteira como o parâmetro s16 do upstream.
    info->params.point.x = LinkSpan_S16F(x);
    info->params.point.y = LinkSpan_S16F(y);
    info->params.point.z = LinkSpan_S16F(z);
    Lights_PointSetColorAndRadius(info, r, g, b, radius);
}

void Lights_PointNoGlowSetInfo(LightInfo* info, f32 x, f32 y, f32 z, u8 r, u8 g, u8 b, s16 radius) {
    Lights_PointSetInfo(info, x, y, z, r, g, b, radius, LIGHT_POINT_NOGLOW);
}

void Lights_PointGlowSetInfo(LightInfo* info, f32 x, f32 y, f32 z, u8 r, u8 g, u8 b, s16 radius) {
    Lights_PointSetInfo(info, x, y, z, r, g, b, radius, LIGHT_POINT_GLOW);
}

void Lights_PointSetColorAndRadius(LightInfo* info, u8 r, u8 g, u8 b, s16 radius) {
    LinkSpan_PointLightColor(info, &r, &g, &b, radius);
    info->params.point.color[0] = r;
    info->params.point.color[1] = g;
    info->params.point.color[2] = b;
    info->params.point.radius = radius;
}

void Lights_DirectionalSetInfo(LightInfo* info, s8 x, s8 y, s8 z, u8 r, u8 g, u8 b) {
    info->type = LIGHT_DIRECTIONAL;
    info->params.dir.x = x;
    info->params.dir.y = y;
    info->params.dir.z = z;
    info->params.dir.color[0] = r;
    info->params.dir.color[1] = g;
    info->params.dir.color[2] = b;
}

// unused
void Lights_Reset(Lights* lights, u8 ambentR, u8 ambentG, u8 ambentB) {
    lights->l.a.l.col[0] = lights->l.a.l.colc[0] = ambentR;
    lights->l.a.l.col[1] = lights->l.a.l.colc[1] = ambentG;
    lights->l.a.l.col[2] = lights->l.a.l.colc[2] = ambentB;
    lights->numLights = 0;
}

/*
 * Draws every light in the provided Lights group
 */
void Lights_Draw(Lights* lights, GraphicsContext* gfxCtx) {
    Light* light;
    s32 i;

#if 1

    OPEN_DISPS(gfxCtx);

    gSPNumLights(POLY_OPA_DISP++, lights->numLights);
    gSPNumLights(POLY_XLU_DISP++, lights->numLights);

    i = 0;
    light = &lights->l.l[0];

    while (i < lights->numLights) {
        i++;
        gSPLight(POLY_OPA_DISP++, light, i);
        gSPLight(POLY_XLU_DISP++, light, i);
        light++;
    }

    i++; // abmient light is total number of lights + 1
    gSPLight(POLY_OPA_DISP++, &lights->l.a, i);
    gSPLight(POLY_XLU_DISP++, &lights->l.a, i);

    CLOSE_DISPS(gfxCtx);
#endif
}

Light* Lights_FindSlot(Lights* lights) {
    if (lights->numLights >= 7) {
        return NULL;
    } else {
        return &lights->l.l[lights->numLights++];
    }
}

void Lights_BindPoint(Lights* lights, LightParams* params, Vec3f* vec) {
    f32 xDiff;
    f32 yDiff;
    f32 zDiff;
    f32 posDiff;
    f32 scale;
    Light* light;

    if (vec != NULL) {
        xDiff = params->point.x - vec->x;
        yDiff = params->point.y - vec->y;
        zDiff = params->point.z - vec->z;
        scale = params->point.radius;
        posDiff = SQ(xDiff) + SQ(yDiff) + SQ(zDiff);

        if (posDiff < SQ(scale)) {
            light = Lights_FindSlot(lights);

            if (light != NULL) {
                posDiff = sqrtf(posDiff);
                scale = posDiff / scale;
                scale = 1 - SQ(scale);

                light->l.col[0] = light->l.colc[0] = params->point.color[0] * scale;
                light->l.col[1] = light->l.colc[1] = params->point.color[1] * scale;
                light->l.col[2] = light->l.colc[2] = params->point.color[2] * scale;

                scale = (posDiff < 1.0f) ? 120.0f : 120.0f / posDiff;

                light->l.dir[0] = xDiff * scale;
                light->l.dir[1] = yDiff * scale;
                light->l.dir[2] = zDiff * scale;
            }
        }
    }
}

void Lights_BindDirectional(Lights* lights, LightParams* params, Vec3f* vec) {
    Light* light = Lights_FindSlot(lights);

    if (light != NULL) {
        light->l.col[0] = light->l.colc[0] = params->dir.color[0];
        light->l.col[1] = light->l.colc[1] = params->dir.color[1];
        light->l.col[2] = light->l.colc[2] = params->dir.color[2];
        light->l.dir[0] = params->dir.x;
        light->l.dir[1] = params->dir.y;
        light->l.dir[2] = params->dir.z;
    }
}

/**
 * For every light in a provided list, try to find a free slot in the provided Lights group and bind
 * a light to it. Then apply color and positional/directional info for each light
 * based on the parameters supplied by the node.
 *
 * Note: Lights in a given list can only be binded to however many free slots are
 * available in the Lights group. This is at most 7 slots for a new group, but could be less.
 */
void Lights_BindAll(Lights* lights, LightNode* listHead, Vec3f* vec) {
    LightsBindFunc bindFuncs[] = { Lights_BindPoint, Lights_BindDirectional, Lights_BindPoint };
    LightInfo* info;

    while (listHead != NULL) {
        info = listHead->info;
        bindFuncs[info->type](lights, &info->params, vec);
        listHead = listHead->next;
    }
}

// SOH [Unbound] Search only the caller's partition; never borrow actor slots.
static LightNode* Lights_FindBufSlotRange(s32 start, s32 end, s32 occupied, s32* searchIndex) {
    LightNode* node;

    if (occupied >= end - start) {
        return NULL;
    }

    node = &sLightsBuffer.buf[*searchIndex];
    while (node->info != NULL) {
        (*searchIndex)++;
        if (*searchIndex < end) {
            node++;
        } else {
            *searchIndex = start;
            node = &sLightsBuffer.buf[start];
        }
    }
    sLightsBuffer.numOccupied++;
    return node;
}

// SOH [Unbound] Existing callers retain all 32 vanilla slots.
LightNode* Lights_FindBufSlot() {
    return Lights_FindBufSlotRange(0, LIGHTS_ACTOR_BUFFER_SIZE,
                                  sLightsBuffer.numOccupied - sLightsBuffer.numRoomLights,
                                  &sLightsBuffer.searchIndex);
}

// return type must not be void to match
s32 Lights_FreeNode(LightNode* light) {
    // SOH [Link-Span] OOT-VANILLA-001: sem mod, como o upstream: sem proteção contra liberar duas vezes e com o
    // searchIndex dividido de novo por sizeof(LightNode).
    if (!LinkSpan_EngineExtended()) {
        if (light != NULL) {
            sLightsBuffer.numOccupied--;
            light->info = NULL;
            sLightsBuffer.searchIndex = (light - sLightsBuffer.buf) / sizeof(LightNode);
        }
        return 0;
    }
    // SOH [Unbound] Pointer subtraction already yields an index; ignore a repeated free.
    if (light != NULL && light->info != NULL) {
        s32 index = light - sLightsBuffer.buf;
        sLightsBuffer.numOccupied--;
        light->info = NULL;
        if (index >= LIGHTS_ACTOR_BUFFER_SIZE) {
            sLightsBuffer.numRoomLights--;
            sLightsBuffer.roomSearchIndex = index;
        } else {
            sLightsBuffer.searchIndex = index;
        }
    }
    return 0; // SOH [Unbound] Defined return value for the legacy s32 signature.
}

void LightContext_Init(PlayState* play, LightContext* lightCtx) {
    LightContext_InitList(play, lightCtx);
    LightContext_SetAmbientColor(lightCtx, 80, 80, 80);
    LightContext_SetFog(lightCtx, 0, 0, 0, 996, 12800);
    // SOH [Unbound]
    lightCtx->worldFog = 0;
    lightCtx->fogStart = 0.0f;
    lightCtx->fogEnd = 0.0f;
    lightCtx->zNear = 10.0f;
    lightCtx->zFar = 12800.0f;
    memset(&sLightsBuffer, 0, sizeof(sLightsBuffer));
    // SOH [Unbound] The room allocator starts beyond the vanilla partition.
    sLightsBuffer.roomSearchIndex = LIGHTS_ACTOR_BUFFER_SIZE;
}

void LightContext_SetAmbientColor(LightContext* lightCtx, u8 r, u8 g, u8 b) {
    lightCtx->ambientColor[0] = r;
    lightCtx->ambientColor[1] = g;
    lightCtx->ambientColor[2] = b;
}

void LightContext_SetFog(LightContext* lightCtx, u8 r, u8 g, u8 b, s16 fogNear, s16 fogFar) {
    lightCtx->fogColor[0] = r;
    lightCtx->fogColor[1] = g;
    lightCtx->fogColor[2] = b;
    lightCtx->fogNear = fogNear;
    lightCtx->fogFar = fogFar;
}

/**
 * Allocate a new Lights group and initilize the ambient color with that provided by LightContext
 */
Lights* LightContext_NewLights(LightContext* lightCtx, GraphicsContext* gfxCtx) {
    return Lights_New(gfxCtx, lightCtx->ambientColor[0], lightCtx->ambientColor[1], lightCtx->ambientColor[2]);
}

void LightContext_InitList(PlayState* play, LightContext* lightCtx) {
    lightCtx->listHead = NULL;
}

void LightContext_DestroyList(PlayState* play, LightContext* lightCtx) {
    // SOH [Unbound] Owners retain raw nodes: refuse atomically while any owned slot is live.
    for (s32 i = 0; i < LIGHTS_ACTOR_BUFFER_SIZE; i++) {
        if (sLightsBuffer.buf[i].info != NULL) {
            // SOH [Unbound] WARN remains visible in release; callers must remove owners first.
            lusprintf(__FILE__, __LINE__, 3,
                      "[Unbound M18] cena=%d sala=%d: DestroyList recusado; luz de ator/ambiente/bridge ativa\n",
                      play->sceneNum, play->roomCtx.curRoom.num);
            return;
        }
    }
    while (lightCtx->listHead != NULL) {
        LightContext_RemoveLight(play, lightCtx, lightCtx->listHead);
        // SOH [Unbound] RemoveLight already advances listHead; do not skip/dereference NULL.
    }
}

/**
 * Insert a new light into the list pointed to by LightContext
 *
 * Note: Due to the limited number of slots in a Lights group, inserting too many lights in the
 * list may result in older entries not being bound to a Light when calling Lights_BindAll
 */
// SOH [Unbound] Both producers share linking and one warning per scene initialization.
static LightNode* LightContext_LinkLight(PlayState* play, LightContext* lightCtx, LightInfo* info, LightNode* node) {
    if (node != NULL) {
        node->info = info;
        node->prev = NULL;
        node->next = lightCtx->listHead;
        if (lightCtx->listHead != NULL) {
            lightCtx->listHead->prev = node;
        }
        lightCtx->listHead = node;
    } else if (!sLightsBuffer.exhaustionWarned && LinkSpan_EngineExtended()) {
        // SOH [Unbound] WARN directly: osSyncPrintf is disabled in release builds.
        lusprintf(__FILE__, __LINE__, 3,
                  "[Unbound M18] cena=%d sala=%d: limite de luzes atingido; listas=%d/%d "
                  "atores/ambiente=%d/%d; luz descartada\n",
                  play->sceneNum, play->roomCtx.curRoom.num, sLightsBuffer.numRoomLights,
                  LIGHTS_ROOM_BUFFER_SIZE, sLightsBuffer.numOccupied - sLightsBuffer.numRoomLights,
                  LIGHTS_ACTOR_BUFFER_SIZE);
        sLightsBuffer.exhaustionWarned = 1;
    }
    return node;
}

// SOH [Unbound] Vanilla actors/effects/environment keep the original entry point.
LightNode* LightContext_InsertLight(PlayState* play, LightContext* lightCtx, LightInfo* info) {
    return LightContext_LinkLight(play, lightCtx, info, Lights_FindBufSlot());
}

// SOH [Unbound] Each command accepts 255 entries; preserve older lists until scene init.
LightNode* LightContext_InsertListLight(PlayState* play, LightContext* lightCtx, LightInfo* info, size_t listIndex) {
    // SOH [Link-Span] OOT-VANILLA-001: sem mod, a lista da cena divide as 32 vagas com atores e ambiente, como no
    // upstream (Scene_CommandLightList chamava LightContext_InsertLight).
    if (!LinkSpan_EngineExtended()) {
        return LightContext_InsertLight(play, lightCtx, info);
    }
    // SOH [Unbound] Enforce the per-command limit without erasing scene/prevRoom lights.
    if (listIndex >= LIGHTS_LIST_LIMIT) {
        return LightContext_LinkLight(play, lightCtx, info, NULL);
    }
    LightNode* node = Lights_FindBufSlotRange(LIGHTS_ACTOR_BUFFER_SIZE, LIGHTS_BUFFER_SIZE,
                                             sLightsBuffer.numRoomLights, &sLightsBuffer.roomSearchIndex);
    if (node != NULL) {
        sLightsBuffer.numRoomLights++;
    }
    return LightContext_LinkLight(play, lightCtx, info, node);
}

void LightContext_RemoveLight(PlayState* play, LightContext* lightCtx, LightNode* node) {
    // SOH [Unbound] A retained free node must never modify the list links.
    // SOH [Link-Span] OOT-VANILLA-001: sem mod, como o upstream, que não conferia info.
    if (node != NULL && (node->info != NULL || !LinkSpan_EngineExtended())) {
        if (node->prev != NULL) {
            node->prev->next = node->next;
        } else {
            lightCtx->listHead = node->next;
        }

        if (node->next != NULL) {
            node->next->prev = node->prev;
        }

        Lights_FreeNode(node);
    }
}

// unused
Lights* Lights_NewAndDraw(GraphicsContext* gfxCtx, u8 ambientR, u8 ambientG, u8 ambientB, u8 numLights, u8 r, u8 g,
                          u8 b, s8 x, s8 y, s8 z) {
    Lights* lights;
    s32 i;

    lights = Graph_Alloc(gfxCtx, sizeof(Lights));

    lights->l.a.l.col[0] = lights->l.a.l.colc[0] = ambientR;
    lights->l.a.l.col[1] = lights->l.a.l.colc[1] = ambientG;
    lights->l.a.l.col[2] = lights->l.a.l.colc[2] = ambientB;
    lights->numLights = numLights;

    for (i = 0; i < numLights; i++) {
        lights->l.l[i].l.col[0] = lights->l.l[i].l.colc[0] = r;
        lights->l.l[i].l.col[1] = lights->l.l[i].l.colc[1] = g;
        lights->l.l[i].l.col[2] = lights->l.l[i].l.colc[2] = b;
        lights->l.l[i].l.dir[0] = x;
        lights->l.l[i].l.dir[1] = y;
        lights->l.l[i].l.dir[2] = z;
    }

    Lights_Draw(lights, gfxCtx);

    return lights;
}

Lights* Lights_New(GraphicsContext* gfxCtx, u8 ambientR, u8 ambientG, u8 ambientB) {
    Lights* lights;

    lights = Graph_Alloc(gfxCtx, sizeof(Lights));

    lights->l.a.l.col[0] = lights->l.a.l.colc[0] = ambientR;
    lights->l.a.l.col[1] = lights->l.a.l.colc[1] = ambientG;
    lights->l.a.l.col[2] = lights->l.a.l.colc[2] = ambientB;
    lights->numLights = 0;

    return lights;
}

void Lights_GlowCheckPrepare(PlayState* play) {
    LightNode* node;
    LightPoint* params;
    Vec3f pos;
    Vec3f multDest;
    f32 wDest;
    f32 wX;
    f32 wY;

    node = play->lightCtx.listHead;

    while (node != NULL) {
        params = &node->info->params.point;

        if (node->info->type == LIGHT_POINT_GLOW) {
            f32 x, y;
            u32 shrink;
            uint32_t height;

            pos.x = params->x;
            pos.y = params->y;
            pos.z = params->z;
            Actor_ProjectPos(play, &pos, &multDest, &wDest);
            wX = multDest.x * wDest;
            wY = multDest.y * wDest;

            x = wX * 160 + 160;
            y = wY * 120 + 120;
            shrink = ShrinkWindow_GetCurrentVal();

            if ((multDest.z > 1.0f) && y >= shrink && y <= SCREEN_HEIGHT - shrink) {
                OTRGetPixelDepthPrepare(x, y);
            }
        }
        node = node->next;
    }
}

void Lights_GlowCheck(PlayState* play) {
    LightNode* node;
    LightPoint* params;
    Vec3f pos;
    Vec3f multDest;
    f32 wDest;
    f32 wX;
    f32 wY;
    s32 wZ;
    s32 zBuf;

    node = play->lightCtx.listHead;

    while (node != NULL) {
        params = &node->info->params.point;

        if (node->info->type == LIGHT_POINT_GLOW) {
            f32 x, y;
            u32 shrink;
            uint32_t height;

            pos.x = params->x;
            pos.y = params->y;
            pos.z = params->z;
            Actor_ProjectPos(play, &pos, &multDest, &wDest);
            params->drawGlow = false;
            wX = multDest.x * wDest;
            wY = multDest.y * wDest;

            x = wX * 160 + 160;
            y = wY * 120 + 120;
            shrink = ShrinkWindow_GetCurrentVal();

            if ((multDest.z > 1.0f) && y >= shrink && y <= SCREEN_HEIGHT - shrink) {
                wZ = (s32)((multDest.z * wDest) * 16352.0f) + 16352;
                zBuf = OTRGetPixelDepth(x, y) * 4;

                if (wZ < (zBuf >> 3)) {
                    params->drawGlow = true;
                }
            }
        }
        node = node->next;
    }
}

void Lights_DrawGlow(PlayState* play) {
    s32 pad;
    LightNode* node;

    if (LinkSpan_RenderHideVanillaPointGlow()) {
        return;
    }

    node = play->lightCtx.listHead;

    OPEN_DISPS(play->state.gfxCtx);

    POLY_XLU_DISP = func_800947AC(POLY_XLU_DISP++);
    gDPSetAlphaDither(POLY_XLU_DISP++, G_AD_NOISE);
    gDPSetColorDither(POLY_XLU_DISP++, G_CD_MAGICSQ);
    gSPDisplayList(POLY_XLU_DISP++, gGlowCircleTextureLoadDL);

    while (node != NULL) {
        LightInfo* info;
        LightPoint* params;
        f32 scale;
        s32 pad[4];

        info = node->info;
        params = &info->params.point;

        if ((info->type == LIGHT_POINT_GLOW) && (params->drawGlow)) {
            scale = SQ(params->radius) * 0.0000026f;

            FrameInterpolation_RecordOpenChild(node, 0);
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, params->color[0], params->color[1], params->color[2], 50);
            Matrix_Translate(params->x, params->y, params->z, MTXMODE_NEW);
            Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, gGlowCircleDL);
            FrameInterpolation_RecordCloseChild();
        }

        node = node->next;
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
