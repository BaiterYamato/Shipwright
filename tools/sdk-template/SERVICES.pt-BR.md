[English](SERVICES.md) · **Português (Brasil)**

# Referência de serviços nativos OoT

Consulte serviços por `ShipNativeRuntime.get_service` com o macro de serviço exato, macro de versão e `sizeof` da tabela correspondente. Confira nulo, `size` da tabela e ponteiros de função antes de usar. Declaração de tabela/header não garante disponibilidade no runtime. A lista abaixo vem somente dos headers iniciais incluídos; o [contrato](OOT-NATIVE-CONTRACT.pt-BR.md) explica ABI/lifetimes. Toda tabela tem campo `size`. Campos de versão mais nova com prefixo compatível são adições à tabela anterior; randomizer v2 é explicitamente outra tabela.

## `linkspan.oot.actor-models`

Header: [include/oot-native/oot_actor_models.h](include/oot-native/oot_actor_models.h). Macro de serviço: `LINKSPAN_OOT_ACTOR_MODELS_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ACTOR_MODELS_VERSION` | `1u` | `ShipOotActorModelsV1` | `size`, `register_type`, `remove_owner`, `list_types` |

Perfis genéricos de modelo estático/NPC: esqueleto/animação ou display list, transformações, segmentos de textura, limbs ocultos, collider e configurações de fala/olhar. O host copia strings/listas; caminhos aceitam `__OTR__`, junta raiz é 1. Registre fora de gameplay e de callbacks de atores na thread do jogo. `remove_owner` destrói recursos das instâncias antes de matar atores; `list_types` inclui atores vanilla e dos mods ativos.

Structs de dados neste header:

- `ShipOotActorModelSegmentV1`: `segment`, `texture`.
- `ShipOotActorModelSpecV1`: `size`, `owner`, `name`, `display_name`, `skeleton`, `animation`, `display_list`, `hold_frame`, `translucent`, `frame`, `speed`, `scale`, `y_offset`, `shadow`, `cull_radius`, `draw_distance`, `segments`, `segment_count`, `hide_limbs`, `hide_limb_count`, `radius`, `height`, `y_shift`, `talks`, `message`, `talk_range`, `looks`, `look_limb`, `look_pivot`, `look_range`, `turn_axis`, `nod_axis`.

## `linkspan.oot.actors`

Header: [include/oot-native/oot_actors.h](include/oot-native/oot_actors.h). Macro de serviço: `LINKSPAN_OOT_ACTORS_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ACTORS_VERSION` | `1u` | `ShipOotActorsV1` | `size`, `register_actor_type`, `unregister_actor_type`, `find_actor_type`, `draw_display_list` |

Registra tipos nomeados de ator de mod no ActorDB e fornece IDs para `spawn_actor` do engine. No callback, `actor` é `Actor*` com `instance_size` bytes e `play_state` é `PlayState*`; `instance_size` deve ser pelo menos `sizeof(Actor)`. Remova no shutdown: instâncias vivas são mortas e callbacks do mod deixam de executar; destroy é ignorado após remover. Registrar o nome novamente reutiliza o ID. `draw_display_list` só vale no draw, usa a matriz atual do ator, caminho VFS sem `__OTR__` e seleção opaca/translúcida. Thread do jogo.

Structs de dados neste header:

- `ShipOotActorTypeSpecV1`: `size`, `name`, `category`, `flags`, `object_id`, `instance_size`, `init`, `destroy`, `update`, `draw`, `user`.

## `linkspan.oot.anchor`

Header: [include/oot-native/oot_anchor.h](include/oot-native/oot_anchor.h). Macro de serviço: `LINKSPAN_OOT_ANCHOR_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ANCHOR_VERSION` | `1u` | `ShipOotAnchorV1` | `size`, `connected`, `share_namespace`, `unshare_namespace` |

Compartilhamento multiplayer de namespaces na thread do jogo usando handles de save. `connected` exige sala com sincronização de itens/flags. O conteúdo mais recente do time substitui namespace local com schema correspondente e emite `oot.anchor.state`; schemas diferentes são recusados. O host emite `oot.save.saving` antes de montar estado de time solicitado fora de um save. Até 32 namespaces compartilhados entre todos os mods. Get-items sintéticos são sincronizados pelo nome registrado; parceiros sem esse registro ignoram/registram no log.

