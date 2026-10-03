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
        if (play->activeCamera >= 0 && play->activeCamera < NUM_CAMS && play->cameraPtrs[play->activeCamera] != NULL) {
            camera = play->cameraPtrs[play->activeCamera]->setting;
        }
    }
    int escrito = snprintf(out, capacity,
                           "pipeline: visual=0x%X acao=%d segurando=%d item=%d arma=%d grupo=%d at=%d camera=%d bloq=%d fire=%u lanternHand=%u swing=%u",
                           ativos, acao, segurando, item, arma, grupo, at, camera, bloqueado,
                           gCustomItemState.lanternFireType, gCustomItemState.lanternEquipped,
                           gCustomItemState.lanternSwinging);
    if (escrito < 0 || (uint32_t)escrito >= capacity) {
        return 0;
    }
    return (uint32_t)escrito;
}
