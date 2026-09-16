# Mods nativos do Link-Span no Shipwright

Esta integração usa o SDK Link-Span 0.5 e carrega `.zip`, `.shipmod` ou pastas
com `manifest.toml` na raiz. Providers nativos ficam ativos no host; a DLL faz
parte do mod e suas funções não precisam ser cadastradas no loader.

O serviço legado `linkspan.oot.engine` permanece na versão 1 para mods
existentes. O serviço separado `linkspan.oot.movement` v1 oferece input virtual,
botões físicos SDL, bindings transitórios, analógicos, estado de chão/rolamento
e ações nativas de pulo e rolamento. A v2 acrescenta eixos físicos, gatilho ligado
a botão virtual, atalho nativo de Lente e máscaras e a posição dos botões C no
HUD. O Lua chama funções privadas da DLL por
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

## Serviço `linkspan.oot.movement` v2 (OOT-MOVE-004)

Header do contrato: `soh/soh/native/oot_engine.h`; decisão em
`NATIVE-001/rfcs/0018-oot-movement-v2.md`. A tabela `ShipOotMovementV2` repete a
v1 e acrescenta:

- `get_gamepad_axis(port, sdl_axis)`: valor SDL bruto do eixo físico;
- `bind_gamepad_axis(port, virtual_button, sdl_axis, direction)`: metade de um
  eixo ligada a um botão virtual, só em memória. Com ZR segurando um botão C, o
  jogo mira arco e gancho como no botão nativo;
- `player_use_item_shortcut(item)`: Lente da Verdade, máscara, ocarina, traje ou
  botas sem exigir o item num botão C. O host aplica as regras do jogo para Player,
  cena, idade, inventário e magia. Cada família segue seu caminho nativo:
  - lente e máscaras passam pelo `Player_UseItem`;
  - a ocarina começa a tocar no mesmo frame pelo `Player_ActionHandler_13`;
  - traje e botas seguem o `AssignableTunicsAndBoots`, e usar o equipado volta ao
    Kokiri;
- `get_item_button_rect(button, x, y, size, alpha)`: posição final de C-Left,
  C-Down e C-Right desenhada pelo HUD no frame atual ou no anterior.

A lente ligada pelo atalho continua ativa fora dos botões até ser desligada ou
aparecer num botão (exceção no `Magic_Update` de `z_parameter.c`). Máscara fora
dos botões exige `gEnhancements.PersistentMasks`; sem ela o host recusa o uso. O
`hook.oot.hud.draw` roda antes de `Interface_Draw`, por isso a captura do frame
anterior é aceita. O header do contrato entra no layout id: recompile providers
nativos contra o SDK do host novo.

## Serviço `linkspan.oot.ocarina` v1 (OOT-MIC-001)

Header do contrato: `soh/soh/native/oot_ocarina.h`; decisão em
`NATIVE-001/rfcs/0019-oot-ocarina-service.md`. O update de input da ocarina
(`func_800EE6F4`) publica se o jogo espera uma música e quais aceita. O serviço
oferece:

- `is_active()` e `get_available_song_flags()`: um bit por `OcarinaSongId`;
- `get_song_count()` e `get_song_pattern(song, notes, capacity, count)`: índices
  de nota de `gOcarinaSongNotes` (0=A, 1=C-Down, 2=C-Right, 3=C-Left, 4=C-Up);
- `submit_song(song)`: entrega uma música aceita, que o jogo recebe no próximo
  update como se tivesse sido tocada.

O mod `soh/native-sdk/mic-ocarina` usa o serviço: com a ocarina aberta, Start/+
liga o microfone padrão, o detector YIN reconhece o contorno da melodia
cantarolada e B desliga. O provider liga o SDL2 estático e só precisa do SDK para
os headers:

```powershell
$sdl2 = "<vcpkg>/installed/x64-windows-static/share/sdl2"
cmake -S soh/native-sdk/mic-ocarina -B build/mic-ocarina -G "Visual Studio 17 2022" -A x64 -DOOT_NATIVE_SDK="$sdk" -DSDL2_DIR="$sdl2"
cmake --build build/mic-ocarina --config Release
ctest --test-dir build/mic-ocarina -C Release
```

## Serviço `linkspan.oot.scenes` v1 (OOT-UNBOUND-003C)

Header do contrato: `soh/soh/native/oot_scenes.h`; decisão em
`NATIVE-001/rfcs/0020-oot-scene-registry.md`; layout em
`docs/architecture/unbound-scene-registry.md`. O host deixa de depender das
tabelas compiladas para cenas e entradas:

- `register_scene(definição, handle, id)`: nome namespaced (não pode ser um enum
  `SCENE_*`), caminho do recurso no VFS, draw config e id 128 a 32767 ou
  automático;