## `linkspan.oot.camera`

Header: [include/oot-native/oot_camera.h](include/oot-native/oot_camera.h). Macro de serviço: `LINKSPAN_OOT_CAMERA_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_CAMERA_VERSION` | `1u` | `ShipOotCameraV1` | `size`, `acquire`, `release`, `set_view`, `get_view`, `is_owned` |

Subcâmera de gameplay com dono único. `acquire` exige gameplay com câmera principal ativa; cutscene ou outro dono retornam `LIMIT`. A posse se perde na troca de cena ou quando o jogo ativa outra câmera; consulte `is_owned`. `set_view` persiste até a próxima chamada e é reaplicado por frame; `get_view` lê qualquer câmera ativa. Libere no shutdown; liberar token que perdeu posse retorna OK. Thread do jogo.

Structs de dados neste header:

- `ShipOotCameraViewV1`: `size`, `eye`, `at`, `fov`.

## `linkspan.oot.colliders`

Header: [include/oot-native/oot_colliders.h](include/oot-native/oot_colliders.h). Macro de serviço: `LINKSPAN_OOT_COLLIDERS_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_COLLIDERS_VERSION` | `1u` | `ShipOotCollidersV1` | `size`, `create_cylinder`, `destroy`, `submit`, `read_hits`, `knockback_player` |

Cilindros pertencentes ao host durante gameplay na thread do jogo. `submit` executa a cada update do ator dono para mover/registrar checagens AT/AC/OC. `read_hits` retorna e limpa acertos do frame anterior; ponteiros de ator atingido valem no frame. Destrua no destroy do ator; armazenamento é liberado no frame seguinte após checagens atuais. Trocas de cena/reset removem colliders da cena. Flags seguem `ColliderCylinderInit`; dano do knockback é em unidades de vida de 1/16 de coração.

Structs de dados neste header:

- `ShipOotCylinderSpecV1`: `size`, `actor`, `col_type`, `at_flags`, `ac_flags`, `oc1_flags`, `oc2_flags`, `elem_type`, `touch_dmg_flags`, `touch_effect`, `touch_damage`, `bump_dmg_flags`, `bump_effect`, `bump_defense`, `touch_flags`, `bump_flags`, `oc_elem_flags`, `radius`, `height`, `y_shift`.
- `ShipOotColliderHitsV1`: `size`, `at_hit`, `ac_hit`, `oc_hit`, `at_actor`, `ac_actor`, `oc_actor`, `ac_dmg_flags`.

## `linkspan.oot.engine`

Header: [include/oot-native/oot_engine.h](include/oot-native/oot_engine.h). Macro de serviço: `LINKSPAN_OOT_ENGINE_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ENGINE_VERSION` | `1u` | `ShipOotEngineV1` | `size`, `layout_id`, `play_state_size`, `player_size`, `save_context_size`, `get_play_state`, `get_player`, `get_save_context`, `spawn_actor`, `kill_actor` |

Acesso a `PlayState`, `Player` e `SaveContext`, além de spawn/kill de atores, somente na thread do jogo. Valide `layout_id` e os três tamanhos de structs antes dos casts. Ponteiros de gameplay não sobrevivem a frames ou trocas de cena; o provider responde pelas suas escritas.

## `linkspan.oot.movement`

Header: [include/oot-native/oot_engine.h](include/oot-native/oot_engine.h). Macro de serviço: `LINKSPAN_OOT_MOVEMENT_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_MOVEMENT_VERSION` | `1u` | `ShipOotMovementV1` | `size`, `layout_id`, `get_input_current`, `get_input_pressed`, `get_input_released`, `get_stick_x`, `get_stick_y`, `get_right_stick_x`, `get_right_stick_y`, `has_gamepad`, `get_gamepad_buttons`, `clear_gamepad_button_bindings`, `bind_gamepad_button`, `reload_gamepad_mappings`, `get_setting_int`, `set_setting_int`, `is_player_grounded`, `is_player_rolling`, `player_jump`, `player_roll` |
| `LINKSPAN_OOT_MOVEMENT_VERSION_2` | `2u` | `ShipOotMovementV2` | Prefixo anterior + `get_gamepad_axis`, `bind_gamepad_axis`, `player_use_item_shortcut`, `get_item_button_rect` |

