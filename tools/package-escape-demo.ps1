[CmdletBinding()]
param(
    [string]$ProviderDirectory = 'build\escape-demo-native\mod\provider',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$OutputDirectory = 'build\escape-demo',
    [string]$HostExecutable = 'x64\Release\soh.exe'
)

# Packages the native escape hatch demo (ABI 1.3) as a core extension locked to the soh.exe SHA-256.

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

$demoDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_escape_demo.dll') 'Escape demo DLL'
$validatorExe = Resolve-InputFile $Validator 'Link-Span validator'
$layoutText = Get-Content -LiteralPath (Resolve-InputFile $LayoutHeader 'Layout id header') -Raw
$layoutMatch = [regex]::Match($layoutText, 'LINKSPAN_OOT_LAYOUT_ID "([0-9a-f]{64})"')
if (-not $layoutMatch.Success) {
    throw "Layout id not found in $LayoutHeader"
}
$layout = $layoutMatch.Groups[1].Value.Substring(0, 8)
$fingerprint = (Get-FileHash -LiteralPath (Resolve-InputFile $HostExecutable 'Host executable') -Algorithm SHA256).Hash.ToLowerInvariant()

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path 'soh\native-sdk\escape-demo'))
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputDirectory))
$stage = Join-Path $packageRoot 'staging\escape-demo'
if (Test-Path -LiteralPath (Join-Path $packageRoot 'staging')) {
    Remove-Item -LiteralPath (Join-Path $packageRoot 'staging') -Recurse -Force
}
[System.IO.Directory]::CreateDirectory((Join-Path $stage 'provider')) | Out-Null

$manifest = @'
id = "linkspan.escape-demo"
name = "Link-Span Native Escape Hatch Demo"
version = "0.1.0"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]
kind = "core_extension"
load_phase = "pre_game"

[provider]
abi_version = "1.3"
win64 = "provider/linkspan_escape_demo.dll"
host_fingerprints = ["__FINGERPRINT__"]
'@
Write-Utf8NoBom (Join-Path $stage 'manifest.toml') $manifest.Replace('__FINGERPRINT__', $fingerprint)
Copy-Item -LiteralPath (Join-Path $sourceRoot 'main.lua') -Destination (Join-Path $stage 'main.lua')
Copy-Item -LiteralPath $demoDll -Destination (Join-Path $stage 'provider\linkspan_escape_demo.dll')

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = Join-Path $packageRoot "LinkSpan-Escape-Demo-0.1.0-host-$($fingerprint.Substring(0, 8)).zip"
New-DeterministicZip $stage $zip

& $validatorExe $zip
if ($LASTEXITCODE -ne 0) { throw 'Invalid escape demo package.' }

[ordered]@{
    path = $zip
    size = (Get-Item -LiteralPath $zip).Length
    sha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
} | ConvertTo-Json
