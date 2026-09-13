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

## Serviço `linkspan.oot.resources` v1 (OOT-CORE-001)

Header do contrato: `soh/soh/native/oot_resources.h`. Nome/versão:
`LINKSPAN_OOT_RESOURCES_SERVICE` / versão 1. A estrutura `ShipOotResourcesV1`
tem `size` + 8 ponteiros de função:

- `has_file(path)`
- `read_file(path, output, capacity, output_size)`
- `list_files(search_mask, callback, user)`
- `dirty_resources(search_mask)`
- `unload_resource(path)`
- `mount_archive(archive_path, &handle)`
- `unmount_archive(handle)`
- `get_game_versions(output, capacity, output_count)`

Semântica: ABI C estável, handles opacos; buffers e callbacks pertencem ao
chamador; nenhum tipo C++ do libultraship atravessa a DLL. `read_file` e
`get_game_versions` aceitam primeira chamada com `output=NULL, capacity=0` para
consultar o tamanho e retornam `SHIP_NATIVE_LIMIT` se a capacidade for
insuficiente. `list_files` usa máscara glob (ex:
`objects/gameplay_keep/gArrow*`), ordena os caminhos e invoca o callback por
caminho; retornar status não-OK no callback interrompe a enumeração.
`mount_archive` aceita `.o2r`/`.zip` (ZIP), `.otr`/`.mpq` (MPQ, quando
compilado) ou pasta, e devolve handle opaco uint64; quem monta deve desmontar
no shutdown do provider, e o host desmonta sozinho no shutdown como rede de
segurança (`ClearNativeResourceArchives`). Todo o serviço só é válido na thread
do jogo — chamadas de outras threads são recusadas.

Limites de concorrência conhecidos na v1: a guarda de thread bloqueia chamadas
externas, mas não serializa contra o thread pool interno do `ResourceManager`
nem contra a thread de áudio, que leem os mesmos mapas do `ArchiveManager` (que
não tem mutex). `dirty_resources` e `unload_resource` são best-effort de cache.
`mount_archive`/`unmount_archive` são as operações de maior risco: faça-as
somente em janelas sem loads em voo (ex: no `game.ready`, antes de entrar em
cena). A correção estrutural é um mutex no `ArchiveManager` (upstream
libultraship), fora do escopo desta fatia.

O host inicializa o `ArchiveManager` com o conjunto de versões válidas vazio,
portanto um archive sem arquivo `version` também é aceito por `mount_archive`.

Formato do arquivo `version` dentro de um archive: byte 0 = endianness
(0 = little, 1 = big), seguido de uint32 com a versão do jogo (ex:
`01 d4 3d a8 1f` = big-endian 0xD43DA81F). A sonda de runtime montou um ZIP
contendo `version` copiado do `oot.o2r`.

O provider de exemplo (`soh/native-sdk/example/provider.cpp`) registra
`resource_probe` (leitura sintética) e `resource_runtime_probe` (prova contra o
VFS real); o `main.lua` chama via `ship.native.call` no `game.ready` e loga com
o marcador `core-001-probe`.

Validação de 2026-09-12: sessão de runtime `core-001-20260912-1155` (evidências
em `build/runtime-evidence/core-001-20260912-1155/`), boot completo até a title
screen, sem exceções, executável e archives inalterados (SHA-256 antes=depois).
Linha de prova registrada no log:
`core-001-probe: versions=1:0xD43DA81F; has=1/0; version_file=0xD43DA81F listed=1; arrows=13 first=objects/gameplay_keep/gArrow1Anim; mount=ok handle=1 marker=match unmount=ok gone=1`.

## Serviço `linkspan.oot.registry` v1 (OOT-CORE-002)

Header do contrato: `soh/soh/native/oot_registry.h`. O serviço oferece um
registro dinâmico genérico; o Shipwright guarda identidade e bytes, enquanto o
coremod decide se eles representam cenas, entradas, itens, transformações ou
outro catálogo. A tabela `ShipOotRegistryV1` contém:

