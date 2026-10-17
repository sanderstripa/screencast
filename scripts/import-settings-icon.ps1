param([switch]$Verify)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$source=[IO.File]::ReadAllText((Join-Path $root 'assets/CONNECT-settings-08.svg')).Replace("`r`n","`n")
$sha=[Security.Cryptography.SHA256]::Create()
$digest=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($source)))).Replace('-','').ToLowerInvariant();$sha.Dispose()
if($digest-ne'3f14c9094a96c13584a5514ef648a25cfbcb95371668036279ef4cb8051baf18'){throw 'SVG is not the approved CONNECT settings design 08'}
$xml=[xml]$source
$view=$xml.svg.viewBox.Split(' ')
if(($view -join ' ')-ne'5 0 85 85' -or $xml.svg.path.fill-ne'currentColor' -or $xml.svg.path.'fill-rule'-ne'evenodd') {throw 'Unsupported approved SVG format'}
$lines=[Collections.Generic.List[string]]::new()
$lines.Add('// Generated from the approved CONNECT-settings-08.svg. Do not redraw or edit these contours.')
$lines.Add('#pragma once')
$lines.Add('#include <gdiplus.h>')
$lines.Add('inline void drawApprovedSettingsIcon(HDC dc,int x,int y,int size,COLORREF ink) {')
$lines.Add(' Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);')
$lines.Add(' graphics.TranslateTransform(float(x),float(y));graphics.ScaleTransform(size/85.f,size/85.f);graphics.TranslateTransform(-5.f,0.f);')
$lines.Add(' Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);')
$tokens=[regex]::Matches($xml.svg.path.d,'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?') | ForEach-Object {$_.Value}
$i=0;$cx='0';$cy='0'
function F([string]$n){return $n+'f'}
while($i-lt$tokens.Count){
 $command=$tokens[$i++];switch($command){
  'M' {$cx=$tokens[$i++];$cy=$tokens[$i++];$lines.Add(' path.StartFigure();')}
  'C' {$a=$tokens[$i++];$b=$tokens[$i++];$c=$tokens[$i++];$d=$tokens[$i++];$ex=$tokens[$i++];$ey=$tokens[$i++];$lines.Add(' path.AddBezier('+((@($cx,$cy,$a,$b,$c,$d,$ex,$ey)|ForEach-Object {F $_}) -join ',')+');');$cx=$ex;$cy=$ey}
  'A' {
   $rx=$tokens[$i++];$ry=$tokens[$i++];$rotation=$tokens[$i++];$large=$tokens[$i++];$sweep=$tokens[$i++];$ex=$tokens[$i++];$ey=$tokens[$i++]
   $culture=[Globalization.CultureInfo]::InvariantCulture
   $vx=[double]::Parse($cx,$culture);$end=[double]::Parse($ex,$culture);$radius=[double]::Parse($rx,$culture)
   if($rx-ne$ry-or$rotation-ne'0'-or$large-ne'1'-or$sweep-ne'1'-or$cy-ne$ey-or[Math]::Abs([Math]::Abs($end-$vx)-2*$radius)-gt.00001){throw 'Approved SVG arc changed; do not approximate another shape'}
   $left=[Math]::Min($vx,$end);$top=[double]::Parse($cy,$culture)-$radius;$start=if($vx-gt$end){0}else{180}
   $args=@($left,$top,(2*$radius),(2*$radius),$start,180)|ForEach-Object { $_.ToString('0.0#####',$culture)+'f' }
   $lines.Add(' path.AddArc('+($args-join ',')+');');$cx=$ex;$cy=$ey
  }
  'Z' {$lines.Add(' path.CloseFigure();')}
  default {throw "Unsupported SVG command: $command"}
 }
}
$circle=$xml.svg.circle;$culture=[Globalization.CultureInfo]::InvariantCulture
$radius=[double]::Parse($circle.r,$culture);$left=[double]::Parse($circle.cx,$culture)-$radius;$top=[double]::Parse($circle.cy,$culture)-$radius
$args=@($left,$top,(2*$radius),(2*$radius))|ForEach-Object { $_.ToString('0.0#####',$culture)+'f' }
$lines.Add(' path.StartFigure();path.AddEllipse('+($args-join ',')+');')
$lines.Add(' Gdiplus::SolidBrush fill(Gdiplus::Color(255,GetRValue(ink),GetGValue(ink),GetBValue(ink)));graphics.FillPath(&fill,&path);')
$lines.Add('}')
$generated=($lines-join "`n")+"`n"
$target=Join-Path $root 'src/settings_icon.h'
if($Verify){if([IO.File]::ReadAllText($target).Replace("`r`n","`n")-cne$generated){throw 'Native icon geometry does not match the approved SVG'};'Approved SVG geometry matches native renderer.'}
else{[IO.File]::WriteAllText($target,$generated,[Text.UTF8Encoding]::new($false))}
