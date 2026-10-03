/*
 * File: z_player.c
 * Overlay: ovl_player_actor
 * Description: Link
 */

#include <libultraship/libultra.h>
#include "global.h"

#include "overlays/actors/ovl_Bg_Heavy_Block/z_bg_heavy_block.h"
#include "overlays/actors/ovl_Door_Shutter/z_door_shutter.h"
#include "overlays/actors/ovl_En_Boom/z_en_boom.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "overlays/actors/ovl_En_M_Thunder/z_en_m_thunder.h"
#include "overlays/actors/ovl_En_Box/z_en_box.h"
#include "overlays/actors/ovl_En_Door/z_en_door.h"
#include "overlays/actors/ovl_En_Elf/z_en_elf.h"
#include "overlays/actors/ovl_En_Fish/z_en_fish.h"
#include "overlays/actors/ovl_En_Horse/z_en_horse.h"
#include "overlays/effects/ovl_Effect_Ss_Fhg_Flash/z_eff_ss_fhg_flash.h"
#include "overlays/misc/ovl_kaleido_scope/z_kaleido_scope.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "objects/object_link_child/object_link_child.h"
#include <soh/Enhancements/custom-message/CustomMessageTypes.h>
#include "soh/Enhancements/item-tables/ItemTableTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/vanilla-behavior/PlayerAnimOverride.h"
#include "soh/Enhancements/randomizer/randomizer_entrance.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/randomizer_entrance.h"
#include "soh/Enhancements/randomizer/randomizer_grotto.h"
#include "soh/frame_interpolation.h"
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/Enhancements/savestate_serialize.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "backend_unit.h"

// Declared up here because the .c files pasted in below use them before their definitions further
// down this same translation unit.
BAD_RETURN(s32) Player_ZeroSpeedXZ(Player* this);
void Player_AnimPlayOnce(PlayState* play, Player* this, LinkAnimationHeader* anim);
void Player_AnimPlayLoop(PlayState* play, Player* this, LinkAnimationHeader* anim);
void Player_SetIntangibility(Player* this, s32 timer);
s32 func_80837B18(PlayState* play, Player* this, s32 damage);
s32 Player_PutAwayHeldItem(PlayState* play, Player* this);

// Fork subsystems this file still calls into directly. Every entry is a coupling left to move
// behind a hook in PlayerHooks.cpp. The .c files have no translation unit of their own — CMake
// globs only *.cpp under mods/ and expansions/ — so they are pasted in here.
#include "mods/items/custom_items.h"
#include "mods/extended_player.h"
#include "mods/extended_inventory.h"
#include "mods/items/custom_items.h"
#include "mods/extended_player.h"
#include "mods/extended_inventory.h"
#include "mods/extended_player.c"
#include "mods/items/logic/custom_items.c"
#include "mods/extended_equipment.h"
#include "mods/extended_equipment.c"
#include "mods/items/custom_bottles.h"
#include "motion_visuals.h"

int NeiVisual_SuppressRipple(void* state, const void* position) {
    PlayState* play = (PlayState*)state;
    return play != NULL && gSaveContext.gameMode == GAMEMODE_NORMAL &&
           NeiVisual_SuppressPlayerRipple(GET_PLAYER(play), (const Vec3f*)position);
}

void NeiLantern_RestoreFire(u8 fireType) {
    gCustomItemState.lanternFireType = fireType < LANTERN_FIRE_MAX ? fireType : LANTERN_FIRE_NONE;
    gCustomItemState.lanternEquipped = 0;
    gCustomItemState.lanternSwinging = 0;
    Bottle_WheelResetTracking();
    // A captura pendente também pertence ao arquivo anterior, não ao novo save.
    u8 pendingWheel, pendingItem;
    Bottle_ConsumeCatchSync(&pendingWheel, &pendingItem);
    ExtEquip_Init();
}

// EnPartner (spawn_boomerang_ivan do fork) e SW97_MEDALLIONS_ENABLED() do Player_UseItem do fork.
#include "overlays/actors/ovl_En_Partner/z_en_partner.h"
#include "expansions/sw97/sw97_config.h"

// Formas do MM (custom_forms.cpp, C++ fora da DLL; stub em nei_stubs.c): o Player_DrawImpl do fork passa o
// callback de membro das formas para o desenho do esqueleto.
s32 CustomForms_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg);

