param([string]$Destination=(Join-Path $PSScriptRoot '../assets'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
function RoundPath($x,$y,$w,$h,$radius) {
 $p=[Drawing.Drawing2D.GraphicsPath]::new();$d=$radius*2
 $p.AddArc($x,$y,$d,$d,180,90);$p.AddArc($x+$w-$d,$y,$d,$d,270,90);$p.AddArc($x+$w-$d,$y+$h-$d,$d,$d,0,90);$p.AddArc($x,$y+$h-$d,$d,$d,90,90);$p.CloseFigure();return $p
}
function FillGradient($g,$path,$a,$b) {
 $bounds=$path.GetBounds();$brush=[Drawing.Drawing2D.LinearGradientBrush]::new($bounds,[Drawing.ColorTranslator]::FromHtml($a),[Drawing.ColorTranslator]::FromHtml($b),45.0)
 $g.FillPath($brush,$path);$brush.Dispose();$path.Dispose()
}
New-Item -ItemType Directory -Force $Destination | Out-Null
$master=[Drawing.Bitmap]::new(2048,2048,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g=[Drawing.Graphics]::FromImage($master);$g.Clear([Drawing.Color]::Transparent);$g.SmoothingMode='AntiAlias';$g.ScaleTransform(2,2)
$g.TranslateTransform(-128,-110.5);$g.ScaleTransform(1.25,1.25)
FillGradient $g (RoundPath 102 102 820 792 185) '#213e68' '#091425'
FillGradient $g (RoundPath 331 219 497 425 62) '#405f88' '#253957'
FillGradient $g (RoundPath 213 303 531 429 54) '#ffffff' '#abd5f7'
$red=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#ff252d'));$g.FillEllipse($red,259,343,56,56);$red.Dispose()
FillGradient $g (RoundPath 316 430 244 190 43) '#29b6fa' '#0053f6'
$p=[Drawing.Drawing2D.GraphicsPath]::new();$p.AddLine(592,481,639,453);$p.AddBezier(639,453,659,440,671,446,671,470);$p.AddLine(671,470,671,582);$p.AddBezier(671,582,671,614,657,617,640,603);$p.AddLine(640,603,593,574);$p.AddBezier(593,574,579,566,576,559,576,547);$p.AddLine(576,547,576,507);$p.AddBezier(576,507,576,494,581,487,592,481);$p.CloseFigure();FillGradient $g $p '#0989ff' '#0053f6'
$g.Dispose();$master.Save((Join-Path $Destination 'Screencast-2048.png'),[Drawing.Imaging.ImageFormat]::Png)
$images=@();foreach($size in @(16,20,24,32,40,48,64,96,128,256)) {
 $bitmap=[Drawing.Bitmap]::new($size,$size);$draw=[Drawing.Graphics]::FromImage($bitmap);$draw.InterpolationMode='HighQualityBicubic';$draw.PixelOffsetMode='HighQuality';$draw.DrawImage($master,0,0,$size,$size);$draw.Dispose()
 $memory=[IO.MemoryStream]::new();$bitmap.Save($memory,[Drawing.Imaging.ImageFormat]::Png);$images+=,@{Size=$size;Bytes=$memory.ToArray()};$bitmap.Dispose();$memory.Dispose()
}
$file=[IO.File]::Create((Join-Path $Destination 'Screencast.ico'));$writer=[IO.BinaryWriter]::new($file);$writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$images.Count)
$offset=6+16*$images.Count
foreach($image in $images) { $dimension=if($image.Size-eq256){0}else{$image.Size};$writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([byte]0);$writer.Write([byte]0);$writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$image.Bytes.Length);$writer.Write([uint32]$offset);$offset+=$image.Bytes.Length }
foreach($image in $images){$writer.Write([byte[]]$image.Bytes)};$writer.Dispose();$master.Dispose()
