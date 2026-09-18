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

#endif