- `create_space` / `find_space` / `destroy_space`;
- `register_entry` / `unregister_entry`;
- `find_entry_by_name` / `find_entry_by_id`;
- `read_entry` para copiar nome, payload e ID;
- `list_entries` para enumeração determinística.

Cada espaço declara um intervalo numérico e um `stride`. Ao passar
`LINKSPAN_OOT_REGISTRY_AUTO_ID`, o host escolhe o menor ID livre alinhado. Nome
e ID são únicos dentro do espaço. O host copia todos os dados, limita nomes a
255 bytes, cada payload a 64 KiB, mantém no máximo 64 espaços e 65.536 entradas
vivas e recusa chamadas fora da thread do jogo.

`read_entry` aceita consulta inicial com os dois buffers nulos e capacidades
zero. O nome devolvido tem comprimento explícito e não inclui terminador NUL.
`destroy_space` remove suas entradas em cascata; o provider que criou o espaço
deve chamá-lo no shutdown. Assim, desativar ou trocar o `.shipmod` não deixa IDs
ocupados no processo.

O provider de exemplo cria `example/dynamic_movement/actions` e registra as
entradas `jump` e `sprint` durante seu próprio `init`. `registry_probe` confirma
busca por nome, leitura do payload e listagem, e o teste do host confirma que o
espaço desaparece após o unload. Adicionar novas entradas a esse catálogo exige
recompilar somente a DLL do mod.

Validação de 2026-09-12: a sessão isolada
`build/runtime-evidence/core-002-20260912-1445/` iniciou o Shipwright 9.2.3,
carregou o ZIP 0.2.4, chegou a `ShipLua inicializado`, encerrou normalmente e
registrou:
`core-002-registry: space=ok; entries=2; first=128; jump=example/dynamic_movement/jump:action=jump; id=128`.
O resultado da sessão ficou `valid: true`; `soh.exe`, `oot.o2r`, `soh.o2r` e os
ZIPs rastreados mantiveram seus hashes antes e depois.

## Serviço `linkspan.oot.resources` v2 (OOT-UNBOUND-002A)

Header do contrato: `soh/soh/native/oot_resources.h`. A v2 é prefixo binário
compatível com a v1 e adiciona `read_file_layers`: o host enumera **todas** as
cópias montadas de um mesmo caminho virtual, da menor para a maior prioridade,
chamando um callback síncrono por camada com:

- bytes do arquivo (válidos apenas durante o callback);
- caminho do archive de origem (`base.o2r`, `override.shipmod`, ...);
- versão de jogo do archive (0 quando ausente);
- hash FNV-1a 64 do conteúdo e o tamanho em bytes;
- `layer_index` / `layer_count` para detecção de ordem e total.

A regra de merge (JSON por schema, substituição binária etc.) pertence ao
provider; o host apenas entrega as camadas em ordem estável. Retornar outro
status no callback interrompe a enumeração e propaga o status. Chamadas fora da
thread do jogo são recusadas com `SHIP_NATIVE_INVALID_ARGUMENT`. No
libultraship, `ArchiveManager::LoadFileFromAllLayers` oferece a mesma
enumeração para consumidores internos.

O provider de exemplo expõe `layer_probe`, que compõe um hash determinístico
sobre as camadas na ordem recebida; o teste `oot_native_engine_tests` valida
ordem, bytes, origem, hash, interrupção do callback e recusa de thread externa
com dois archives sintéticos, e a prova independente recompila a DLL fora da
árvore contra o mesmo `soh.exe`. É o substrato que o Unbound precisa para
aplicar deltas JSON sobre cenas vanilla sem loader específico no executável.

A prova de runtime `layer_runtime_probe` monta dois ZIPs com
`unbound/layer-probe.json`, confirma a ordem base → override, compõe o hash e
desmonta ambos antes de retornar. Ela existe para validar o contrato; uma
factory real do Unbound consumirá a mesma função com seu próprio schema.

Aviso de robustez (descoberto no playtest, fora do escopo do serviço): o boot
crasha com 0xc0000005 em `Fast3dGui::LoadGuiTexture` se o `soh.o2r` instalado
não contém as texturas custom do SoH (ex: `textures/parameter_static/gTriforcePiece`,
`gClimbTex`, `gCrawlTex`, `gGrabTex`, `gOpenChestsTex`) — o host não faz
null-check ao carregar ícones GUI. O `soh.o2r` precisa ser o gerado pela mesma
árvore/branch do executável.

