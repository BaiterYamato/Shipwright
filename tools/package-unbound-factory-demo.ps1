[CmdletBinding()]
param(
    [string]$CoreLibrary = 'build\unbound-factory-native\mod\provider\linkspan_unbound_core.dll',
    [string]$ConsumerLibrary = 'build\unbound-factory-native\mod\provider\linkspan_unbound_consumer.dll',
    [string]$Validator = '..\NATIVE-001\build\Release\shiplua_manifest_validator.exe',
    [string]$OutputDirectory = 'build\unbound-factory-packages'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-InputFile([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label não encontrado: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function New-DeterministicZip([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Force
    }
    $fixed = [datetime]::SpecifyKind([datetime]'2000-01-01T00:00:00', [DateTimeKind]::Utc)
    Get-ChildItem -LiteralPath $Source -Recurse -File | ForEach-Object { $_.LastWriteTimeUtc = $fixed }
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $Source, $Destination, [System.IO.Compression.CompressionLevel]::Optimal, $false)
}

$coreDll = Resolve-InputFile $CoreLibrary 'DLL da factory Unbound'
$consumerDll = Resolve-InputFile $ConsumerLibrary 'DLL consumidora'
$validatorExe = Resolve-InputFile $Validator 'Validador Link-Span'
$sourceRoot = [System.IO.Path]::GetFullPath('soh\native-sdk\unbound-core')
$packageRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$staging = Join-Path $packageRoot 'staging'
$coreStage = Join-Path $staging 'unbound-core'
$consumerStage = Join-Path $staging 'unbound-consumer'
$baseStage = Join-Path $staging 'layer-base'
$overrideStage = Join-Path $staging 'layer-override'

if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
foreach ($directory in @(
    (Join-Path $coreStage 'provider'),
    (Join-Path $coreStage 'include\linkspan\unbound'),
    (Join-Path $consumerStage 'provider'),
    (Join-Path $baseStage 'unbound'),
    (Join-Path $overrideStage 'unbound')
)) {
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
}

Write-Utf8NoBom (Join-Path $coreStage 'manifest.toml') @'
id = "linkspan.unbound.framework"
name = "Link-Span Unbound Framework"
version = "0.1.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]
kind = "core_extension"
load_phase = "pre_game"

[provider]
abi_version = "1.1"
win64 = "provider/linkspan_unbound_core.dll"
'@
Copy-Item -LiteralPath (Join-Path $sourceRoot 'core-main.lua') -Destination (Join-Path $coreStage 'main.lua')
Copy-Item -LiteralPath $coreDll -Destination (Join-Path $coreStage 'provider\linkspan_unbound_core.dll')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'include\linkspan\unbound\json_factory.h') `
    -Destination (Join-Path $coreStage 'include\linkspan\unbound\json_factory.h')

Write-Utf8NoBom (Join-Path $consumerStage 'manifest.toml') @'
id = "linkspan.unbound.factory-demo"
name = "Link-Span Unbound Factory Demo"
version = "0.1.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.1"
win64 = "provider/linkspan_unbound_consumer.dll"

[dependencies]
"linkspan.unbound.framework" = ">=0.1.0 <0.2.0"
'@
Copy-Item -LiteralPath (Join-Path $sourceRoot 'consumer-main.lua') -Destination (Join-Path $consumerStage 'main.lua')
Copy-Item -LiteralPath $consumerDll -Destination (Join-Path $consumerStage 'provider\linkspan_unbound_consumer.dll')

Write-Utf8NoBom (Join-Path $baseStage 'unbound\factory-probe.json') `
    '{"$schema":"linkspan.unbound.actor-patch/v1","actor":{"name":"guard","stats":{"health":3,"speed":1},"drops":["rupee"]},"enabled":true}'
Write-Utf8NoBom (Join-Path $overrideStage 'unbound\factory-probe.json') `
    '{"$schema":"linkspan.unbound.actor-patch/v1","actor":{"stats":{"health":8},"drops":["heart"]},"enabled":false}'

Add-Type -AssemblyName System.IO.Compression.FileSystem
$coreZip = Join-Path $packageRoot 'LinkSpan-Unbound-Framework-0.1.0.zip'
$consumerZip = Join-Path $packageRoot 'LinkSpan-Unbound-Factory-Demo-0.1.0.zip'
$baseZip = Join-Path $packageRoot 'linkspan-unbound-factory-base.zip'
$overrideZip = Join-Path $packageRoot 'linkspan-unbound-factory-override.zip'
New-DeterministicZip $coreStage $coreZip
New-DeterministicZip $consumerStage $consumerZip
New-DeterministicZip $baseStage $baseZip
New-DeterministicZip $overrideStage $overrideZip

& $validatorExe $coreZip
if ($LASTEXITCODE -ne 0) { throw 'Coremod Unbound inválido.' }
& $validatorExe $consumerZip
if ($LASTEXITCODE -ne 0) { throw 'Consumer Unbound inválido.' }

$bundleStage = Join-Path $staging 'install-bundle'
$bundleMods = Join-Path $bundleStage 'mods'
[System.IO.Directory]::CreateDirectory($bundleMods) | Out-Null
foreach ($package in @($coreZip, $consumerZip, $baseZip, $overrideZip)) {
    Copy-Item -LiteralPath $package -Destination (Join-Path $bundleMods ([IO.Path]::GetFileName($package)))
}
Write-Utf8NoBom (Join-Path $bundleStage 'README-Unbound-Factory.txt') @'
LINK-SPAN OOT — UNBOUND JSON FACTORY 0.1.0

REQUISITO
- Overlay Link-Span para Shipwright 9.2.3 com Core Extensions ABI 1.1 e
  linkspan.oot.resources v2.

INSTALAÇÃO
1. Feche o jogo.
2. Extraia este ZIP na raiz da instalação que já recebeu o overlay Link-Span.
3. Inicie soh.exe.

O pacote instala dois mods independentes e duas fixtures de validação na pasta
mods. Não substitui soh.exe, soh.o2r, oot.o2r, saves ou configuração.
'@
$bundleHashLines = Get-ChildItem -LiteralPath $bundleMods -File | Sort-Object Name | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  mods/$($_.Name)"
}
Write-Utf8NoBom (Join-Path $bundleStage 'checksums.sha256') (($bundleHashLines -join "`n") + "`n")
$bundleZip = Join-Path $packageRoot 'LinkSpan-OOT-UNBOUND-002B-Mods-0.1.0.zip'
New-DeterministicZip $bundleStage $bundleZip

@($coreZip, $consumerZip, $baseZip, $overrideZip, $bundleZip) | ForEach-Object {
    [ordered]@{
        path = [System.IO.Path]::GetFullPath($_)
        size = (Get-Item -LiteralPath $_).Length
        sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
    }
} | ConvertTo-Json
