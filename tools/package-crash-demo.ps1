[CmdletBinding()]
param(
    [string]$ProviderDirectory = 'build\crash-demo-native\mod\provider',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$OutputDirectory = 'build\crash-demo',
    # Empacota a variante core_extension (pre_game, ABI 1.2): prova a recuperação de uma falha de coremod.
    [switch]$CoreExtension
)

# Packages the boot guard crash demo: a native mod whose init crashes on purpose. Test builds only.
# -CoreExtension packages the same crash as a core extension (linkspan.crash-coremod).

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'linkspan-zip.ps1')
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
    New-LinkSpanZip -Source $Source -Destination $Destination
}

$variant = if ($CoreExtension) {
    @{ Id = 'linkspan.crash-coremod'; Name = 'Link-Span Boot Guard Crash Coremod'; Dll = 'linkspan_crash_coremod.dll'
       Stage = 'crash-coremod'; Zip = 'LinkSpan-Crash-Coremod'; Abi = '1.2'
       Kind = "kind = `"core_extension`"`nload_phase = `"pre_game`"`n" }
} else {
    @{ Id = 'linkspan.crash-demo'; Name = 'Link-Span Boot Guard Crash Demo'; Dll = 'linkspan_crash_demo.dll'
       Stage = 'crash-demo'; Zip = 'LinkSpan-Crash-Demo'; Abi = '1.0'; Kind = '' }
}
$demoDll = Resolve-InputFile (Join-Path $ProviderDirectory $variant.Dll) 'Crash demo DLL'
$validatorExe = Resolve-InputFile $Validator 'Link-Span validator'
$layoutText = Get-Content -LiteralPath (Resolve-InputFile $LayoutHeader 'Layout id header') -Raw
$layoutMatch = [regex]::Match($layoutText, 'LINKSPAN_OOT_LAYOUT_ID "([0-9a-f]{64})"')
if (-not $layoutMatch.Success) {
    throw "Layout id not found in $LayoutHeader"
}
$layout = $layoutMatch.Groups[1].Value.Substring(0, 8)

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path 'soh\native-sdk\crash-demo'))
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputDirectory))
$stage = Join-Path (Join-Path $packageRoot 'staging') $variant.Stage
if (Test-Path -LiteralPath (Join-Path $packageRoot 'staging')) {
    Remove-Item -LiteralPath (Join-Path $packageRoot 'staging') -Recurse -Force
}
[System.IO.Directory]::CreateDirectory((Join-Path $stage 'provider')) | Out-Null

Write-Utf8NoBom (Join-Path $stage 'manifest.toml') @"
id = "$($variant.Id)"
name = "$($variant.Name)"
version = "0.1.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]
$($variant.Kind)
[provider]
abi_version = "$($variant.Abi)"
win64 = "provider/$($variant.Dll)"
"@
Copy-Item -LiteralPath (Join-Path $sourceRoot 'main.lua') -Destination (Join-Path $stage 'main.lua')
Copy-Item -LiteralPath $demoDll -Destination (Join-Path (Join-Path $stage 'provider') $variant.Dll)

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = Join-Path $packageRoot "$($variant.Zip)-0.1.0-layout-$layout.zip"
New-DeterministicZip $stage $zip

& $validatorExe $zip
if ($LASTEXITCODE -ne 0) { throw 'Invalid crash demo package.' }

[ordered]@{
    path = $zip
    size = (Get-Item -LiteralPath $zip).Length
    sha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
} | ConvertTo-Json