V1 cobre input virtual, analógicos, botões SDL, bindings transitórios, configurações, estado no chão/rolando, salto e rolamento. V2 preserva o prefixo V1 e acrescenta eixos SDL brutos (-32768..32767), bindings de meio eixo (`direction` 1 ou -1), atalhos de item e retângulos de botão C do HUD no frame atual/anterior. Atalhos ainda aplicam condições do jogo; ações indisponíveis/sem mudança e retângulos de HUD oculto retornam `UNSUPPORTED`. Lente, máscaras, ocarinas, trajes e botas seguem as regras específicas do header. Thread do jogo; valide `layout_id`.

## `linkspan.oot.items`

Header: [include/oot-native/oot_items.h](include/oot-native/oot_items.h). Macro de serviço: `LINKSPAN_OOT_ITEMS_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_ITEMS_VERSION` | `1u` | `ShipOotItemsV1` | `size`, `register_item`, `unregister_item`, `find_item`, `get_button_item`, `set_button_item` |
| `LINKSPAN_OOT_ITEMS_VERSION_2` | `2u` | `ShipOotItemsV2` | Prefixo anterior + `set_get_item`, `give_item` |
| `LINKSPAN_OOT_ITEMS_VERSION_3` | `3u` | `ShipOotItemsV3` | Prefixo anterior + `set_item_icon`, `set_item_ammo`, `get_language` |

V1 registra IDs sintéticos 0xA0..0xEF com nomes, ícones VFS RGBA32 32×32 e callbacks de uso em botão C. O host grava itens equipados por nome em `linkspan.items`; remover esvazia seus botões C. V1 não fornece inventário/get-item. V2 acrescenta modelo/mensagem de get-item copiados e callback de recebimento posterior; condições indisponíveis do jogador retornam `LIMIT`. V3 acrescenta troca de ícone, munição visual controlada pelo chamador (até 99; `NO_AMMO` retira o número) e idioma do jogo. O host não grava nem desconta essa munição. As versões preservam os prefixos anteriores. Thread do jogo.

Structs de dados neste header:

- `ShipOotItemSpecV1`: `size`, `name`, `icon_path`, `age`, `use`, `user`.
- `ShipOotGetItemSpecV1`: `size`, `model_path`, `model_layer`, `model_scale`, `message`, `receive`, `user`.

## `linkspan.oot.lights`

Header: [include/oot-native/oot_lights.h](include/oot-native/oot_lights.h). Macro de serviço: `LINKSPAN_OOT_LIGHTS_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_LIGHTS_VERSION` | `1u` | `ShipOotLightsV1` | `size`, `create_point_light`, `update_point_light`, `destroy_point_light`, `get_point_light_info` |

Luzes pontuais na thread do jogo durante gameplay; até 8 luzes de mods, também usando o buffer compartilhado do jogo. Updates passam pelo hook vanilla de cor da luz. Trocas de cena/unload removem luzes; handles antigos retornam `UNSUPPORTED` no update. Operações não são reentrantes. `get_point_light_info` retorna identidade somente para comparação, não armazenamento editável. Raio zero apaga a luz; raio negativo é inválido.

Structs de dados neste header:

- `ShipOotPointLightV1`: `size`, `position`, `radius`, `color`, `glow`.

## `linkspan.oot.ocarina`

Header: [include/oot-native/oot_ocarina.h](include/oot-native/oot_ocarina.h). Macro de serviço: `LINKSPAN_OOT_OCARINA_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_OCARINA_VERSION` | `1u` | `ShipOotOcarinaV1` | `size`, `is_active`, `get_available_song_flags`, `get_song_count`, `get_song_pattern`, `submit_song` |

