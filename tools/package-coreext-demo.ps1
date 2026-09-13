[CmdletBinding()]
param(
    [string]$CoreLibrary = '..\NATIVE-001\build\tests\Release\native_core_fixture.dll',
    [string]$ConsumerLibrary = '..\NATIVE-001\build\tests\Release\native_core_consumer.dll',
    [string]$Validator = '..\NATIVE-001\build\Release\shiplua_manifest_validator.exe',
    [string]$OutputDirectory = 'build\coreext-packages-v1'
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

$coreDll = Resolve-InputFile $CoreLibrary 'DLL da core extension'
$consumerDll = Resolve-InputFile $ConsumerLibrary 'DLL consumidora'
$validatorExe = Resolve-InputFile $Validator 'Validador Link-Span'
$packageRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$coreStage = Join-Path $packageRoot 'staging\core-service'
$consumerStage = Join-Path $packageRoot 'staging\core-consumer'
[System.IO.Directory]::CreateDirectory((Join-Path $coreStage 'provider')) | Out-Null
[System.IO.Directory]::CreateDirectory((Join-Path $consumerStage 'provider')) | Out-Null

Write-Utf8NoBom (Join-Path $coreStage 'manifest.toml') @'
id = "linkspan.demo.core-service"
name = "Link-Span Core Service Demo"
version = "1.0.0"
api = ">=0.5 <1.0"
entrypoint = "main.lua"
games = ["oot"]
kind = "core_extension"
load_phase = "pre_game"

[provider]
abi_version = "1.1"
win64 = "provider/core-service.dll"
'@
Write-Utf8NoBom (Join-Path $coreStage 'main.lua') "loaded = true`n"
Copy-Item -LiteralPath $coreDll -Destination (Join-Path $coreStage 'provider\core-service.dll') -Force

Write-Utf8NoBom (Join-Path $consumerStage 'manifest.toml') @'
id = "linkspan.demo.core-consumer"
name = "Link-Span Core Consumer Demo"
version = "1.0.0"
api = ">=0.5 <1.0"
entrypoint = "main.lua"
games = ["oot"]

[provider]
abi_version = "1.1"
win64 = "provider/core-consumer.dll"

[dependencies]
"linkspan.demo.core-service" = ">=1.0 <2.0"
'@
Write-Utf8NoBom (Join-Path $consumerStage 'main.lua') @'
assert(require("ship").native.call("read_core", "") == "42")
'@
Copy-Item -LiteralPath $consumerDll -Destination (Join-Path $consumerStage 'provider\core-consumer.dll') -Force

$fixed = [datetime]::SpecifyKind([datetime]'2000-01-01T00:00:00', [DateTimeKind]::Utc)
Get-ChildItem -LiteralPath (Join-Path $packageRoot 'staging') -Recurse -File |
    ForEach-Object { $_.LastWriteTimeUtc = $fixed }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$coreZip = Join-Path $packageRoot 'linkspan-core-service-demo-1.0.0.zip'
$consumerZip = Join-Path $packageRoot 'linkspan-core-consumer-demo-1.0.0.zip'
foreach ($target in @($coreZip, $consumerZip)) {
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
}
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $coreStage, $coreZip, [System.IO.Compression.CompressionLevel]::Optimal, $false)
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $consumerStage, $consumerZip, [System.IO.Compression.CompressionLevel]::Optimal, $false)

& $validatorExe $coreZip
if ($LASTEXITCODE -ne 0) { throw 'Core ZIP inválido.' }
& $validatorExe $consumerZip
if ($LASTEXITCODE -ne 0) { throw 'Consumer ZIP inválido.' }

@($coreZip, $consumerZip) | ForEach-Object {
    [ordered]@{
        path = [System.IO.Path]::GetFullPath($_)
        size = (Get-Item -LiteralPath $_).Length
        sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
    }
} | ConvertTo-Json
