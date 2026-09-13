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

## Compilar

```powershell
$sdk = (Resolve-Path ..\..\..\build\x64\native-sdk\Release\OotNativeSdk.cmake).Path
cmake -S . -B ..\..\..\build\unbound-factory-native -A x64 -DOOT_NATIVE_SDK="$sdk"
cmake --build ..\..\..\build\unbound-factory-native --config Release
ctest --test-dir ..\..\..\build\unbound-factory-native -C Release --output-on-failure
```

Na raiz da worktree do host, `tools/package-unbound-factory-demo.ps1` cria e
valida os dois mods, as duas camadas de prova e o bundle instalável.
