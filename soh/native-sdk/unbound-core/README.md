# Framework Unbound (Link-Span)

`linkspan.unbound.framework` é a core extension que lê o formato 2 do SoH: Unbound (a SPEC do fork
`roborich/Shipwright`, `unbound-docs/SPEC.md`) sobre o host Link-Span do Shipwright 9.2.3. Não linka
`soh.exe`: usa só os serviços `linkspan.oot.*`.

## O que a versão 0.6.0 faz

### Unbound 0.9: atores declarados em JSON

- Cada tipo fica em `unbound/actors/<nome>.json`; `unbound/actors/meumod/guarda.json`
  é o tipo `meumod/guarda`. Use esse nome no `id` de um ator da sala.
- O modelo pode ser um esqueleto normal/flex com animação ou uma display list estática,
  opaca ou translúcida, do jogo ou de um mod. Os recursos são carregados pelo caminho.
- Suporta animação em loop, pose em um frame, escala, sombra, distância de desenho,
  texturas nos segmentos 8–12, juntas ocultas, colisão cilíndrica, diálogo e cabeça
  acompanhando Link com eixos configuráveis.
- Tipos no mesmo caminho combinam por chave entre mods; `null` remove o tipo.
  Nomes de atores existentes, como `En_Kanban`, também funcionam na lista de atores da sala.
- Nomes desconhecidos, IDs numéricos na faixa dinâmica e `params` em objeto são
  pulados com diagnóstico. Atores de transição continuam usando IDs numéricos;
  uma entrada inválida conserva seu índice e não cria ator.
- Modelos, animações e texturas incompatíveis são recusados antes de chegar ao desenho.
  O host recusa spawn de IDs inexistentes e remove o limite antigo de 255 instâncias por tipo.

O framework requer o novo serviço `linkspan.oot.actor-models` v1. Os tipos são
registrados ao iniciar; para trocar arquivos de tipos, reinicie o jogo.

Exemplo:

```json
{
  "name": "Guarda",
  "model": {
    "skeleton": "objects/object_toryo/object_toryo_Skel_007150",
    "animation": "objects/object_toryo/object_toryo_Anim_000E50",
    "shadow": 42
  },
  "collision": { "radius": 18, "height": 63 },
  "talk": { "message": "0xA001" },
  "look": { "limb": 15 }
}
```

Coloque a mensagem em `text/eng/messages.json` e o ator no JSON da sala:
`"guarda": { "id": "meumod/guarda", "pos": [0, 0, 0], "params": 0 }`.
O número do ator é atribuído ao carregar e nunca deve ser gravado no mapa.
Veja [NOTICE.md](./NOTICE.md) para a origem do código.

- **Base convertida (UNBOUND-006).** No init, antes de o SoH montar os mods, converte as cenas vanilla dos
  archives do jogo para o formato 2 e grava `oot-unbound.o2r` ao lado do `oot.o2r`, com a proveniência em
  `oot-unbound.o2r.source.json` (versão do conversor, versões da ROM, tamanho e data de `oot.o2r`,
  `oot-mq.o2r` e do executável). Proveniência igual reaproveita o arquivo; diferente converte de novo. A
  gravação é num `.tmp` seguida de rename: um arquivo pela metade nunca substitui o anterior. A base é
  montada logo em seguida, abaixo dos mods.
- **Tipos JSON (UNBOUND-002/005).** Registra `unbound/scene/1`, `unbound/room/1`, `unbound/collision/3` e
  `unbound/paths/1` em `linkspan.oot.resources` v3. Um recurso JSON com esse `$schema` é mesclado em todas
  as camadas (SPEC §3) e transcodificado para o XML que as fábricas do host leem. Documento recusado não
  carrega e o motivo vai para `logs/linkspan-unbound.log` e para o log do jogo.
