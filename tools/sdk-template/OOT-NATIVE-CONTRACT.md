# Contrato nativo OoT — recorte da release

Fonte de verdade: `include/shiplua/native/ship_native_abi.h`,
`include/oot-native/oot_*.h` e `include/oot_layout_id.h` deste bundle.
Metadados de configuração e host em `linkspan-sdk.json`. Não existe ABI C++ estável
entre o executável e o provider; a comunicação ocorre por tabelas C versionadas.

- Export: `extern "C" SHIP_NATIVE_EXPORT ... SHIP_NATIVE_CALL ShipNative_Query()`;
  descriptor informa size, ABI major/minor, init e shutdown. ABI disponível até 1.3.
- Init valida runtime e consulta `get_service(context, nome, versão, sizeof)`;
  serviço ausente ou incompatível é `UNSUPPORTED`, sem dereference nulo.
- `linkspan.oot.engine` v1 expõe layout e sizeof de PlayState, Player, SaveContext;
  `movement` v1/v2 tem contrato separado. Compare todos os tamanhos que converter.
- `oot_*.h` cobrem registry, actors/models, colliders, hooks, items, resources,
  scenes, save, randomizer, anchor, render, view/world, lights, text e ocarina.
  Cada header define versões, structs e regras próprias; não invente suporte.
- Ponteiros de gameplay só valem na thread do jogo e no lifetime documentado;
  payload de hook só durante callback. Não chamar tabelas depois do shutdown.
- Core extension: `kind="core_extension"`, `load_phase="pre_game"`; publicação
  de serviços é ABI 1.1. Consumidores saem antes do framework.
- Hooks: ABI 1.2, observe/transform/replace, payload size e versão corretos;
  handles são removidos pelo loader antes de shutdown. Undo das mutações e CVars
  do próprio mod continua sendo responsabilidade do autor.
- Escape hatch: ABI 1.3 em core extension, `host_fingerprints` SHA-256 do exe,
  `soh.symbols` correspondente. Patches de init ativam juntos após o retorno;
  não depender deles no init nem criar patches no shutdown. Falha de remoção
  pode exigir quarentena para não descarregar código ainda usado.
- Compilação: MSVC x64 C++20, Release `/MT`, `/utf-8`, `/Zc:preprocessor`,
  defines exportados do host. `F3DEX_GBI_2`, `NOMINMAX`, `INCLUDE_GAME_PRINTF`
  e demais definições estão no config. Sem lib de importação `soh`, `shiplua`,
  Lua ou libultraship. Uma nova função interna do mod não requer rebuild do host.
- Layout hash usa headers/compilador/plataforma/flags. Fingerprint identifica
  bytes do exe; são proteções diferentes. Novo host pode exigir reempacote
  mesmo sem mudar layout. DLL nativa executa com acesso do processo, sem sandbox.

Referências de origem: `soh/native-sdk/ExportSdk.cmake:1-31`,
`soh/native-sdk/README.md:3-5,32-45,66-83`,
`soh/soh/native/oot_engine.h:16-32`,
`extern/ship-lua/docs/native-providers.md:42-137`.
