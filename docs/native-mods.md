# Mods nativos do Link-Span no Shipwright

Esta integração usa o SDK Link-Span 0.5 e carrega `.zip`, `.shipmod` ou pastas
com `manifest.toml` na raiz. Providers nativos ficam ativos no host; a DLL faz
parte do mod e suas funções não precisam ser cadastradas no loader.

O serviço legado `linkspan.oot.engine` permanece na versão 1 para mods
existentes. O serviço separado `linkspan.oot.movement` v1 oferece input virtual,
botões físicos SDL, bindings transitórios, analógicos, estado de chão/rolamento
e ações nativas de pulo e rolamento. O Lua chama funções privadas da DLL por
`ship.native.call`; eventos usam a API Lua existente.
Os ponteiros ficam somente no código nativo e não devem ser retidos entre cenas
ou frames. Consulte novamente o serviço durante cada chamada. Fora de gameplay
o jogador e a cena retornam nulo. Use apenas a thread do jogo.

## Compilar o host uma vez

Configure o host com `LINKSPAN_SDK_SOURCE_DIR` apontando para o SDK novo. O
submódulo continua sendo o padrão quando o override não é fornecido. Nesta
worktree de desenvolvimento, o override é necessário porque o SDK ainda não
foi commitado/integrado ao submódulo.

```powershell
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64 -DLINKSPAN_SDK_SOURCE_DIR=../NATIVE-001
cmake --build build/x64 --config Release --target soh
```

O CMake gera `build/x64/native-sdk/Release/OotNativeSdk.cmake`, com os includes
e definições usados pelo host. É um SDK local que referencia seus fontes e as
dependências; ainda não é um SDK relocável para distribuição independente.

## Compilar só o mod

```powershell
$sdk = (Resolve-Path build/x64/native-sdk/Release/OotNativeSdk.cmake).Path
cmake -S soh/native-sdk/example -B build/dynamic-movement -DOOT_NATIVE_SDK="$sdk" "-DJUMP_VELOCITY=7.0"
cmake --build build/dynamic-movement --config Release
Compress-Archive -Path build/dynamic-movement/mod/* -DestinationPath build/dynamic-movement-remake.zip -Force
```

Instale somente esse ZIP em `mods/` do host integrado. O manifesto precisa ficar
na raiz do ZIP, junto de `main.lua`; a DLL fica em
`provider/dynamic_movement_remake.dll`. Para mudar o impulso, os tempos ou a
mecânica, edite e recompile somente o mod.

## Perfil Nintendo

O mod lê as posições físicas do Switch Pro: B=`SDL A`, A=`SDL B`, Y=`SDL X` e
X=`SDL Y`. Os bindings de face são aplicados somente em memória e o mapeamento
do usuário é restaurado quando o mod descarrega.

O mod também habilita `FreeLook`, libera o analógico direito dos atalhos C
somente durante sua sessão e restaura a opção anterior no unload. Depois de quatro
segundos sem movimento do analógico direito, a câmera volta ao comportamento
automático; mover o stick novamente reativa o `FreeLook`. O acesso a settings
inteiros é uma primitiva genérica do SDK, não uma regra fixa para este mod.

Nesta fatia funcional:

1. X executa o pulo dedicado;
2. A permanece como ação contextual e produz o rolamento normal quando Link se
   move;
3. manter A até o fim do rolamento entra em sprint;
4. soltar A encerra o sprint;
5. Y aciona a função normal de espada e B a função normal de cancelar/guardar;
6. o analógico direito controla a câmera livre.

O `main.lua` registra o esquema final: analógicos esquerdo/direito, X pulo, A
contexto/rolamento/sprint, Y espada, B cancelar, ZL target, ZR item, R Quick
Swap, L escudo, Ocarina/Navi, seletores de Gear/Boots e +/- para menu/mapa. As
Os atalhos e hotbars permanecem nas próximas fatias.

## Compatibilidade e alcance

O provider confere fingerprint dos headers e tamanhos dos tipos antes de acessar
layouts. Quando o host muda esses layouts, recompile o provider contra o SDK
correspondente. Código nativo roda com os poderes do processo; erros de memória
podem derrubar o jogo. A verificação de layout não transforma a DLL em sandbox.

O serviço é exclusivo de OoT. O núcleo permanece compartilhado, mas a integração
MM ainda precisa fornecer seu próprio adaptador. Esta etapa não exporta todas
as funções internas do executável nem implementa Mixins ou hot reload de DLLs.
O teste `oot_native_independent_mod` usa o bridge real e estruturas reais, com
stubs de spawn/kill; isso comprova ABI e escrita, mas não gameplay em jogo.

## Validação local de 2026-09-09

A validação de `engine` v1 com `movement` v1 usa o pacote final
`Dynamic-Movement-Remake-0.2.3.zip`, com impulso 7, animação nativa de salto e cobre pulo em X,
aterrissagem, rolamento contextual em A, transição para sprint e rearme ao soltar A. O mesmo
host também carrega uma segunda compilação do provider com impulso 10, sem ser
recompilado.
