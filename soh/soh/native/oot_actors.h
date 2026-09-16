#ifndef LINKSPAN_OOT_ACTORS_H
#define LINKSPAN_OOT_ACTORS_H

#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_ACTORS_SERVICE "linkspan.oot.actors"
#define LINKSPAN_OOT_ACTORS_VERSION 1u
#define LINKSPAN_OOT_ACTORS_MAX_NAME 128u
#define LINKSPAN_OOT_ACTORS_MAX_PATH 256u
#define LINKSPAN_OOT_ACTORS_MAX_TYPES 256u

/* `actor` é um Actor* (com instance_size bytes) e `play_state` um PlayState*. */
typedef void(SHIP_NATIVE_CALL* ShipOotActorFn)(void* user, void* actor, void* play_state);

typedef struct ShipOotActorTypeSpecV1 {
    uint32_t size;
    /* Nome namespaced ("meu.mod.ator"), único. */
    const char* name;
    uint8_t category;     /* ACTORCAT_* */
    uint32_t flags;       /* ACTOR_FLAG_* */
    int16_t object_id;    /* objeto exigido; OBJECT_GAMEPLAY_KEEP está sempre carregado */
    uint32_t instance_size; /* >= sizeof(Actor) */
    ShipOotActorFn init;
    ShipOotActorFn destroy;
    ShipOotActorFn update;
    ShipOotActorFn draw;
    void* user;
} ShipOotActorTypeSpecV1;

/* Tipos de ator fornecidos por mods, disponível apenas na thread do jogo.
 *
 * O host cria a entrada no ActorDB sem editar a tabela estática e devolve o id
 * para spawn_actor do serviço engine. Os callbacks passam por funções do host:
 * unregister_actor_type mata as instâncias vivas e, a partir daí, init/update/draw
 * não chegam mais ao mod e destroy é ignorado. Registrar de novo o mesmo nome
 * reutiliza o id. Quem registra deve remover no shutdown. */
typedef struct ShipOotActorsV1 {
    uint32_t size;
    ShipNativeStatus(SHIP_NATIVE_CALL* register_actor_type)(const ShipOotActorTypeSpecV1* spec, int16_t* actor_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* unregister_actor_type)(int16_t actor_id);
    ShipNativeStatus(SHIP_NATIVE_CALL* find_actor_type)(const char* name, int16_t* actor_id);
    /* Só dentro de um draw: desenha o display list `path` (resource manager, sem
     * __OTR__) com a matriz atual do ator, opaco (0) ou translúcido (1). */
    ShipNativeStatus(SHIP_NATIVE_CALL* draw_display_list)(void* play_state, const char* path, uint8_t translucent);
} ShipOotActorsV1;

#endif