Suporte a instrumento na thread do jogo. Estado ativo e bits de músicas permitidas refletem o último update de input da ocarina. São 12 músicas fixas, ou 13 depois de gravar Espantalho. Padrões usam índices de nota do jogo 0..4 e aceitam consulta de contagem com nulo/capacidade zero; capacidade insuficiente retorna `LIMIT`. `submit_song` entrega música aceita no próximo update da ocarina, com efeito vanilla; a entrega é descartada se o instrumento fechar antes.

## `linkspan.oot.randomizer`

Header: [include/oot-native/oot_randomizer.h](include/oot-native/oot_randomizer.h). Macro de serviço: `LINKSPAN_OOT_RANDOMIZER_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_RANDOMIZER_VERSION` | `1u` | `ShipOotRandomizerV1` | `size`, `add_item`, `remove_item`, `seed_item_count`, `seed_item_name` |
| `LINKSPAN_OOT_RANDOMIZER_VERSION_2` | `2u` | `ShipOotRandomizerV2` | **Outra tabela:** `size`, `item_count`, `item_info`, `find_uncollected` |

V1 oferece itens sintéticos registrados com get-item às seeds geradas depois com Link-Span Mod Items habilitado. Cópias vão de 1..4, até 64 itens distintos; substituem junk sem peso de lógica. Também lê nomes de mods na seed carregada. Mods ausentes resultam em rupia azul e log do nome estável do item. **V2 é outra tabela, não extensão de V1:** metadados de item `RandomizerGet` e dicas de checks não coletadas na seed atual, somente leitura, com IDs do SDK em vez de IDs runtime de item. Não entrega itens nem altera checks. `UNSUPPORTED` fora de save randomizer, `FAILURE` sem resultado. Strings UTF-8 copiadas/terminadas em NUL. Ambas exigem a thread do jogo.

Structs de dados neste header:

- `ShipOotRandomizerItemSpecV1`: `size`, `item`, `copies`, `reserved`.
- `ShipOotRandomizerItemInfoV2`: `size`, `item`, `name`.
- `ShipOotRandomizerHintV2`: `size`, `item`, `check`, `item_name`, `area_name`, `description`.

## `linkspan.oot.registry`

Header: [include/oot-native/oot_registry.h](include/oot-native/oot_registry.h). Macro de serviço: `LINKSPAN_OOT_REGISTRY_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_REGISTRY_VERSION` | `1u` | `ShipOotRegistryV1` | `size`, `create_space`, `find_space`, `destroy_space`, `register_entry`, `unregister_entry`, `find_entry_by_name`, `find_entry_by_id`, `read_entry`, `list_entries` |
| `LINKSPAN_OOT_REGISTRY_VERSION_2` | `2u` | `ShipOotRegistryV2` | Prefixo anterior + `create_space_owned`, `register_entry_owned`, `read_entry_owner` |

Espaços e entradas nomeados genéricos, somente na thread do jogo. O host copia nomes/payloads. `read_entry` aceita buffers nulos/capacidades zero para consultar comprimentos; nomes não incluem NUL. Nomes enumerados valem somente durante o callback síncrono. O criador destrói seu espaço no shutdown. V2 preserva V1 e acrescenta donos explícitos por mod, limpeza em unload/init falho e leitura do dono; nomes/IDs duplicados são recusados.

## `linkspan.oot.render`

Header: [include/oot-native/oot_render.h](include/oot-native/oot_render.h). Macro de serviço: `LINKSPAN_OOT_RENDER_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_RENDER_VERSION` | `1u` | `ShipOotRenderV1` | `size`, `draw_display_list`, `matrix_push`, `matrix_pop`, `matrix_translate`, `matrix_scale`, `matrix_rotate_zyx` |
| `LINKSPAN_OOT_RENDER_VERSION_2` | `2u` | `ShipOotRenderV2` | Prefixo anterior + `acquire_state`, `release_state`, `set_state`, `emit_toon_key`, `emit_stencil`, `emit_toon_shadow`, `flush_toon_shadows`, `set_toon_ramp`, `set_toon_shadow_params` |
| `LINKSPAN_OOT_RENDER_VERSION_3` | `3u` | `ShipOotRenderV3` | Prefixo anterior + `set_actor_toon_enabled`, `matrix_translate_new`, `matrix_rotate_axis`, `export_current_matrix`, `draw_native_display_list`, `get_frame_info`, `interpolation_begin`, `interpolation_end` |

