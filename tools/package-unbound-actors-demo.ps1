[CmdletBinding()]
param(
    [string]$OutputPath = 'build\unbound-actors-demo\LinkSpan-Unbound09-Atores-Demo.o2r',
    [string]$Converter = 'build\unbound-scene-native\Release\linkspan_unbound_convert.exe'
)
$ErrorActionPreference = 'Stop'
$assets = (Resolve-Path -LiteralPath 'soh\native-sdk\unbound-core\custom-actors-demo\assets').Path
& $Converter --check $assets
if ($LASTEXITCODE -ne 0) { throw 'Os documentos de demonstração foram recusados.' }
$destination = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $destination) { throw 'O pacote de destino já existe.' }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
Add-Type -AssemblyName System.IO.Compression
$stream = [IO.File]::Open($destination, [IO.FileMode]::CreateNew)
try {
    $zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in (Get-ChildItem -LiteralPath $assets -Recurse -File | Sort-Object FullName)) {
            $name = $file.FullName.Substring($assets.Length + 1).Replace('\', '/')
            $entry = $zip.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
            $content = $entry.Open()
            try { $bytes = [IO.File]::ReadAllBytes($file.FullName); $content.Write($bytes, 0, $bytes.Length) }
            finally { $content.Dispose() }
        }
    } finally { $zip.Dispose() }
} finally { $stream.Dispose() }
Write-Output $destination