- **Cenas vanilla pelo JSON (§1.3/§1.6).** No `game.ready`, com uma camada cujo `unbound.json` traz
  `"scenes"` em `features`, cada cena vanilla com `scenes/<cena>/scene.json` no VFS passa a carregar dele
  (`override_scene`). Um mod altera uma cena vanilla com uma camada do mesmo caminho: só as chaves que ele
  traz mudam. Com a base ativa o patch de atores da 0.3.0 (hook `oot.room.actors`) sai de cena; o mesmo
  documento agora mescla direto na sala.
- **Registro (§7).** `unbound/scenes.json` registra cenas e entradas (`linkspan.oot.scenes` v2, com
  `titleCardTexture`), na ordem de chave da §3.5. O jogo numera cena e entrada nessa ordem; `sceneId` e
  `entrances.*.index` são obsoletos e ignorados com nota. Exits por nome (`ENTR_*`, `ENTR_RETURN_*` ou
  `"<cena>/<entrada>"`) são resolvidos quando a cena carrega.
- **Texto (§5).** `text/<lang>/messages.json` (eng, ger, fra, jpn, staff) mescla por mensagem sobre a
  tabela do jogo (`linkspan.oot.text`): acrescenta, troca e `null` remove. `$replace` em `messages`
  esvazia a tabela antes. Mensagem acima de 8 192 bytes é truncada com aviso.
- **Conflito entre mods (0.6.1).** No `game.ready`, antes do gameplay, todo arquivo que dois ou mais mods
  Unbound trazem no mesmo caminho é comparado. Documento que o Unbound mescla (JSON com `$schema`
  `unbound/...` em alguma camada, `unbound/scenes.json` e `text/**`) é comparado folha a folha pela mesma regra
  do jogo: se o merge mantém tudo o que cada mod pôs (chaves diferentes, ou o mesmo valor), é um delta
  mesclável e só fica registrado. Se um mod sobrescreve, remove ou descarta com `$replace` um valor que outro
  trouxe, sai o aviso `unbound: conflito: <arquivo>: <mod de cima> sobrescreve N valor(es) de <mod de baixo>
  (no par, vale <mod de cima>, montado depois)`, com até três chaves de exemplo. Qualquer outro arquivo (binário
  ou JSON sem tipo) o VFS entrega inteiro: conteúdo igual é cópia idêntica; diferente é `conflito: ... substitui
  o arquivo inteiro de ...`. Vence o archive montado depois (ordem de caminho em `mods/`). O resumo sai em
  `unbound: mods: camadas=... conflitos=...` e a lista completa vai para `logs/linkspan-unbound.log`. A base
  convertida não conta como mod.
- **Referências de recurso (0.6.2).** Nomes de recurso mudam entre versões da ROM (display lists e texturas com
  offset no nome, por exemplo), e uma cena de mod feita sobre outra versão aponta para algo que o `oot.o2r` do
  usuário não tem; o jogo só descobriria ao entrar na cena. No mesmo `game.ready`, cada `.json` que um mod
  Unbound traz é mesclado com as camadas de baixo e transcodificado como o jogo faria (só os tipos que o
  framework registra: `unbound/scene/1`, `room/1`, `collision/3`, `paths/1`), e cada recurso que o XML manda o
  host carregar é conferido no VFS: `mesh` (`opa`, `xlu`, imagens de fundo), `collision`, `rooms`, `paths`,
  `cutscene`, texturas do `texCycle`, `sound.song` e o `bulk.file` da colisão. Recurso ausente sai como aviso
  `unbound: aviso: <documento>: <campo> aponta para <caminho>, que nenhum archive montado tem (<mods>)`;
  documento que o jogo recusaria sai como `aviso: <documento> seria recusado ao carregar: <motivo>`. O resumo
  é `unbound: referências: documentos=... conferidas=... ausentes=... recusados=... jogo=<versões do oot.o2r>`;
  as notas do transcodificador (atores desconhecidos, entradas descartadas) vão só para o log do Unbound. Até
  4 096 documentos por boot.
