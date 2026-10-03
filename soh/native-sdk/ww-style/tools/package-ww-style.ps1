param(
    [Parameter(Mandatory = $true)] [string] $BuildDirectory,
    [string] $OutputDirectory
)

# Wind Waker Style: .shipmod com manifesto, Lua, docs e a DLL. As entradas vão com '/', como o loader espera: no
# PowerShell 5.1 tanto o Compress-Archive quanto o ZipFile.CreateFromDirectory gravam '\'. O padrão da saída
# fica no corpo: no 5.1 o $PSScriptRoot ainda está vazio quando o default de um parâmetro é avaliado.
$ErrorActionPreference = 'Stop'
$source = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $source 'build' }
$dll = Join-Path $BuildDirectory 'mod\provider\linkspan_ww_style.dll'
if (-not (Test-Path -LiteralPath $dll)) { throw "DLL não encontrada: $dll" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$stage = Join-Path $OutputDirectory 'ww-style-stage'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'provider') | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'manifest.toml') -Destination $stage
Copy-Item -LiteralPath (Join-Path $source 'main.lua') -Destination $stage
foreach ($doc in 'README.md', 'README.pt-BR.md', 'NOTICE.md') {
    Copy-Item -LiteralPath (Join-Path $source $doc) -Destination $stage
}
Copy-Item -LiteralPath $dll -Destination (Join-Path $stage 'provider\linkspan_ww_style.dll')
$version = (Select-String -LiteralPath (Join-Path $source 'manifest.toml') -Pattern '^version\s*=\s*"([^"]+)"').Matches[0].Groups[1].Value
$shipmod = Join-Path $OutputDirectory "LinkSpan-WindWakerStyle-$version.shipmod"
Remove-Item -LiteralPath $shipmod -Force -ErrorAction SilentlyContinue
. (Join-Path $PSScriptRoot '..\..\..\..\tools\linkspan-zip.ps1')
New-LinkSpanZip -Source $stage -Destination $shipmod
Remove-Item -LiteralPath $stage -Recurse -Force
Write-Output $shipmod
