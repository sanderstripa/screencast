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
# Packaging only: the approved, freshly redrawn transparent master is the source.
$source=[Drawing.Bitmap]::new((Join-Path $Destination 'Screencast-master.png'))
function RenderIcon([int]$size) {
 $bitmap=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
 $g=[Drawing.Graphics]::FromImage($bitmap)
 $g.Clear([Drawing.Color]::Transparent)
 $g.CompositingMode='SourceCopy';$g.CompositingQuality='HighQuality'
 $g.InterpolationMode='HighQualityBicubic';$g.PixelOffsetMode='HighQuality'
 $attributes=[Drawing.Imaging.ImageAttributes]::new();$attributes.SetWrapMode([Drawing.Drawing2D.WrapMode]::TileFlipXY)
 # Tight bounds of the approved mark; preserve aspect ratio, with a 1% safe margin.
 $width=$size*.98;$height=$width*1101/1171
 $target=[Drawing.Rectangle]::new([int][Math]::Round(($size-$width)/2),[int][Math]::Round(($size-$height)/2),[int][Math]::Round($width),[int][Math]::Round($height))
 $g.DrawImage($source,$target,47,73,1171,1101,[Drawing.GraphicsUnit]::Pixel,$attributes)
 $attributes.Dispose();$g.Dispose();return ,$bitmap
}
$master=RenderIcon 2048;$master.Save((Join-Path $Destination 'Screencast-2048.png'),[Drawing.Imaging.ImageFormat]::Png)
$small=RenderIcon 256;$small.Save((Join-Path $Destination "Screencast-256.png"),[Drawing.Imaging.ImageFormat]::Png);$small.Dispose()
$images=@();foreach($size in @(16,20,24,28,30,32,36,40,44,48,56,60,64,72,80,96,112,128,144,160,192,256)) {
 # Each physical shell size is sampled from the master, with alpha-aware area coverage.
 $coverage=RenderIcon ($size*4);$bitmap=[IconCoverage]::Reduce($coverage,$size);$coverage.Dispose()
 $memory=[IO.MemoryStream]::new()
 if($size-eq256){$bitmap.Save($memory,[Drawing.Imaging.ImageFormat]::Png)}else{
  # Classic 32-bit DIB entries are reliably extracted by shell/shortcut paths.
  # Keep the large entry PNG-compressed. Each entry is sampled independently from the approved master.
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

Copy-Item (Join-Path $Destination "Screencast.ico") (Join-Path $Destination "Screencast-shell-0.4.3.ico") -Force
$source.Dispose()
Copy-Item (Join-Path $Destination 'Screencast.ico') (Join-Path $Destination 'Screencast-shell-0.4.4.ico') -Force
foreach($alias in @('0.2.5','0.4.5')) { Copy-Item (Join-Path $Destination 'Screencast.ico') (Join-Path $Destination "Screencast-shell-$alias.ico") -Force }