V1 desenha display lists VFS e transforma a matriz atual do ator/limb em escopo de draw permitido; caminhos omitem `__OTR__`. O host desfaz pushes de matriz sem pop. V2 preserva V1, acrescenta estado de features por mod, emissões toon/stencil/sombra e parâmetros globais de toon/sombra; estados saem no unload. Emissões exigem draw; `set_toon_ramp` e `set_toon_shadow_params` executam fora do draw na thread do jogo, com último escritor prevalecendo. V3 preserva V2 e acrescenta geometria nativa, dados de frame, rotação de eixo, exportação de matriz e filhos de interpolação. `set_actor_toon_enabled` só vale em `oot.render.actor_draw`; `matrix_translate_new` exige push de matriz do mod. Display lists nativas usam Gfx/Vtx do layout e permanecem válidas até o fim do frame. Adições de V3 exigem draw, exceto `get_frame_info`; chaves de irmãos de interpolação devem ser únicas e estáveis.

Structs de dados neste header:

- `ShipOotRenderFrameInfoV1`: `size`, `frame`, `camera_epoch`, `delta_seconds`, `aspect_ratio`.

## `linkspan.oot.resources`

Header: [include/oot-native/oot_resources.h](include/oot-native/oot_resources.h). Macro de serviço: `LINKSPAN_OOT_RESOURCES_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_RESOURCES_VERSION` | `1u` | `ShipOotResourcesV1` | `size`, `has_file`, `read_file`, `list_files`, `dirty_resources`, `unload_resource`, `mount_archive`, `unmount_archive`, `get_game_versions` |
| `LINKSPAN_OOT_RESOURCES_VERSION_2` | `2u` | `ShipOotResourcesV2` | Prefixo anterior + `read_file_layers` |
| `LINKSPAN_OOT_RESOURCES_VERSION_3` | `3u` | `ShipOotResourcesV3` | Prefixo anterior + `register_json_type`, `unregister_json_type` |

V1 fornece existência/leitura/listagem VFS, invalidação/unload de cache, mount/unmount de archives e enumeração de versões do jogo. Buffers de saída pertencem ao chamador; `read_file` e `get_game_versions` aceitam consultas de tamanho com nulo/capacidade zero. Desmonte archives próprios no shutdown. Caminhos enumerados valem somente no callback síncrono. V2 preserva V1 e acrescenta leitura de camadas estáveis da menor para a maior prioridade; o provider define o merge. V3 preserva V2 e registra tipo/faixa de versão de `$schema` JSON com callback de transcodificação JSON para XML do host; remova no shutdown e invalide recursos em cache quando necessário. Operações normais são da thread do jogo, mas o callback de transcodificação executa na thread/pool que carrega o recurso. Seu header permite `read_file_layers` nesse contexto: proteja o estado e não chame serviços exclusivos da thread do jogo no callback. Ponteiros/writer do callback valem durante a chamada.

Structs de dados neste header:

- `ShipOotResourceLayerV2`: `size`, `layer_index`, `layer_count`, `game_version`, `content_hash`, `data_size`, `archive_path_length`.
- `ShipOotJsonTypeSpecV1`: `size`, `type`, `min_version`, `max_version`, `transcode`, `user`.

## `linkspan.oot.save`

Header: [include/oot-native/oot_save.h](include/oot-native/oot_save.h). Macro de serviço: `LINKSPAN_OOT_SAVE_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_SAVE_VERSION` | `1u` | `ShipOotSaveV1` | `size`, `open_namespace`, `read`, `write`, `erase`, `get_stored_version`, `set_required`, `begin`, `commit`, `rollback`, `get_slot` |

Blocos JSON por namespace na seção `linkspan` do save, somente na thread do jogo. Trocar arquivo carregado substitui os dados; escritas chegam ao disco no próximo save do jogo. Namespaces não abertos são preservados. Nomes contêm ponto e caracteres permitidos; `linkspan.` é reservado ao host. Leitura aceita consulta de tamanho com nulo/capacidade zero. Reabrir com versão de schema diferente é inválido; use a versão gravada para migração. Transações gravam o estado anterior ao begin até commit. `get_slot` é 0..2 ou -1; JSON máximo de 1048576 bytes.

