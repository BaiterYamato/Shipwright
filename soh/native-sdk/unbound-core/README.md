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

## Funções nativas (para o `main.lua`)

| Função | Quando | Resultado |
|---|---|---|
| `ready` | `game.ready` | base e registro com as camadas atuais (chamar de novo refaz) |
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
