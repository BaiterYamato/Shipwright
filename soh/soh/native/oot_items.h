#ifndef LINKSPAN_OOT_ITEMS_H
#define LINKSPAN_OOT_ITEMS_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_ITEMS_SERVICE "linkspan.oot.items"
#define LINKSPAN_OOT_ITEMS_VERSION 1u
/* Faixa de ids de item sintético. O HUD só desenha botões com id < 0xF0. */
#define LINKSPAN_OOT_ITEMS_FIRST_ID 0xA0u
#define LINKSPAN_OOT_ITEMS_LAST_ID 0xEFu
#define LINKSPAN_OOT_ITEMS_MAX_NAME 128u
#define LINKSPAN_OOT_ITEMS_MAX_PATH 256u

/* Idade exigida para usar o item, nos valores de gItemAgeReqs. */
#define LINKSPAN_OOT_ITEM_AGE_ADULT 0u
#define LINKSPAN_OOT_ITEM_AGE_CHILD 1u
#define LINKSPAN_OOT_ITEM_AGE_ANY 9u

/* Botões aceitos por get/set_button_item: índices de SaveContext.equips.buttonItems. */
#define LINKSPAN_OOT_ITEMS_BUTTON_C_LEFT 1u
#define LINKSPAN_OOT_ITEMS_BUTTON_C_DOWN 2u
#define LINKSPAN_OOT_ITEMS_BUTTON_C_RIGHT 3u

/* Botão com o item apertado, na thread do jogo, dentro do update do Player. O
 * jogo já conferiu estado do Player, cena e botão habilitado; o host conferiu a
 * idade. Não há troca de item na mão: o callback decide o que acontece. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotItemUseFn)(void* user, uint8_t item, uint8_t button);

typedef struct ShipOotItemSpecV1 {
    uint32_t size;
    /* Nome namespaced ("meu.mod.item"), único; é o que vai para o save. */
    const char* name;
    /* Textura RGBA32 32x32 no resource manager, sem prefixo __OTR__. */
    const char* icon_path;
    uint8_t age;
    ShipOotItemUseFn use;
    void* user;
} ShipOotItemSpecV1;

/* Itens sintéticos, disponível apenas na thread do jogo.
 *
 * Um item registrado recebe um id livre da faixa sintética e pode ocupar botões
 * C. O host desenha o ícone, bloqueia a ação vanilla do Player e chama `use`.
 * Os botões com itens sintéticos são gravados pelo nome no bloco do host
 * "linkspan.items" e restaurados na carga; nome sem registro esvazia o botão.
 * Quem registra deve remover no shutdown: unregister_item esvazia os botões com
 * o item. Página de inventário e get-item ficam fora desta versão. */
typedef struct ShipOotItemsV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* register_item)(const ShipOotItemSpecV1* spec, uint8_t* item);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_item)(uint8_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_item)(const char* name, uint8_t* item);
    /* Item de qualquer origem (vanilla ou sintético) no botão; 0xFF = vazio. */
    ShipNativeStatus(SHIP_NATIVE_CALL* get_button_item)(uint8_t button, uint8_t* item);
    /* Coloca um item sintético registrado no botão, ou 0xFF para esvaziar. */
    ShipNativeStatus(SHIP_NATIVE_CALL* set_button_item)(uint8_t button, uint8_t item);
} ShipOotItemsV1;

#endif
