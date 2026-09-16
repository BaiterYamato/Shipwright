[CmdletBinding()]
param(
    [string]$ProviderDirectory = 'build\unbound-scene-native\mod\provider',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$OutputDirectory = 'build\unbound-scene-demo'
)

# Packages the Unbound framework (JSON factory + scene registry adapter) and the scene demo
# as two Link-Span mods named after the host layout id.

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

$coreDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_unbound_core.dll') 'Unbound framework DLL'
$demoDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_unbound_scene_demo.dll') 'Scene demo DLL'
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
$coreStage = Join-Path $staging 'unbound-framework'
$demoStage = Join-Path $staging 'unbound-scene-demo'
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
foreach ($directory in @(
    (Join-Path $coreStage 'provider'),
    (Join-Path $coreStage 'include\linkspan\unbound'),
    (Join-Path $demoStage 'provider'),
    (Join-Path $demoStage 'assets\unbound')
)) {
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
}

Write-Utf8NoBom (Join-Path $coreStage 'manifest.toml') @'
id = "linkspan.unbound.framework"
name = "Link-Span Unbound Framework"
version = "0.2.0"
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

Write-Utf8NoBom (Join-Path $demoStage 'manifest.toml') @'
id = "linkspan.unbound.scene-demo"
name = "Link-Span Unbound Scene Demo"
version = "0.1.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.0"
win64 = "provider/linkspan_unbound_scene_demo.dll"

[dependencies]
"linkspan.unbound.framework" = ">=0.2.0 <0.3.0"
'@
Copy-Item -LiteralPath (Join-Path $sourceRoot 'scene-demo\main.lua') -Destination (Join-Path $demoStage 'main.lua')
Copy-Item -LiteralPath $demoDll -Destination (Join-Path $demoStage 'provider\linkspan_unbound_scene_demo.dll')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'scene-demo\assets\unbound\scenes.json') `
    -Destination (Join-Path $demoStage 'assets\unbound\scenes.json')

Add-Type -AssemblyName System.IO.Compression.FileSystem
$coreZip = Join-Path $packageRoot "LinkSpan-Unbound-Framework-0.2.0-layout-$layout.zip"
$demoZip = Join-Path $packageRoot "LinkSpan-Unbound-Scene-Demo-0.1.0-layout-$layout.zip"
New-DeterministicZip $coreStage $coreZip
New-DeterministicZip $demoStage $demoZip

& $validatorExe $coreZip
if ($LASTEXITCODE -ne 0) { throw 'Invalid Unbound framework package.' }
& $validatorExe $demoZip
if ($LASTEXITCODE -ne 0) { throw 'Invalid scene demo package.' }

@($coreZip, $demoZip) | ForEach-Object {
    [ordered]@{
        path = $_
        size = (Get-Item -LiteralPath $_).Length
        sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
    }
} | ConvertTo-Json