## `linkspan.oot.scenes`

Header: [include/oot-native/oot_scenes.h](include/oot-native/oot_scenes.h). Macro de serviço: `LINKSPAN_OOT_SCENES_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_SCENES_VERSION` | `1u` | `ShipOotScenesV1` | `size`, `register_scene`, `register_entrance`, `unregister_scene`, `find_scene`, `find_entrance`, `travel_to_entrance` |
| `LINKSPAN_OOT_SCENES_VERSION_2` | `2u` | `ShipOotScenesV2` | Prefixo anterior + `register_scene_v2`, `get_scene_count`, `get_scene_info`, `override_scene` |
| `LINKSPAN_OOT_SCENES_VERSION_3` | `3u` | `ShipOotScenesV3` | Prefixo anterior + `register_scene_v3` |

Registro de cenas/entradas por nome e viagem na thread do jogo. O host copia nomes/caminhos. IDs de cena customizada começam em 128; entradas ocupam quatro posições consecutivas e usam nomes cena/key. Viagem exige jogador em cena sem transição ativa. Remova cenas no shutdown. V2 preserva V1, acrescenta textura de título I8 144×24, consulta de cenas e overrides de recurso vanilla com dono único efetivos na próxima carga; limpe seus overrides no shutdown. V3 preserva V2, acrescenta `register_scene_v3` e campos de habilitação/spawn do cavalo; com `horse_enabled` zero, os campos de cavalo são ignorados.

Structs de dados neste header:

- `ShipOotSceneDefinitionV1`: `size`, `name`, `display_name`, `scene_path`, `requested_id`, `draw_config`.
- `ShipOotEntranceDefinitionV1`: `size`, `key`, `requested_index`, `spawn`, `continue_bgm`, `show_title_card`, `end_transition`, `start_transition`.
- `ShipOotSceneDefinitionV2`: `size`, `name`, `display_name`, `scene_path`, `requested_id`, `draw_config`, `title_card_texture`.
- `ShipOotSceneInfoV1`: `size`, `scene_id`, `draw_config`, `is_custom`, `has_master_quest`, `overridden`, `name`, `file_name`, `scene_path`.
- `ShipOotSceneDefinitionV3`: `size`, `name`, `display_name`, `scene_path`, `requested_id`, `draw_config`, `title_card_texture`, `horse_enabled`, `horse_has_spawn`, `horse_x`, `horse_y`, `horse_z`, `horse_angle`.

## `linkspan.oot.skeletons`

Header: [include/oot-native/oot_skeletons.h](include/oot-native/oot_skeletons.h). Macro de serviço: `LINKSPAN_OOT_SKELETONS_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_SKELETONS_VERSION` | `1u` | `ShipOotSkeletonsV1` | `size`, `create`, `destroy`, `play_animation`, `update`, `get_frame`, `draw` |

Esqueletos SkelAnime normal/flex na thread do jogo a partir de caminhos VFS sem `__OTR__`. Recurso de curva/ausente falha; o provider deve fornecer juntas de animação correspondentes. Atualize uma vez por update do ator dono; animações ONCE param no último frame. Draw é opaco e exige escopo de draw de render. Esqueletos sobrevivem a cenas, mas guardam poses de recursos do jogo; destrua no destroy do ator ou shutdown. Reset do host libera o restante.

## `linkspan.oot.text`

Header: [include/oot-native/oot_text.h](include/oot-native/oot_text.h). Macro de serviço: `LINKSPAN_OOT_TEXT_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_TEXT_VERSION` | `1u` | `ShipOotTextV1` | `size`, `set_message`, `remove_message`, `clear_language`, `reset_language`, `list_messages` |

Edite tabelas de mensagem do jogo na thread do jogo fora da exibição de caixa de texto, entre frames. Mensagens são bytes/códigos de controle crus do jogo, com 0x02 acrescentado se faltar. IDs até 0xFFFE; tipo/posição da caixa de 0..15; mensagem máxima com terminador de 8192 bytes. Tabelas de inglês, alemão, francês, japonês e staff. `reset_language` restaura recursos/overrides do boot; bytes do callback de listagem valem durante a chamada.