- **Limites do Prelude (0.6.3).** Listas posicionais no teto não custam mais o quadrado do tamanho: o
  `ordered_json` procura chave em ordem, e um documento com 65 535 surface types levava ~9 s entre parse e
  transcode. Agora o parse monta o objeto com índice de chaves (`ParseJson`), o merge indexa objetos grandes e o
  transcode percorre as listas sem procurar chave por chave; o mesmo documento leva ~120 ms, e duas camadas
  completas, ~330 ms. Vale também para a remoção de diretivas, os atores de sala (`unbound/room/1`) e overlay
  grande sobre base pequena. Notas novas, antes do gameplay para documento de mod: mais de 32 768 salas (índice
  s16), mais de 65 535 atores numa sala (8 192 desde a 0.6.9), mais de 1 020 objetos numa sala (o banco tem 1 024 vagas, até 4 delas
  com objetos permanentes: gameplay_keep, Link, keep da cena e cavalo), posição de câmera fixa fora de s16
  (o §2 continua embrulhando o valor) e setup acima de 255 (ignorado). O resumo do `ready` traz `ms=` da checagem.
  As fronteiras limite-1/limite/limite+1 estão em `limits_tests.cpp`.
- **Limite do mundo (0.6.4).** Spawn, ator, transition actor ou `bounds` da colisão com coordenada de módulo
  acima de 1 048 576 (`BGCHECK_XYZ_ABSMAX`), no valor que o jogo recebe (f32 nas posições, inteiro arredondado nos
  bounds), saem em nota: lá o jogo apaga os efeitos de partícula `EffectSs` (poeira, faíscas, respingos) em vez de
  desenhá-los. A colisão continua: o `BgCheck_PosErrorCheck` só registra. Na UNBOUND-021 o piso passa do limite
  até a borda em ±1 048 800 e o Link anda sobre ele; a sonda registra a perda de chão logo depois da borda.
- **Malha tipo 2 (0.6.5).** Sala com `mesh.type = 2` e mais de 1 024 entradas sai em nota: o `z_room.c` só
  considera as primeiras 1 024 (`SHAPE_SORT_MAX`), antes do teste de distância, e não desenha as demais. O aviso
  do próprio jogo é `osSyncPrintf`, desligado no build. A tipo 0 não tem esse teto.
- **Câmeras da colisão (0.6.6).** A câmera de uma superfície ou de uma water box é índice da tabela `cameras`, e o
  jogo não confere o tamanho (`BgCheck_GetBgCamSettingImpl`, `WaterBox_GetCameraSType`): fora da tabela, ele lê fora
  da lista. A da superfície é lida sempre, até -1; a da água só quando positiva (`Camera_GetWaterBoxDataIdx` trata 0
  e negativo como sem câmera). Saem em nota a câmera fora da tabela (inclusive qualquer câmera numa tabela vazia), a
  câmera acima de 32 767 (o estado da câmera do jogo é s16), a câmera cujas posições não cabem nas `cameraPositions`
  (o jogo lê `max(count, 3)` vetores a partir de `positionIndex`: o `BgCamFuncData` da câmera fixa tem três, o
  crawlspace lê `count` pontos), `count` sem `positionIndex` (a fábrica dá um único vetor zero) e `count` fora de
  0–32 767. Não se confere qual preset usa as posições: `count` 0 sem `positionIndex` num preset que as lê passa sem
  nota. O XML não muda. Nenhum dos 101 documentos de colisão do jogo base gera essas notas.
