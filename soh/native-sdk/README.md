# SDK nativo OoT

Este diretório contém mods nativos de exemplo para o Shipwright OoT. Um mod
nativo é uma DLL e um `manifest.toml` carregados pelo ShipLua. Ele recebe
tabelas C no init; não linka `soh`, `shiplua` ou Lua.

## Gerar o SDK do mesmo host

Configure e compile o host primeiro. O build gera:

```text
<build-do-host>/native-sdk/<config>/OotNativeSdk.cmake
<build-do-host>/native-sdk/oot_layout_id.h
```

`OotNativeSdk.cmake` exporta os includes e as definições do host. Passe o arquivo
da mesma configuração do executável que vai carregar a DLL:

```powershell
$sdk = (Resolve-Path .\build\x64\native-sdk\Release\OotNativeSdk.cmake).Path
$hostExe = (Resolve-Path .\x64\Release\soh.exe).Path   # $host é reservado no PowerShell
cmake -S soh/native-sdk/example -B build/dmr -DOOT_NATIVE_SDK=$sdk -DHOST_EXECUTABLE=$hostExe
cmake --build build/dmr --config Release
```

A saída do exemplo é `build/dmr/mod/`: `manifest.toml`, `main.lua`,
`provider/` e os assets que o exemplo copiar. Use `example` (DMR) como modelo de
CMake, init e verificação de serviços. `hooks-demo`, `item-demo`, `save-demo`,
`view-demo`, `world-demo`, `familiar-demo`, `escape-demo` e `crash-demo` são
demos de código; prepare manifesto e README próprios antes de distribuí-los.

## Layout id

O host calcula `LINKSPAN_OOT_LAYOUT_ID` a partir dos headers públicos, da
plataforma, compilador e flags. Um provider que converte ponteiros para tipos do
host deve comparar esse id e os `sizeof` relevantes antes de usar a tabela de
serviço. `native-sdk/example/provider.cpp` mostra a checagem de engine e
movement.

Depois de atualizar o executável, headers públicos, compilador, plataforma ou
flags do host:

1. reconfigure/compile o host para regenerar o SDK;
2. use o novo `OotNativeSdk.cmake` para recompilar o mod;
3. recrie o pacote; não misture DLL, layout id e executável de builds distintos.

## Empacotar e instalar

O manifesto e `main.lua` ficam na raiz do ZIP. A DLL fica no caminho declarado
em `[provider]`, normalmente `provider/<nome>.dll`. Para um diretório `mod`
já montado pelo CMake:

```powershell
.\tools\package-linkspan-mod.ps1 -Source build\dmr\mod -OutputPath build\dmr\Meu-Mod-0.1.0.zip
```

Não use `Compress-Archive` nem `ZipFile.CreateFromDirectory` no Windows PowerShell 5.1: eles gravam as
entradas com `\` e o loader não acha o manifesto. O script cria as entradas com `/`, em ordem fixa e com
horário fixo (`tools/linkspan-zip.ps1`), então o mesmo conteúdo gera o mesmo ZIP.

Copie o `.shipmod` para `mods/` ao lado do mesmo `soh.exe` usado no build e abra
o jogo. Confirme no log que o pacote foi carregado; uma rejeição informa a causa.
Depois de trocar pacote ou executável, feche o host, substitua o arquivo inteiro
e faça a mesma verificação de log.

## Serviços e hooks

Os headers `oot_*.h` são a referência pública de versões, tamanho de structs,
thread e ownership. Os serviços principais cobrem registry, randomizer,
actor-models, lights, render, scenes, save namespaces e resources. Peça cada
serviço por `runtime->get_service`, com nome, versão e `sizeof` corretos, e trate
retorno nulo/`UNSUPPORTED` como recurso indisponível.

Use `oot_hooks.h` para hooks. `oot.actor.init` só observa um ator já
inicializado; `oot.play.draw_end` ocorre no contexto de render ao fim do frame.
Ponteiros recebidos em payload só valem durante o callback.

## Escape hatch

O escape hatch requer core extension ABI 1.3 e fingerprint do executável no
manifesto. O host pode ativar os patches de init em lote e mantém a DLL em
quarentena se não conseguir remover todos os patches no unload. Não dependa de
um patch durante o próprio init e não instale patch no shutdown.