- `register_entrance(handle da cena, definição, índice)`: grupo de quatro posições
  da tabela de entradas (criança/adulto, dia/noite), a partir do primeiro grupo
  livre depois das 1.556 vanilla, com nome final `"<cena>/<entrada>"`;
- `unregister_scene(handle)`: remove a cena e as entradas; a tabela não encolhe;
- `find_scene` e `find_entrance`: nomes vanilla (`SCENE_HYRULE_FIELD`,
  `ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN`) e nomes dos mods;
- `travel_to_entrance(índice)`: transição com fade, só com o jogador em cena e sem
  outra transição.

Por dentro, `EntranceInfo.scene` passou a `s16` e `gEntranceTable` virou ponteiro:
aponta para a tabela vanilla até um mod registrar entradas e depois para a tabela
combinada. `OTRPlay_SpawnScene` resolve ids a partir de 128 pelo registro, e as
flags salvas dessas cenas ficam fora do array fixo de 124 (`LinkSpan_SceneFlags`),
guardadas pelo nome da cena durante a sessão. Um save com entrada fora da tabela
cai no Hyrule Field com uma linha de log.

O framework `linkspan.unbound.framework` 0.2.0 usa o serviço para ler
`unbound/scenes.json` de todas as camadas no `game.ready` (§7 do SPEC do
Unbound). `soh/native-sdk/unbound-core/scene-demo` registra uma cópia do Hyrule
Field e viaja até ela; `tools/package-unbound-scene-demo.ps1` empacota os dois.
As mudanças em `soh/include` e o header novo mudam o layout id: recompile os
providers nativos contra o SDK do host novo.

Ainda não: `titleCardTexture`,
camadas de entrada diferentes (`layers`) e mapa/minimapa para cenas novas.

## Hooks nativos do OoT (COREEXT-006)

Com provider `abi_version = "1.2"`, um mod entra no fluxo do jogo por
`register_hook` (contrato em `NATIVE-001/rfcs/0021-native-hooks.md`). O host
declara em `soh/soh/native/oot_hooks.h`:

| Ponto v1 | Payload | Modos | Onde |
|---|---|---|---|
| `oot.play.update` | `ShipOotPlayHookV1` | observe, replace | `Play_Main` → `Play_Update` |
| `oot.actor.update` | `ShipOotActorHookV1` | observe, replace | `Actor_UpdateAll`, depois do culling e do GameInteractor |
| `oot.actor.draw` | `ShipOotActorHookV1` | observe, replace | `Actor_Draw`, entre segmentos e sombra |

- Sem hook no ponto, o jogo chama a função original direto; o custo é uma
  consulta ao registro por ator.
- `replace` é exclusivo por ponto: quem substitui o update ou o draw filtra por
  `actor_id` e chama `call->call_original(call)` para os outros atores.
- Um segundo `replace` recebe `SHIP_NATIVE_LIMIT`, com log do dono atual.
- Os hooks saem no unload do mod, antes do `shutdown`.

`soh/native-sdk/hooks-demo` conta frames e updates por observe e pisca o Link
pelo replace do draw. Empacote com `tools/package-hooks-demo.ps1`.

## Serviço `linkspan.oot.save` v1 (OOT-CORE-006)

Header: `soh/soh/native/oot_save.h`; decisão em
`NATIVE-001/rfcs/0022-oot-save-namespaces.md`. Cada mod guarda um bloco JSON por
namespace na seção `linkspan` do arquivo de save:

- `open_namespace("autor.mod", versão, handle)`: o prefixo `linkspan.` é do host;
- `read`/`write`/`erase`: JSON de até 1 MiB; a escrita chega ao disco no próximo
  save do jogo;
- `get_stored_version`: versão com que o bloco foi escrito, para migrar;
- `begin`/`commit`/`rollback`: transação; o save grava o estado de antes do begin;
- `set_required`: o arquivo registra a dependência, e carregar sem o mod gera aviso;
- `get_slot`: arquivo carregado ou -1.

Eventos são hooks observe (ABI 1.2): `oot.save.loaded`, `oot.save.saving`,
`oot.save.deleted` e `oot.save.copied`, com `ShipOotSaveHookV1`.

Blocos de mods ausentes são preservados no arquivo. As flags das cenas de mod
(RFC 0020) passam a ser gravadas no bloco `linkspan.scenes`, pelo nome da cena.

`soh/native-sdk/save-demo` conta as cargas de arquivo no namespace
`linkspan-demo.save`; empacote com `tools/package-save-demo.ps1`.

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
X=`SDL Y`. Os bindings são aplicados somente em memória e o mapeamento do usuário
é restaurado quando o mod descarrega.

