[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$HostExecutable,

    [Parameter(Mandatory = $true)]
    [string]$HostResources,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$ExampleMod,

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
$resolvedExample = $null
$exampleModName = $null
if ($ExampleMod) {
    $resolvedExample = Resolve-InputFile -Path $ExampleMod -Label 'Mod de exemplo'
    if ([System.IO.Path]::GetExtension($resolvedExample) -notin @('.zip', '.shipmod')) {
        throw 'O mod de exemplo deve ser um pacote .zip ou .shipmod.'
    }
    $exampleModName = [System.IO.Path]::GetFileName($resolvedExample)
}
$exampleModDisplay = if ($exampleModName) { $exampleModName } else { 'nenhum' }

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

    [System.IO.Directory]::CreateDirectory((Join-Path $stage 'mods')) | Out-Null
    if ($resolvedExample) {
        Copy-Item -LiteralPath $resolvedExample -Destination (Join-Path $stage "mods\$exampleModName")
    }

    $metadata = [ordered]@{
        schemaVersion = 1
        packageType = 'linkspan.shipwright.overlay'
        target = [ordered]@{
            game = 'oot'
            host = 'Shipwright'
            hostVersion = $ShipwrightVersion
            platform = 'windows-x64'
        }
        linkSpan = [ordered]@{
            apiVersion = $LinkSpanVersion
            nativeProviderAbi = '1.1'
            coreExtensions = $true
        }
        replaces = @('soh.exe', 'soh.o2r')
        preserves = @('oot.o2r', 'mods', 'saves', 'configuração do usuário')
        includesExampleMod = [bool]$resolvedExample
        exampleMod = $exampleModName
    }
    Write-Utf8NoBom -Path (Join-Path $stage 'linkspan-overlay.json') -Content ($metadata | ConvertTo-Json -Depth 5)

    $readme = @"
LINK-SPAN PARA SHIPWRIGHT $ShipwrightVersion (WINDOWS X64)

INSTALAÇÃO
1. Extraia a release oficial Shipwright $ShipwrightVersion em uma pasta nova.
2. Execute o Shipwright uma vez e gere/copie seu oot.o2r legítimo normalmente.
3. Feche o jogo.
4. Extraia este ZIP na raiz dessa instalação e confirme a substituição de
   soh.exe e soh.o2r.
5. Inicie soh.exe.

MODS
- Coloque cada mod .zip ou .shipmod diretamente na pasta mods.
- O pacote do mod deve conter manifest.toml na raiz.
- Core extensions usam kind="core_extension", load_phase="pre_game" e ABI 1.1.
- Mod incluído neste overlay: $exampleModDisplay.

CONTEÚDO E RESTAURAÇÃO
- Este overlay não contém ROM, oot.o2r, saves ou configuração pessoal.
- soh.exe e soh.o2r formam um par compatível e devem ser substituídos juntos.
- Para restaurar o executável oficial, extraia novamente a release oficial
  sobre a pasta ou use uma cópia limpa.
"@
    Write-Utf8NoBom -Path (Join-Path $stage 'README-LinkSpan.txt') -Content $readme

    $hashLines = Get-ChildItem -LiteralPath $stage -Recurse -File |
        Where-Object Name -ne 'checksums.sha256' |
        Sort-Object FullName |
        ForEach-Object {
            $relative = [System.IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\', '/')
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $relative"
        }
    Write-Utf8NoBom -Path (Join-Path $stage 'checksums.sha256') -Content (($hashLines -join "`n") + "`n")

    $reproducibleTimestamp = [datetime]::SpecifyKind([datetime]'2000-01-01T00:00:00', [DateTimeKind]::Utc)
    Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
        $_.LastWriteTimeUtc = $reproducibleTimestamp
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $stage,
        $temporaryZip,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false
    )

    Move-Item -LiteralPath $temporaryZip -Destination $resolvedOutput -Force

    $result = [ordered]@{
        outputPath = $resolvedOutput
        sha256 = (Get-FileHash -LiteralPath $resolvedOutput -Algorithm SHA256).Hash
        size = (Get-Item -LiteralPath $resolvedOutput).Length
        shipwrightVersion = $ShipwrightVersion
        linkSpanVersion = $LinkSpanVersion
        includesExampleMod = [bool]$resolvedExample
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
