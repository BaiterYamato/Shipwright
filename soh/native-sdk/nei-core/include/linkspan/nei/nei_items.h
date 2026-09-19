#ifndef LINKSPAN_NEI_ITEMS_H
#define LINKSPAN_NEI_ITEMS_H

#include <shiplua/native/ship_native_abi.h>

/* Registro de itens do Not Enough Items no Link-Span (NEI-002), publicado pelo coremod
 * linkspan.nei sobre linkspan.oot.items v3 e linkspan.oot.save.
 *
 * Um item é identificado pelo id namespaced ("autor.mod.item"); o id interno do jogo é
 * alocado em runtime e pode mudar entre sessões. O coremod guarda posse, quantidade e nível
 * de cada item no bloco de save "linkspan.nei", pelo id; um item que nenhum mod definiu nesta
 * sessão mantém o estado gravado. Tudo só na thread do jogo. */

#define LINKSPAN_NEI_ITEMS_SERVICE "linkspan.nei.items"
#define LINKSPAN_NEI_ITEMS_VERSION 1u
#define LINKSPAN_NEI_MAX_ID 128u
#define LINKSPAN_NEI_MAX_LEVELS 4u
#define LINKSPAN_NEI_MAX_NAME 64u
#define LINKSPAN_NEI_MAX_COUNT 99u
/* Idiomas de nomes e mensagens, nos valores de LINKSPAN_OOT_LANGUAGE_*. */
#define LINKSPAN_NEI_LANGUAGES 3u
#define LINKSPAN_NEI_NO_BUTTON 0u

/* Botão C apertado com o item. O item está possuído e, se tem quantidade, o contador é > 0.
 * O NEI não desconta nada: o mod chama add_count quando o uso consome. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* NeiItemUseFn)(void* user, uint64_t item, uint8_t button);
/* O Link terminou de receber o item por give_item; posse e quantidade já foram aplicadas. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* NeiItemReceivedFn)(void* user, uint64_t item);

typedef struct NeiItemLevelV1 {
    uint32_t size;
    /* Textura RGBA32 32x32 no VFS, sem __OTR__. */
    const char* icon_path;
    /* Nome por idioma (inglês, alemão, francês), ASCII, até LINKSPAN_NEI_MAX_NAME bytes.
     * NULL ou vazio usa o inglês, que é obrigatório. */
    const char* names[LINKSPAN_NEI_LANGUAGES];
    /* 0 = item sem quantidade; senão 1..LINKSPAN_NEI_MAX_COUNT, desenhado no botão C. */
    uint16_t max_count;
} NeiItemLevelV1;

typedef struct NeiItemDefinitionV1 {
    uint32_t size;
    /* "autor.mod.item": [A-Za-z0-9._-], com ponto, até LINKSPAN_NEI_MAX_ID bytes. */
    const char* id;
    /* LINKSPAN_OOT_ITEM_AGE_*. */
    uint8_t age;
    /* Modelo do get-item: display list do VFS, camada LINKSPAN_OOT_ITEMS_LAYER_* e escala. */
    const char* model_path;
    uint8_t model_layer;
    float model_scale;
    /* Texto da caixa do get-item por idioma, ASCII com os códigos do CustomMessage do SoH, até
     * LINKSPAN_OOT_ITEMS_MAX_MESSAGE bytes. NULL ou vazio usa o inglês, que é obrigatório. */
    const char* get_messages[LINKSPAN_NEI_LANGUAGES];
    /* Níveis de upgrade, 1..LINKSPAN_NEI_MAX_LEVELS; o nível 0 é o inicial. */
    uint32_t level_count;
    const NeiItemLevelV1* levels;
    /* Quantidade somada ao contador por give_item/grant_item (limitada ao máximo do nível). */
    uint16_t give_count;
    NeiItemUseFn use;
    NeiItemReceivedFn received;
    void* user;
} NeiItemDefinitionV1;

typedef struct NeiItemStateV1 {
    uint32_t size;
    uint8_t owned;
    uint8_t level;
    uint16_t count;
    uint16_t max_count;
    /* Id do jogo (linkspan.oot.items); muda entre sessões. */
    uint8_t runtime_id;
    /* Botão C com o item (LINKSPAN_OOT_ITEMS_BUTTON_*), ou LINKSPAN_NEI_NO_BUTTON. */
    uint8_t button;
} NeiItemStateV1;

/* Enumeração em ordem de id; `id` pertence ao NEI e só vale durante a chamada. Outro status
 * que não OK interrompe e é devolvido. */
typedef ShipNativeStatus(SHIP_NATIVE_CALL* NeiItemVisitFn)(void* user, uint64_t item, const char* id);

/* define_item: copia a definição. Um id já definido = INVALID_ARGUMENT. Sem vaga de id no jogo
 *   = LIMIT. O estado gravado com esse id volta na hora.
 * remove_item: tira o item do jogo e dos botões; o estado gravado fica. Quem define remove no
 *   shutdown.
 * give_item: o Link recebe o item com a animação e a caixa de texto do idioma atual; posse e
 *   give_count entram quando a caixa abre, e então `received` é chamado. LIMIT se o Player não
 *   pode receber agora.
 * grant_item / revoke_item: posse sem animação / tira posse, zera o contador e desequipa.
 * set_count / add_count: contador limitado a 0..máximo do nível. add_count devolve LIMIT sem
 *   mudar nada se o resultado ficaria negativo (uso sem munição).
 * set_level: troca ícone, nomes e máximo; o contador é cortado ao novo máximo.
 * equip: põe um item possuído no botão C (tira de outro botão C se estiver lá). unequip tira
 *   de todos os botões.
 * get_name: nome do nível atual no idioma pedido (LINKSPAN_OOT_LANGUAGE_*), com a regra de
 *   capacity 0 / output NULL para consultar o tamanho (sem o terminador). */
typedef struct NeiItemsV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* define_item)(const NeiItemDefinitionV1* definition, uint64_t* item);
    ShipNativeStatus(SHIP_NATIVE_CALL* remove_item)(uint64_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_item)(const char* id, uint64_t* item);
    ShipNativeStatus(SHIP_NATIVE_CALL* list_items)(NeiItemVisitFn visit, void* user);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_state)(uint64_t item, NeiItemStateV1* state);
    ShipNativeStatus(SHIP_NATIVE_CALL* give_item)(uint64_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* grant_item)(uint64_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* revoke_item)(uint64_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_count)(uint64_t item, uint16_t count);
    ShipNativeStatus(SHIP_NATIVE_CALL* add_count)(uint64_t item, int32_t delta, uint16_t* result);
    ShipNativeStatus(SHIP_NATIVE_CALL* set_level)(uint64_t item, uint8_t level);
    ShipNativeStatus(SHIP_NATIVE_CALL* equip)(uint64_t item, uint8_t button);
    ShipNativeStatus(SHIP_NATIVE_CALL* unequip)(uint64_t item);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_name)(uint64_t item, uint8_t language, char* output, uint32_t capacity,
                                                 uint32_t* output_size);
} NeiItemsV1;

#endif
