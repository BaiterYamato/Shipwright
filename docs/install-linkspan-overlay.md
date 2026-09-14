# Instalar o Link-Span sobre o Shipwright 9.2.3

O pacote Windows é um overlay ZIP. Ele foi desenhado para ser extraído na raiz
de uma instalação nova da [release Shipwright 9.2.3](https://github.com/HarbourMasters/Shipwright/releases/tag/9.2.3),
substituindo os arquivos do host que precisam conhecer o Link-Span.

Desde `OOT-UPSTREAM-001`, o host do overlay é compilado a partir da `develop`
oficial `78dc6d970` com o Link-Span integrado. A instalação base continua sendo
a 9.2.3 oficial: a `develop` ainda se declara 9.2.3 e aceita o `oot.o2r` gerado
por ela, porque só pede nova extração quando a versão principal gravada no
arquivo muda.

## O que o overlay altera

- substitui `soh.exe` pelo host compilado com o Link-Span;
- substitui `soh.o2r` pela versão gerada junto com esse executável;
- substitui `gamecontrollerdb.txt` pela base de controles baixada pelo build;
- adiciona em `assets` os `.yml` do extrator Torch da `develop` (`config.yml` e
  uma pasta por versão de ROM). A 9.2.3 extraía com XML e ZAPD; esses arquivos
  antigos ficam na pasta sem uso, e nenhum deles é sobrescrito;
- adiciona a pasta `mods` e, quando solicitado no empacotamento, mods de teste;
- adiciona metadados, instruções e checksums do pacote.

O overlay não inclui nem substitui `oot.o2r`, ROMs, saves ou arquivos pessoais.
`soh.exe` e `soh.o2r` devem sempre ser distribuídos e instalados como o mesmo
par. Misturar o executável Link-Span com o `soh.o2r` oficial causa falha durante
o carregamento da interface.

A `develop` registra o `ConfigVersion7Updater`, que migra `shipofharkinian.json`
para um formato que a 9.2.3 oficial não conhece. Quem já usou a pasta deve
guardar esse arquivo e a pasta `Save` antes de aplicar o overlay.

## Instalação para o jogador

1. Extraia `SoH-Ackbar-Delta-Win64.zip` da release 9.2.3 em uma pasta nova.
2. Execute o Shipwright uma vez e conclua a geração normal do seu `oot.o2r`.
3. Feche o jogo.
4. Se a pasta já foi usada, copie `shipofharkinian.json` e `Save` para outro
   lugar.
5. Extraia `LinkSpan-Shipwright-9.2.3-Upstream-78dc6d970-Win64-overlay.zip`
   nessa mesma pasta e confirme a substituição dos arquivos.
6. Coloque mods `.zip` ou `.shipmod` diretamente em `mods` e execute `soh.exe`.

O pacote do mod precisa ter `manifest.toml` na raiz. Um mod nativo pode carregar
uma DLL própria declarada no manifesto; por isso ele tem a mesma capacidade de
acesso ao processo e o mesmo risco de crash que um mod nativo tradicional.
Core extensions usam `kind = "core_extension"`, `load_phase = "pre_game"` e
provider ABI 1.1. Elas carregam antes dos mods comuns e podem publicar serviços
versionados para outros ZIPs sem recompilar novamente o Shipwright.

Mods nativos que consultam `linkspan.oot.engine` ou `linkspan.oot.movement`
comparam o `LINKSPAN_OOT_LAYOUT_ID` com o do host e são recusados quando ele
difere. Como a `develop` mudou headers de `soh/include`, DLLs compiladas para os
hosts anteriores precisam ser recompiladas contra o SDK deste build. O id do
host fica em `linkspan-overlay.json`, no campo `linkSpan.ootLayoutId`.

## Compilar o host

O host compila o core Link-Span a partir de `LINKSPAN_SDK_SOURCE_DIR`, cujo
default é o submódulo `extern/ship-lua`. Desde `OOT-UPSTREAM-001`, o submódulo
aponta para o core dos hosts OoT: o commit `4244f7d` da branch
`agent/NATIVE-001-provider-runtime` de `BaiterYamato/link-span` (`ship-lua.git`
é o nome antigo desse repositório). Um clone com submódulos compila sem ajuste
de cache. Só aponte `LINKSPAN_SDK_SOURCE_DIR` para outra pasta, como a worktree
`NATIVE-001`, ao desenvolver o próprio core: outro checkout pode trazer outra
descoberta de pacotes, outro ABI de provider e outro layout id.

O `CMakeLists.txt` raiz prefixa com `shiplua_` os símbolos do miniz do Link-Span:
gera `build/x64/ship-lua/shiplua_miniz_prefix.h` e o injeta com `/FI`. O Torch da
`develop` embute outro miniz, o 9.1.15, com os mesmos nomes em C e antes na linha
de link. Sem o prefixo, o `soh.exe` ligava o ship-lua a essa cópia; o host abria
normalmente, mas carregava 0 mods em ZIP, sem nenhuma linha de rejeição.

O mesmo arquivo declara o tinyxml2 antes do Torch. O Torch baixa o tinyxml2
10.0.0 com `OVERRIDE_FIND_PACKAGE`, e a libultraship liga essa cópia. O vcpkg
também instala o tinyxml2 (11.0.0), e o include dele vem antes do baixado no
Torch, na libultraship e no soh: todos compilavam com o header 11 e ligavam a
lib 10. Entre as duas versões, o `DynArray` trocou `int` por `size_t`, e o
layout de `XMLPrinter` e `XMLDocument` mudou. Com
`gDeveloperTools.ResourceLogging` ligado, `XMLPrinter::CStr()` devolvia o
próprio texto como ponteiro, e o host caía no primeiro carregamento de cena. A
declaração reaproveita o pacote do vcpkg, e o configure falha se o vcpkg tiver
o header sem o pacote CMake.

```powershell
git submodule update --init --recursive
cmake -S . -B build/x64   # mais as opções de docs/BUILDING.md
cmake --build build/x64 --config Release --target soh -- /m:1
```

O `LINKSPAN_OOT_LAYOUT_ID` sai em `build/x64/native-sdk/oot_layout_id.h` e muda
junto com a fonte do core. O cálculo usa os bytes dos headers: com
`core.autocrlf=true`, o mesmo commit do core sai com CRLF num checkout e com LF
em outro, e o id muda. Compile os mods nativos contra o
`build/x64/native-sdk/Release/OotNativeSdk.cmake` do mesmo build que vai no
overlay.

## Gerar o overlay

O script roda no Windows PowerShell 5.1 e no PowerShell 7:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/package-linkspan-overlay.ps1 `
  -HostExecutable x64/Release/soh.exe `
  -HostResources build/x64/soh/soh.o2r `
  -ExtractorAssets soh/assets/yml `
  -ControllerDatabase build/x64/gamecontrollerdb.txt `
  -LayoutIdHeader build/x64/native-sdk/oot_layout_id.h `
  -HostBase "HarbourMasters/Shipwright develop 78dc6d970 + Link-Span core 4244f7d" `
  -ExampleMod build/distribution/Dynamic-Movement-Remake-0.2.8-layout-16ff0d5a.zip `
  -OutputPath build/distribution/LinkSpan-Shipwright-9.2.3-Upstream-78dc6d970-Win64-overlay.zip
```

O script cria o ZIP por um diretório temporário único, confere que o layout id
do SDK está dentro do executável, gera `linkspan-overlay.json` e
`checksums.sha256`, grava as entradas do ZIP com `/` e não lê nem empacota
`oot.o2r`. `-ExampleMod` aceita vários pacotes.

## Validar o overlay

`tools/package-linkspan-mod.ps1` empacota a pasta `mod` que o CMake de um mod
nativo gera: `manifest.toml` na raiz e entradas com `/`.

`tools/test-linkspan-overlay.ps1` faz a prova limpa:

1. copia uma instalação oficial que já tem `oot.o2r`;
2. extrai o overlay por cima;
3. confere o `checksums.sha256` e a preservação, byte a byte, dos arquivos do
   usuário;
4. com `-ResourceLogging`, liga `gDeveloperTools.ResourceLogging` na cópia;
5. roda o jogo numa sessão da skill `soh-runtime-playtest`;
6. falha se o log não mostrar a quantidade esperada de mods carregados, se
   registrar exceção do `CrashHandler` ou se, com `-ResourceLogging`, não tiver
   comandos de cena em XML.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/test-linkspan-overlay.ps1 `
  -Overlay build/distribution/LinkSpan-Shipwright-9.2.3-Upstream-78dc6d970-Win64-overlay.zip `
  -Base <instalação 9.2.3 limpa com oot.o2r> `
  -Smoke <pasta nova para a prova> `
  -Evidence <pasta de evidências> `
  -PlaytestScripts <scripts da skill soh-runtime-playtest> `
  -ExpectedMods 1 -ResourceLogging -NoCapture
```

A contagem de mods é o que pega regressão de link: com o miniz errado, o host
abre e passa nos testes ROM-free, mas carrega 0 mods. O `-ResourceLogging` pega
a mistura de tinyxml2: o host carrega os mods e cai no primeiro carregamento de
cena.
