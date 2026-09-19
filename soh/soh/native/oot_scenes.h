#ifndef LINKSPAN_OOT_SCENES_H
#define LINKSPAN_OOT_SCENES_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_SCENES_SERVICE "linkspan.oot.scenes"
#define LINKSPAN_OOT_SCENES_VERSION 1u
#define LINKSPAN_OOT_SCENES_AUTO (-1)
#define LINKSPAN_OOT_SCENES_FIRST_CUSTOM_ID 128
#define LINKSPAN_OOT_SCENES_MAX_ID 32767
#define LINKSPAN_OOT_SCENES_ENTRANCE_LAYERS 4
#define LINKSPAN_OOT_SCENES_MAX_ENTRANCE_INDEX 32764
#define LINKSPAN_OOT_SCENES_MAX_NAME 255u
#define LINKSPAN_OOT_SCENES_MAX_PATH 1024u
#define LINKSPAN_OOT_SCENES_MAX_SPAWN 127u

/* Cena fornecida por um mod. O host copia as strings; elas só precisam valer
 * durante a chamada. O nome é o id estável da cena ("mod/cena") e não pode ser
 * um nome de enum vanilla (SCENE_*). */
typedef struct ShipOotSceneDefinitionV1 {
    uint32_t size;
    const char* name;
    const char* display_name; /* opcional: NULL ou vazio usa o nome */
    const char* scene_path;   /* recurso da cena no VFS, no formato que o loader do host aceita */
    int32_t requested_id;     /* LINKSPAN_OOT_SCENES_AUTO ou 128..32767 */
    uint8_t draw_config;      /* índice de draw config (SDC_*) */
} ShipOotSceneDefinitionV1;

/* Entrada de uma cena registrada. Ocupa quatro posições consecutivas da tabela
 * de entradas (criança dia/noite, adulto dia/noite), iguais nesta versão. O nome
 * final é "<nome da cena>/<key>". */
typedef struct ShipOotEntranceDefinitionV1 {
    uint32_t size;
    const char* key;
    int32_t requested_index; /* LINKSPAN_OOT_SCENES_AUTO ou >= contagem vanilla, <= 32764 e múltiplo de 4 */
    uint8_t spawn;           /* índice na lista de spawns da cena, 0..127 */
    uint8_t continue_bgm;
    uint8_t show_title_card;
    uint8_t end_transition;
    uint8_t start_transition;
} ShipOotEntranceDefinitionV1;

/* Registro de cenas e entradas por nome, disponível apenas na thread do jogo.
 *
 * Ids automáticos seguem o maior já registrado: cenas a partir de 128 e entradas
 * a partir do primeiro grupo livre depois das vanilla. find_scene e find_entrance
 * aceitam nomes vanilla (SCENE_HYRULE_FIELD, ENTR_HYRULE_FIELD_0) e os nomes dos
 * mods. unregister_scene remove a cena e as entradas dela; quem registra deve
 * remover no shutdown. travel_to_entrance só age com o jogador em cena e sem
 * transição em andamento. */
typedef struct ShipOotScenesV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* register_scene)(const ShipOotSceneDefinitionV1* scene, uint64_t* scene_handle,
                                                       int32_t* scene_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_entrance)(uint64_t scene_handle,
                                                          const ShipOotEntranceDefinitionV1* entrance,
                                                          int32_t* entrance_index);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_scene)(uint64_t scene_handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_scene)(const char* name, int32_t* scene_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_entrance)(const char* name, int32_t* entrance_index);
    ShipNativeStatus(SHIP_NATIVE_CALL* travel_to_entrance)(int32_t entrance_index);
} ShipOotScenesV1;

#define LINKSPAN_OOT_SCENES_VERSION_2 2u

/* Definição V2: V1 com o texto do título. title_card_texture é uma textura I8
 * 144x24 (o formato dos títulos vanilla) no VFS, desenhada quando a entrada tem
 * show_title_card; NULL ou vazio = sem título. */
typedef struct ShipOotSceneDefinitionV2 {
    uint32_t size;
    const char* name;
    const char* display_name;
    const char* scene_path;
    int32_t requested_id;
    uint8_t draw_config;
    const char* title_card_texture;
} ShipOotSceneDefinitionV2;