- **Grafo cena ↔ colisão e salas (0.6.7).** No `game.ready`, cada cena de mod é cruzada com a colisão e as salas
  dela (mescladas, de mod ou da base), onde o host usa um índice de um documento para ler a lista de outro sem
  conferir. Saem em nota: exit de superfície fora de 1..N saídas de cada setup emitido (`z_player.c` lê
  `setupExitList[exit - 1]`), room de water box fora das salas (a água não liga em sala nenhuma; -1 é todas), room
  de lado de porta além das salas (`z_room.c` lê `roomList[room]` ao montar a cena) e câmera de lado de porta fora
  da tabela da colisão (`Camera_ChangeDoorCam`; -1 e -99 não leem). O que não dá para conferir sai como "grafo
  incompleto" e não conta como nota: dependência ausente, recusada, alias `.meta` (o jogo carrega o alvo; vale
  também para documento de mod e para a própria cena) ou além do orçamento (4 096 leituras e 64 MB disparadas pelo
  grafo, de mod ou da base), e sala que traz `exits` (troca a lista conforme o percurso). O cache da checagem guarda
  só o que o grafo lê de salas e colisões. Num delta sobre uma cena da base, o grafo também roda na cena só da base,
  e cada item que sai igual nas duas (mesmo lugar, mesmo limite, mesmo campo=valor) fica fora, contado em
  `herdadas=`: a `spot04` vanilla tem setups de cutscene com menos saídas que os exits da colisão, e isso não é do
  mod. Lacunas nunca saem; a releitura da base conta no orçamento, e sem saldo as notas ficam todas, com uma lacuna.
  O resumo ganha `grafos=`, `incompletos=`, `herdadas=` e
  `dependencias=`. Só as cenas que um mod traz são raízes; um mod que só troca uma colisão da base não é cruzado
  com as cenas da base.
- **Minimapa e mapa da pausa (0.6.8).** Duas notas locais da cena. Uma cena de dungeon vanilla, da variante MQ
  dela (`scenes/<cena>_mq`; chefe não tem MQ) ou do chefe sai em nota quando passa das salas do minimapa da dungeon ou da sala 31: o
  `z_map_exp.c` (`Map_InitData`, `Map_SetPaletteData`, `Map_InitRoomData`) lê textura, paleta e bússola pela sala
  sem conferir, e as tabelas são contíguas. A nota diz, por tabela, em que salas a leitura cai nas dungeons
  seguintes e de que sala em diante passa da tabela inteira (239 nomes de textura, linhas de 32 paletas e de 44
  offsets de bússola por dungeon), e que a visita deixa de ser marcada da sala 32 em diante. A paleta lida além da
  tabela é índice de escrita em `mapPalette` e pode escrever fora dela. Na `ice_doukutu`, última da lista, a textura já passa da
  lista na sala 12; na `MIZUsin` e na `HIDAN`, a nota pode sair a partir da 32, antes de acabarem as texturas delas.
  A nota diz o que o jogo lê: os chefes não desenham o minimapa, e o desenho depende do mapa da dungeon. A outra nota
  é `cameraSettings.worldMapArea` fora de 0..22 num setup emitido: o jogo guarda o valor como s16, um negativo passa
  nas guardas `< 22` do mapa-múndi da pausa, que lê as tabelas de área fora, e numa cena de exterior vanilla o valor
  também indexa `gBitFlags` e marca `worldMapAreaData` no save. O 22 (fora do mapa: fontes de fada e grutas) não sai
  em nota, mas o host o lê além das quatro tabelas de 22 da moldura da pausa (`func_80823A0C`, como no vanilla): a
  nota não certifica a pausa, e a guarda é do host. Cena nova não tem minimapa (`Map_Init` e `Minimap_Draw` só
  conhecem as cenas vanilla) e, na pausa, mostra o mapa-múndi na área do `worldMapArea` dela. Nenhuma cena da base
  gera essas notas (as 18 cenas de dungeon cabem no minimapa; as áreas vão de 0 a 22).
