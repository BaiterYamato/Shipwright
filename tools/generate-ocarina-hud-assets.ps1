param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../soh/native-sdk/mic-ocarina/assets/textures/baiteryamato/mic_ocarina'),
    [string]$PreviewDirectory = (Join-Path $PSScriptRoot '../build/ocarina-remake-20260930/assets-preview')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$destination = [IO.Path]::GetFullPath($OutputDirectory)
$preview = [IO.Path]::GetFullPath($PreviewDirectory)
New-Item -ItemType Directory -Force -Path $destination,$preview | Out-Null
function Color($r,$g,$b,$a=255) { [Drawing.Color]::FromArgb($a,$r,$g,$b) }
function Save-Texture([Drawing.Bitmap]$bitmap,[string]$name) {
    # HUD icons are RGBA32 32x32: tile large art instead of changing the host ABI.
    $data = New-Object byte[] (0x50 + 32*32*4)
    ([byte[]](0x58,0x45,0x54,0x4F)).CopyTo($data,4)
    ([byte[]](0xEF,0xBE,0xAD,0xDE,0xEF,0xBE,0xAD,0xDE)).CopyTo($data,0x0C)
    [BitConverter]::GetBytes([uint32]1).CopyTo($data,0x40)
    [BitConverter]::GetBytes([uint32]32).CopyTo($data,0x44)
    [BitConverter]::GetBytes([uint32]32).CopyTo($data,0x48)
    [BitConverter]::GetBytes([uint32]4096).CopyTo($data,0x4C)
    for ($y=0; $y -lt 32; $y++) { for ($x=0; $x -lt 32; $x++) {
        $c=$bitmap.GetPixel($x,$y); $o=0x50+($y*32+$x)*4
        $data[$o]=$c.R; $data[$o+1]=$c.G; $data[$o+2]=$c.B; $data[$o+3]=$c.A
    } }
    $path=Join-Path $destination $name
    [IO.File]::WriteAllBytes(($path+'.tmp'),$data)
    Move-Item -LiteralPath ($path+'.tmp') -Destination $path -Force
    $bitmap.Save((Join-Path $preview ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
}
function Surface { New-Object Drawing.Bitmap 32,32 }
function Graphics([Drawing.Bitmap]$b) {
    $g=[Drawing.Graphics]::FromImage($b)
    $g.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint=[Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    return $g
}
$palette=@{
    panel=@(10,20,25); ink=@(93,68,40); grid=@(141,174,181);
    cyan=@(129,230,245); amber=@(246,192,97); ivory=@(246,235,205); muted=@(53,68,72)
}
foreach($name in $palette.Keys) {
    $b=Surface; $g=Graphics $b; $c=$palette[$name]
    $g.Clear((Color $c[0] $c[1] $c[2])); Save-Texture $b $name; $g.Dispose(); $b.Dispose()
}
$font=New-Object Drawing.Font 'Segoe UI',17,([Drawing.FontStyle]::Bold),([Drawing.GraphicsUnit]::Pixel)
$format=New-Object Drawing.StringFormat
$format.Alignment=[Drawing.StringAlignment]::Center; $format.LineAlignment=[Drawing.StringAlignment]::Center
foreach($label in @('L','R','Y','X','A','B','ZL','PLUS','MINUS','START','Z','CD','CR','CL','CU')) {
    foreach($style in @('note','button')) {
        $b=Surface; $g=Graphics $b; $g.Clear([Drawing.Color]::Transparent)
        $fill=if($style -eq 'note'){Color 57 45 32}else{Color 245 236 215}
        $fg=if($style -eq 'note'){Color 254 242 208}else{Color 43 48 47}
        $brush=New-Object Drawing.SolidBrush $fill; $textBrush=New-Object Drawing.SolidBrush $fg
        $pen=New-Object Drawing.Pen (Color 171 140 84),1.1
        if($label -in @('L','R','ZL','START')) {
            $g.FillRectangle($brush,2,5,28,22); $g.DrawRectangle($pen,2,5,28,22)
        }else{ $g.FillEllipse($brush,2,2,28,28); $g.DrawEllipse($pen,2,2,28,28) }
        $display=switch($label){ 'PLUS'{'+'} 'MINUS'{'-'} 'START'{'St'} 'CD'{'v'} 'CU'{'^'} 'CL'{'<'} 'CR'{'>'} default{$label} }
        $g.DrawString($display,$font,$textBrush,([Drawing.RectangleF]::new(0,0,32,31)),$format)
        Save-Texture $b ($style+'_'+$label)
        $g.Dispose();$brush.Dispose();$textBrush.Dispose();$pen.Dispose();$b.Dispose()
    }
}
$b=Surface;$g=Graphics $b;$g.Clear([Drawing.Color]::Transparent)
$orange=New-Object Drawing.SolidBrush (Color 232 100 39)
$white=New-Object Drawing.SolidBrush (Color 255 244 219)
$outline=New-Object Drawing.Pen (Color 53 40 30),2
$p=New-Object Drawing.Pen (Color 255 244 219),1.8
$g.FillEllipse($orange,2,2,28,28);$g.DrawEllipse($outline,2,2,28,28)
$g.FillEllipse($white,13,7,6,11);$g.DrawArc($p,10,9,12,13,0,180)
$g.DrawLine($p,16,22,16,25);$g.DrawLine($p,12,25,20,25)
Save-Texture $b 'mic';$g.Dispose();$b.Dispose();$orange.Dispose();$white.Dispose();$outline.Dispose();$p.Dispose()

# Original parchment artwork, generated as a deterministic 7x3 tiled resource.
$paper=New-Object Drawing.Bitmap 224,96
$random=New-Object Random 730
for($y=0;$y -lt 96;$y++){ for($x=0;$x -lt 224;$x++){
    $edge=[Math]::Min([Math]::Min($x,223-$x),[Math]::Min($y,95-$y))
    $shade=[Math]::Max(0,11-$edge)*2.3+$random.Next(-3,4)
    $paper.SetPixel($x,$y,(Color ([int](234-$shade)) ([int](218-$shade)) ([int](173-$shade))))
} }
$g=Graphics $paper
$border=New-Object Drawing.Pen (Color 152 117 66),1.3
$soft=New-Object Drawing.Pen (Color 180 149 94),0.7
$g.DrawRectangle($border,3,3,217,89);$g.DrawRectangle($soft,6,6,211,83)
foreach($corner in @(@(8,8),@(216,8),@(8,88),@(216,88))) {
    $cx=$corner[0];$cy=$corner[1];$dx=if($cx -lt 100){1}else{-1};$dy=if($cy -lt 40){1}else{-1}
    $g.DrawBezier($border,$cx,($cy+13*$dy),($cx+13*$dx),($cy+13*$dy),($cx+13*$dx),$cy,$cx,$cy)
    $g.DrawEllipse($soft,($cx-2),($cy-2),4,4)
}
# Treble clef, kept in the art so the native text font does not need a music glyph.
$clef=New-Object Drawing.Pen (Color 149 129 83),1.6
$g.DrawBezier($clef,23,82,18,75,31,35,26,25)
$g.DrawBezier($clef,26,25,20,13,14,36,27,48)
$g.DrawBezier($clef,27,48,43,62,30,83,18,74)
$g.DrawBezier($clef,18,74,4,63,20,48,28,56)
$g.DrawBezier($clef,28,56,33,64,22,70,19,63)
$g.DrawBezier($clef,23,82,28,92,14,93,16,84)
$g.Dispose();$border.Dispose();$soft.Dispose();$clef.Dispose()
$paper.Save((Join-Path $preview 'parchment.png'),[Drawing.Imaging.ImageFormat]::Png)
for($row=0;$row -lt 3;$row++){for($column=0;$column -lt 7;$column++){
    $tile=$paper.Clone(([Drawing.Rectangle]::new($column*32,$row*32,32,32)),[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    Save-Texture $tile ('paper_'+$row+'_'+$column);$tile.Dispose()
} }
$paper.Dispose();$font.Dispose()
# The native font always adds a black shadow. Bake the dark parchment headings
# into transparent tiles so they remain crisp and centered on light paper.
$headings=[ordered]@{ play='Play a melody'; listen='Listen to the melody'; retry='Try again' }
$songNames=@('Minuet of Forest','Bolero of Fire','Serenade of Water','Requiem of Spirit','Nocturne of Shadow','Prelude of Light',"Saria's Song","Epona's Song","Zelda's Lullaby","Sun's Song",'Song of Time','Song of Storms',"Scarecrow's Song")
for($i=0;$i -lt $songNames.Count;$i++){ $headings['song'+$i]='You played '+$songNames[$i]+'!' }
$headingFont=New-Object Drawing.Font 'Georgia',14,([Drawing.FontStyle]::Bold),([Drawing.GraphicsUnit]::Pixel)
$headingBrush=New-Object Drawing.SolidBrush (Color 79 57 32)
foreach($name in $headings.Keys){
    $line=New-Object Drawing.Bitmap 256,32; $g=Graphics $line; $g.Clear([Drawing.Color]::Transparent)
    $g.DrawString($headings[$name],$headingFont,$headingBrush,([Drawing.RectangleF]::new(0,0,256,32)),$format)
    $g.Dispose()
    for($column=0;$column -lt 8;$column++){
        $tile=$line.Clone(([Drawing.Rectangle]::new($column*32,0,32,32)),[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        Save-Texture $tile ('heading_'+$name+'_'+$column); $tile.Dispose()
    }
    $line.Save((Join-Path $preview ('heading_'+$name+'.png')),[Drawing.Imaging.ImageFormat]::Png); $line.Dispose()
}
$headingFont.Dispose();$headingBrush.Dispose();$format.Dispose()
Write-Output 'Generated ocarina parchment, controller glyphs, microphone badge and owned HUD fills.'
