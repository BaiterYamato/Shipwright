#requires -Version 5.1
[CmdletBinding(DefaultParameterSetName = 'Build')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Build')]
    [string]$ArtifactDirectory,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$Version,

    # $PSScriptRoot ainda está vazio durante param() no Windows PowerShell 5.1.
    [string]$ReleaseRoot = '',

    # Filtros aplicados aos nomes dos arquivos no primeiro nivel de ArtifactDirectory.
    [string[]]$Include = @('*'),

    [Int64]$MaxFileBytes = 67108864,

    [string[]]$AllowLarge = @(),
    # Obrigatório ao montar; opcional no parameter set Verify.
    [Parameter(Mandatory = $true, ParameterSetName = 'Build')]
    [string]$ReleaseNotes,

    [Parameter(ParameterSetName = 'Verify')]
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $ReleaseRoot) { $ReleaseRoot = Join-Path $PSScriptRoot '..\build\release' }

function Write-Utf8NoBom {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Content)
    [System.IO.File]::WriteAllText($Path, $Content, (New-Object System.Text.UTF8Encoding($false)))
}

function Assert-WithinDirectory {
    param([Parameter(Mandatory = $true)][string]$Candidate, [Parameter(Mandatory = $true)][string]$Parent)
    $parentFull = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    $candidateFull = [System.IO.Path]::GetFullPath($Candidate)
    if (-not $candidateFull.StartsWith($parentFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Caminho fora do diretório permitido: $Candidate"
    }
}

function Get-TextZipEntry {
    param([Parameter(Mandatory = $true)]$Entry)
    $stream = $Entry.Open()
    try {
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8, $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    }
    finally { $stream.Dispose() }
}

function Get-Python {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) { return [pscustomobject]@{ Source = $py.Source; Prefix = @('-3') } }
    $python = Get-Command python -ErrorAction Stop
    return [pscustomobject]@{ Source = $python.Source; Prefix = @() }
}

# Antes de montar a release: sem tomllib (Python < 3.11) nem tomli, todo manifesto pareceria inválido.
function Assert-ManifestReader {
    $reader = Join-Path $PSScriptRoot 'read-linkspan-manifest.py'
    $python = Get-Python
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $output = & $python.Source @($python.Prefix) $reader --check 2>&1 }
    finally { $ErrorActionPreference = $previousPreference }
    if ($LASTEXITCODE -ne 0) { throw "Leitor de manifest.toml indisponível em $($python.Source): $(@($output) -join ' ')" }
}

# Regex sobre o TOML aprovava hash citado em comentário e pulava chave entre aspas; o tomllib lê como o host.
function Read-ManifestToml {
    param([Parameter(Mandatory = $true)][string]$Toml, [Parameter(Mandatory = $true)][string]$Package)
    $reader = Join-Path $PSScriptRoot 'read-linkspan-manifest.py'
    $python = Get-Python
    $temp = [System.IO.Path]::GetTempFileName()
    try {
        [System.IO.File]::WriteAllText($temp, $Toml, (New-Object System.Text.UTF8Encoding($false)))
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { $output = & $python.Source @($python.Prefix) $reader $temp 2>&1 }
        finally { $ErrorActionPreference = $previousPreference }
        if ($LASTEXITCODE -ne 0) { throw "manifest.toml recusado em ${Package}: $(@($output) -join ' ')" }
        return (@($output) -join "`n") | ConvertFrom-Json
    }
    finally { Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue }
}

# Convenção: <nome>-<semver>[-layout-<id hex>[-<hash hex>]].<ext>. O pré-release SemVer pode ter '-', então só o
# sufixo de layout do fim sai antes; um "-layout-" no meio do nome ou do pré-release fica.
function Get-FileVersionFromName {
    param([Parameter(Mandatory = $true)][string]$Name)
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Name)
    $stem = [regex]::Replace($stem, '(?i)-layout-[0-9a-f]{8,64}(?:-[0-9a-f]{8,64})?$', '')
    $match = [regex]::Match($stem, '-(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?)$')
    if ($match.Success) { return $match.Groups[1].Value }
    if ($stem -match '\d+\.\d+\.\d+') { throw "Versão no nome do pacote não segue <nome>-<semver>: $Name" }
    return $null
}

