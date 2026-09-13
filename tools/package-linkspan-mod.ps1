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

# No PowerShell 5.1, ZipFile.CreateFromDirectory grava entradas com '\'. As entradas são criadas uma a
# uma com '/', com o manifest da raiz primeiro.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$files = @(Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object @{
        Expression = { $_.Name -ne 'manifest.toml' -or ($_.DirectoryName.TrimEnd('\') + '\') -ine $root }
    }, FullName)
$entryNames = @($files | ForEach-Object { $_.FullName.Substring($root.Length).Replace('\', '/') })

$stream = [System.IO.File]::Open($target, [System.IO.FileMode]::CreateNew)
try {
    $archive = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        for ($i = 0; $i -lt $files.Count; $i++) {
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive,
                $files[$i].FullName,
                $entryNames[$i],
                [System.IO.Compression.CompressionLevel]::Optimal
            ) | Out-Null
        }
    }
    finally {
        $archive.Dispose()
    }
}
catch {
    $stream.Dispose()
    Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
    throw
}
$stream.Dispose()

[ordered]@{
    outputPath = $target
    sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    entries = $entryNames
} | ConvertTo-Json
