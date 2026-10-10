[English](README.md) · **Português (Brasil)**

# SDK nativo OoT do Link-Span

Compile um mod nativo sem depender do checkout do host. Este kit fornece headers públicos, configuração CMake relocável, fontes de exemplo e ferramentas de pacote para **Shipwright OoT em Windows x64 / Release**. Extraia o SDK fora da instalação jogável; não instale o ZIP do SDK em `mods/`.

## Leia primeiro

Comece pelo [guia de autor](AUTHOR-GUIDE.pt-BR.md), depois consulte o [contrato nativo](OOT-NATIVE-CONTRACT.pt-BR.md) e a [referência de serviços](SERVICES.pt-BR.md). [linkspan-sdk.json](linkspan-sdk.json) identifica a release/build correspondente. O kit não inclui executável do host, bibliotecas de importação, ROM, archives locais de assets do jogo nem saves.

## Conteúdo do ZIP

| Caminho | Conteúdo |
| --- | --- |
| [linkspan-sdk.json](linkspan-sdk.json) | Release, layout ID completo, SHA-256 do host, compilador, configuração e limitações. |
| [checksums.sha256](checksums.sha256) | SHA-256 de todos os demais arquivos preparados, incluindo metadados. |
| `include/host/include/`, `include/host/` | Headers do host/jogo alcançados pelo percurso de dependências de include; não é a árvore inteira de fontes. |
| `include/oot-native/` | Todos os headers iniciais `oot_*.h` e suas dependências resolvidas. |
| `include/shiplua/native/` | ABI C do provider e dependências alcançadas a partir dela. |
| [include/linkspan/nei/nei_items.h](include/linkspan/nei/nei_items.h) | Header do serviço de itens NEI e suas dependências. |
| `include/lus/` | Headers do libultraship alcançados como dependências. |
| [include/oot_layout_id.h](include/oot_layout_id.h) | Layout ID completo gerado pelo build correspondente do host. |
| [lib/cmake/LinkSpanOotSdk/LinkSpanOotSdkConfig.cmake](lib/cmake/LinkSpanOotSdk/LinkSpanOotSdkConfig.cmake) | Pacote Release relocável que exporta `LinkSpan::OotSdk`. |
| [OotNativeSdk.cmake](OotNativeSdk.cmake) | Include de compatibilidade relocável para o config do pacote. |
| [examples/provider-example/README.md](examples/provider-example/README.md) | Probe mínimo: CMakeLists.txt, provider.cpp, manifest.toml, main.lua e README.md. |
| [examples/example/CMakeLists.txt](examples/example/CMakeLists.txt) | Arquivos .cpp/.h/.lua/.toml/.md da raiz do DMR e CMake gerado; assets do HUD omitidos. |
| [examples/heart-seeds/CMakeLists.txt](examples/heart-seeds/CMakeLists.txt) | nei_demo.cpp, main.lua e manifest.toml/CMakeLists.txt gerados; exige NEI. |
| `docs/` | Cópias de writing-mods(.en), native-providers, shipmod-cli(.en), manifest-validator(.en), host-integration(.en), Markdown de API/arquitetura e oot-native-sdk-source.md. |
| `docs/rfcs/` | 0021-native-hooks.md, 0023-native-escape-hatch.md e 0024-native-crash-attribution.md. |
| `tools/` | shipmod.py, LinkSpanPackaging.psm1, package-linkspan-mod.ps1, linkspan-zip.ps1 e scan-protected-content.py. |
| [LICENSE](LICENSE), [NOTICE.md](NOTICE.md), [licenses/libultraship-LICENSE.txt](licenses/libultraship-LICENSE.txt) | Dedicação CC0, avisos por componente e licença MIT do libultraship. |

A raiz do SDK também contém README.md/README.pt-BR.md, AUTHOR-GUIDE.md/AUTHOR-GUIDE.pt-BR.md, OOT-NATIVE-CONTRACT.md/OOT-NATIVE-CONTRACT.pt-BR.md e SERVICES.md/SERVICES.pt-BR.md. Os diretórios de headers contêm somente o fechamento de dependências selecionado pelo script.

## Requisitos

Use Windows x64, CMake 3.26+, Python 3.11+ para a validação estrutural do CLI incluído e a **versão exata do compilador MSVC em `compiler.version`** de [linkspan-sdk.json](linkspan-sdk.json). Use um Developer PowerShell x64. O config recusa outra versão do compilador, compilador diferente do MSVC ou destino diferente de Windows/x64. Os exemplos usam C++20 e Release `/MT` (`MSVC_RUNTIME_LIBRARY MultiThreaded`). Preserve as definições e opções de compilação do SDK.

## Build rápido

Supondo que a pasta extraída se chame `LinkSpan-OoT-SDK`:

```powershell
$sdk = (Resolve-Path .\LinkSpan-OoT-SDK).Path
cmake -S "$sdk/examples/provider-example" -B .\probe-build -A x64 "-DCMAKE_PREFIX_PATH=$sdk"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build .\probe-build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
```

A saída do probe fica em `probe-build/mod/`. Continue com validação, pacote e verificações no jogo no [guia de autor](AUTHOR-GUIDE.pt-BR.md). `LinkSpan::OotSdk` fornece headers/opções; não vincula ao host.

## Exemplos e leitura complementar

O probe confere layout e tamanhos de structs do engine sem reter ponteiros de gameplay. O DMR é um exemplo de compilação sem suas texturas do HUD; não é um pacote jogável completo. Heart Seeds é incluído pelo script atual e seu manifesto exige `linkspan.nei` na versão `>=0.3.0 <0.4.0`. O script falha se os fontes dele estiverem ausentes.

Outros exemplos ficam em [link-span-examples](https://github.com/BaiterYamato/link-span-examples) (`lua/`, `oot-native/`, `oot-native/unbound/`, `tools/`). Consulte o [guia de modders do core](https://github.com/BaiterYamato/link-span/blob/main/docs/native-mods.md) e o [guia do host Shipwright](https://github.com/BaiterYamato/Shipwright/blob/agent/OOT-UPSTREAM-001-78dc6d970-integration/docs/native-mods.md).

## Licença

O trabalho próprio do Link-Span está em domínio público pela CC0 1.0: [LICENSE](LICENSE). Isso não concede direitos sobre todos os componentes. [NOTICE.md](NOTICE.md) identifica os headers do host/jogo e do libultraship; estes últimos mantêm a [licença MIT](licenses/libultraship-LICENSE.txt).