- **Atores e luzes de sala (0.6.9).** Uma sala com mais de 8 192 atores grava só os primeiros 8 192, com nota. O
  host copia a lista da sala para o buffer do `oot.room.actors` (`LinkSpan_RoomActors`), que tem 8 192 vagas, e
  com mais escreve além dele: uma sala de 8 200 rúpias corrompeu o heap em jogo (UNBOUND-029). O total de atores
  vivos também para em 8 192, com o Link na conta, então o excesso nunca nasceria. Antes disso, a arena do jogo
  pode acabar: na e-fixtures, cabem 4 132 rúpias (En_Item00), e cada uma das outras falha com "Cannot allocate
  actor" no log. Uma lista `lights` com mais de 32 entradas também sai em nota: o pool de luzes do host
  (`z_lights.c`) tem 32 vagas para a cena, as salas já carregadas, os atores e o ambiente, e as luzes de lista só
  voltam ao pool na troca de cena. As que não cabem não acendem.

## Unbound 0.8 (novo na 0.5.0)

O formato continua na versão 2 e mod antigo continua carregando. O que mudou (SPEC §4.2 e §7,
`prelude-handoff.md` de 2026-09-19 e 2026-09-20):

- **Número de cena e de entrada.** O número depende dos mods montados, então dois mods que fixavam o mesmo
  colidiam e o segundo sumia. `sceneId` e `entrances.*.index` agora são ignorados, com nota.
- **Saídas.** Número entre `ENTR_MAX` (1556) e `0x7FF8`, o intervalo das entradas de mod, recusa o documento:
  entrada de mod só pelo nome, `"<cena>/<entrada>"`. Continuam valendo como número as vanilla e as de
  retorno dinâmico, `0x7FF9`–`0x7FFF` (grutas, fontes, galeria de tiro e bazar), que também aceitam o nome
  `ENTR_RETURN_*`.
- **`sound`.** `seq` vale de 0 a 109 ou 127 (sem música); `natureAmbience` vale de 0 a 19, com 19 = nenhuma.
  Fora disso vira "nenhum", com nota: o motor indexa tabelas com esses bytes, e o 255 que exportadores usavam
  como "nenhum" derrubava a thread de áudio no primeiro pôr do sol. `song` com `seq` 127 não toca, também
  com nota.
- **Save.** O host grava pelo nome a entrada, a cena salva e o Farore's Wind. Acrescentar ou tirar um mod não
  leva mais o save para a cena de outro mod.

## Diferenças conhecidas em relação ao Unbound 0.8

- A base não exporta texto: a tabela vanilla faz o papel da camada de baixo. Para um mod, o efeito é o da
  SPEC. Só muda a precedência de `override/`: o JSON é aplicado depois dele.
- O transcodificador roda numa thread do pool de recursos da libultraship, um por vez, enquanto a thread
  do jogo espera o recurso. Dentro dele valem `has_file`, `read_file`, `read_file_layers` e
  `find_entrance`. O comentário de `oot_resources.h` ainda diz "thread do jogo" e será corrigido na
  próxima mudança de layout do SDK.

### Prelude e arquivos de mapas antigos

