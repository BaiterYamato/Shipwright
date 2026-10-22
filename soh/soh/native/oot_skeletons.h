#ifndef LINKSPAN_OOT_SKELETONS_H
#define LINKSPAN_OOT_SKELETONS_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_SKELETONS_SERVICE "linkspan.oot.skeletons"
#define LINKSPAN_OOT_SKELETONS_VERSION 1u
#define LINKSPAN_OOT_SKELETONS_MAX 256u
#define LINKSPAN_OOT_SKELETONS_MAX_PATH 256u

/* Modos de play_animation, na semântica de ANIMMODE_* do jogo. */
#define LINKSPAN_OOT_ANIM_LOOP 0u
#define LINKSPAN_OOT_ANIM_ONCE 2u

/* Esqueletos animados de mods (SkelAnime do jogo), só na thread do jogo.
 *
 * create: esqueleto normal ou flex do resource manager (caminho sem __OTR__) com
 *   uma animação inicial em loop. Esqueleto de curva ou recurso ausente = FAILURE.
 *   A animação precisa ter sido feita para o esqueleto; o host não confere juntas.
 * play_animation: troca a animação. speed 1.0 = velocidade do jogo; morph_frames
 *   > 0 transiciona da pose atual. LINKSPAN_OOT_ANIM_ONCE para no último frame.
 * update: avança um frame (chame uma vez por update do ator dono); `finished` = 1
 *   quando uma animação ONCE chegou ao fim.
 * get_frame: frame atual e último frame da animação corrente.
 * draw: desenha com a matriz corrente, opaco. Só em escopo de draw de
 *   linkspan.oot.render (draw de ator de mod, oot.actor.draw ou
 *   oot.player.limb_draw); fora dele UNSUPPORTED.
 *
 * Os esqueletos não dependem da cena, mas guardam poses de recursos do jogo: quem
 * cria deve destruir no destroy do ator ou no shutdown. O reset do host libera o
 * que sobrar. */
typedef struct ShipOotSkeletonsV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* create)(const char* skeleton_path, const char* animation_path,
                                               uint64_t* skeleton);
    ShipNativeStatus(SHIP_NATIVE_CALL* destroy)(uint64_t skeleton);
    ShipNativeStatus(SHIP_NATIVE_CALL* play_animation)(uint64_t skeleton, const char* animation_path, float speed,
                                                       uint8_t mode, float morph_frames);
    ShipNativeStatus(SHIP_NATIVE_CALL* update)(uint64_t skeleton, uint8_t* finished);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_frame)(uint64_t skeleton, float* frame, float* last_frame);
    ShipNativeStatus(SHIP_NATIVE_CALL* draw)(void* play_state, uint64_t skeleton);
} ShipOotSkeletonsV1;

/* Animação tipada e pós-membro; prefixo V1 literal. layout_id vem na cauda.
 * Handles novos têm owner; teardown/unload drenam animação, render e callbacks. */
#define LINKSPAN_OOT_SKELETONS_VERSION_2 2u
#define SHIP_OOT_SKELETON_STANDARD 0u
#define SHIP_OOT_SKELETON_LINK 1u
#define SHIP_OOT_ROOT_DISABLED 0u
#define SHIP_OOT_ROOT_XZ 1u
#define SHIP_OOT_ANIM_OVERRIDE 0u
#define SHIP_OOT_ANIM_FALLBACK 1u

typedef struct ShipOotSkeletonSpecV2 {
    uint32_t size;
    uint32_t format;
    const char* skeleton_path;
    const char* initial_animation_path;
    int16_t base_translation[3]; /* unidades da junta raiz; nunca hardcoded por forma */
    uint16_t reserved;
} ShipOotSkeletonSpecV2;

typedef struct ShipOotAnimationPlayV2 {
    uint32_t size;
    float speed;        /* magnitude > 0; reverse escolhe o sinal */
    float morph_frames; /* >= 0 */
    uint8_t mode;       /* LINKSPAN_OOT_ANIM_LOOP ou LINKSPAN_OOT_ANIM_ONCE */
    uint8_t reverse;
    uint8_t reserved[2];
} ShipOotAnimationPlayV2;

typedef struct ShipOotAnimationInfoV2 {
    uint32_t size;
    uint8_t present; /* declarado/encontrado */
    uint8_t loaded;  /* resolvido e validado como o formato da instância */
    uint8_t reserved[2];
    float asset_last_frame; /* limite do asset, independentemente de reverse */
} ShipOotAnimationInfoV2;

typedef struct ShipOotAnimationStateV2 {
    uint32_t size;
    uint32_t reserved_align;
    uint64_t scene_epoch;
    uint64_t tick;             /* último update desta instância */
    uint64_t animation_serial; /* muda a cada restart/troca */
    float current_frame;
    float asset_last_frame;
    float target_end_frame; /* 0 em reverse; não confundir com asset_last_frame */
    float speed;            /* com sinal */
    uint8_t finished_previous_update;
    uint8_t reserved[7];
} ShipOotAnimationStateV2;

typedef struct ShipOotSkeletonLimbV2 {
    uint32_t size;
    uint32_t reserved_align;
    uint64_t scene_epoch;
    uint64_t skeleton;
    void* play_state;
    void* actor;  /* dono visual; NULL para draw genérico sem ator */
    int32_t limb; /* índice emitido pelo draw do jogo, não normalizado */
    uint32_t reserved_tail;
} ShipOotSkeletonLimbV2;

typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotSkeletonLimbFnV2)(void* user, const ShipOotSkeletonLimbV2* limb);

typedef struct ShipOotSkeletonsV2 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* create)(const char* skeleton_path, const char* animation_path,
                                               uint64_t* skeleton);
    ShipNativeStatus(SHIP_NATIVE_CALL* destroy)(uint64_t skeleton);
    ShipNativeStatus(SHIP_NATIVE_CALL* play_animation)(uint64_t skeleton, const char* animation_path, float speed,
                                                       uint8_t mode, float morph_frames);
    ShipNativeStatus(SHIP_NATIVE_CALL* update)(uint64_t skeleton, uint8_t* finished);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_frame)(uint64_t skeleton, float* frame, float* last_frame);
    ShipNativeStatus(SHIP_NATIVE_CALL* draw)(void* play_state, uint64_t skeleton);
    const char* layout_id;
    ShipNativeStatus(SHIP_NATIVE_CALL* create_ex)(const char* owner, const ShipOotSkeletonSpecV2* spec,
                                                  uint64_t* skeleton);
    ShipNativeStatus(SHIP_NATIVE_CALL* play_ex)(uint64_t skeleton, const char* animation_path,
                                                const ShipOotAnimationPlayV2* play);
    ShipNativeStatus(SHIP_NATIVE_CALL* animation_info)(uint64_t skeleton, const char* animation_path,
                                                       ShipOotAnimationInfoV2* info);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_state)(uint64_t skeleton, ShipOotAnimationStateV2* state);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_post_limb)(uint64_t skeleton, ShipOotSkeletonLimbFnV2 callback, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* draw_ex)(void* play_state, void* actor, uint64_t skeleton);
} ShipOotSkeletonsV2;

#endif
