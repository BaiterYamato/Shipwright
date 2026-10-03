param(
    [Parameter(Mandatory = $true)] [string] $BuildDirectory,
    [string] $OutputDirectory = (Join-Path $PSScriptRoot '..\build')
)

# Fixture CEL-003: .shipmod com manifesto, Lua e a DLL. ZipFile grava as entradas com '/', como o loader
# espera (o Compress-Archive do PowerShell 5.1 grava '\').
$ErrorActionPreference = 'Stop'
$source = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $BuildDirectory 'mod\provider\linkspan_cel_render_demo.dll'
if (-not (Test-Path -LiteralPath $dll)) { throw "DLL não encontrada: $dll" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$stage = Join-Path $OutputDirectory 'cel-render-demo-stage'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'provider') | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'manifest.toml') -Destination $stage
Copy-Item -LiteralPath (Join-Path $source 'main.lua') -Destination $stage
Copy-Item -LiteralPath $dll -Destination (Join-Path $stage 'provider\linkspan_cel_render_demo.dll')
$shipmod = Join-Path $OutputDirectory 'cel-render-demo.shipmod'
Remove-Item -LiteralPath $shipmod -Force -ErrorAction SilentlyContinue
. (Join-Path $PSScriptRoot '..\..\..\..\tools\linkspan-zip.ps1')
New-LinkSpanZip -Source $stage -Destination $shipmod
Write-Output $shipmod