typedef struct ShipOotSceneInfoV1 {
    uint32_t size;
    int32_t scene_id;
    uint8_t draw_config;
    uint8_t is_custom;
    /* Cena vanilla com pastas nonmq/mq (masmorras com Master Quest). */
    uint8_t has_master_quest;
    uint8_t overridden;
    /* SCENE_* ou nome do registro. */
    char name[LINKSPAN_OOT_SCENES_MAX_NAME + 1];
    /* Arquivo vanilla sem pasta ("spot00_scene"); vazio em cena de mod. */
    char file_name[LINKSPAN_OOT_SCENES_MAX_NAME + 1];
    /* Recurso que o jogo carrega para a variante pedida, já com override. */
    char scene_path[LINKSPAN_OOT_SCENES_MAX_PATH + 1];
} ShipOotSceneInfoV1;

/* Prefixo binário compatível com ShipOotScenesV1.
 *
 * register_scene_v2: register_scene com title_card_texture.
 * get_scene_count: ids vanilla vão de 0 a count-1.
 * get_scene_info: dados de uma cena vanilla ou registrada; `master_quest` escolhe
 *   a variante do caminho (ignorado sem has_master_quest).
 * override_scene: troca o recurso de uma cena vanilla (e da variante escolhida)
 *   por outro caminho do VFS, no formato que o loader aceita; NULL ou vazio volta
 *   ao vanilla. Vale a partir do próximo carregamento da cena. O override é de um
 *   dono só: quem define deve limpar no shutdown. */
typedef struct ShipOotScenesV2 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* register_scene)(const ShipOotSceneDefinitionV1* scene, uint64_t* scene_handle,
                                                       int32_t* scene_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_entrance)(uint64_t scene_handle,
                                                          const ShipOotEntranceDefinitionV1* entrance,
                                                          int32_t* entrance_index);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_scene)(uint64_t scene_handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_scene)(const char* name, int32_t* scene_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_entrance)(const char* name, int32_t* entrance_index);
    ShipNativeStatus(SHIP_NATIVE_CALL* travel_to_entrance)(int32_t entrance_index);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_scene_v2)(const ShipOotSceneDefinitionV2* scene,
                                                          uint64_t* scene_handle, int32_t* scene_id);
    int32_t(SHIP_NATIVE_CALL* get_scene_count)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_scene_info)(int32_t scene_id, uint8_t master_quest,
                                                       ShipOotSceneInfoV1* info);
    ShipNativeStatus(SHIP_NATIVE_CALL* override_scene)(int32_t scene_id, uint8_t master_quest,
                                                       const char* scene_path);
} ShipOotScenesV2;

#define LINKSPAN_OOT_SCENES_VERSION_3 3u

/* Definição V3: V2 com a capacidade genérica de cavalo da cena. Quando
 * horse_enabled é zero, todos os campos horse_* são ignorados. A posição
 * opcional é o ponto onde o jogo deixa o cavalo já existente depois de uma
 * transição; sem ela, a cena ainda pode chamar o cavalo pela canção. */
typedef struct ShipOotSceneDefinitionV3 {
    uint32_t size;
    const char* name;
    const char* display_name;
    const char* scene_path;
    int32_t requested_id;
    uint8_t draw_config;
    const char* title_card_texture;
    uint8_t horse_enabled;
    uint8_t horse_has_spawn;
    float horse_x;
    float horse_y;
    float horse_z;
    int16_t horse_angle;
} ShipOotSceneDefinitionV3;

/* Prefixo binário compatível com ShipOotScenesV2. */
typedef struct ShipOotScenesV3 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* register_scene)(const ShipOotSceneDefinitionV1* scene, uint64_t* scene_handle,
                                                       int32_t* scene_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_entrance)(uint64_t scene_handle,
                                                          const ShipOotEntranceDefinitionV1* entrance,
                                                          int32_t* entrance_index);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_scene)(uint64_t scene_handle);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_scene)(const char* name, int32_t* scene_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_entrance)(const char* name, int32_t* entrance_index);
    ShipNativeStatus(SHIP_NATIVE_CALL* travel_to_entrance)(int32_t entrance_index);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_scene_v2)(const ShipOotSceneDefinitionV2* scene,
                                                          uint64_t* scene_handle, int32_t* scene_id);
    int32_t(SHIP_NATIVE_CALL* get_scene_count)(void);
    ShipNativeStatus(SHIP_NATIVE_CALL* get_scene_info)(int32_t scene_id, uint8_t master_quest,
                                                       ShipOotSceneInfoV1* info);
    ShipNativeStatus(SHIP_NATIVE_CALL* override_scene)(int32_t scene_id, uint8_t master_quest,
                                                       const char* scene_path);
    ShipNativeStatus(SHIP_NATIVE_CALL* register_scene_v3)(const ShipOotSceneDefinitionV3* scene,
                                                          uint64_t* scene_handle, int32_t* scene_id);
} ShipOotScenesV3;

#endif