function Get-FileLayoutFromName {
    param([Parameter(Mandatory = $true)][string]$Name)
    $match = [regex]::Match($Name, '(?i)layout-([0-9a-f]{8,64})')
    if ($match.Success) { return $match.Groups[1].Value.ToLowerInvariant() }
    return $null
}

function Test-SafeZipEntryName {
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][string]$Package)
    if ($Name.Contains('\') -or $Name.StartsWith('/') -or $Name -match '(^|/)\.\.(/|$)' -or $Name.IndexOf([char]0) -ge 0) {
        throw "Entrada ZIP insegura em ${Package}: $Name"
    }
}

if (-not ('LinkSpanCrc32' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class LinkSpanCrc32 {
    private static readonly uint[] Table = MakeTable();
    private static uint[] MakeTable() {
        var table = new uint[256];
        for (uint i = 0; i < 256; i++) {
            uint value = i;
            for (int bit = 0; bit < 8; bit++) value = (value & 1) != 0 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
            table[i] = value;
        }
        return table;
    }
    public static uint Compute(Stream input) {
        uint crc = 0xFFFFFFFFu;
        var buffer = new byte[1024 * 1024];
        int read;
        while ((read = input.Read(buffer, 0, buffer.Length)) > 0) {
            for (int i = 0; i < read; i++) crc = Table[(crc ^ buffer[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }
}
'@
}

function Get-ZipInventory {
    param([Parameter(Mandatory = $true)][string]$Path)
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entries = @()
        foreach ($entry in $archive.Entries) {
            Test-SafeZipEntryName -Name $entry.FullName -Package $Path
            $crc = [uint32]0
            if (-not $entry.FullName.EndsWith('/')) {
                $stream = $entry.Open()
                try { $crc = [LinkSpanCrc32]::Compute($stream) } finally { $stream.Dispose() }
            }
            $entries += [ordered]@{
                path = $entry.FullName
                size = [Int64]$entry.Length
                crc32 = ('{0:x8}' -f $crc)
            }
        }

        $manifest = $archive.Entries | Where-Object { $_.FullName -eq 'manifest.toml' } | Select-Object -First 1
        $overlay = $archive.Entries | Where-Object { $_.FullName -eq 'linkspan-overlay.json' } | Select-Object -First 1
        $sdk = $archive.Entries | Where-Object { $_.FullName -eq 'linkspan-sdk.json' } | Select-Object -First 1
        $id = $null
        $packageVersion = $null
        $layoutId = Get-FileLayoutFromName -Name ([System.IO.Path]::GetFileName($Path))
        $layoutSource = if ($layoutId) { 'filename' } else { $null }
        $hostRequired = $null
        $hostFingerprints = @()
        $hostSha256 = $null
        $metadataSource = $null
        $fileName = [System.IO.Path]::GetFileName($Path)
        if ($manifest) {
            $fields = Read-ManifestToml -Toml (Get-TextZipEntry -Entry $manifest) -Package $fileName
            $id = [string]$fields.id
            $packageVersion = [string]$fields.version
            # O manifesto de mod nativo é gerado no configure do CMake; um build velho chegou a sair com o nome
            # da versão nova e o manifesto (versão e fingerprint) da anterior.
            $nameVersion = Get-FileVersionFromName -Name $fileName
            if ($nameVersion -and $packageVersion -and $nameVersion -cne $packageVersion) {
                throw "Versão no nome ($nameVersion) difere do manifest.toml ($packageVersion): $fileName"
            }
            $hostFingerprints = @($fields.hostFingerprints)
            $manifestLayout = [string]$fields.layoutId
            if ($manifestLayout) { $layoutId = $manifestLayout; $layoutSource = 'manifest.toml' }
            $metadataSource = 'manifest.toml'
        }
        elseif ($overlay) {
            $overlayJson = Get-TextZipEntry -Entry $overlay | ConvertFrom-Json
            $id = [string]$overlayJson.packageType
            $packageVersion = [string]$overlayJson.linkSpan.apiVersion
            $layoutId = [string]$overlayJson.linkSpan.ootLayoutId
            $layoutSource = 'linkspan-overlay.json'
            $hostRequired = (([string]$overlayJson.target.host) + ' ' + ([string]$overlayJson.target.hostVersion)).Trim()
            if ($overlayJson.target.hostBase) { $hostRequired += ' (' + [string]$overlayJson.target.hostBase + ')' }
            $hostEntry = $archive.Entries | Where-Object { $_.FullName -eq 'soh.exe' } | Select-Object -First 1
            if ($hostEntry) {
                $hostStream = $hostEntry.Open()
                $sha = [System.Security.Cryptography.SHA256]::Create()
                try { $hostSha256 = ([BitConverter]::ToString($sha.ComputeHash($hostStream)) -replace '-', '').ToLowerInvariant() }
                finally { $sha.Dispose(); $hostStream.Dispose() }
            }
            $metadataSource = 'linkspan-overlay.json (overlay sem manifest.toml)'
        }
        elseif ($sdk) {
            $sdkJson = Get-TextZipEntry -Entry $sdk | ConvertFrom-Json
            if ($sdkJson.schemaVersion -ne 1 -or $sdkJson.packageType -cne 'linkspan.oot.sdk' -or
                $sdkJson.platform -cne 'windows-x64' -or $sdkJson.configuration -cne 'Release' -or
                $sdkJson.ootLayoutId -cnotmatch '^[0-9a-f]{64}$' -or
                $sdkJson.hostSha256 -cnotmatch '^[0-9a-f]{64}$') {
                throw "Metadados SDK inválidos: $Path"
            }
            $id = [string]$sdkJson.packageType
            $packageVersion = [string]$sdkJson.version
            $layoutId = [string]$sdkJson.ootLayoutId
            $layoutSource = 'linkspan-sdk.json'
            $hostRequired = [string]$sdkJson.hostRequired
            $hostFingerprints = @([string]$sdkJson.hostSha256)
            $expectedName = 'LinkSpan-OoT-SDK-' + $packageVersion + '-layout-' +
                $layoutId.Substring(0, 8) + '-Win64.zip'
            if ($fileName -cne $expectedName) { throw "Nome/metadados SDK divergem: $fileName" }
            if (-not $archive.GetEntry('lib/cmake/LinkSpanOotSdk/LinkSpanOotSdkConfig.cmake') -or
                -not $archive.GetEntry('include/oot_layout_id.h')) { throw "SDK incompleto: $Path" }
            # Não preencher hostSha256: o SDK não contém um soh.exe.
            $metadataSource = 'linkspan-sdk.json'
        }
        elseif ([System.IO.Path]::GetExtension($Path) -ieq '.o2r') {
            # Arquivo de recursos solto em mods/ (ex.: dados do Unbound): o VFS do host monta, sem manifesto.
            $id = [System.IO.Path]::GetFileNameWithoutExtension($Path)
            $packageVersion = '-'
            $metadataSource = 'arquivo de recursos .o2r (sem manifest.toml)'
        }
        else {
            throw "Pacote sem manifest.toml nem linkspan-overlay.json: $Path"
        }
        if (-not $id -or -not $packageVersion) { throw "Metadados obrigatórios ausentes em $Path" }
        return [ordered]@{
            file = [System.IO.Path]::GetFileName($Path)
            id = $id
            version = $packageVersion
            layoutId = $layoutId
            layoutSource = $layoutSource
            hostRequired = $hostRequired
            hostFingerprints = $hostFingerprints
            hostSha256 = $hostSha256
            metadataSource = $metadataSource
            size = [Int64](Get-Item -LiteralPath $Path).Length
            sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
            entries = $entries
        }
    }
    finally { $archive.Dispose() }
}

function Invoke-ProtectedScanner {
    param([Parameter(Mandatory = $true)][string[]]$Paths, [Parameter(Mandatory = $true)][string]$JsonOut, [Parameter(Mandatory = $true)][string]$TextOut)
    $scanner = Join-Path $PSScriptRoot 'scan-protected-content.py'
    if (-not (Test-Path -LiteralPath $scanner -PathType Leaf)) { throw "Scanner não encontrado: $scanner" }
    $python = Get-Python
    $scannerArgs = @($python.Prefix)
    $scannerArgs += @($scanner, '--max-file-bytes', [string]$MaxFileBytes, '--json-out', $JsonOut, '--text-out', $TextOut)
    foreach ($pattern in $AllowLarge) { $scannerArgs += @('--allow-large', $pattern) }
    $scannerArgs += $Paths
    # O scanner manda o JSON no stdout e o resumo no stderr; os dois já ficam em arquivo. No PowerShell 5.1,
    # stderr de executável com a saída redirecionada vira erro terminante sob ErrorActionPreference Stop.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $python.Source @scannerArgs 2>&1 | Out-Null }
    finally { $ErrorActionPreference = $previousPreference }
    if ($LASTEXITCODE -ne 0) { throw "Scanner de conteúdo protegido recusou os artefatos; veja $TextOut" }
    Write-Host (Get-Content -LiteralPath $TextOut -TotalCount 1)
}

function Invoke-ReleaseVerification {
    param([Parameter(Mandatory = $true)][string]$Directory)
    $sums = Join-Path $Directory 'SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $sums -PathType Leaf)) { throw "SHA256SUMS.txt ausente: $sums" }
    $verified = 0
    foreach ($line in [System.IO.File]::ReadAllLines($sums)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $match = [regex]::Match($line, '^([0-9a-fA-F]{64})  (.+)$')
        if (-not $match.Success) { throw "Linha inválida em SHA256SUMS.txt: $line" }
        $relative = $match.Groups[2].Value
        if ($relative.Contains('\') -or [System.IO.Path]::IsPathRooted($relative) -or $relative -match '(^|/)\.\.(/|$)') {
            throw "Caminho inseguro em SHA256SUMS.txt: $relative"
        }
        $file = Join-Path $Directory $relative
        Assert-WithinDirectory -Candidate $file -Parent $Directory
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Arquivo listado ausente: $relative" }
        $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $match.Groups[1].Value.ToLowerInvariant()) { throw "SHA-256 divergente: $relative" }
        $verified++
    }
    if ($verified -eq 0) { throw 'SHA256SUMS.txt não contém pacotes.' }
    [ordered]@{ verified = $verified; release = $Directory } | ConvertTo-Json
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$resolvedReleaseRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReleaseRoot)
$releaseDirectory = Join-Path $resolvedReleaseRoot $Version

if ($Verify) {
    if (-not (Test-Path -LiteralPath $releaseDirectory -PathType Container)) { throw "Release inexistente: $releaseDirectory" }
    Invoke-ReleaseVerification -Directory $releaseDirectory
    exit 0
}

if (-not (Test-Path -LiteralPath $ArtifactDirectory -PathType Container)) { throw "Pasta de artefatos inexistente: $ArtifactDirectory" }
if (Test-Path -LiteralPath $releaseDirectory) { throw "A release já existe e não será sobrescrita: $releaseDirectory" }
[System.IO.Directory]::CreateDirectory($resolvedReleaseRoot) | Out-Null
$artifactRoot = (Resolve-Path -LiteralPath $ArtifactDirectory).Path
$selected = @(
    Get-ChildItem -LiteralPath $artifactRoot -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in @('.zip', '.shipmod', '.o2r') } |
    Where-Object {
        $includeThis = $false
        foreach ($pattern in $Include) { if ($_.Name -like $pattern) { $includeThis = $true; break } }
        $includeThis
    } |
    Sort-Object Name
)
if ($selected.Count -eq 0) { throw "Nenhum .zip, .shipmod ou .o2r selecionado em $artifactRoot" }
if (-not (Test-Path -LiteralPath $ReleaseNotes -PathType Leaf)) { throw "Notas ausentes: $ReleaseNotes" }
if ([IO.File]::ReadAllText((Resolve-Path -LiteralPath $ReleaseNotes).Path) -match '\{\{[^}]+\}\}') {
    throw 'Preencha os marcadores das release notes antes de montar o candidato final.'
}
Assert-ManifestReader
$names = @($selected | ForEach-Object { $_.Name })
if (@($names | Sort-Object -Unique).Count -ne $names.Count) { throw 'Nomes de pacote duplicados não são permitidos.' }

$stage = Join-Path $resolvedReleaseRoot ('.linkspan-release-' + [guid]::NewGuid().ToString('N'))
Assert-WithinDirectory -Candidate $stage -Parent $resolvedReleaseRoot
[System.IO.Directory]::CreateDirectory($stage) | Out-Null
try {
    Invoke-ProtectedScanner -Paths @($selected | ForEach-Object { $_.FullName }) -JsonOut (Join-Path $stage 'protected-content.json') -TextOut (Join-Path $stage 'protected-content.txt')
    foreach ($file in $selected) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $stage $file.Name) }
    Copy-Item -LiteralPath $ReleaseNotes -Destination (Join-Path $stage 'RELEASE-NOTES.md')
    Invoke-ProtectedScanner -Paths @($stage) -JsonOut (Join-Path $stage 'protected-content.json') -TextOut (Join-Path $stage 'protected-content.txt')

    $packages = @()
    foreach ($file in (Get-ChildItem -LiteralPath $stage -File | Where-Object { $_.Extension.ToLowerInvariant() -in @('.zip', '.shipmod', '.o2r') } | Sort-Object Name)) {
        $packages += Get-ZipInventory -Path $file.FullName
    }
    # Mods normalmente declaram apenas o prefixo de oito caracteres no nome do pacote.
    $sdkPackages = @($packages | Where-Object { $_.id -eq 'linkspan.oot.sdk' })
    if ($sdkPackages.Count -ne 1 -or $sdkPackages[0].version -cne $Version) {
        throw 'A release exige exatamente um SDK da mesma versão.'
    }
    if (@($packages | Where-Object { $_.hostSha256 }).Count -ne 1) { throw 'A release exige exatamente um overlay para conferir o SDK.' }
    # O overlay traz o ID completo: complete-o apenas quando houver uma única correspondência.
    $fullLayouts = @($packages | Where-Object { $_.layoutId -and $_.layoutId.Length -eq 64 } | ForEach-Object { $_.layoutId } | Sort-Object -Unique)
    foreach ($package in $packages) {
        if ($package.layoutId -and $package.layoutId.Length -lt 64) {
            $matches = @($fullLayouts | Where-Object { $_.StartsWith($package.layoutId, [System.StringComparison]::OrdinalIgnoreCase) })
            if ($matches.Count -eq 1) {
                $package.layoutId = $matches[0]
                $package.layoutSource = $package.layoutSource + ' (completado pelo overlay)'
            }
        }
    }
    # Mods com host_fingerprints só resolvem símbolos no soh.exe que fixaram: precisam citar o do overlay.
    $overlayHosts = @($packages | Where-Object { $_.hostSha256 } | ForEach-Object { $_.hostSha256 })
    if ($overlayHosts.Count -gt 0) {
        foreach ($package in $packages) {
            if (@($package.hostFingerprints).Count -eq 0) { continue }
            $hit = @($package.hostFingerprints | Where-Object { $overlayHosts -contains $_ })
            if ($hit.Count -eq 0) {
                throw "host_fingerprints de $($package.file) não citam o soh.exe do overlay ($($overlayHosts -join ', '))"
            }
        }
    }
    $notesPath = Join-Path $stage 'RELEASE-NOTES.md'
    $attachments = @([ordered]@{
        file = 'RELEASE-NOTES.md'; type = 'release-notes'
        size = [Int64](Get-Item -LiteralPath $notesPath).Length
        sha256 = (Get-FileHash -LiteralPath $notesPath -Algorithm SHA256).Hash.ToLowerInvariant()
    })
    # Campo aditivo; packages mantém a estrutura anterior.
    $inventory = [ordered]@{ schemaVersion = 1; releaseVersion = $Version; packages = $packages; attachments = $attachments }
    Write-Utf8NoBom -Path (Join-Path $stage 'inventory.json') -Content (($inventory | ConvertTo-Json -Depth 10) + "`n")
    # Sort-Object com nome de propriedade não enxerga a chave de um [ordered]; o bloco de script, sim.
    $sumLines = @((@($packages) + @($attachments)) | Sort-Object { $_.file } | ForEach-Object { $_.sha256 + '  ' + $_.file })
    Write-Utf8NoBom -Path (Join-Path $stage 'SHA256SUMS.txt') -Content (($sumLines -join "`n") + "`n")

    $md = @('# Link-Span OoT ' + $Version, '', '## Pacotes', '', '| Arquivo | ID | Versão | Layout | Host exigido | SHA-256 |', '|---|---|---|---|---|---|')
    foreach ($package in $packages) {
        $layout = if ($package.layoutId) { $package.layoutId } else { 'não declarado' }
        $requiredHost = if ($package.hostRequired) { $package.hostRequired } else { 'conforme manifest.toml / host compatível' }
        $md += '| `' + $package.file + '` | `' + $package.id + '` | ' + $package.version + ' | `' + $layout + '` | ' + $requiredHost + ' | `' + $package.sha256 + '` |'
    }
    $md += @('', '## Instalação e rollback', '', '- Instale o overlay sobre Shipwright 9.2.3 fresco; execute uma vez para validar os assets locais.', '- Copie os mods independentes para `mods/` e reinicie.', '- Atualize com o jogo fechado: tire a versão anterior de `mods/` (guarde a cópia para voltar) e copie a nova. O save preserva os blocos de mods ausentes.', '- Mod que derruba o boot é desativado no início seguinte (lista `mods/.shiplua-disabled`, sem apagar arquivo); apague a linha da lista para reativar.', '- O scanner `protected-content.txt` deve permanecer OK; nenhum ROM, archive derivado ou save é distribuído.')
    $md += @('', '## Autores e limitações', '', 'Consulte [RELEASE-NOTES.md](RELEASE-NOTES.md). O pacote SDK listado acima contém guias PT/EN, contrato OoT e fontes; extraia-o fora de mods/.')
    $md += @('', '## Licenças', '',
        '- O trabalho próprio do Link-Span está em domínio público pela CC0 1.0 Universal. Cada pacote traz `LICENSE` e `NOTICE.md`; no overlay, `LICENSE-LinkSpan.txt`, `NOTICE-LinkSpan.md` e `THIRD-PARTY-NOTICES-LinkSpan.md`, com os componentes de terceiros do `soh.exe`.',
        '- Shipwright, Not Enough Items (skijer), Unbound e Wind Waker Style (roborich) não publicam licença. O código portado deles segue com seus autores, e esta release não concede licença sobre ele.',
        '- `LinkSpan-Unbound09-Actors-Demo.o2r` é só dados, sem documentos internos: os atores adaptam o exemplo oficial do Unbound 0.9 (roborich) e usam modelos do jogo extraídos pelo próprio jogador; o restante é trabalho do Link-Span sob a CC0.')
    Write-Utf8NoBom -Path (Join-Path $stage 'RELEASE.md') -Content (($md -join "`n") + "`n")
    Invoke-ProtectedScanner -Paths @($stage) -JsonOut (Join-Path $stage 'protected-content.json') -TextOut (Join-Path $stage 'protected-content.txt')
    Move-Item -LiteralPath $stage -Destination $releaseDirectory
    [ordered]@{ release = $releaseDirectory; packages = $packages.Count; sha256Sums = 'SHA256SUMS.txt'; inventory = 'inventory.json' } | ConvertTo-Json
}
finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
