[CmdletBinding()]
param(
    [string]$ProviderDirectory = 'build\unbound-scene-native\mod\provider',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$OutputDirectory = 'build\unbound-scene-demo',
    [ValidateSet('zip', 'shipmod')][string]$Extension = 'zip'
)

# Packages the scene demo (slice D2) and the Hyrule Field actor patch demo (slice D1) as Link-Span mods
# named after the host layout id. Both depend on the Unbound framework 0.6.x, which
# tools/package-unbound-framework.ps1 packages. -Extension shipmod writes the .shipmod container (the same
# ZIP) instead of .zip.

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

$demoDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_unbound_scene_demo.dll') 'Scene demo DLL'
$fieldDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_unbound_field_demo.dll') 'Field demo DLL'
$validatorExe = Resolve-InputFile $Validator 'Link-Span validator'
$layoutText = Get-Content -LiteralPath (Resolve-InputFile $LayoutHeader 'Layout id header') -Raw
$layoutMatch = [regex]::Match($layoutText, 'LINKSPAN_OOT_LAYOUT_ID "([0-9a-f]{64})"')
if (-not $layoutMatch.Success) {
    throw "Layout id not found in $LayoutHeader"
}
$layout = $layoutMatch.Groups[1].Value.Substring(0, 8)

$sourceRoot = [System.IO.Path]::GetFullPath('soh\native-sdk\unbound-core')
$packageRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$staging = Join-Path $packageRoot 'staging'
$demoStage = Join-Path $staging 'unbound-scene-demo'
$fieldStage = Join-Path $staging 'unbound-field-demo'
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
foreach ($directory in @(
    (Join-Path $demoStage 'provider'),
    (Join-Path $demoStage 'assets\unbound'),
    (Join-Path $fieldStage 'provider'),
    (Join-Path $fieldStage 'assets\scenes\spot00\rooms')
)) {
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
}

Write-Utf8NoBom (Join-Path $demoStage 'manifest.toml') @'
id = "linkspan.unbound.scene-demo"
name = "Link-Span Unbound Scene Demo"
version = "0.1.3"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.0"
win64 = "provider/linkspan_unbound_scene_demo.dll"

[dependencies]
"linkspan.unbound.framework" = ">=0.6.0 <0.7.0"
'@
Copy-Item -LiteralPath (Join-Path $sourceRoot 'scene-demo\main.lua') -Destination (Join-Path $demoStage 'main.lua')
Copy-Item -LiteralPath $demoDll -Destination (Join-Path $demoStage 'provider\linkspan_unbound_scene_demo.dll')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'scene-demo\assets\unbound\scenes.json') `
    -Destination (Join-Path $demoStage 'assets\unbound\scenes.json')

Write-Utf8NoBom (Join-Path $fieldStage 'manifest.toml') @'
id = "linkspan.unbound.field-demo"
name = "Link-Span Unbound Field Demo"
version = "0.1.2"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.0"
win64 = "provider/linkspan_unbound_field_demo.dll"

[dependencies]
"linkspan.unbound.framework" = ">=0.6.0 <0.7.0"
'@
Copy-Item -LiteralPath (Join-Path $sourceRoot 'field-demo\main.lua') -Destination (Join-Path $fieldStage 'main.lua')
Copy-Item -LiteralPath $fieldDll -Destination (Join-Path $fieldStage 'provider\linkspan_unbound_field_demo.dll')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'field-demo\assets\scenes\spot00\rooms\0.json') `
    -Destination (Join-Path $fieldStage 'assets\scenes\spot00\rooms\0.json')

foreach ($stage in @($demoStage, $fieldStage)) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\docs\licensing\LICENSE-PACKAGE.txt') -Destination (Join-Path $stage 'LICENSE')
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'NOTICE.md') -Destination (Join-Path $stage 'NOTICE.md')
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$demoZip = Join-Path $packageRoot "LinkSpan-Unbound-Scene-Demo-0.1.3-layout-$layout.$Extension"
$fieldZip = Join-Path $packageRoot "LinkSpan-Unbound-Field-Demo-0.1.2-layout-$layout.$Extension"
New-DeterministicZip $demoStage $demoZip
New-DeterministicZip $fieldStage $fieldZip

& $validatorExe $demoZip
if ($LASTEXITCODE -ne 0) { throw 'Invalid scene demo package.' }
& $validatorExe $fieldZip
if ($LASTEXITCODE -ne 0) { throw 'Invalid field demo package.' }

@($demoZip, $fieldZip) | ForEach-Object {
    [ordered]@{
        path = $_
        size = (Get-Item -LiteralPath $_).Length
        sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
    }
} | ConvertTo-Json
