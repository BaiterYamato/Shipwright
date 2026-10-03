#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$FirstDirectory,
    [Parameter(Mandatory = $true)][string]$SecondDirectory,
    # $PSScriptRoot ainda está vazio durante param() no Windows PowerShell 5.1.
    [string]$ReportPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $ReportPath) { $ReportPath = Join-Path $PSScriptRoot 'reproducibility-report.json' }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-RelativePackageMap {
    param([Parameter(Mandatory = $true)][string]$Root)
    $rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\') + '\'
    $map = @{}
    Get-ChildItem -LiteralPath $rootPath -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in @('.zip', '.shipmod', '.o2r') } |
        Sort-Object FullName | ForEach-Object {
            $relative = $_.FullName.Substring($rootPath.Length).Replace('\', '/')
            if ($map.ContainsKey($relative)) { throw "Pacote duplicado: $relative" }
            $map[$relative] = $_.FullName
        }
    return $map
}

function Get-StreamSha256 {
    param([Parameter(Mandatory = $true)]$Stream)
    $hash = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $hash.ComputeHash($Stream)
        return ([System.BitConverter]::ToString($bytes).Replace('-', '').ToLowerInvariant())
    }
    finally { $hash.Dispose() }
}

function Get-ZipEntryHash {
    param([Parameter(Mandatory = $true)]$Entry)
    $stream = $Entry.Open()
    try { return Get-StreamSha256 -Stream $stream } finally { $stream.Dispose() }
}

function Get-EntryMetadata {
    param([Parameter(Mandatory = $true)]$Entry)
    return [ordered]@{
        timestampUtc = $Entry.LastWriteTime.UtcDateTime.ToString('o')
        externalAttributes = [Int64]$Entry.ExternalAttributes
        size = [Int64]$Entry.Length
        compressedSize = [Int64]$Entry.CompressedLength
    }
}

function Compare-ZipPackage {
    param([Parameter(Mandatory = $true)][string]$Relative, [Parameter(Mandatory = $true)][string]$First, [Parameter(Mandatory = $true)][string]$Second)
    $left = [System.IO.Compression.ZipFile]::OpenRead($First)
    $right = [System.IO.Compression.ZipFile]::OpenRead($Second)
    try {
        $differences = @()
        $leftOrder = @($left.Entries | ForEach-Object { $_.FullName })
        $rightOrder = @($right.Entries | ForEach-Object { $_.FullName })
        if (($leftOrder -join "`0") -ne ($rightOrder -join "`0")) {
            $differences += [ordered]@{ kind = 'entry_order'; first = $leftOrder; second = $rightOrder }
        }
        $max = [Math]::Max($left.Entries.Count, $right.Entries.Count)
        for ($index = 0; $index -lt $max; $index++) {
            if ($index -ge $left.Entries.Count) { $differences += [ordered]@{ kind = 'extra_entry_second'; index = $index; entry = $right.Entries[$index].FullName }; continue }
            if ($index -ge $right.Entries.Count) { $differences += [ordered]@{ kind = 'extra_entry_first'; index = $index; entry = $left.Entries[$index].FullName }; continue }
            $a = $left.Entries[$index]
            $b = $right.Entries[$index]
            if ($a.FullName -ne $b.FullName) {
                $differences += [ordered]@{ kind = 'entry_name'; index = $index; first = $a.FullName; second = $b.FullName }
                continue
            }
            $firstMetadata = Get-EntryMetadata -Entry $a
            $secondMetadata = Get-EntryMetadata -Entry $b
            if (($firstMetadata | ConvertTo-Json -Compress) -ne ($secondMetadata | ConvertTo-Json -Compress)) {
                $differences += [ordered]@{ kind = 'entry_metadata'; index = $index; entry = $a.FullName; first = $firstMetadata; second = $secondMetadata }
            }
            if (-not $a.FullName.EndsWith('/')) {
                $firstHash = Get-ZipEntryHash -Entry $a
                $secondHash = Get-ZipEntryHash -Entry $b
                if ($firstHash -ne $secondHash) {
                    $differences += [ordered]@{ kind = 'entry_content'; index = $index; entry = $a.FullName; firstSha256 = $firstHash; secondSha256 = $secondHash }
                }
            }
        }
        $outerFirst = (Get-FileHash -LiteralPath $First -Algorithm SHA256).Hash.ToLowerInvariant()
        $outerSecond = (Get-FileHash -LiteralPath $Second -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($outerFirst -ne $outerSecond) { $differences += [ordered]@{ kind = 'archive_bytes'; firstSha256 = $outerFirst; secondSha256 = $outerSecond }
        }
        return [ordered]@{ package = $Relative; zip = $true; equal = ($differences.Count -eq 0); differences = $differences }
    }
    finally { $left.Dispose(); $right.Dispose() }
}

function Compare-Package {
    param([Parameter(Mandatory = $true)][string]$Relative, [Parameter(Mandatory = $true)][string]$First, [Parameter(Mandatory = $true)][string]$Second)
    try { return Compare-ZipPackage -Relative $Relative -First $First -Second $Second }
    catch [System.IO.InvalidDataException] {
        $firstHash = (Get-FileHash -LiteralPath $First -Algorithm SHA256).Hash.ToLowerInvariant()
        $secondHash = (Get-FileHash -LiteralPath $Second -Algorithm SHA256).Hash.ToLowerInvariant()
        $diff = @()
        if ($firstHash -ne $secondHash) { $diff += [ordered]@{ kind = 'file_bytes'; firstSha256 = $firstHash; secondSha256 = $secondHash } }
        return [ordered]@{ package = $Relative; zip = $false; equal = ($diff.Count -eq 0); differences = $diff }
    }
}

$first = Get-RelativePackageMap -Root $FirstDirectory
$second = Get-RelativePackageMap -Root $SecondDirectory
$onlyFirst = @($first.Keys | Where-Object { -not $second.ContainsKey($_) } | Sort-Object)
$onlySecond = @($second.Keys | Where-Object { -not $first.ContainsKey($_) } | Sort-Object)
$common = @($first.Keys | Where-Object { $second.ContainsKey($_) } | Sort-Object)
$packages = @()
foreach ($relative in $common) { $packages += Compare-Package -Relative $relative -First $first[$relative] -Second $second[$relative] }
$different = @($packages | Where-Object { -not $_.equal })
$result = [ordered]@{
    schemaVersion = 1
    firstDirectory = (Resolve-Path -LiteralPath $FirstDirectory).Path
    secondDirectory = (Resolve-Path -LiteralPath $SecondDirectory).Path
    onlyFirst = $onlyFirst
    onlySecond = $onlySecond
    packages = $packages
    summary = [ordered]@{ common = $common.Count; onlyFirst = $onlyFirst.Count; onlySecond = $onlySecond.Count; different = $different.Count; reproducible = ($onlyFirst.Count -eq 0 -and $onlySecond.Count -eq 0 -and $different.Count -eq 0) }
}
$resolvedReport = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)
[System.IO.File]::WriteAllText($resolvedReport, (($result | ConvertTo-Json -Depth 12) + "`n"), (New-Object System.Text.UTF8Encoding($false)))
if ($result.summary.reproducible) {
    "REPRODUCIBLE: $($result.summary.common) pacote(s) idêntico(s). Relatório: $resolvedReport"
    exit 0
}
"NÃO REPRODUCIBLE: $($result.summary.different) pacote(s) diferente(s), $($result.summary.onlyFirst + $result.summary.onlySecond) ausente(s). Relatório: $resolvedReport"
exit 1
