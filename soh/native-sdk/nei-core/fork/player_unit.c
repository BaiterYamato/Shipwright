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
    NeiFork_ToLogicalItem((gSaveContext.buttonStatus[0] == ITEM_NONE)                    ? ITEM_NONE      \
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
