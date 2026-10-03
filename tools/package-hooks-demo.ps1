[CmdletBinding()]
param(
    [string]$ProviderDirectory = 'build\hooks-demo-native\mod\provider',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$OutputDirectory = 'build\hooks-demo'
)

# Packages the native hooks demo (ABI 1.2) as a Link-Span mod named after the host layout id.

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

$demoDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_hooks_demo.dll') 'Hooks demo DLL'
$validatorExe = Resolve-InputFile $Validator 'Link-Span validator'
$layoutText = Get-Content -LiteralPath (Resolve-InputFile $LayoutHeader 'Layout id header') -Raw
$layoutMatch = [regex]::Match($layoutText, 'LINKSPAN_OOT_LAYOUT_ID "([0-9a-f]{64})"')
if (-not $layoutMatch.Success) {
    throw "Layout id not found in $LayoutHeader"
}
$layout = $layoutMatch.Groups[1].Value.Substring(0, 8)

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path 'soh\native-sdk\hooks-demo'))
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputDirectory))
$stage = Join-Path $packageRoot 'staging\hooks-demo'
if (Test-Path -LiteralPath (Join-Path $packageRoot 'staging')) {
    Remove-Item -LiteralPath (Join-Path $packageRoot 'staging') -Recurse -Force
}
[System.IO.Directory]::CreateDirectory((Join-Path $stage 'provider')) | Out-Null

Write-Utf8NoBom (Join-Path $stage 'manifest.toml') @'
id = "linkspan.hooks-demo"
name = "Link-Span Native Hooks Demo"
version = "0.1.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.2"
win64 = "provider/linkspan_hooks_demo.dll"
'@
Copy-Item -LiteralPath (Join-Path $sourceRoot 'main.lua') -Destination (Join-Path $stage 'main.lua')
Copy-Item -LiteralPath $demoDll -Destination (Join-Path $stage 'provider\linkspan_hooks_demo.dll')

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = Join-Path $packageRoot "LinkSpan-Hooks-Demo-0.1.0-layout-$layout.zip"
New-DeterministicZip $stage $zip

& $validatorExe $zip
if ($LASTEXITCODE -ne 0) { throw 'Invalid hooks demo package.' }

[ordered]@{
    path = $zip
    size = (Get-Item -LiteralPath $zip).Length
    sha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
} | ConvertTo-Json