// O botão guarda o id runtime do linkspan.oot.items (o host desenha o ícone por ele); o z_player do fork usa o item
// do botão como id lógico dele: Player_UseItem(ITEM_ROD_FIRE) monta o PLAYER_IA_ROD_FIRE que golpeia com B. Sem a
// troca, o item do NEI chegava cru ao Player_UseItem e o Link só ganhava o visual do item (NEI-HOST-001). Só aqui: o
// custom_items.c e o extended_player.c de cima já traduzem com o patch 0002, e traduzir duas vezes erra, porque a
// faixa runtime (0xA0-0xEF) cruza os ids lógicos do fork.
#undef B_BTN_ITEM
#undef C_BTN_ITEM
#undef DPAD_ITEM
#define B_BTN_ITEM                                                                                  \
    NeiFork_BButtonItem((gSaveContext.buttonStatus[0] == ITEM_NONE)                    ? ITEM_NONE      \
                          : (gSaveContext.equips.buttonItems[0] == ITEM_SWORD_KNIFE) ? ITEM_SWORD_BGS \
                                                                                     : gSaveContext.equips.buttonItems[0])
#define C_BTN_ITEM(button)                                                                                  \
    NeiFork_ToLogicalItem((gSaveContext.buttonStatus[(button) + 1] != BTN_DISABLED)                        \
                              ? gSaveContext.equips.buttonItems[(button) + 1]                              \
                              : ITEM_NONE)
#define DPAD_ITEM(button)                                                                                   \
    NeiFork_ToLogicalItem((gSaveContext.buttonStatus[(button) + 5] != BTN_DISABLED)                        \
                              ? gSaveContext.equips.buttonItems[(button) + 4]                              \
                              : ITEM_NONE)

// Funções que o fork acrescentou ao z_player.c do host (fork/extract.txt) e as que ele mudou (overlay.py),
// extraídas pelo sync.py.
#include "extracted/unit/z_player.c"
#include "mods/equipment/kite_surf.c"

u8 FourSword_TotemGrabAllowed(void) {
    return !IS_RANDO || Flags_GetRandomizerInf(RAND_INF_CAN_GRAB);
}

// unregister_actor_type desliga os callbacks, inclusive destroy. Libere os
// colliders/rastros enquanto a DLL ainda está carregada e as instâncias estão vivas.
void NeiBackends_ClearRuntime(void) {
    KiteSurf_Abort(gPlayState ? GET_PLAYER(gPlayState) : NULL);
    for (u8 i = 0; i < FSC_MAX; ++i) {
        FourSwordClone* clone = FourSwordClone_Live(i);
        if (clone) {
            FourSwordClone_Destroy(&clone->actor, gPlayState);
            clone->colInit = 0;
            Actor_Kill(&clone->actor);
        }
    }
    FourSwordClone_KillAll();
    gFourSwordCloneDrawing = -1;
}

// Player_Update is kept in the host, so the per-frame item calls added at the
// end of the fork's Player_UpdateCommon are not present in that original.
// Restore their original order after the player's normal update.
void NeiItems_TickInput(void* actor, void* state) {
    Player* player = (Player*)actor;
    PlayState* play = (PlayState*)state;
    Slate_TickInput(play, player);
    Hourglass_TickInput(play, player);
    Seasons_TickInput(play, player);
    Wand_TickInput(play, player);
}

// The host's Player_Update leaves sControlInput pointing at its local Input.
// Our post-update hooks run after that stack frame has returned. Use the exact
// filtered frame captured by the mod's input detour for all donor callbacks.
static u32 sLastSurfGate;
void NeiItems_PostUpdate(void* actor, void* state, const void* input) {
    Input frame = *(const Input*)input;
    Input* previous = sControlInput;
    sControlInput = &frame;
    NeiItems_TickInput(actor, state);
    CustomItems_Update(actor, state);
    ExtEquip_Update();
    if (frame.press.button & BTN_R) {
        Player* player = (Player*)actor;
        sLastSurfGate = gExtEquipState.currentExtShield |
            ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 0x10 : 0) |
            (KiteSurf_Allowed(player) ? 0x20 : 0) |
            (Player_InBlockingCsMode((PlayState*)state, player) ? 0x40 : 0);
    }
    ExtEquip_UpdateBehavior((Player*)actor, (PlayState*)state);
    NeiBottles_Project(state);
    sControlInput = previous;
}
uint32_t NeiEquipment_Describe(char* out, uint32_t capacity) {
    int n = snprintf(out, capacity, "equipment: sword=%u shield=%u surf=%u lastR=0x%02X",
        gExtEquipState.currentExtSword, gExtEquipState.currentExtShield, sKSurf.state, sLastSurfGate);
    return n > 0 && (uint32_t)n < capacity ? (uint32_t)n : 0;
}
