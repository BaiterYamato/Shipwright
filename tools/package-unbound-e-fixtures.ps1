[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BaseArchive,
    [string]$ProviderDirectory = 'build\unbound-scene-native\mod\provider',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$Checker = 'build\unbound-scene-native\Release\linkspan_unbound_convert.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$OutputDirectory = 'build\unbound-e-fixtures',
    [string]$Python = 'python'
)

# Packages the Unbound phase E fixtures (UNBOUND-007 to 011) as a local .shipmod. The scenes are generated
# from -BaseArchive (the oot-unbound.o2r converted on this machine), so the package holds game data and
# stays in build/: it is never published or committed.

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'linkspan-zip.ps1')
Set-StrictMode -Version Latest

function Resolve-InputFile([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

$base = Resolve-InputFile $BaseArchive 'Unbound base archive'
$dll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_unbound_e_fixtures.dll') 'Fixtures DLL'
$validatorExe = Resolve-InputFile $Validator 'Link-Span validator'
$checkerExe = Resolve-InputFile $Checker 'Unbound checker'
$layoutText = Get-Content -LiteralPath (Resolve-InputFile $LayoutHeader 'Layout id header') -Raw
$layoutMatch = [regex]::Match($layoutText, 'LINKSPAN_OOT_LAYOUT_ID "([0-9a-f]{64})"')
if (-not $layoutMatch.Success) {
    throw "Layout id not found in $LayoutHeader"
}
$layout = $layoutMatch.Groups[1].Value.Substring(0, 8)

$sourceRoot = [System.IO.Path]::GetFullPath('soh\native-sdk\unbound-core\e-fixtures')
$packageRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$staging = Join-Path $packageRoot 'staging'
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
[System.IO.Directory]::CreateDirectory((Join-Path $staging 'provider')) | Out-Null

& $Python (Join-Path $sourceRoot 'make_fixtures.py') $base (Join-Path $staging 'assets')
if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
& $checkerExe --check (Join-Path $staging 'assets')
if ($LASTEXITCODE -ne 0) { throw 'Fixture documents rejected by the Unbound checker.' }

[System.IO.File]::WriteAllText((Join-Path $staging 'manifest.toml'), @'
id = "linkspan.unbound.e-fixtures"
name = "Link-Span Unbound E Fixtures"
version = "0.1.7"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]
description = "Fixtures locais da fase E e do Prelude: cena JSON com titulo e exits por nome, texto, mundo amplo, materialAnims e recursos proprios nos limites (mundo, 33 exits, 65 540 vertices, malha tipo 2 com 1 025 entradas, 301 cameras, agua por room, 2 049 caixas DynaPoly, 8 193 texturas num quadro). Tecla K viaja para a proxima."

[provider]
abi_version = "1.0"
win64 = "provider/linkspan_unbound_e_fixtures.dll"

[dependencies]
"linkspan.unbound.framework" = ">=0.4.0 <0.7.0"
'@, [System.Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $sourceRoot 'main.lua') -Destination (Join-Path $staging 'main.lua')
Copy-Item -LiteralPath $dll -Destination (Join-Path $staging 'provider\linkspan_unbound_e_fixtures.dll')

Add-Type -AssemblyName System.IO.Compression.FileSystem
$package = Join-Path $packageRoot "LinkSpan-Unbound-E-Fixtures-0.1.7-layout-$layout.shipmod"
if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Force
}
New-LinkSpanZip -Source $staging -Destination $package
& $validatorExe $package
if ($LASTEXITCODE -ne 0) { throw 'Invalid fixtures package.' }
[ordered]@{
    path = $package
    size = (Get-Item -LiteralPath $package).Length
    sha256 = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
} | ConvertTo-Json
