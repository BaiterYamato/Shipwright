# Unbound: layout de colisão ampliado

## Objetivo

O recorte `OOT-UNBOUND-003B` remove os limites de 16 bits da colisão herdados do N64. Cenas e atores dinâmicos com
mais de 8.192 vértices ou 65.535 polígonos passam a ser representáveis, e as tabelas de busca deixam de depender do
orçamento fixo de bytes da arena da cena.

## Capacidades

| Estrutura | Antes | Agora |
| --- | ---: | ---: |
| Índice de vértice em `CollisionPoly` | 13 bits | 29 bits |
| Flags de exclusão (`xpFlags`) e de esteira | 3 bits no topo do `u16` | 3 bits no topo do `u32` (bits 29 a 31) |
| `numVertices` e `numPolygons` do `CollisionHeader` | 16 bits | 32 bits |
| `SSNode::polyId` | 16 bits com sinal | 32 bits com sinal |
| Índices de nó (`SSNode::next`, `SSList::head`, `SS_NULL`) | 16 bits (`0xFFFF`) | 32 bits (`0xFFFFFFFF`) |
| `DynaLookup::polyStartIndex` e `BgActor::vtxStartIndex` | 16 bits | 32 bits |
| Nós da colisão estática | o que sobrasse do `memSize` da cena | `max(2 × polígonos, 4.096)`, crescendo sob demanda |
| Nós da colisão dinâmica | 1.000 | 16.384, crescendo sob demanda |
| Polígonos e vértices dinâmicos | 512 (1.024 com o ajuste do SoH) | 16.384, fixos |
| Grade de busca estática | fixa por cena | igual até 16.384 polígonos; acima disso cresce pela raiz cúbica da contagem, até 64 por eixo |

`CollisionPoly` passou de 0x10 para 0x18 bytes. `SOH::CollisionPoly` e `SOH::CollisionHeaderData` espelham o layout do
runtime campo a campo, porque o jogo usa diretamente o ponteiro do recurso carregado.

## Compatibilidade com OTR v0

O formato binário continua gravando cada palavra de vértice como o `u16` do N64. O importador desempacota o índice de
13 bits e move os 3 bits de flag para o topo da palavra de 32 bits (`Unbound_UnpackLegacyVtxWord`), então os arquivos
`.otr` e `.o2r` existentes carregam sem regeneração.

O importador XML aceita duas formas:

- legada: `VertexA` e `VertexB` com a palavra empacotada do N64;
- ampliada: índices simples em `VertexA`, `VertexB` e `VertexC`, com `XpFlags` (0 a 7) e `Conveyor` como atributos
  separados. A presença de qualquer um dos dois atributos seleciona essa forma.

O XML também passou a gravar `numVertices` e `numPolygons` com 32 bits; antes os dois eram truncados para 16.

## Memória e ciclo de vida

- A tabela de busca, os nós estáticos, o `polyCheckTbl` e as listas de polígonos, vértices e nós dinâmicos saem do heap,
  e não mais da arena `THA` da cena. `Play_Destroy` chama `BgCheck_Free` depois de limpar os atores.
- O estado do jogo não vem zerado. `Play_Init` zera o `CollisionContext` antes de carregar a cena, para que
  `BgCheck_Free` só encontre `NULL` ou tabelas do heap.
- As tabelas de nós dobram de tamanho com `Unbound_GrowNodeCapacity`. O crescimento é recusado quando a capacidade
  passaria de 32 bits (estática), de `s32` (dinâmica) ou do tamanho endereçável em bytes. Os nós são endereçados por
  índice e `StaticLookup_AddPolyToSSList` recalcula o ponteiro do nó depois de cada inserção, então o `realloc` não
  deixa ponteiros pendurados.
- Se a tabela de nós dinâmica não puder crescer, o polígono fica fora da busca naquele frame, sem escrita fora do
  limite. Uma falha de crescimento da tabela estática é fatal (`LOG_HUNGUP_THREAD`), como a falta de memória do
  `BgCheck_Allocate` original.
- As listas de polígonos e vértices dinâmicos não crescem durante o jogo, porque atores guardam ponteiros diretos para
  `dyna.polyList` (`floorPoly`, `wallPoly`). Um `BgActor` que não caiba nas 16.384 entradas fica sem colisão naquele
  frame em vez de escrever além da tabela.
- `memSize` não limita mais nada e continua sendo calculado apenas para o log.

## Verificação incorporada

- `soh/soh/unbound/UnboundCollisionChecks.cpp` é compilado junto do host e contém `static_assert`s para:
  - largura de 32 bits das palavras de vértice, das contagens, dos índices de nó e dos inícios de lista dinâmica;
  - espelho campo a campo entre `CollisionPoly`/`CollisionHeader` e os tipos do recurso;
  - igualdade entre as macros `COLPOLY_*` e os helpers de `soh/soh/unbound/CollisionVertexWords.h`.
- `oot_unbound_collision_tests` roda sem ROM e verifica que:
  - as 65.536 palavras `u16` legadas mantêm índice, `xpFlags` e esteira depois do desempacotamento;
  - índices de 29 bits convivem com os 3 bits de flag;
  - a realocação de índices do `DynaPoly_ExpandSRT` passa de 13 bits sem perder flags;
  - o crescimento das tabelas de nós respeita índice, limite de 32 bits, limite `s32` e tamanho em bytes.

## Limites que continuam para os próximos recortes

- Os vértices continuam em `Vec3s`, então as coordenadas de colisão ficam entre -32.768 e 32.767.
- `SSNode::polyId` tem sinal: cada busca endereça no máximo 2³¹ - 1 polígonos.
- `BG_ACTOR_MAX` continua 50, e as listas de polígonos e vértices dinâmicos continuam com teto fixo.
- `numWaterBoxes`, o índice de tipo de superfície (`CollisionPoly::type`) e os dados de câmera continuam em 16 bits.
- O formato binário OTR v0 ainda grava palavras de 13 bits. Cenas com mais de 8.192 vértices exigem o XML ampliado ou
  uma versão nova do formato binário.
- Os headers de colisão mudaram, então o layout id muda: mods nativos compilados contra os headers anteriores são
  recusados e precisam ser recompilados.
