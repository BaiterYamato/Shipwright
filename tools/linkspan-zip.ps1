# ZIP determinístico para os pacotes do Link-Span (dot-source: . "$PSScriptRoot\linkspan-zip.ps1").
# Entradas com '/', manifest.toml da raiz primeiro e o resto em ordem ordinal, todas com o mesmo horário
# fixo: o mesmo conteúdo gera o mesmo ZIP byte a byte na mesma máquina. No PowerShell 5.1,
# ZipFile.CreateFromDirectory delega a ordem ao sistema de arquivos e o CreateEntryFromFile grava o horário
# real do arquivo.

function New-LinkSpanZip {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    Add-Type -AssemblyName System.IO.Compression
    $root = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\') + '\'
    $names = @(Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
            $_.FullName.Substring($root.Length).Replace('\', '/')
        })
    $rest = [string[]]@($names | Where-Object { $_ -cne 'manifest.toml' })
    [Array]::Sort($rest, [StringComparer]::Ordinal)
    $ordered = @($names | Where-Object { $_ -ceq 'manifest.toml' }) + $rest

    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Force }
    $fixed = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
    $stream = [System.IO.File]::Open($Destination, [System.IO.FileMode]::CreateNew)
    $completed = $false
    try {
        $archive = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($name in $ordered) {
                $entry = $archive.CreateEntry($name, [System.IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = $fixed
                $output = $entry.Open()
                try {
                    $bytes = [System.IO.File]::ReadAllBytes($root + $name.Replace('/', '\'))
                    $output.Write($bytes, 0, $bytes.Length)
                } finally {
                    $output.Dispose()
                }
            }
        } finally {
            $archive.Dispose()
        }
        $completed = $true
    } finally {
        $stream.Dispose()
        if (-not $completed) { Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue }
    }
}
