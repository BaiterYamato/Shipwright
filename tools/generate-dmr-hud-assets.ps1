param([string]$OutputDirectory = (Join-Path $PSScriptRoot '../soh/native-sdk/example/assets/textures/baiteryamato/dynamic_movement_remake'))
$ErrorActionPreference = 'Stop'
$destination = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$palette = @{
    gSelectorPanelTex = @(8,14,20)
    gSelectorSlotTex = @(23,32,39)
    gSelectorActiveTex = @(47,48,39)
    gSelectorGoldTex = @(255,214,105)
    gSelectorMutedTex = @(116,124,132)
    gSelectorRTex = @(221,225,226)
}
foreach ($name in $palette.Keys) {
    # libultraship OTEX header and a 32x32 RGBA32 texture.
    $data = New-Object byte[] (0x50 + 32*32*4)
    ([byte[]](0x58,0x45,0x54,0x4F)).CopyTo($data,4)
    ([byte[]](0xEF,0xBE,0xAD,0xDE,0xEF,0xBE,0xAD,0xDE)).CopyTo($data,0x0C)
    [BitConverter]::GetBytes([uint32]1).CopyTo($data,0x40)
    [BitConverter]::GetBytes([uint32]32).CopyTo($data,0x44)
    [BitConverter]::GetBytes([uint32]32).CopyTo($data,0x48)
    [BitConverter]::GetBytes([uint32]4096).CopyTo($data,0x4C)
    $glyph = @('11110','10001','10001','11110','10100','10010','10001')
    for ($y=0; $y -lt 32; $y++) {
        for ($x=0; $x -lt 32; $x++) {
            $offset = 0x50 + ($y*32+$x)*4
            for ($channel=0; $channel -lt 3; $channel++) { $data[$offset+$channel] = $palette[$name][$channel] }
            $alpha = 255
            if ($name -eq 'gSelectorRTex') {
                $alpha = 0
                $gx = [int][Math]::Floor(($x-11)/2)
                $gy = [int][Math]::Floor(($y-3)/4)
                if ($gx -ge 0 -and $gx -lt 5 -and $gy -ge 0 -and $gy -lt 7 -and $glyph[$gy][$gx] -eq '1') { $alpha=255 }
            }
            $data[$offset+3] = $alpha
        }
    }
    $path = Join-Path $destination $name
    $temporary = $path + '.tmp'
    [IO.File]::WriteAllBytes($temporary,$data)
    Move-Item -LiteralPath $temporary -Destination $path -Force
}
Write-Output "Generated six selector textures."
