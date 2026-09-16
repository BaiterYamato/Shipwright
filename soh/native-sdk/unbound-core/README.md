# Factory JSON do Unbound

Este projeto é um coremod externo de prova para o Shipwright 9.2.3 com o
overlay Link-Span. Ele não linka `soh.exe` e não contém código específico no
host.

O pacote `linkspan.unbound.framework` consulta `linkspan.oot.resources` v2,
registra o schema `linkspan.unbound.actor-patch/v1` e publica o serviço C
`linkspan.unbound.json_factory` v1. Um consumidor pode:

- descobrir schemas e tipos registrados;
- carregar todas as camadas JSON de um caminho virtual;
- receber um handle opaco para o resultado mesclado;
- consultar JSON, número de camadas e hash determinístico;
- liberar o handle explicitamente.

Nesta primeira versão, objetos são mesclados recursivamente e arrays ou valores
escalares são substituídos pela camada de maior prioridade. Todas as camadas
precisam declarar o mesmo `$schema`. As regras de `null`, `$replace`, `$order`
e remoção de campos pertencem ao próximo recorte do VFS mergeável.

## Registro de cenas (0.2.0)

Com o serviço `linkspan.oot.scenes` v1 do host, a função nativa `load_scene_registry`
lê `unbound/scenes.json` em todas as camadas montadas, mescla (objetos por chave,
camada de cima vence, `null` remove, `$schema` opcional) e registra cada cena e
entrada. O `main.lua` do framework chama a função no `game.ready`, quando os
archives dos mods já estão montados. Chamar de novo troca o registro anterior pelo
das camadas atuais, e o shutdown remove tudo.

O documento segue o §7 do SPEC do Unbound 0.6: a chave é o id da cena (não pode
ser um enum vanilla), `scene` é obrigatório, `sceneId` vai de 128 a 32767 e cada
entrada ocupa um grupo de quatro posições da tabela, endereçável como
`"<cena>/<entrada>"`. Entradas recusadas viram notas no resultado e as demais
seguem. `titleCardTexture` e `layers` ainda não são aplicados.

`scene-demo/` é um mod de prova: monta o próprio `assets/` na raiz do VFS com um
`unbound/scenes.json` que registra uma cópia do Hyrule Field e, com um save aberto,
viaja uma vez para `linkspan_demo/field_copy/main`.

## Compilar

```powershell
$sdk = (Resolve-Path ..\..\..\build\x64\native-sdk\Release\OotNativeSdk.cmake).Path
cmake -S . -B ..\..\..\build\unbound-factory-native -A x64 -DOOT_NATIVE_SDK="$sdk"
cmake --build ..\..\..\build\unbound-factory-native --config Release
ctest --test-dir ..\..\..\build\unbound-factory-native -C Release --output-on-failure
```

Na raiz da worktree do host, `tools/package-unbound-factory-demo.ps1` cria e
valida os dois mods, as duas camadas de prova e o bundle instalável.
`tools/package-unbound-scene-demo.ps1` empacota o framework 0.2.0 e a demo de
cenas com o layout id do host no nome (build em `build/unbound-scene-native`).
