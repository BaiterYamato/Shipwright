# Link-Span OoT SDK — Windows x64 / Release

Leia [o guia em português](AUTHOR-GUIDE.pt-BR.md), [o guia em inglês](AUTHOR-GUIDE.en.md)
e [o contrato do OoT](OOT-NATIVE-CONTRACT.md). Versão, layout, toolchain e fingerprint
do host ficam em `linkspan-sdk.json`; hashes internos em `checksums.sha256`.

O SDK fornece headers, config CMake relocável e fontes de exemplo. Não contém o
jogo, libs de importação do host, ROM, archives de assets locais ou saves.
Descompacte numa pasta separada da instalação jogável; não coloque este ZIP em `mods/`.

| Exemplo | O que é |
|---|---|
| `examples/provider-example` | probe mínimo: confere serviço, layout e `sizeof` no init |
| `examples/example` | Dynamic Movement Remake (as texturas do HUD vêm no pacote do mod, não aqui) |
| `examples/heart-seeds` | add-on do NEI com `linkspan.nei.items` (exige o NEI instalado) |

Mais demos (Shovel do NEI, itens, hooks, save, câmera, Unbound) estão em
[link-span-examples](https://github.com/BaiterYamato/link-span-examples).

Os três configuram e linkam fora do checkout do host, só com este SDK no
`CMAKE_PREFIX_PATH`, e o probe carrega no jogo. Use o MSVC da versão gravada em
`linkspan-sdk.json`: o config recusa outra, porque o layout depende do compilador.

Licença: o trabalho do Link-Span neste SDK está em domínio público pela CC0 1.0
(`LICENSE`); os headers do Shipwright e do libultraship mantêm os próprios termos
(`NOTICE.md`).
