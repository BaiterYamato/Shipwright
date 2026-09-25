[CmdletBinding()]
param(
    [string]$ProviderDirectory = 'build\nei-core-native\mod\provider',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$OutputDirectory = 'build\nei-core',
    [string]$HostExecutable = 'x64\Release\soh.exe'
)

# Empacota o coremod Not Enough Items (linkspan.nei, NEI-002) e o mod de demonstração, com o layout id do host
# no nome. O coremod leva o header público para quem compila mods de conteúdo. Desde a fase F o coremod traz o
# código de itens do fork NEI, que chama o soh.exe pelo escape hatch (ABI 1.3): o manifesto lista o SHA-256 do
# executável alvo, e em outro executável só o registro de itens funciona.
# Os assets do fork (modelos, ícones, texturas) não vão no pacote: não têm licença. Quem joga gera
# <jogo>/nei-assets/nei-assets-<componente>.o2r com tools/build-nei-assets.py (NEI-007); sem eles o coremod
# define só o Feather e o Cape, com ícone e modelo provisórios.

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

function New-DeterministicZip([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Force
    }
    $fixed = [datetime]::SpecifyKind([datetime]'2000-01-01T00:00:00', [DateTimeKind]::Utc)
    Get-ChildItem -LiteralPath $Source -Recurse -File | ForEach-Object { $_.LastWriteTimeUtc = $fixed }
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $Source, $Destination, [System.IO.Compression.CompressionLevel]::Optimal, $false)
}

$coreDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_nei_core.dll') 'NEI core DLL'
$demoDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_nei_demo.dll') 'NEI demo DLL'
$validatorExe = Resolve-InputFile $Validator 'Link-Span validator'
$layoutText = Get-Content -LiteralPath (Resolve-InputFile $LayoutHeader 'Layout id header') -Raw
$layoutMatch = [regex]::Match($layoutText, 'LINKSPAN_OOT_LAYOUT_ID "([0-9a-f]{64})"')
if (-not $layoutMatch.Success) {
    throw "Layout id not found in $LayoutHeader"
}
$layout = $layoutMatch.Groups[1].Value.Substring(0, 8)
$fingerprint = (Get-FileHash -LiteralPath (Resolve-InputFile $HostExecutable 'Host executable') -Algorithm SHA256).Hash.ToLowerInvariant()

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path 'soh\native-sdk\nei-core'))
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputDirectory))
$staging = Join-Path $packageRoot 'staging'
$coreStage = Join-Path $staging 'nei-core'
$demoStage = Join-Path $staging 'nei-demo'
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
foreach ($directory in @((Join-Path $coreStage 'provider'), (Join-Path $coreStage 'include\linkspan\nei'),
                         (Join-Path $demoStage 'provider'))) {
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
}

$coreManifest = @'
id = "linkspan.nei"
name = "Not Enough Items (Link-Span)"
version = "0.2.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]
kind = "core_extension"
load_phase = "pre_game"

[provider]
abi_version = "1.3"
win64 = "provider/linkspan_nei_core.dll"
host_fingerprints = ["__FINGERPRINT__"]
'@
Write-Utf8NoBom (Join-Path $coreStage 'manifest.toml') $coreManifest.Replace('__FINGERPRINT__', $fingerprint)
Copy-Item -LiteralPath (Join-Path $sourceRoot 'core-main.lua') -Destination (Join-Path $coreStage 'main.lua')
Copy-Item -LiteralPath $coreDll -Destination (Join-Path $coreStage 'provider\linkspan_nei_core.dll')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'include\linkspan\nei\nei_items.h') `
    -Destination (Join-Path $coreStage 'include\linkspan\nei\nei_items.h')

Write-Utf8NoBom (Join-Path $demoStage 'manifest.toml') @'
id = "linkspan.nei-demo"
name = "Not Enough Items Demo"
version = "0.2.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.2"
win64 = "provider/linkspan_nei_demo.dll"

[dependencies]
"linkspan.nei" = ">=0.2.0 <0.3.0"
'@
Copy-Item -LiteralPath (Join-Path $sourceRoot 'demo\main.lua') -Destination (Join-Path $demoStage 'main.lua')
Copy-Item -LiteralPath $demoDll -Destination (Join-Path $demoStage 'provider\linkspan_nei_demo.dll')

Add-Type -AssemblyName System.IO.Compression.FileSystem
$coreZip = Join-Path $packageRoot "LinkSpan-NEI-Core-0.2.0-layout-$layout.zip"
$demoZip = Join-Path $packageRoot "LinkSpan-NEI-Demo-0.2.0-layout-$layout.zip"
New-DeterministicZip $coreStage $coreZip
New-DeterministicZip $demoStage $demoZip

foreach ($package in @($coreZip, $demoZip)) {
    & $validatorExe $package
    if ($LASTEXITCODE -ne 0) { throw "Pacote NEI inválido: $package" }
}

@($coreZip, $demoZip) | ForEach-Object {
    [ordered]@{
        path = $_
        size = (Get-Item -LiteralPath $_).Length
        sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
    }
} | ConvertTo-Json
