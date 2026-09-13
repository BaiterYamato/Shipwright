[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$HostExecutable,

    [Parameter(Mandatory = $true)]
    [string]$HostResources,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string[]]$ExampleMod = @(),

    # soh/assets/yml da mesma árvore do executável; vira assets/ para o extrator Torch regenerar oot.o2r.
    [string]$ExtractorAssets,

    # gamecontrollerdb.txt baixado pelo configure do mesmo build.
    [string]$ControllerDatabase,

    # build/x64/native-sdk/oot_layout_id.h do mesmo build; o id precisa estar dentro do executável.
    [string]$LayoutIdHeader,

    [string]$HostBase,

    [string]$ShipwrightVersion = '9.2.3',

    [string]$LinkSpanVersion = '0.5.0'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-InputFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label não encontrado: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    [System.IO.File]::WriteAllText(
        $Path,
        $Content,
        [System.Text.UTF8Encoding]::new($false)
    )
}

$resolvedExecutable = Resolve-InputFile -Path $HostExecutable -Label 'Executável do host'
$resolvedResources = Resolve-InputFile -Path $HostResources -Label 'Arquivo soh.o2r do host'

$resolvedExamples = @()
foreach ($mod in $ExampleMod) {
    $resolvedMod = Resolve-InputFile -Path $mod -Label 'Mod de exemplo'
    if ([System.IO.Path]::GetExtension($resolvedMod) -notin @('.zip', '.shipmod')) {
        throw "O mod de exemplo deve ser um pacote .zip ou .shipmod: $resolvedMod"
    }
    $resolvedExamples += $resolvedMod
}
$exampleModNames = @($resolvedExamples | ForEach-Object { [System.IO.Path]::GetFileName($_) })
if (@($exampleModNames | Sort-Object -Unique).Count -ne $exampleModNames.Count) {
    throw 'Os mods de exemplo precisam ter nomes de arquivo distintos.'
}
$exampleModDisplay = 'nenhum'
$firstExampleMod = $null
if ($exampleModNames.Count -gt 0) {
    $exampleModDisplay = $exampleModNames -join ', '
    $firstExampleMod = $exampleModNames[0]
}

$resolvedExtractorAssets = $null
if ($ExtractorAssets) {
    if (-not (Test-Path -LiteralPath $ExtractorAssets -PathType Container)) {
        throw "Pasta de assets do extrator não encontrada: $ExtractorAssets"
    }
    $resolvedExtractorAssets = (Resolve-Path -LiteralPath $ExtractorAssets).Path
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedExtractorAssets 'config.yml') -PathType Leaf)) {
        throw 'A pasta de assets do extrator precisa conter config.yml (use soh/assets/yml).'
    }
}

$resolvedControllerDatabase = $null
if ($ControllerDatabase) {
    $resolvedControllerDatabase = Resolve-InputFile -Path $ControllerDatabase -Label 'gamecontrollerdb.txt'
}

