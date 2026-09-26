# Not Enough Items (Link-Span)

Coremod `linkspan.nei`. Traz os itens do fork [skijer/Not-Enough-Items](https://github.com/skijer/Not-Enough-Items)
(commit `c29262b`) para o Ship of Harkinian com Link-Span, sem recompilar o jogo:

- página extra do inventário (o L troca de página no menu de itens);
- itens com get-item, modelo e texto próprios;
- posse, quantidade e nível no save;
- serviço `linkspan.nei.items` para outros mods criarem itens.

## Instalação

1. Tenha o Ship of Harkinian com o overlay do Link-Span instalado. O nome do pacote traz o layout id do host
   (`layout-xxxxxxxx`), e ele precisa ser igual ao do `soh.exe` instalado.
2. Copie o `.shipmod` para a pasta `mods` do jogo, sem descompactar.
3. Abra o jogo. No log (`logs/Ship of Harkinian.log`) aparece uma linha `nei-core:` com o estado do coremod.

O pacote traz o SHA-256 do `soh.exe` para o qual foi compilado:

- **Com esse executável:** o código de itens do fork roda pelo escape hatch (`fork: ativo`).
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

## Randomizer e Anchor

- **Randomizer:** com a opção de seed "Link-Span Mod Items" ligada, os itens do NEI entram no pool, pelo
  serviço `linkspan.oot.randomizer` do host. Sem o mod carregado, essas checks dão uma rupia azul.
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
