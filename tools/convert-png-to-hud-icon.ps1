param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$PreviewPath
)

# Converte um PNG em textura RGBA32 32x32 no formato de recurso do libultraship (OTEX), o mesmo dos
# ícones de textures/icon_item_static. O resultado serve para ship.hud.draw_icon depois que o
# provider monta a pasta de assets do mod com mount_archive.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$size = 32
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$image = [System.Drawing.Image]::FromFile($sourcePath)
$bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        # Mantém a proporção e centraliza no quadrado; TileFlipXY evita borda escura no reamostrado.
        $scale = [Math]::Min($size / $image.Width, $size / $image.Height)
        $width = [Math]::Max(1, [int][Math]::Floor($image.Width * $scale + 0.5))
        $height = [Math]::Max(1, [int][Math]::Floor($image.Height * $scale + 0.5))
        $destination = New-Object System.Drawing.Rectangle ([int][Math]::Floor(($size - $width) / 2)), ([int][Math]::Floor(($size - $height) / 2)), $width, $height
        $attributes = New-Object System.Drawing.Imaging.ImageAttributes
        $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
        $graphics.DrawImage($image, $destination, 0, 0, $image.Width, $image.Height, [System.Drawing.GraphicsUnit]::Pixel, $attributes)
    } finally {
        $graphics.Dispose()
    }

    # Cabeçalho de 0x40 bytes: little-endian, tipo 'OTEX', versão 0 e id 0xDEADBEEFDEADBEEF.
    # Depois: tipo RGBA32 (1), largura, altura, tamanho dos dados e os pixels em R, G, B, A.
    $data = New-Object byte[] (0x50 + $size * $size * 4)
    [byte[]](0x58, 0x45, 0x54, 0x4F) | ForEach-Object -Begin { $i = 4 } -Process { $data[$i++] = $_ }
    [byte[]](0xEF, 0xBE, 0xAD, 0xDE, 0xEF, 0xBE, 0xAD, 0xDE) | ForEach-Object -Begin { $i = 0x0C } -Process { $data[$i++] = $_ }
    [BitConverter]::GetBytes([uint32]1).CopyTo($data, 0x40)
    [BitConverter]::GetBytes([uint32]$size).CopyTo($data, 0x44)
    [BitConverter]::GetBytes([uint32]$size).CopyTo($data, 0x48)
    [BitConverter]::GetBytes([uint32]($size * $size * 4)).CopyTo($data, 0x4C)
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $pixel = $bitmap.GetPixel($x, $y)
            $offset = 0x50 + ($y * $size + $x) * 4
            $data[$offset] = $pixel.R
            $data[$offset + 1] = $pixel.G
            $data[$offset + 2] = $pixel.B
            $data[$offset + 3] = $pixel.A
        }
    }

    $outputFull = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
    New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($outputFull)) | Out-Null
    [IO.File]::WriteAllBytes($outputFull, $data)

    if ($PreviewPath) {
        $previewFull = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PreviewPath)
        $preview = New-Object System.Drawing.Bitmap ($size * 4), ($size * 4)
        $previewGraphics = [System.Drawing.Graphics]::FromImage($preview)
        try {
            $previewGraphics.Clear([System.Drawing.Color]::FromArgb(255, 40, 40, 40))
            $previewGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
            $previewGraphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
            $previewGraphics.DrawImage($bitmap, 0, 0, $size * 4, $size * 4)
        } finally {
            $previewGraphics.Dispose()
        }
        $preview.Save($previewFull, [System.Drawing.Imaging.ImageFormat]::Png)
        $preview.Dispose()
    }

    [pscustomobject]@{
        outputPath = $outputFull
        bytes = $data.Length
        sha256 = (Get-FileHash -LiteralPath $outputFull -Algorithm SHA256).Hash
        drawn = "${width}x${height}"
    } | ConvertTo-Json
} finally {
    $bitmap.Dispose()
    $image.Dispose()
}