$layoutId = $null
if ($LayoutIdHeader) {
    $resolvedLayoutHeader = Resolve-InputFile -Path $LayoutIdHeader -Label 'oot_layout_id.h'
    $layoutMatch = [regex]::Match(
        [System.IO.File]::ReadAllText($resolvedLayoutHeader),
        '#define\s+LINKSPAN_OOT_LAYOUT_ID\s+"([0-9a-f]{64})"'
    )
    if (-not $layoutMatch.Success) {
        throw "LINKSPAN_OOT_LAYOUT_ID não encontrado em $resolvedLayoutHeader"
    }
    $layoutId = $layoutMatch.Groups[1].Value
    # O host publica o id nas tabelas engine/movement; sem ele no executável, exe e SDK não vêm do mesmo build.
    $executableText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($resolvedExecutable))
    if (-not $executableText.Contains($layoutId)) {
        throw "O executável não contém o layout id $layoutId de $resolvedLayoutHeader."
    }
}
$layoutDisplay = 'do host que acompanha este pacote'
if ($layoutId) {
    $layoutDisplay = $layoutId
}
$hostBaseDisplay = "Shipwright $ShipwrightVersion"
if ($HostBase) {
    $hostBaseDisplay = $HostBase
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
if ([System.IO.Path]::GetExtension($resolvedOutput) -ne '.zip') {
    throw 'OutputPath deve terminar em .zip.'
}

$outputDirectory = [System.IO.Path]::GetDirectoryName($resolvedOutput)
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$stageName = '.linkspan-overlay-' + [guid]::NewGuid().ToString('N')
$stage = Join-Path $outputDirectory $stageName
$temporaryZip = Join-Path $outputDirectory ($stageName + '.zip')
$expectedStagePrefix = [System.IO.Path]::GetFullPath($outputDirectory).TrimEnd('\') + '\.linkspan-overlay-'
if (-not ([System.IO.Path]::GetFullPath($stage).StartsWith($expectedStagePrefix, [System.StringComparison]::OrdinalIgnoreCase))) {
    throw "Diretório temporário recusado por segurança: $stage"
}
[System.IO.Directory]::CreateDirectory($stage) | Out-Null

try {
    Copy-Item -LiteralPath $resolvedExecutable -Destination (Join-Path $stage 'soh.exe')
    Copy-Item -LiteralPath $resolvedResources -Destination (Join-Path $stage 'soh.o2r')
    if ($resolvedControllerDatabase) {
        Copy-Item -LiteralPath $resolvedControllerDatabase -Destination (Join-Path $stage 'gamecontrollerdb.txt')
    }
    if ($resolvedExtractorAssets) {
        Copy-Item -LiteralPath $resolvedExtractorAssets -Destination (Join-Path $stage 'assets') -Recurse
    }

    [System.IO.Directory]::CreateDirectory((Join-Path $stage 'mods')) | Out-Null
    foreach ($resolvedMod in $resolvedExamples) {
        Copy-Item -LiteralPath $resolvedMod -Destination (Join-Path (Join-Path $stage 'mods') ([System.IO.Path]::GetFileName($resolvedMod)))
    }

    $replaces = @('soh.exe', 'soh.o2r')
    if ($resolvedControllerDatabase) {
        $replaces += 'gamecontrollerdb.txt'
    }
    $adds = @('mods')
    if ($resolvedExtractorAssets) {
        $adds += 'assets/config.yml e assets/<versão da ROM>/**/*.yml (extrator Torch)'
    }

    $metadata = [ordered]@{
        schemaVersion = 1
        packageType = 'linkspan.shipwright.overlay'
        target = [ordered]@{
            game = 'oot'
            host = 'Shipwright'
            hostVersion = $ShipwrightVersion
            hostBase = $HostBase
            platform = 'windows-x64'
        }
        linkSpan = [ordered]@{
            apiVersion = $LinkSpanVersion
            nativeProviderAbi = '1.1'
            coreExtensions = $true
            ootLayoutId = $layoutId
        }
        replaces = $replaces
        adds = $adds
        preserves = @('oot.o2r', 'oot-mq.o2r', 'mods já instalados', 'Save', 'shipofharkinian.json', 'imgui.ini')
        includesExampleMod = ($exampleModNames.Count -gt 0)
        exampleMod = $firstExampleMod
        exampleMods = $exampleModNames
    }
    Write-Utf8NoBom -Path (Join-Path $stage 'linkspan-overlay.json') -Content ($metadata | ConvertTo-Json -Depth 5)

    $changeLines = @('- soh.exe e soh.o2r formam um par compatível e devem ser substituídos juntos.')
    if ($resolvedExtractorAssets) {
        $changeLines += '- assets recebe os .yml do extrator Torch deste host. Os arquivos antigos da'
        $changeLines += '  release oficial continuam na pasta sem uso; nenhum deles é sobrescrito.'
    }
    if ($resolvedControllerDatabase) {
        $changeLines += '- gamecontrollerdb.txt é trocado pela base de controles deste build.'
    }
    $changeText = $changeLines -join [Environment]::NewLine

    $readme = @"
LINK-SPAN PARA SHIPWRIGHT $ShipwrightVersion (WINDOWS X64)
Host: $hostBaseDisplay

INSTALAÇÃO
1. Extraia a release oficial Shipwright $ShipwrightVersion em uma pasta nova.
2. Execute o Shipwright uma vez e gere/copie seu oot.o2r legítimo normalmente.
3. Feche o jogo.
4. Se a pasta já foi usada, copie shipofharkinian.json e a pasta Save para
   outro lugar.
5. Extraia este ZIP na raiz dessa instalação e confirme a substituição dos
   arquivos.
6. Inicie soh.exe.

MODS
- Coloque cada mod .zip ou .shipmod diretamente na pasta mods.
- O pacote do mod deve conter manifest.toml na raiz.
- Core extensions usam kind="core_extension", load_phase="pre_game" e ABI 1.1.
- Mods nativos que usam linkspan.oot.engine ou linkspan.oot.movement precisam
  ser compilados para o layout $layoutDisplay.
  DLLs compiladas para outros hosts são recusadas no carregamento.
- Mods incluídos neste overlay: $exampleModDisplay.

O QUE O OVERLAY MUDA
$changeText

CONTEÚDO E RESTAURAÇÃO
- Este overlay não contém ROM, oot.o2r, saves ou configuração pessoal.
- O host só pede nova extração do oot.o2r quando a versão principal gravada
  nele é diferente da sua.
- O host pode migrar shipofharkinian.json para um formato que a release
  oficial não lê de volta; para voltar a ela, restaure a cópia do passo 4.
- Para restaurar o executável oficial, extraia novamente a release oficial
  sobre a pasta ou use uma cópia limpa.
"@
    # O README é lido no Bloco de Notas: fins de linha uniformes, seja qual for o EOL do checkout.
    $readme = $readme -replace "`r?`n", "`r`n"
    Write-Utf8NoBom -Path (Join-Path $stage 'README-LinkSpan.txt') -Content $readme

    # GetRelativePath só existe no .NET do PowerShell 7; subtrair o prefixo funciona também no 5.1.
    $stageRoot = [System.IO.Path]::GetFullPath($stage).TrimEnd('\') + '\'
    function Get-StageRelativePath {
        param([Parameter(Mandatory = $true)][string]$FullName)
        return $FullName.Substring($stageRoot.Length).Replace('\', '/')
    }

    $hashLines = Get-ChildItem -LiteralPath $stage -Recurse -File |
        Where-Object Name -ne 'checksums.sha256' |
        Sort-Object FullName |
        ForEach-Object {
            $relative = Get-StageRelativePath -FullName $_.FullName
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $relative"
        }
    Write-Utf8NoBom -Path (Join-Path $stage 'checksums.sha256') -Content (($hashLines -join "`n") + "`n")

    $reproducibleTimestamp = [datetime]::SpecifyKind([datetime]'2000-01-01T00:00:00', [DateTimeKind]::Utc)
    Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
        $_.LastWriteTimeUtc = $reproducibleTimestamp
    }

    # No PowerShell 5.1, ZipFile.CreateFromDirectory grava entradas com '\' (ex.: "mods\mod.zip"),
    # separador que o formato ZIP não define; as entradas são criadas uma a uma com '/'.
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zipStream = [System.IO.File]::Open($temporaryZip, [System.IO.FileMode]::CreateNew)
    try {
        $archive = [System.IO.Compression.ZipArchive]::new($zipStream, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            Get-ChildItem -LiteralPath $stage -Recurse -Directory | Sort-Object FullName | ForEach-Object {
                if (-not (Get-ChildItem -LiteralPath $_.FullName -Force | Select-Object -First 1)) {
                    $directoryEntry = $archive.CreateEntry((Get-StageRelativePath -FullName $_.FullName) + '/')
                    $directoryEntry.LastWriteTime = [DateTimeOffset]$reproducibleTimestamp
                }
            }
            Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive,
                    $_.FullName,
                    (Get-StageRelativePath -FullName $_.FullName),
                    [System.IO.Compression.CompressionLevel]::Optimal
                ) | Out-Null
            }
        }
        finally {
            $archive.Dispose()
        }
    }
    finally {
        $zipStream.Dispose()
    }

    Move-Item -LiteralPath $temporaryZip -Destination $resolvedOutput -Force

    $verification = [System.IO.Compression.ZipFile]::OpenRead($resolvedOutput)
    try {
        $invalidEntry = $verification.Entries | Where-Object { $_.FullName.Contains('\') } | Select-Object -First 1
        if ($invalidEntry) {
            throw "ZIP gerado com separador inválido: $($invalidEntry.FullName)"
        }
        $entryCount = $verification.Entries.Count
    }
    finally {
        $verification.Dispose()
    }

    $result = [ordered]@{
        outputPath = $resolvedOutput
        sha256 = (Get-FileHash -LiteralPath $resolvedOutput -Algorithm SHA256).Hash
        size = (Get-Item -LiteralPath $resolvedOutput).Length
        entries = $entryCount
        shipwrightVersion = $ShipwrightVersion
        hostBase = $HostBase
        linkSpanVersion = $LinkSpanVersion
        ootLayoutId = $layoutId
        exampleMods = $exampleModNames
    }
    $result | ConvertTo-Json
}
finally {
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    if (Test-Path -LiteralPath $temporaryZip) {
        Remove-Item -LiteralPath $temporaryZip -Force
    }
}