O `oot-unbound.o2r` gerado pelo Link-Span é uma camada de cenas para este host. Embora tenha o mesmo nome,
não é o arquivo base completo produzido pelo executável oficial SoH: Unbound. O Prelude verifica a
proveniência do arquivo e recusa o conversor `linkspan-unbound-converter 1`. Para usar o Prelude, gere a
base com [SoH: Unbound 9.2.3-unbound0.8](https://github.com/roborich/Shipwright/releases/tag/9.2.3-unbound0.8)
a partir do seu próprio `oot.o2r` ou importe sua ROM diretamente no Prelude e escolha Unbound. Não altere
apenas o nome ou o campo `source.converter`: faltam recursos da base completa.

Um ZIP de distribuição que contenha um `.o2r` precisa ser extraído antes de ser adicionado no botão
**Add override .o2r or .otr** do editor. A importação no editor só comprova que ele consegue ler e
mostrar os recursos; não comprova que a alteração será usada pelo jogo. Com uma base Unbound montada,
as cenas vanilla são lidas de `scenes/<cena>/scene.json`: substituições binárias em
`scenes/shared/<cena>_scene/` não alteram essas cenas. Uma cena nova também precisa de uma entrada em
`unbound/scenes.json` para ser acessível no jogo. Veja a [SPEC do Unbound, §§1 e 7](https://github.com/roborich/Shipwright/blob/unbound/unbound-docs/SPEC.md).
Para usar substituições binárias de cenas vanilla sem conversão, abra o `oot.o2r` normal no Prelude e
adicione os `.o2r` como camadas; isso executa o SoH normal no navegador. Uma cena nova continua precisando
de um caminho de entrada próprio no jogo.

## Ferramenta de linha de comando

`tools/linkspan_unbound_convert.exe`:

```
linkspan_unbound_convert <pasta-extraida> <saida.o2r>   # converte recursos extraídos de um oot.o2r
linkspan_unbound_convert --check <pasta-de-assets>      # valida documentos como o framework faria
```

O `--check` passa cada `.json` pelo mesmo caminho do jogo (merge de uma camada, transcodificação, leitor de
texto, registro e manifesto) e termina com código 1 se algum for recusado. Nomes de entrada não são
resolvidos fora do jogo.

## Schemas JSON

O pacote traz em `schemas/unbound/` um JSON Schema (draft 2020-12) para cada documento do formato 2:
`unbound.json`, cena, sala, colisão, paths, textos, registro de cenas (`unbound/scenes.json`) e tipo de ator
(`unbound/actors/<nome>.json`). Eles seguem o que este leitor aceita; onde a SPEC do Unbound é mais
permissiva (inteiros como string, `horse` como 0/1), vale o leitor. Cada schema descreve uma camada, não o
resultado da mescla: camadas parciais e as diretivas `$replace`/`$order` passam.

```text
python schemas/validate_schemas.py --root <pasta com unbound.json>
```

O validador precisa do pacote `jsonschema` do Python. Passar no schema é prova só de estrutura: não confere
nomes registrados, recursos do jogo, `collision.bin` nem limites que dependem de outro documento.

## Funções nativas (para o `main.lua`)

| Função | Quando | Resultado |
|---|---|---|
| `ready` | `game.ready` | base, registro, conflitos entre mods e referências de recurso com as camadas atuais (chamar de novo refaz) |
| `apply_text` | primeiro `game.frame` | texto; o SoH só carrega as tabelas de mensagens depois do `game.ready` |
| `unbound_report` | a qualquer momento | estado da base, contagem de documentos e as últimas notas |
| `load_scene_registry` | compatibilidade 0.2/0.3 | só o registro |
| `room_report` | compatibilidade 0.3 | salas vistas pelo patch de atores |

O serviço C `linkspan.unbound.json_factory` v1 (`include/linkspan/unbound/json_factory.h`) continua como na
0.1.0.

## Compilar e testar

```powershell
$sdk = (Resolve-Path ..\..\..\build\x64\native-sdk\Release\OotNativeSdk.cmake).Path
cmake -S . -B ..\..\..\build\unbound-scene-native -A x64 -DOOT_NATIVE_SDK="$sdk"
cmake --build ..\..\..\build\unbound-scene-native --config Release
ctest --test-dir ..\..\..\build\unbound-scene-native -C Release --output-on-failure
```

Na raiz da worktree do host:

- `tools/package-unbound-framework.ps1` gera o `.shipmod` único do framework (DLL, Lua, ferramenta,
  header, documentação e licenças), com SHA-256 e relatório de conteúdo;
- `tools/package-unbound-scene-demo.ps1` gera as demos de cena e de atores;
- `tools/package-unbound-e-fixtures.ps1 -BaseArchive <oot-unbound.o2r>` gera as fixtures da fase E. Elas
  saem da base local e contêm dados do jogo, então ficam em `build/` e não são publicadas.
