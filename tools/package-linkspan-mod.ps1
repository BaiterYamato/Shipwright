[CmdletBinding()]
param(
    # Pasta do mod com manifest.toml na raiz (ex.: build/<mod>/mod gerada pelo CMake do mod).
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\') + '\'
if (-not (Test-Path -LiteralPath (Join-Path $root 'manifest.toml') -PathType Leaf)) {
    throw "manifest.toml não encontrado na raiz de $Source"
}

$target = [System.IO.Path]::GetFullPath($OutputPath)
if ([System.IO.Path]::GetExtension($target) -notin @('.zip', '.shipmod')) {
    throw 'OutputPath deve terminar em .zip ou .shipmod.'
}
if (Test-Path -LiteralPath $target) {
    throw "Destino já existe: $target"
}
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($target)) | Out-Null

# Entradas com '/', manifest da raiz primeiro, ordem ordinal e horário fixo (linkspan-zip.ps1).
. (Join-Path $PSScriptRoot 'linkspan-zip.ps1')
New-LinkSpanZip -Source $root -Destination $target
Add-Type -AssemblyName System.IO.Compression.FileSystem
$read = [System.IO.Compression.ZipFile]::OpenRead($target)
try { $entryNames = @($read.Entries | ForEach-Object { $_.FullName }) } finally { $read.Dispose() }

[ordered]@{
    outputPath = $target
    sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    entries = $entryNames
} | ConvertTo-Json