## `linkspan.oot.world`

Header: [include/oot-native/oot_world.h](include/oot-native/oot_world.h). Macro de serviço: `LINKSPAN_OOT_WORLD_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_OOT_WORLD_VERSION` | `1u` | `ShipOotWorldV1` | `size`, `raycast_floor`, `line_test`, `wall_check`, `water_surface` |

Consultas somente leitura à colisão da cena atual na thread do jogo durante gameplay; caso contrário `UNSUPPORTED`. Raycast de chão sem chão retorna OK e hit=0. Line tests filtram parede/chão/teto; wall-check corrige o destino de esfera em movimento. `water_surface` retorna `FAILURE` fora da água. Normais de hit são unitárias; bg_id identifica cena ou DynaPoly e floor_type segue o tipo de superfície do jogo.

Structs de dados neste header:

- `ShipOotWorldHitV1`: `size`, `hit`, `pos`, `normal`, `bg_id`, `floor_type`.

## `linkspan.nei.items`

Header: [include/linkspan/nei/nei_items.h](include/linkspan/nei/nei_items.h). Macro de serviço: `LINKSPAN_NEI_ITEMS_SERVICE`.

| Macro de versão | Valor | Tabela | Campos/funções |
| --- | --- | --- | --- |
| `LINKSPAN_NEI_ITEMS_VERSION` | `1u` | `NeiItemsV1` | `size`, `define_item`, `remove_item`, `find_item`, `list_items`, `get_state`, `give_item`, `grant_item`, `revoke_item`, `set_count`, `add_count`, `set_level`, `equip`, `unequip`, `get_name` |

Fornecido pelo coremod `linkspan.nei`, não pela inclusão do header. Somente thread do jogo. Definições têm IDs namespaced estáveis; IDs de jogo runtime podem mudar entre sessões. Posse/quantidade/nível persistem por ID estável no namespace de save `linkspan.nei`, mesmo sem definição presente. `define_item` copia a definição e restaura o estado gravado; remova suas definições no shutdown sem apagar estado gravado. Até 4 níveis, quantidades até 99, nomes/get-messages em inglês obrigatórios com fallback de alemão/francês. `give_item` aplica posse/quantidade quando a caixa de texto abre, depois chama received; `grant_item` pula animação. Callbacks de uso não consomem quantidade automaticamente. `add_count` recusa resultado negativo com `LIMIT`; troca de nível limita a quantidade. Strings de ID enumeradas valem durante o callback; `get_name` aceita consulta de tamanho com nulo/capacidade zero, sem terminador.

Structs de dados neste header:

- `NeiItemLevelV1`: `size`, `icon_path`, `names`, `max_count`.
- `NeiItemDefinitionV1`: `size`, `id`, `age`, `model_path`, `model_layer`, `model_scale`, `get_messages`, `level_count`, `levels`, `give_count`, `use`, `received`, `user`.
- `NeiItemStateV1`: `size`, `owned`, `level`, `count`, `max_count`, `runtime_id`, `button`.

## Escopo de hooks e Unbound

[oot_hooks.h](include/oot-native/oot_hooks.h) define pontos de hook nomeados/payloads usados pelas funções de hook do runtime; não declara tabela `LINKSPAN_OOT_*_SERVICE`. Consulte o [contrato nativo](OOT-NATIVE-CONTRACT.pt-BR.md).

O script atual parte de todos os `oot_*.h`, ABI genérica, headers z64 do host, layout gerado e NEI. O percurso de dependências conferido não inclui header de serviço próprio do Unbound. Portanto, esta referência do SDK não inventa nome, versão ou tabela de serviço Unbound. `oot_resources.h` declara a ponte de transcodificação JSON e usa `unbound/scene` somente como exemplo de tipo de schema. Para exemplos Unbound e contratos próprios do framework, use [link-span-examples](https://github.com/BaiterYamato/link-span-examples/tree/main/oot-native/unbound).