## Coremod `linkspan.unbound.json_factory` v1 (OOT-UNBOUND-002B)

O projeto externo `soh/native-sdk/unbound-core` prova que uma core extension
pode implementar uma factory sem adicionar regras do Unbound ao executável. O
coremod consulta `linkspan.oot.resources` v2, registra o schema
`linkspan.unbound.actor-patch/v1` e publica a tabela C
`LinkSpanUnboundJsonFactoryV1` para outros mods.

O serviço enumera schemas, carrega todas as camadas de um caminho, valida
`$schema`, mescla objetos recursivamente e substitui arrays e escalares pela
camada de maior prioridade. O resultado fica em um handle opaco pertencente ao
coremod; o consumidor consulta JSON, quantidade de camadas e hash FNV-1a 64 e
depois chama `release`. JSON e handles são limitados e todas as chamadas
ocorrem na thread do jogo.

`tools/package-unbound-factory-demo.ps1` gera dois mods independentes, duas
camadas de fixture e um bundle que instala apenas em `mods/`. A sessão isolada
`build/runtime-evidence/unbound-002b-20260912-1938/` carregou primeiro o
framework e depois o consumidor, sem recompilar ou alterar `soh.exe`, e
registrou:

`unbound-002b-factory: schema=linkspan.unbound.actor-patch/v1; layers=2; hash=412c9d58a4e6de21; json={..."health":8,"speed":1..."drops":["heart"]...}; cleanup=ok`

Esta fatia ainda não conecta o resultado a uma scene factory do
`ResourceLoader`. Os operadores `null`, `$replace` e `$order`, factories de
cena/sala e o adapter que materializa o recurso pertencem aos recortes
seguintes do Unbound.

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
Swap, L escudo, Ocarina/Navi, seletores de Gear/Boots e +/- para menu/mapa. Os
atalhos e hotbars permanecem nas próximas fatias.

## Compatibilidade e alcance

O provider confere fingerprint dos headers e tamanhos dos tipos antes de acessar
layouts. O fingerprint (`LINKSPAN_OOT_LAYOUT_ID`, gerado em `ExportSdk.cmake`)
cobre apenas a superfície que atravessa a fronteira C: as structs z64 de
`soh/include`, os contratos de serviço de `soh/soh/native` e os headers
`shiplua/native` do SDK, além das flags do compilador. Mudanças internas em
`soh/src`, no restante de `soh/soh` ou no `libultraship` **não** alteram o
fingerprint nem invalidam mods compilados; mudanças em structs z64 ou nos
contratos de serviço alteram, e nesse caso recompile o provider contra o SDK
correspondente. Código nativo roda com os poderes do processo; erros de memória
podem derrubar o jogo. A verificação de layout não transforma a DLL em sandbox.

O serviço é exclusivo de OoT. O núcleo permanece compartilhado, mas a integração
MM ainda precisa fornecer seu próprio adaptador. Esta etapa não exporta todas
as funções internas do executável nem implementa Mixins ou hot reload de DLLs.
O teste `oot_native_independent_mod` usa o bridge real e estruturas reais, com
stubs de spawn/kill; isso comprova ABI e escrita, mas não gameplay em jogo.
O serviço `resources` v1, por sua vez, já tem prova em jogo na sessão
`core-001-20260912-1155`. O serviço `registry` v1 está validado sem ROM; a ligação
das entradas a SceneDB e ao gameplay será o próximo adapter Unbound.

## Validação local de 2026-09-09

A validação de `engine` v1 com `movement` v1 usa o pacote final
`Dynamic-Movement-Remake-0.2.3.zip`, com impulso 7, animação nativa de salto e cobre pulo em X,
aterrissagem, rolamento contextual em A, transição para sprint e rearme ao soltar A. O mesmo
host também carrega uma segunda compilação do provider com impulso 10, sem ser
recompilado.
