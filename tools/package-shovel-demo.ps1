[CmdletBinding()]
param(
    [string]$ProviderDirectory = 'build\shovel-demo-native\mod\provider',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$OutputDirectory = 'build\shovel-demo'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'linkspan-zip.ps1')
Set-StrictMode -Version Latest

function InputFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Arquivo ausente: $Path" }
    return (Resolve-Path -LiteralPath $Path).Path
}

$dll = InputFile (Join-Path $ProviderDirectory 'linkspan_shovel_demo.dll')
$validatorExe = InputFile $Validator
$layoutText = Get-Content -LiteralPath (InputFile $LayoutHeader) -Raw
$layoutMatch = [regex]::Match($layoutText, 'LINKSPAN_OOT_LAYOUT_ID "([0-9a-f]{64})"')
if (-not $layoutMatch.Success) { throw "Layout id ausente: $LayoutHeader" }
$layout = $layoutMatch.Groups[1].Value.Substring(0, 8)
$dllHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.Substring(0, 8).ToLowerInvariant()
$source = Join-Path (Get-Location).Path 'soh\native-sdk\shovel-demo'
$output = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputDirectory))
$stage = Join-Path $output ([System.IO.Path]::GetRandomFileName())
[System.IO.Directory]::CreateDirectory((Join-Path $stage 'provider')) | Out-Null
foreach ($name in @('manifest.toml', 'main.lua')) {
    Copy-Item -LiteralPath (InputFile (Join-Path $source $name)) -Destination (Join-Path $stage $name)
}
Copy-Item -LiteralPath $dll -Destination (Join-Path $stage 'provider\linkspan_shovel_demo.dll')

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = Join-Path $output "LinkSpan-Shovel-Demo-0.1.1-layout-$layout-$dllHash.zip"
if (Test-Path -LiteralPath $zip) { throw "Pacote já existe: $zip" }
New-LinkSpanZip -Source $stage -Destination $zip
& $validatorExe $zip
if ($LASTEXITCODE -ne 0) { throw 'Pacote inválido.' }
[ordered]@{ path = $zip; sha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash } |
    ConvertTo-Json