O mod habilita `FreeLook`, `PersistentMasks`, `Controls.RightStickAim` e
`MoveInFirstPerson` e fixa `FreeLook.InvertYAxis`, `Controls.InvertAimingYAxis` e
`Controls.InvertZAimingYAxis` em 0 enquanto está carregado; no unload, restaura as
opções anteriores. O analógico
direito controla a câmera livre com o eixo vertical normal (sem a chave na config,
o SoH inverte o eixo): parada, ela mantém o ângulo; quando Link volta a andar com
o analógico direito parado há 500 ms (`configure "0,500"`, aceita 0 a 60000), o
`FreeLook` é desligado e a câmera automática assume a partir da posição atual,
sem salto. O acesso a settings inteiros é uma primitiva genérica do SDK, não uma
regra fixa deste mod.

Alguns nomes não viram CVar: o host os intercepta e guarda só em memória, então
nada vai para a config, e a troca da ponte de settings, quando os mods
descarregam, zera tudo. `linkspan.transient_settings` devolve a soma dos recursos
que o host suporta:

- `1`: `linkspan.hud.hide_item_button.c_left`, `.c_down` e `.c_right` com 1
  ocultam o botão C no HUD (botão, seta de vazio, ícone e munição);
- `2`: `linkspan.input.sword_over_shield` com 1 tira o R do input do Player
  enquanto B está pressionado. Se B foi apertado com o escudo erguido, o aperto
  chega no frame seguinte, depois de o escudo baixar; sem isso o OoT não ataca
  defendendo;
- `4`: `linkspan.hud.dpad` com 1 mostra o D-pad do HUD mesmo sem `DpadEquips` e
  tira os ícones dos itens do D-pad. O fundo passa a sair no começo do
  `hook.oot.hud.draw`, que roda antes do `Interface_Draw`, para os ícones do
  provider ficarem por cima; o provider os desenha nas posições que
  `get_item_button_rect` devolve para 4 a 7 (cima, baixo, esquerda e direita).

As CVars de cosméticos do SoH não servem para ocultar botões: qualquer opção
mudada no menu salva a config com o valor do mod.

Na versão 0.2.9:

1. X executa o pulo dedicado;
2. A permanece como ação contextual e produz o rolamento normal quando Link se
   move; manter A até o fim do rolamento entra em sprint e soltar A encerra;
3. Y aciona a função normal de espada e B a de cancelar/guardar;
4. ZR usa o item do botão C equipado e mira enquanto segurado. O HUD mostra só
   esse botão C, com um anel branco; o host oculta os outros só em memória,
   sem tocar na config;
5. segurar R abre o menu de itens com os botões C que têm item, a partir do
   equipado. O analógico direito anda para a esquerda e para a direita, e soltar o
   R equipa o destacado no ZR; com o ZR pressionado a troca é recusada;
6. ZL mira (Z do N64) e ergue o escudo (R do N64). Y ou B pressionado baixa o
   escudo, e a espada sai mesmo defendendo. `-` é o L do N64 e o L físico fica livre;
7. toque no R3 liga ou desliga a Lente da Verdade sem ocupar um C; segurar R3 por
   400 ms coloca ou tira a máscara do slot infantil;
8. D-pad direita é o C-Up nativo: Navi e primeira pessoa;
9. D-pad esquerda tira a ocarina do inventário sem ocupar um C;
10. D-pad cima veste o traje: o toque volta ao último usado;
11. D-pad baixo alterna as botas com as Kokiri;
12. segurar D-pad cima ou baixo por 400 ms abre o menu com os ícones no HUD, a
    partir do equipado. O analógico direito escolhe para os lados, sem ciclo
    automático, e o destacado é vestido ao soltar;
13. com um menu aberto o analógico direito não gira a câmera; depois de fechar, a
    câmera só volta a responder quando o analógico passa pelo centro;
14. em primeira pessoa (D-pad direita) e na mira de arco, estilingue e gancho, o
    analógico direito move a visão com o eixo vertical normal e o esquerdo anda com
    o Link (`Controls.RightStickAim` e `MoveInFirstPerson` do SoH). Na mira com o
    ZL, o vertical do analógico direito também é o normal. A câmera livre fica
    parada nesse tempo e só volta depois de o analógico passar pelo centro;
15. o D-pad do HUD mostra as funções do mod: traje em cima, botas embaixo, ocarina
    na esquerda e Navi na direita. O ícone da Navi é um PNG convertido por
    `tools/convert-png-to-hud-icon.ps1` para `assets/`, pasta que o provider monta
    no configure; o PNG fica fora do git, que ignora `*.png` em `soh/`.

Hotbars permanecem nas próximas fatias.

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
