/* Funções que o fork NEI acrescentou ao host e que a DLL implementa com o que o host já exporta (NEI-008).
 *
 * O stub "recurso ausente" do gen_stubs.py devolve NULL/0; para as funções daqui isso desligava item inteiro sem
 * aviso (a Shovel não cavava porque a animação vinha NULL). Cada uma sai da lista stub-names.txt quando entra
 * aqui. */
#include "z64.h"
#include <string.h>

char* ResourceMgr_GetResourceDataByNameHandlingMQ(const char* path);
size_t ResourceGetSizeByName(const char* name);

/* ResourceManagerHelpers.cpp do fork: um SOH_PlayerAnimation é s16 cru (67 por quadro), não um
 * LinkAnimationHeader; o cabeçalho é montado aqui e guardado por caminho, para o ponteiro valer a sessão toda. */
#define NEI_ANIM_S16_PER_FRAME 67
#define NEI_ANIM_WRAPPERS 64

typedef struct {
    char path[128];
    LinkAnimationHeader header;
} NeiAnimWrapper;

static NeiAnimWrapper sAnimWrappers[NEI_ANIM_WRAPPERS];
static u32 sAnimWrapperCount;

LinkAnimationHeader* ResourceMgr_LoadPlayerAnimAsHeader(const char* animPath) {
    if (animPath == NULL || strlen(animPath) >= sizeof(sAnimWrappers[0].path)) {
        return NULL;
    }
    for (u32 i = 0; i < sAnimWrapperCount; i++) {
        if (strcmp(sAnimWrappers[i].path, animPath) == 0) {
            return &sAnimWrappers[i].header;
        }
    }
    if (sAnimWrapperCount >= NEI_ANIM_WRAPPERS) {
        return NULL;
    }
    char* data = ResourceMgr_GetResourceDataByNameHandlingMQ(animPath);
    const size_t bytes = ResourceGetSizeByName(animPath);
    if (data == NULL || bytes < NEI_ANIM_S16_PER_FRAME * sizeof(s16)) {
        return NULL;
    }
    NeiAnimWrapper* wrapper = &sAnimWrappers[sAnimWrapperCount++];
    strcpy(wrapper->path, animPath);
    wrapper->header.common.frameCount = (s16)((bytes / sizeof(s16)) / NEI_ANIM_S16_PER_FRAME);
    wrapper->header.segment = data;
    return &wrapper->header;
}
