[CmdletBinding()]
param(
    [string]$ProviderDirectory = 'build\nei-core-native\mod\provider',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$JsonLicense = '..\shipwright-limpo\Shipwright\build\x64\vcpkg\installed\x64-windows-static\share\nlohmann-json\copyright',
    [string]$OutputDirectory = 'build\nei-core',
    [string]$HostExecutable = 'x64\Release\soh.exe',
    [string]$HostSymbols = 'x64\Release\soh.symbols',
    # O mod de exemplo como pacote próprio, para testar o serviço em jogo; o fonte dele já vai no .shipmod.
    [switch]$Demo
)

# NEI-016: o .shipmod único do coremod Not Enough Items (linkspan.nei). Leva:
# - DLL, Lua e o header público de add-ons;
# - guia de instalação e de add-ons, componentes opcionais e licenças;
# - o fonte do add-on de exemplo.
# Ao lado do pacote fica o relatório com SHA-256 de cada arquivo.
# O código de itens do fork NEI chama o soh.exe pelo escape hatch (ABI 1.3): o manifesto lista o SHA-256 do
# executável alvo, e em outro executável só o registro de itens funciona.
# Os assets do fork (modelos, ícones, texturas) não vão no pacote: não têm licença. Quem joga gera
# <jogo>/nei-assets/nei-assets-<componente>.o2r com tools/build-nei-assets.py (NEI-007), e o README explica.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-InputFile([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

. (Join-Path $PSScriptRoot 'linkspan-zip.ps1')

function New-DeterministicZip([string]$Source, [string]$Destination) {
    New-LinkSpanZip -Source $Source -Destination $Destination
}

$version = '0.3.17'
$coreDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_nei_core.dll') 'NEI core DLL'
$validatorExe = Resolve-InputFile $Validator 'Link-Span validator'
$jsonCopyright = Resolve-InputFile $JsonLicense 'nlohmann/json license'
$layoutText = Get-Content -LiteralPath (Resolve-InputFile $LayoutHeader 'Layout id header') -Raw
$layoutMatch = [regex]::Match($layoutText, 'LINKSPAN_OOT_LAYOUT_ID "([0-9a-f]{64})"')
if (-not $layoutMatch.Success) {
    throw "Layout id not found in $LayoutHeader"
}
$layout = $layoutMatch.Groups[1].Value.Substring(0, 8)
$fingerprint = (Get-FileHash -LiteralPath (Resolve-InputFile $HostExecutable 'Host executable') -Algorithm SHA256).Hash.ToLowerInvariant()
$symbolsFile = Resolve-InputFile $HostSymbols 'Host symbols'
$symbolsHeader = @(Get-Content -LiteralPath $symbolsFile -TotalCount 2)
if ($symbolsHeader.Count -ne 2 -or $symbolsHeader[0] -ne 'linkspan-symbols 1' -or
    $symbolsHeader[1] -ne "sha256 $fingerprint") {
    throw "soh.symbols não corresponde ao soh.exe ($fingerprint): $symbolsFile"
}

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path 'soh\native-sdk\nei-core'))
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputDirectory))
$staging = Join-Path $packageRoot 'staging'
$coreStage = Join-Path $staging 'nei'
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
foreach ($directory in @('provider', 'include\linkspan\nei', 'docs\examples\heart-seeds')) {
    [System.IO.Directory]::CreateDirectory((Join-Path $coreStage $directory)) | Out-Null
}
Copy-Item -LiteralPath $symbolsFile -Destination (Join-Path $packageRoot 'soh.symbols') -Force

$coreManifest = @'
id = "linkspan.nei"
name = "Not Enough Items (Link-Span)"
version = "__VERSION__"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]
kind = "core_extension"
load_phase = "pre_game"
description = "Not Enough Items items, an extra inventory page, and the linkspan.nei.items service for add-ons."

[provider]
abi_version = "1.3"
win64 = "provider/linkspan_nei_core.dll"
host_fingerprints = ["__FINGERPRINT__"]
'@
Write-Utf8NoBom (Join-Path $coreStage 'manifest.toml') $coreManifest.Replace('__VERSION__', $version).Replace(
    '__FINGERPRINT__', $fingerprint)
