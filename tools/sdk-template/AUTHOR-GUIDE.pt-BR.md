# Guia de autor — OoT

Use Python 3.11+, CMake 3.26+ e MSVC Windows x64 **da versão registrada em
linkspan-sdk.json**. O config verifica o compilador exato, pois o layout depende
dele. Escolha o toolset correspondente (`-T version=...` quando necessário).
O exemplo usa C++20 e `/MT` Release. Não copie defines de um host diferente.

## Compilar fora do checkout

Extraia o SDK e abra Developer PowerShell/Prompt com MSVC x64. Caminhos abaixo são
exemplos de diretórios escolhidos pelo autor, não dependências do checkout:

```powershell
$sdk = (Resolve-Path .\LinkSpan-OoT-SDK).Path
cmake -S "$sdk/examples/provider-example" -B .\probe-build -A x64 "-DCMAKE_PREFIX_PATH=$sdk"
if ($LASTEXITCODE -ne 0) { throw 'Configure falhou' }
cmake --build .\probe-build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build falhou' }
python "$sdk/tools/shipmod.py" validate .\probe-build\mod
python "$sdk/tools/shipmod.py" doctor .\probe-build\mod --host .\jogo\soh.exe
& "$sdk/tools/package-linkspan-mod.ps1" -Source .\probe-build\mod -OutputPath .\SDK-Probe.shipmod
```

`shipmod.py validate` usa uma verificação estrutural básica se o validador C++
canônico não estiver disponível. Isso não prova todos os contratos do manifest.
`doctor <mod> --host <exe>` confere PE/exports/imports/ABI/fingerprints sem carregar
o provider; `doctor` sem caminhos é voltado ao repositório completo e pode apontar
schemas/codegen ausentes neste recorte. **Não existe subcomando `pack` nesta versão**:
use o script PowerShell incluído. `shipmod.py test` precisa do runner C++ externo,
selecionado por `SHIPLUA_MOD_TEST_RUNNER`; o bundle não entrega esse binário.

## Autor e runtime

Leia `docs/writing-mods.md`, `docs/native-providers.md`, `docs/shipmod-cli.md` e
`docs/manifest-validator.md`. Um `.shipmod` é ZIP com `manifest.toml` na raiz e
`provider/<nome>.dll` no caminho de `[provider].win64`. O empacotador cria entradas
com `/`, ordenação ordinal e data fixa; não use `Compress-Archive` no PS 5.1.

Providers exportam `ShipNative_Query` com linkage C e a convenção
`SHIP_NATIVE_CALL`. Recebem tabelas no init, sem link com `soh`, `shiplua`, Lua ou
libultraship. O target `LinkSpan::OotSdk` é INTERFACE e fornece includes/defines;
as únicas libs implícitas do probe são as do toolchain C++/Windows.

Consulte serviços por nome, versão e tamanho. Verifique layout completo e sizeof
antes de usar structs do jogo. Não guarde PlayState/Player/Actor entre cenas.
Uma DLL executa dentro do processo: use apenas providers de origem confiável.

Para DMR/shovel, configure `examples/example` ou `examples/shovel-demo` com o
mesmo prefixo. O manifesto gerado inclui o SHA do host do bundle; confirme seu
soh.exe. O DMR precisa dos assets/procedimento da versão original para gameplay;
compilar suas fontes aqui não entrega um pacote funcional completo. Item-demo
requer manifesto e revisão de recursos próprios antes de `pack`.

## Instalar, atualizar e provar

Com o jogo fechado, copie somente o pacote do mod em `mods/`, reinicie e confirme
`loaded mod`/ausência de rejeições no log do host correspondente. Faça backup do
pacote anterior e dos saves antes de atualizar; restaure-os para rollback de esquema.
ABI 1.3/escape hatch exige `host_fingerprints` e `soh.symbols` do executável exato.
Layout igual não substitui essa checagem. Mod desligado pelo boot guard aparece
em `mods/.shiplua-disabled`; preserve pacote e saves durante a recuperação.

Uma aprovação de SDK exige configure/link externo, `dumpbin`, validate/doctor/pack,
hash inalterado do host e depois prova de init/shutdown no jogo. O probe incluído
é inicial, não substitui o pacote nativo de conformidade completo do §17.
