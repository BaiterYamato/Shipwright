# Not Enough Items (Link-Span)

Coremod `linkspan.nei`. Traz os itens do fork [skijer/Not-Enough-Items](https://github.com/skijer/Not-Enough-Items)
(commit `c29262b`) para o Ship of Harkinian com Link-Span, sem recompilar o jogo:

- página extra do inventário (o L troca de página no menu de itens);
- itens com get-item, modelo e texto próprios;
- posse, quantidade e nível no save;
- serviço `linkspan.nei.items` para outros mods criarem itens.

## Instalação

1. Tenha o Ship of Harkinian com o overlay do Link-Span instalado. O nome do pacote traz o layout id do host
   (`layout-xxxxxxxx`), e ele precisa ser igual ao do `soh.exe` instalado. Instale também o `soh.symbols`
   gerado para esse mesmo executável ao lado de `soh.exe`.
2. Copie o `.shipmod` para a pasta `mods` do jogo, sem descompactar.
3. Abra o jogo. No log (`logs/Ship of Harkinian.log`) aparece uma linha `nei-core:` com o estado do coremod.

## Testar itens pela interface

A partir do NEI 0.3.10, com um arquivo aberto, vá a **Mods > Not Enough Items > NEI Items**. Os itens
aparecem numa grade de ícones: colorido e com borda branca significa ativo no
save; escurecido significa desativado. Clique no ícone para ativar ou desativar.
Ao desativar um item equipado, ele também sai do botão C. Pressione **L** na
página de itens do jogo para chegar à página do NEI. A grade inclui itens de
add-ons registrados nesta sessão. O resultado aparece em **Status** e no log.

## Itens e habilidades na versão 0.3.16

### Animações e efeitos

- **Roc's Cape**: o segundo salto usa a pirueta nativa de OoT quando as animações de MM não estão disponíveis. O primeiro salto, as alturas, a aterrissagem e o limite de um salto extra são preservados.
- **Winter**: o contato com a água congelada é verificado no Player atual, inclusive na primeira aterrissagem. Ondulações, respingos e passos de água são suprimidos sobre o gelo; água normal e natação abaixo da superfície mantêm os efeitos originais.

### Controles do inventário com Dynamic Movement Remake 0.2.14

- Toque A e solte para abrir os três slots da hotbar. Selecione com qualquer analógico ou D-pad e confirme com A; B cancela.
- Segure A por 0,4 segundo na célula para abrir a troca de variantes (rods, canes, runas, garrafas e outros seletores NEI). Soltar esse A não equipa o item.
- Nos seletores de variantes, use qualquer analógico ou D-pad. Toque A para fechar a escolha.
- Fora do inventário, segure R, escolha um slot com qualquer analógico ou D-pad e solte R para selecioná-lo no ZR.
- O D-pad funciona nos seletores mesmo com a navegação normal por D-pad no inventário desativada.

- **Sheikah Slate**, **Rod of Seasons** e **Phantom Hourglass** entram no registro normal: podem ser
  concedidos pela grade ou por `give_item linkspan skijer.nei.sheikah_slate` (troque o último nome por
  `rod_of_seasons` ou `phantom_hourglass`). Aparecem na página NEI e usam IDs persistentes nos botões C.
- **Mods > Not Enough Items > Modes & Abilities** permite conceder/remover individualmente seis modos
  da Elemental Wand, cinco runas da Slate, quatro estações e seis habilidades das canes. Não altera
  os medalhões nem as flags de progresso do jogo. O modo padrão do randomizer continua exigindo medalhões.
- A seleção de tipos da Cane usa as funções originais de contagem e navegação, em vez dos stubs.
- O ícone dos botões C e do seletor DMR acompanha a runa/modo selecionado. Os pixels I5 da Pictograph
  passam a persistir em `nei.state`, junto dos demais campos.
- As rotinas de uso de Slate, Hourglass, Seasons e Wand voltam a executar a cada atualização de Link;
  a cola do port restaura as chamadas que existiam no Player do fork e faltavam no host.
- A bomba remota descarta a referência antiga ao trocar ou recarregar a cena, inclusive quando o
  endereço do ator é reutilizado. O rastro do Hourglass usa geometria do frame e descarta segmentos
  muito curtos para evitar que pequenas oscilações de um NPC preencham a tela.

### Backends e controles adicionados

- **Sheikah Sensor**: configure até cinco desejos na aba própria de Mods. Consulta a seed randomizer
  atual e ignora checks já coletados/salvos. A confirmação original custa permanentemente um Heart
  Container; sem dica disponível, sem seed randomizer ou ao cancelar, a cobrança não ocorre.
- **Rod of Seasons**: adaptadores originais de água, lago, Water Temple, areia, clima e magic beans
  de primavera. A colisão ampla do Unbound usa coordenadas de 32 bits; removendo a Rod, a água volta
  ao estado normal. Flores Deku douradas de verão dependem da expansão de formas, ainda ausente.
- **NEI Equipment**: concede/remove os equipamentos e equipa quando a idade permite. Para trocar
  entre peças já possuídas, use L na página Equipment do inventário. A Four Sword é infantil:
  segure R+B para invocar clones e L para selecionar a formação. A Kite Shield permite surf:
  pressione R no ar; A salta, B gira e R+B desmonta.
  A Four Sword precisa de um escudo vanilla possuído/equipado para erguer a defesa e carregar
  os clones. Com DMR ativo, a combinação R+B desses equipamentos tem prioridade sobre o
  ataque comum; os demais equipamentos mantêm a regra normal do controle.
- **Bottles**: adicionar uma garrafa ativa as duas rodas de quatro posições nos slots 1 e 2,
  preservando seus conteúdos atuais. Slots 3 e 4 comuns permanecem disponíveis. Bottomless Bottle
  usa o slot 4, mantém o conteúdo e o contador original; desativar conserva a garrafa/conteúdo.
  Garrafas com o mesmo conteúdo continuam sendo posições distintas. A rede e conteúdos exclusivos
  do MM permanecem fora deste port.

Código compilado, testes automatizados e teste em jogo são evidências diferentes. Consulte o
relatório da pasta jogável para os cenários efetivamente observados nesta entrega.

O pacote traz o SHA-256 do `soh.exe` para o qual foi compilado:

- **Com esse executável e o `soh.symbols` correspondente:** o código de itens do fork roda pelo escape hatch
  (`fork: ativo`). O log deve informar itens definidos; `definidos=0` e `fork: desligado` indicam que o NEI
  não inicializou os itens.
- **Com outro:** só o registro de itens funciona, e a linha do log mostra o fork desligado. O jogo segue
  normal nos dois casos.

## Componentes opcionais

Nenhum destes vai no pacote.

| Componente | O que muda | Como obter |
|---|---|---|
| `nei-assets/nei-assets-core.o2r` | modelos, ícones e texturas dos itens | gerado no seu computador (abaixo) |
| `nei-assets-form.*`, `nei-assets-expansion.*` | formas e expansões | gerados pelo mesmo script; ainda sem uso nesta versão |
| ROM de Super Mario 64 | expansão Mario | não usada nesta versão; nunca é distribuída |

**Sem o `nei-assets-core.o2r`:** o coremod define só a Roc's Feather e a Roc's Cape, com ícone e modelo
provisórios. Os assets do fork não têm licença declarada, por isso não entram em nenhum pacote do Link-Span.

**Para gerar**, a partir de um checkout do host Link-Span com o commit do fork buscado
(`git fetch https://github.com/skijer/Not-Enough-Items c29262b`):

```text
python tools/build-nei-assets.py --repo . --packer build/x64/Release/soh-o2r-packer.exe \
    --base <oot.o2r> --base <soh.o2r> --fork-source build/nei-core-native/nei-fork/fork --out build/nei-assets
```

Copie `build/nei-assets/nei-assets-*.o2r` para `<jogo>/nei-assets/`. O coremod monta o que achar ali ao iniciar,
e o log mostra `assets: core+...`.

A partir da versão 0.3.6, o gerador substitui apenas `sSpinnerTex`, cuja imagem do fork é um padrão colorido
inadequado ao modelo, pelo material de bronze em `assets/spinner-bronze.otex`. O modelo e a movimentação do
Spinner continuam os do fork. O modelo da Deku Leaf e de outros itens montados depois da inicialização é
localizado pelo serviço de recursos ativo; a folha aparece na mão quando Link sopra e acima das mãos quando plana.
É necessário gerar novamente `nei-assets-core.o2r` e instalar um pacote NEI 0.3.6 ou mais recente.

Na versão 0.3.7, o Gust Jar volta a desenhar o cone e as fitas de vento do fork. O modo de sucção mostra
vento branco; ao soprar, a cor segue o elemento selecionado (gelo azul, por exemplo). Esse efeito usa os
recursos `object_nei_tornado` de `nei-assets-core.o2r`, portanto o pacote e os assets gerados localmente
precisam estar juntos na instalação.

## Randomizer e Anchor

- **Randomizer:** com a opção de seed "Link-Span Mod Items" ligada, os itens do NEI entram no pool, pelo
  serviço `linkspan.oot.randomizer` do host. Sem o mod carregado, essas checks dão uma rupia azul.
- **Console:** com um arquivo carregado, abra `Dev Tools > Console` e use
  `give_item linkspan skijer.nei.rocs_feather` para receber a Roc's Feather. O comando usa o nome registrado
  e funciona também em saves comuns; `give_item randomizer <id>` depende da associação feita pela seed.
- **Anchor:** numa sala que sincroniza itens e flags, o get-item de um item do NEI chega aos parceiros pelo nome
  do item. O estado do time leva os blocos `nei.items` e `nei.state`. Todos os clientes da sala precisam do
  Link-Span com este coremod.

## Criar um add-on

Um add-on é um mod nativo que usa `linkspan.nei.items` v1 (`include/linkspan/nei/nei_items.h`, que vem no
pacote).

No manifesto, declare a dependência:

```toml
[provider]
abi_version = "1.2"
win64 = "provider/meu_mod.dll"

[dependencies]
"linkspan.nei" = ">=0.3.0 <0.4.0"
```

No `Init` da DLL:

1. pegue o serviço:
   `runtime->get_service(ctx, LINKSPAN_NEI_ITEMS_SERVICE, LINKSPAN_NEI_ITEMS_VERSION, sizeof(NeiItemsV1))`;
2. chame `define_item` com um `NeiItemDefinitionV1`, com:
   - id `autor.mod.item`;
   - ícone RGBA32 32x32 por nível;
   - nomes em inglês, alemão e francês;
   - modelo e texto do get-item;
   - callbacks `use` e `received`;
3. no `Shutdown`, chame `remove_item`.

O NEI cuida do resto:

- botão C;
- desenho do contador;
- save (bloco `nei.items`, pelo id);
- get-item, com `give_item`;
- randomizer e Anchor.

O fork também reconhece quando um botão C contém um item nativo de outro mod.
Nesse caso, entrega o ID runtime original ao host para que o uso chegue ao dono
do item. Isso evita colisões entre os IDs lógicos da página NEI e os IDs
temporários do registro genérico OoT.

O exemplo completo está em `docs/examples/heart-seeds`:

- um item com quantidade e um upgrade;
- cada uso gasta uma semente e solta um coração;
- a bolsa sobe de nível no terceiro uso.

Compile contra os headers do SDK nativo OoT do mesmo layout id.

## Diagnóstico

A linha `nei-core:` do log mostra:

- itens definidos e possuídos;
- estado do fork, do randomizer, do kaleido e do inventário;
- arquivo carregado;
- assets montados;
- conexão do Anchor.

Ela só é escrita quando algo muda.
