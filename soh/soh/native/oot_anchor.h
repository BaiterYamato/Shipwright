#ifndef LINKSPAN_OOT_ANCHOR_H
#define LINKSPAN_OOT_ANCHOR_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_ANCHOR_SERVICE "linkspan.oot.anchor"
#define LINKSPAN_OOT_ANCHOR_VERSION 1u
/* Namespaces de save compartilhados ao mesmo tempo, somando todos os mods. */
#define LINKSPAN_OOT_ANCHOR_MAX_SHARED 32u

/* Anchor (multiplayer do SoH), na thread do jogo.
 *
 * O host já leva os itens sintéticos: um get-item de item do linkspan.oot.items vai aos parceiros pelo nome do
 * registro, e cada um entrega pelo receive do mod que registrou esse nome. Parceiro sem o item registrado ignora o
 * pacote e registra no log.
 *
 * connected: 1 com o Anchor conectado numa sala que sincroniza itens e flags; 0 fora disso.
 * share_namespace: o namespace do linkspan.oot.save (handle do open_namespace) entra no estado do time. Quem
 *   entra na sala recebe o conteúdo de quem gravou por último, e o host substitui o namespace local (versão do
 *   schema igual; versão diferente é recusada com aviso, sem mexer no local) e dispara oot.anchor.state.
 *   O estado do time também é montado a pedido de outro cliente, fora de um save: antes disso o host dispara
 *   oot.save.saving, e o mod grava o estado no namespace como faria num save.
 *   LIMIT com LINKSPAN_OOT_ANCHOR_MAX_SHARED compartilhados; compartilhar de novo não faz nada.
 * unshare_namespace: FAILURE se o namespace não estava compartilhado. */
typedef struct ShipOotAnchorV1 {
    uint32_t size;
    uint32_t(SHIP_NATIVE_CALL* connected)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* share_namespace)(uint64_t save_handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* unshare_namespace)(uint64_t save_handle);
} ShipOotAnchorV1;

#endif
