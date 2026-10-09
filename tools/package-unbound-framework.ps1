[CmdletBinding()]
param(
    [string]$ProviderDirectory = 'build\unbound-scene-native\mod\provider',
    [string]$ToolDirectory = 'build\unbound-scene-native\Release',
    [string]$Validator = 'build\x64\ship-lua\Release\shiplua_manifest_validator.exe',
    [string]$LayoutHeader = 'build\x64\native-sdk\oot_layout_id.h',
    [string]$JsonLicense = '..\shipwright-limpo\Shipwright\build\x64\vcpkg\installed\x64-windows-static\share\nlohmann-json\copyright',
    [string]$OutputDirectory = 'build\unbound-framework'
)

# UNBOUND-012: the single Unbound framework .shipmod (core DLL, Lua, command-line tool, C header, JSON schemas,
# author documentation and license notices), validated, with SHA-256 and a content report next to it.

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

$version = '0.6.11'
$coreDll = Resolve-InputFile (Join-Path $ProviderDirectory 'linkspan_unbound_core.dll') 'Unbound framework DLL'
$tool = Resolve-InputFile (Join-Path $ToolDirectory 'linkspan_unbound_convert.exe') 'Unbound command-line tool'
$validatorExe = Resolve-InputFile $Validator 'Link-Span validator'
$jsonCopyright = Resolve-InputFile $JsonLicense 'nlohmann/json license'
$layoutText = Get-Content -LiteralPath (Resolve-InputFile $LayoutHeader 'Layout id header') -Raw
$layoutMatch = [regex]::Match($layoutText, 'LINKSPAN_OOT_LAYOUT_ID "([0-9a-f]{64})"')
if (-not $layoutMatch.Success) {
    throw "Layout id not found in $LayoutHeader"
}
$layout = $layoutMatch.Groups[1].Value.Substring(0, 8)

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path 'soh\native-sdk\unbound-core'))
# GetFullPath com caminho relativo usa o diretório do processo .NET, que o Set-Location não muda.
$packageRoot = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) { [System.IO.Path]::GetFullPath($OutputDirectory) }
               else { [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputDirectory)) }
$staging = Join-Path $packageRoot 'staging'
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
foreach ($directory in @('provider', 'tools', 'docs', 'include\linkspan\unbound', 'schemas\unbound')) {
    [System.IO.Directory]::CreateDirectory((Join-Path $staging $directory)) | Out-Null
}

Write-Utf8NoBom (Join-Path $staging 'manifest.toml') @"
id = "linkspan.unbound.framework"
name = "Link-Span Unbound Framework"
version = "$version"
api = ">=0.5.0 <0.6.0"
entrypoint = "main.lua"
games = ["oot"]
kind = "core_extension"
load_phase = "pre_game"
description = "Unbound 0.9 for Link-Span: JSON scenes, text, collision and actors with models, animation, dialogue and head tracking."

[provider]
abi_version = "1.2"
win64 = "provider/linkspan_unbound_core.dll"
"@
Copy-Item -LiteralPath (Join-Path $sourceRoot 'core-main.lua') -Destination (Join-Path $staging 'main.lua')
Copy-Item -LiteralPath $coreDll -Destination (Join-Path $staging 'provider\linkspan_unbound_core.dll')
Copy-Item -LiteralPath $tool -Destination (Join-Path $staging 'tools\linkspan_unbound_convert.exe')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'include\linkspan\unbound\json_factory.h') `
    -Destination (Join-Path $staging 'include\linkspan\unbound\json_factory.h')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'README.md') -Destination (Join-Path $staging 'docs\README.md')
# Schemas JSON do formato Unbound 2 (draft 2020-12), com o validador de exemplo.
Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'schemas\unbound') -File | Where-Object { $_.Extension -in '.json', '.md' } |
    Copy-Item -Destination (Join-Path $staging 'schemas\unbound')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'schemas\validate_schemas.py') -Destination (Join-Path $staging 'schemas\validate_schemas.py')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'NOTICE.md') -Destination (Join-Path $staging 'docs\NOTICE.md')
# Licença (CC0 do trabalho do Link-Span) e atribuição na raiz, como nos outros pacotes.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\docs\licensing\LICENSE-PACKAGE.txt') -Destination (Join-Path $staging 'LICENSE')
Copy-Item -LiteralPath (Join-Path $sourceRoot 'NOTICE.md') -Destination (Join-Path $staging 'NOTICE.md')
$notice = @"
# Licenças

- Framework Unbound do Link-Span (DLL, Lua, ferramenta, header e documentação): domínio público pela CC0 1.0
  Universal (``LICENSE``).
- O formato lido é o da SPEC do SoH: Unbound (``roborich/Shipwright``, ``unbound-docs/SPEC.md``); nenhum
  arquivo integral daquele projeto vai neste pacote. O leitor de atores é adaptado da versão 0.9;
  consulte ``docs/NOTICE.md`` para a proveniência.
- Nenhum dado do jogo vai neste pacote: ``oot-unbound.o2r`` é gerado na máquina de quem joga, a partir dos
  archives dela.

## nlohmann/json (compilado na DLL e na ferramenta)

"@
Write-Utf8NoBom (Join-Path $staging 'docs\LICENSES.md') ($notice + (Get-Content -LiteralPath $jsonCopyright -Raw))

Add-Type -AssemblyName System.IO.Compression.FileSystem
$package = Join-Path $packageRoot "LinkSpan-Unbound-Framework-$version-layout-$layout.shipmod"
if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Force
}
. (Join-Path $PSScriptRoot 'linkspan-zip.ps1')
New-LinkSpanZip -Source $staging -Destination $package
& $validatorExe $package
if ($LASTEXITCODE -ne 0) { throw 'Invalid Unbound framework package.' }

$contents = Get-ChildItem -LiteralPath $staging -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($staging.Length + 1).Replace('\', '/')
        size = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
}
$report = [ordered]@{
    package = [System.IO.Path]::GetFileName($package)
    size = (Get-Item -LiteralPath $package).Length
    sha256 = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
    layout = $layoutMatch.Groups[1].Value
    contents = @($contents)
}
$reportPath = [System.IO.Path]::ChangeExtension($package, '.contents.json')
Write-Utf8NoBom $reportPath ($report | ConvertTo-Json -Depth 4)
$report | ConvertTo-Json -Depth 4
