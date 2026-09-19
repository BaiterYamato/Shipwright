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

#define LINKSPAN_OOT_ITEMS_VERSION_2 2u
#define LINKSPAN_OOT_ITEMS_MAX_MESSAGE 200u
#define LINKSPAN_OOT_ITEMS_LAYER_OPAQUE 0u
#define LINKSPAN_OOT_ITEMS_LAYER_TRANSLUCENT 1u

/* Item recebido: o Link terminou de levantar o item e a caixa de texto abriu. O
 * host não guarda o item em inventário nenhum; o callback decide (botão C, contador
 * no save do mod etc.). Na thread do jogo, dentro do update do Player. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* ShipOotItemReceiveFn)(void* user, uint8_t item);

typedef struct ShipOotGetItemSpecV1 {
    uint32_t size;
    /* Display list mostrado acima do Link (resource manager, sem __OTR__). */
    const char* model_path;
    uint8_t model_layer;
    /* Multiplica a escala do get-item do jogo (0.2); 1.0 serve para modelos GI vanilla. */
    float model_scale;
    /* Texto da caixa em ASCII, até LINKSPAN_OOT_ITEMS_MAX_MESSAGE bytes. Aceita os
     * códigos do CustomMessage do SoH (& quebra linha, %r/%g/%b/%w cores). */
    const char* message;
    ShipOotItemReceiveFn receive;
    void* user;
} ShipOotGetItemSpecV1;

/* Prefixo binário compatível com ShipOotItemsV1.
 *
 * set_get_item: modelo, mensagem e callback de recebimento do item (o host copia
 *   os textos). Chamar de novo substitui.
 * give_item: o Link levanta o item agora, como um item vindo de NPC. Exige
 *   gameplay normal (não vale na cena de abertura do título), item registrado com
 *   set_get_item e o Player livre (sem cutscene, escalada, queda, primeira pessoa
 *   ou ator carregado); senão LIMIT. O receive
 *   chega alguns frames depois, quando a caixa de texto abre. */
typedef struct ShipOotItemsV2 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* register_item)(const ShipOotItemSpecV1* spec, uint8_t* item);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_item)(uint8_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_item)(const char* name, uint8_t* item);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_button_item)(uint8_t button, uint8_t* item);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_button_item)(uint8_t button, uint8_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_get_item)(uint8_t item, const ShipOotGetItemSpecV1* spec);
    ShipNativeStatus(SHIP_NATIVE_CALL* give_item)(uint8_t item);
} ShipOotItemsV2;

#define LINKSPAN_OOT_ITEMS_VERSION_3 3u
/* Quantidade que o HUD desenha como a munição vanilla: dois dígitos. */
#define LINKSPAN_OOT_ITEMS_MAX_AMMO 99u
#define LINKSPAN_OOT_ITEMS_NO_AMMO 0xFFFFu

/* Idioma do jogo, nos valores de gSaveContext.language. */
#define LINKSPAN_OOT_LANGUAGE_ENGLISH 0u
#define LINKSPAN_OOT_LANGUAGE_GERMAN 1u
#define LINKSPAN_OOT_LANGUAGE_FRENCH 2u

/* Prefixo binário compatível com ShipOotItemsV2.
 *
 * set_item_icon: troca o ícone de um item registrado (upgrade, por exemplo). Os
 *   botões com o item passam a desenhar o novo ícone no próximo frame.
 * set_item_ammo: número desenhado no botão C, como a munição vanilla; 0 fica
 *   cinza e `full` (0 = nunca) fica verde. count LINKSPAN_OOT_ITEMS_NO_AMMO tira
 *   o número; acima de LINKSPAN_OOT_ITEMS_MAX_AMMO = INVALID_ARGUMENT. Quem
 *   registra controla o valor; o host não grava nem desconta nada.
 * get_language: idioma atual (LINKSPAN_OOT_LANGUAGE_*), para o mod escolher o
 *   texto de set_get_item. */
typedef struct ShipOotItemsV3 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* register_item)(const ShipOotItemSpecV1* spec, uint8_t* item);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_item)(uint8_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_item)(const char* name, uint8_t* item);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_button_item)(uint8_t button, uint8_t* item);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_button_item)(uint8_t button, uint8_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_get_item)(uint8_t item, const ShipOotGetItemSpecV1* spec);
    ShipNativeStatus(SHIP_NATIVE_CALL* give_item)(uint8_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_item_icon)(uint8_t item, const char* icon_path);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_item_ammo)(uint8_t item, uint16_t count, uint16_t full);
    uint8_t(SHIP_NATIVE_CALL* get_language)(void);
} ShipOotItemsV3;

#endif
