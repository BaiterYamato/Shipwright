/* Leitura do pipeline de Player do fork NEI (NEI-005).
 *
 * O aceite da etapa é "troca rápida entre itens não deixa câmera, collider, animação ou modelo
 * residual". Olhar o print não basta: um collider ligado ou uma câmera presa não aparecem na tela.
 * Esta unidade lê, no mesmo frame, os quatro lugares onde o resíduo apareceria:
 *
 *   - modelo/animação: o campo de visual do fork (CustomItems_BuildVisualSync) diz qual item NEI
 *     ainda se desenha, e o itemAction/modelGroup do Player dizem que ação de item está montada;
 *   - collider: o AT dos quads de arma do Link, que é o que fica ligado quando um item de combate
 *     sai sem limpar;
 *   - câmera: o setting da câmera ativa, que um item de primeira pessoa ou de mira troca;
 *   - prioridade: CustomItems_IsBlocked, o "item bloqueante" do dispatcher.
 *
 * Depois de soltar o item, tudo isso tem de voltar ao valor de repouso. O coremod publica a linha
 * no stats e o teste em jogo compara antes, durante e depois da troca. */
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "mods/items/custom_items.h"

#include <stdio.h>

static int32_t sProbeButton = -1, sProbeButtonItem = -1, sProbeButtons = 0;
static int32_t sProbeUseItem = -1, sProbeUseAction = -1, sProbeUses = 0;
Actor* NeiTest_Target(void);

static int32_t sProbeAttackCalls = 0, sProbeAttackUse = 0, sProbeAttackHits = 0, sProbeAttackUpperSword = 0;

// Chamados do z_player da DLL (extracted-fixes.txt): só o botão com item, não o ITEM_NONE de todo frame.
void NeiProbe_Button(s32 button, s32 item) {
    if (item < ITEM_NONE_FE) {
        sProbeButton = button;
        sProbeButtonItem = item;
        sProbeButtons++;
    }
}

void NeiProbe_Use(s32 item, s32 itemAction) {
    sProbeUseItem = item;
    sProbeUseAction = itemAction;
    sProbeUses++;
}

// Chamado do Player_ActionHandler_7 da DLL: quantas vezes o gatilho do golpe rodou, com o sUseHeldItem ligado e
// com o golpe aceito, e se a ação de cima era a do fork nesse momento.
s32 NeiProbe_Attack(s32 result, s32 useHeldItem, s32 upperIsSword) {
    sProbeAttackCalls++;
    if (useHeldItem) {
        sProbeAttackUse++;
    }
    if (result) {
        sProbeAttackHits++;
    }
    if (upperIsSword) {
        sProbeAttackUpperSword++;
    }
    return result;
}

uint32_t NeiPipeline_Describe(char* out, uint32_t capacity) {
    CustomItemVisualSync sync;
    uint32_t ativos = 0;
    int32_t acao = -1;
    int32_t segurando = -1;
    int32_t grupo = -1;
    int32_t at = -1;
    int32_t camera = -1;
    int32_t bloqueado = -1;
    int32_t item = -1;
    int32_t arma = -1;
    Player* player = NULL;
    PlayState* play = gPlayState;

    Actor* alvo = NeiTest_Target();
    int32_t distancia = -1;

    CustomItems_BuildVisualSync(&sync);
    ativos = sync.activeFlags;

    if (play != NULL) {
        player = GET_PLAYER(play);
    }
    if (player != NULL) {
        acao = player->itemAction;
        segurando = player->heldItemAction;
        grupo = player->modelGroup;
        item = player->heldItemId;
        arma = Player_GetMeleeWeaponHeld(player);
        at = 0;
        for (int32_t i = 0; i < (int32_t)ARRAY_COUNT(player->meleeWeaponQuads); ++i) {
            if (player->meleeWeaponQuads[i].base.atFlags & AT_ON) {
                at = 1;
            }
        }
        bloqueado = CustomItems_IsBlocked(player, play) ? 1 : 0;
        if (alvo != NULL) {
            distancia = (int32_t)Math_Vec3f_DistXZ(&alvo->world.pos, &player->actor.world.pos);
        }
        if (play->activeCamera >= 0 && play->activeCamera < NUM_CAMS && play->cameraPtrs[play->activeCamera] != NULL) {
            camera = play->cameraPtrs[play->activeCamera]->setting;
        }
    }
    int escrito = snprintf(out, capacity, "pipeline: visual=0x%X acao=%d segurando=%d item=%d arma=%d grupo=%d at=%d camera=%d bloq=%d "
                           "botao=%d:0x%X(%d) uso=0x%X->%d(%d) cesq=0x%X/%d ataque=%d/%d/%d/%d mws=%d magia=%d alvo=%d@%d",
                           ativos, acao, segurando, item, arma, grupo, at, camera, bloqueado, sProbeButton,
                           sProbeButtonItem, sProbeButtons, sProbeUseItem, sProbeUseAction, sProbeUses,
                           gSaveContext.equips.buttonItems[1], gSaveContext.buttonStatus[1], sProbeAttackCalls, sProbeAttackUse,
                           sProbeAttackHits, sProbeAttackUpperSword, player != NULL ? player->meleeWeaponState : -1,
                           gSaveContext.magic, alvo != NULL ? alvo->colChkInfo.health : -1, distancia);
    if (escrito < 0 || (uint32_t)escrito >= capacity) {
        return 0;
    }
    return (uint32_t)escrito;
}
