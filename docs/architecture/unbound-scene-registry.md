# Unbound: registro de cenas e entradas

## Objetivo

O recorte `OOT-UNBOUND-003C` tira do executável o limite das tabelas compiladas `gSceneTable` e `gEntranceTable`.
Mods registram cenas e entradas por nome em tempo de execução, sem recompilar o host, e o framework Unbound lê o
`unbound/scenes.json` mesclado para fazer esse registro.

## Capacidades

| Estrutura | Antes | Agora |
| --- | ---: | ---: |
| `EntranceInfo.scene` | `s8` (127) | `s16` (32.767) |
| `EntranceInfo` | 4 bytes | 6 bytes |
| Cenas | 110 fixas | vanilla + ids 128 a 32.767 registrados por mods |
| Entradas | 1.556 fixas | vanilla + grupos de 4 até o índice 32.764 |
| `gEntranceTable` | array | ponteiro para a tabela vigente |
| Flags salvas por cena | `gSaveContext.sceneFlags[124]` | array vanilla para ids até 123; registro por nome para cenas de mod |

## Divisão

- `soh/soh/native/OotNativeScenes.cpp` é o registro puro: ids, índices, nomes, tabela combinada e flags. Não toca em
  globais do jogo e roda em `oot_native_scenes_tests` sem ROM.
- `soh/soh/native/OotNativeScenesGame.cpp` só existe no `soh`: entrega as tabelas vanilla (nomes dos enums vindos das
  X-macros), aponta `gEntranceTable`, expõe `LinkSpan_SceneFlags`, `LinkSpan_EntranceCount` e
  `LinkSpan_ReportInvalidEntrance` ao código C e implementa a viagem.
- `soh/native-sdk/unbound-core` (`linkspan.unbound.framework`) lê e mescla `unbound/scenes.json` e chama o serviço.
  Nenhuma regra do formato Unbound fica no executável.

## Tabela de entradas

- Enquanto nenhum mod registra entrada, `gEntranceTable` aponta para `gEntranceTableVanilla`.
- A primeira entrada de mod cria a tabela combinada: cópia das vanilla, posições sem uso marcadas com
  `SCENE_ID_MAX` (como `SCENE_UNUSED_6E`) e os grupos dos mods. O ouvinte do binding troca o ponteiro a cada mudança.
- A tabela não encolhe quando uma cena sai. As posições voltam a `SCENE_ID_MAX` e os índices já lidos pelo jogo
  continuam dentro dela.
- O reset do host (init e shutdown) devolve o ponteiro à tabela vanilla antes de liberar a combinada.

## Guardas no jogo

- `Play_Init`: entrada fora da tabela (save de outra pilha de mods) vira `ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN`;
  camada fora da tabela usa a camada 0. Os dois casos geram uma linha de log.
- `OTRPlay_SpawnScene`: id acima das vanilla sem registro cai no fallback do upstream (Dodongo's Cavern). Cena de mod
  usa um `SceneTableEntry` local com o draw config registrado e o caminho do registro.
- `Actor_InitContext`, `Play_SaveSceneFlags`, `GameInteractor::RawAction::SetSceneFlag/UnsetSceneFlag` e o editor de
  save leem flags por `LinkSpan_SceneFlags`.
- `SohUtils::GetSceneName` devolve o nome de exibição da cena de mod; o crash handler escreve `mod scene`.

## Limites conhecidos

- Flags de cena de mod vivem só na sessão, guardadas pelo nome; ainda não entram no save.
- `titleCardTexture` e `layers` do SPEC não são aplicados. As quatro camadas de uma entrada são iguais.
- Mapa, minimapa, seleção MQ, warp de debug e randomizer tratam ids de mod como cena sem dados.
- Código que indexa arrays próprios por `play->sceneNum` fora dos pontos acima não foi auditado por inteiro.

## Verificação incorporada

- `UnboundLayoutChecks.cpp` confere que `EntranceInfo::scene` representa 32.767.
- `oot_native_scenes_tests`: ids automáticos e explícitos, recusas (nome vanilla, repetido, fora do intervalo, draw
  config, tamanho de definição, thread), grupos de entrada e empacotamento de `field`, preservação das vanilla na
  tabela combinada, busca por nome, viagem, remoção sem encolher, flags pelo nome e reset.
- `oot_native_engine_tests` confere o serviço publicado pela policy do host.
- `linkspan_unbound_json_tests` cobre o merge sem `$schema` (com `null` removendo chave) e a leitura do registro.
