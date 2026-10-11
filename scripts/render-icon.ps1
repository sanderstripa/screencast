param([string]$Destination=(Join-Path $PSScriptRoot '../assets'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies @([Drawing.Bitmap].Assembly.Location,[Drawing.Rectangle].Assembly.Location) -TypeDefinition @'
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class IconCoverage {
 public static Bitmap Reduce(Bitmap source, int size) {
  int factor=source.Width/size;
  var input=source.LockBits(new Rectangle(0,0,source.Width,source.Height),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);
  var bytes=new byte[input.Stride*source.Height]; Marshal.Copy(input.Scan0,bytes,0,bytes.Length);source.UnlockBits(input);
  var result=new Bitmap(size,size,PixelFormat.Format32bppArgb);
  var output=result.LockBits(new Rectangle(0,0,size,size),ImageLockMode.WriteOnly,PixelFormat.Format32bppArgb);
  var pixels=new byte[output.Stride*size];
  for(int y=0;y<size;y++)for(int x=0;x<size;x++){
   int a=0,b=0,g=0,r=0;
   for(int dy=0;dy<factor;dy++)for(int dx=0;dx<factor;dx++){
    int i=(y*factor+dy)*input.Stride+(x*factor+dx)*4;int alpha=bytes[i+3];
    a+=alpha;b+=bytes[i]*alpha;g+=bytes[i+1]*alpha;r+=bytes[i+2]*alpha;
   }
   int j=y*output.Stride+x*4;if(a>0){pixels[j]=(byte)((b+a/2)/a);pixels[j+1]=(byte)((g+a/2)/a);pixels[j+2]=(byte)((r+a/2)/a);}
   pixels[j+3]=(byte)((a+factor*factor/2)/(factor*factor));
  }
  Marshal.Copy(pixels,0,output.Scan0,pixels.Length);result.UnlockBits(output);return result;
 }
}
'@
function RoundPath($x,$y,$w,$h,$radius) {
 $p=[Drawing.Drawing2D.GraphicsPath]::new();$d=$radius*2
 $p.AddArc($x,$y,$d,$d,180,90);$p.AddArc($x+$w-$d,$y,$d,$d,270,90);$p.AddArc($x+$w-$d,$y+$h-$d,$d,$d,0,90);$p.AddArc($x,$y+$h-$d,$d,$d,90,90);$p.CloseFigure();return $p
}
function FillGradient($g,$path,$a,$b) {
 $bounds=$path.GetBounds();$brush=[Drawing.Drawing2D.LinearGradientBrush]::new($bounds,[Drawing.ColorTranslator]::FromHtml($a),[Drawing.ColorTranslator]::FromHtml($b),45.0)
 $g.FillPath($brush,$path);$brush.Dispose();$path.Dispose()
}
New-Item -ItemType Directory -Force $Destination | Out-Null
function RenderIcon([int]$size) {
$master=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g=[Drawing.Graphics]::FromImage($master);$g.Clear([Drawing.Color]::Transparent);$g.SmoothingMode='AntiAlias';$g.PixelOffsetMode='Half';$g.ScaleTransform($size/1024.0,$size/1024.0)
$g.TranslateTransform(-128,-110.5);$g.ScaleTransform(1.25,1.25)
FillGradient $g (RoundPath 102 102 820 792 185) '#213e68' '#091425'
FillGradient $g (RoundPath 331 219 497 425 62) '#405f88' '#253957'
FillGradient $g (RoundPath 213 303 531 429 54) '#ffffff' '#abd5f7'
$red=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#ff252d'));$g.FillEllipse($red,259,343,56,56);$red.Dispose()
FillGradient $g (RoundPath 316 430 244 190 43) '#29b6fa' '#0053f6'
$p=[Drawing.Drawing2D.GraphicsPath]::new();$p.AddLine(592,481,639,453);$p.AddBezier(639,453,659,440,671,446,671,470);$p.AddLine(671,470,671,582);$p.AddBezier(671,582,671,614,657,617,640,603);$p.AddLine(640,603,593,574);$p.AddBezier(593,574,579,566,576,559,576,547);$p.AddLine(576,547,576,507);$p.AddBezier(576,507,576,494,581,487,592,481);$p.CloseFigure();FillGradient $g $p '#0989ff' '#0053f6'
$g.Dispose();return ,$master
}
$master=RenderIcon 2048;$master.Save((Join-Path $Destination 'Screencast-2048.png'),[Drawing.Imaging.ImageFormat]::Png)
$images=@();foreach($size in @(16,20,24,28,30,32,36,40,44,48,56,60,64,72,80,96,112,128,144,160,192,256)) {
 # Render each size separately; alpha-aware area coverage avoids bicubic blur and ringing.
 $coverage=RenderIcon ($size*4);$bitmap=[IconCoverage]::Reduce($coverage,$size);$coverage.Dispose()
 $memory=[IO.MemoryStream]::new()
 if($size-eq256){$bitmap.Save($memory,[Drawing.Imaging.ImageFormat]::Png)}else{
  # Classic 32-bit DIB entries are reliably extracted by shell/shortcut paths.
  # Keep the large entry PNG-compressed. Each entry is independently rendered from the same vector geometry.
  $writer=[IO.BinaryWriter]::new($memory,[Text.Encoding]::UTF8,$true)
  $writer.Write([uint32]40);$writer.Write([int32]$size);$writer.Write([int32]($size*2));$writer.Write([uint16]1);$writer.Write([uint16]32)
  $maskStride=[int]([Math]::Ceiling($size/32.0)*4)
  $writer.Write([uint32]0);$writer.Write([uint32]($size*$size*4+$maskStride*$size));foreach($field in 1..4){$writer.Write([uint32]0)}
  $pixels=$bitmap.LockBits([Drawing.Rectangle]::new(0,0,$size,$size),[Drawing.Imaging.ImageLockMode]::ReadOnly,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $row=[byte[]]::new($size*4);$mask=[byte[]]::new($maskStride*$size)
  for($y=$size-1;$y-ge0;$y--){[Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($pixels.Scan0,$y*$pixels.Stride),$row,0,$row.Length);$writer.Write($row)
   for($x=0;$x-lt$size;$x++){if($row[$x*4+3]-eq0){$index=($size-1-$y)*$maskStride+[int][Math]::Floor($x/8);$mask[$index]=$mask[$index]-bor(128-shr($x%8))}}
  }
  $bitmap.UnlockBits($pixels);$writer.Write($mask);$writer.Dispose()
 }
 $images+=,@{Size=$size;Bytes=$memory.ToArray()};$bitmap.Dispose();$memory.Dispose()
}
$file=[IO.File]::Create((Join-Path $Destination 'Screencast.ico'));$writer=[IO.BinaryWriter]::new($file);$writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$images.Count)
$offset=6+16*$images.Count
foreach($image in $images) { $dimension=if($image.Size-eq256){0}else{$image.Size};$writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([byte]0);$writer.Write([byte]0);$writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$image.Bytes.Length);$writer.Write([uint32]$offset);$offset+=$image.Bytes.Length }
foreach($image in $images){$writer.Write([byte[]]$image.Bytes)};$writer.Dispose();$master.Dispose()
Copy-Item (Join-Path $Destination 'Screencast.ico') (Join-Path $Destination 'Screencast-shell-0.2.6.ico') -Force