Copy-Item -LiteralPath (Join-Path $sourceRoot 'core-main.lua') -Destination (Join-Path $coreStage 'main.lua')
Copy-Item -LiteralPath $coreDll -Destination (Join-Path $coreStage 'provider\linkspan_nei_core.dll')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'include\linkspan\nei\nei_items.h') `
    -Destination (Join-Path $coreStage 'include\linkspan\nei\nei_items.h')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'README.md') -Destination (Join-Path $coreStage 'docs\README.md')

# Add-on de exemplo: só o fonte. Quem quiser o binário compila contra o SDK do mesmo layout id.
$demoManifest = @'
id = "linkspan.nei-demo"
name = "Not Enough Items Demo"
version = "__VERSION__"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.2"
win64 = "provider/linkspan_nei_demo.dll"

[dependencies]
"linkspan.nei" = ">=0.3.0 <0.4.0"
'@.Replace('__VERSION__', $version)
$example = Join-Path $coreStage 'docs\examples\heart-seeds'
Write-Utf8NoBom (Join-Path $example 'manifest.toml') $demoManifest
Copy-Item -LiteralPath (Join-Path $sourceRoot 'demo\main.lua') -Destination (Join-Path $example 'main.lua')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'demo\nei_demo.cpp') -Destination (Join-Path $example 'nei_demo.cpp')

# Componentes externos: descobertos na máquina de quem joga, nunca empacotados.
$components = [ordered]@{
    version = 1
    components = @(
        [ordered]@{
            id = 'nei-assets-core'
            path = 'nei-assets/nei-assets-core.o2r'
            required = $false
            source = 'tools/build-nei-assets.py, a partir de skijer/Not-Enough-Items c29262b'
            license = 'sem licença declarada: não redistribuível'
            without = 'só a Roc''s Feather e a Roc''s Cape, com ícone e modelo provisórios'
        },
        [ordered]@{
            id = 'nei-assets-form-*, nei-assets-expansion-*'
            path = 'nei-assets/nei-assets-<componente>.o2r'
            required = $false
            source = 'tools/build-nei-assets.py'
            license = 'sem licença declarada: não redistribuível'
            without = 'sem efeito nesta versão (formas e expansões ficam para o NEI-015)'
        },
        [ordered]@{
            id = 'sm64-rom'
            path = $null
            required = $false
            source = 'ROM do próprio usuário'
            license = 'nunca distribuída'
            without = 'sem efeito nesta versão (expansão Mario fica para o NEI-015)'
        }
    )
}
Write-Utf8NoBom (Join-Path $coreStage 'docs\components.json') ($components | ConvertTo-Json -Depth 4)

$notice = @"
# Licenças

- Código do Link-Span neste pacote (cola do coremod, registro, save, Lua, header e documentação): licença do
  projeto Link-Span.
- Código de itens do fork Not Enough Items (``skijer/Not-Enough-Items``, commit ``c29262b``), compilado na DLL: o
  fork não traz LICENSE próprio e segue a licença do Shipwright, de onde deriva (inventário NEI-001, §8).
- Nenhum asset do fork (modelos, ícones, texturas, sons) vai neste pacote: eles não têm licença declarada. Quem
  joga os gera no próprio computador (``docs/README.md``, ``docs/components.json``).
- Nenhum dado do jogo e nenhuma ROM vão neste pacote.

## nlohmann/json (compilado na DLL)

"@
Write-Utf8NoBom (Join-Path $coreStage 'docs\LICENSES.md') ($notice + (Get-Content -LiteralPath $jsonCopyright -Raw))

Add-Type -AssemblyName System.IO.Compression.FileSystem
$package = Join-Path $packageRoot "LinkSpan-NEI-$version-layout-$layout.shipmod"
New-DeterministicZip $coreStage $package
& $validatorExe $package
if ($LASTEXITCODE -ne 0) { throw "Pacote NEI inválido: $package" }

$contents = Get-ChildItem -LiteralPath $coreStage -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($coreStage.Length + 1).Replace('\', '/')
        size = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
}
$report = [ordered]@{
    package = [System.IO.Path]::GetFileName($package)
    size = (Get-Item -LiteralPath $package).Length
    sha256 = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
    layout = $layoutMatch.Groups[1].Value
    host_fingerprint = $fingerprint
    host_symbols = 'soh.symbols (instalar ao lado de soh.exe)'
    contents = @($contents)
}
Write-Utf8NoBom ([System.IO.Path]::ChangeExtension($package, '.contents.json')) ($report | ConvertTo-Json -Depth 4)

if ($Demo) {
    $demoDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_nei_demo.dll') 'NEI demo DLL'
    $demoStage = Join-Path $staging 'nei-demo'
    [System.IO.Directory]::CreateDirectory((Join-Path $demoStage 'provider')) | Out-Null
    Write-Utf8NoBom (Join-Path $demoStage 'manifest.toml') $demoManifest
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'demo\main.lua') -Destination (Join-Path $demoStage 'main.lua')
    Copy-Item -LiteralPath $demoDll -Destination (Join-Path $demoStage 'provider\linkspan_nei_demo.dll')
    $demoPackage = Join-Path $packageRoot "LinkSpan-NEI-Demo-$version-layout-$layout.zip"
    New-DeterministicZip $demoStage $demoPackage
    & $validatorExe $demoPackage
    if ($LASTEXITCODE -ne 0) { throw "Pacote do demo inválido: $demoPackage" }
}

$report | ConvertTo-Json -Depth 4
