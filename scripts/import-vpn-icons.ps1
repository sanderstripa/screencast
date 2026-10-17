param([switch]$Verify)
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$lines=[Collections.Generic.List[string]]::new()
$lines.Add('// Generated from the Lucide 0.468.0 SVGs used by CONNECT. See assets/connect-ui/LICENSE.txt.')
$lines.Add('#pragma once')
$lines.Add('inline void drawVpnIcon(HDC dc,VpnIcon icon,int x,int y,int size,COLORREF color){')
$lines.Add(' Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.TranslateTransform(float(x),float(y));g.ScaleTransform(size/24.f,size/24.f);')
$lines.Add(' Gdiplus::Pen pen(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)),2.f);pen.SetLineJoin(Gdiplus::LineJoinRound);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);')
$lines.Add(' Gdiplus::SolidBrush ink(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)));switch(icon){')
function F($value){return ([double]::Parse([string]$value,[Globalization.CultureInfo]::InvariantCulture)).ToString('0.0#####',[Globalization.CultureInfo]::InvariantCulture)+'f'}
foreach($name in @('Monitor','Sun','Moon','Minus','X','Palette')){
 $xml=[xml][IO.File]::ReadAllText((Join-Path $root ('assets/connect-ui/'+$name.ToLowerInvariant()+'.svg')))
 if($xml.svg.viewBox-ne'0 0 24 24'-or$xml.svg.'stroke-width'-ne'2'){throw 'Unexpected CONNECT source icon'}
 $lines.Add(' case VpnIcon::'+$name+':')
 foreach($node in $xml.svg.ChildNodes){switch($node.LocalName){
  'path' {$lines.Add(' vpnSvg::path(g,pen,L"'+$node.d+'");')}
  'circle' {$fill=if($node.fill-eq'currentColor'){'true'}else{'false'};$lines.Add(' vpnSvg::circle(g,pen,ink,'+((@($node.cx,$node.cy,$node.r)|ForEach-Object {F $_})-join ',')+','+$fill+');')}
  'rect' {$lines.Add(' vpnSvg::rect(g,pen,'+((@($node.x,$node.y,$node.width,$node.height,$node.rx)|ForEach-Object {F $_})-join ',')+');')}
  'line' {$lines.Add(' g.DrawLine(&pen,'+((@($node.x1,$node.y1,$node.x2,$node.y2)|ForEach-Object {F $_})-join ',')+');')}
  default {throw 'Unsupported SVG element'}
 }}
 $lines.Add(' break;')
}
$lines.Add(' }}');$generated=($lines-join "`n")+"`n";$target=Join-Path $root 'src/vpn_icon_data.h'
if($Verify){if([IO.File]::ReadAllText($target).Replace("`r`n","`n")-cne$generated){throw 'CONNECT SVG renderer data differs from source'};'CONNECT window and appearance SVGs match native renderer.'}else{[IO.File]::WriteAllText($target,$generated,[Text.UTF8Encoding]::new($false))}
