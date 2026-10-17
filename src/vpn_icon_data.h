// Generated from the Lucide 0.468.0 SVGs used by CONNECT. See assets/connect-ui/LICENSE.txt.
#pragma once
inline void drawVpnIcon(HDC dc,VpnIcon icon,int x,int y,int size,COLORREF color){
 Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.TranslateTransform(float(x),float(y));g.ScaleTransform(size/24.f,size/24.f);
 Gdiplus::Pen pen(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)),2.f);pen.SetLineJoin(Gdiplus::LineJoinRound);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
 Gdiplus::SolidBrush ink(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)));switch(icon){
 case VpnIcon::Monitor:
 vpnSvg::rect(g,pen,2.0f,3.0f,20.0f,14.0f,2.0f);
 g.DrawLine(&pen,8.0f,21.0f,16.0f,21.0f);
 g.DrawLine(&pen,12.0f,17.0f,12.0f,21.0f);
 break;
 case VpnIcon::Sun:
 vpnSvg::circle(g,pen,ink,12.0f,12.0f,4.0f,false);
 vpnSvg::path(g,pen,L"M12 2v2");
 vpnSvg::path(g,pen,L"M12 20v2");
 vpnSvg::path(g,pen,L"m4.93 4.93 1.41 1.41");
 vpnSvg::path(g,pen,L"m17.66 17.66 1.41 1.41");
 vpnSvg::path(g,pen,L"M2 12h2");
 vpnSvg::path(g,pen,L"M20 12h2");
 vpnSvg::path(g,pen,L"m6.34 17.66-1.41 1.41");
 vpnSvg::path(g,pen,L"m19.07 4.93-1.41 1.41");
 break;
 case VpnIcon::Moon:
 vpnSvg::path(g,pen,L"M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z");
 break;
 case VpnIcon::Minus:
 vpnSvg::path(g,pen,L"M5 12h14");
 break;
 case VpnIcon::X:
 vpnSvg::path(g,pen,L"M18 6 6 18");
 vpnSvg::path(g,pen,L"m6 6 12 12");
 break;
 case VpnIcon::Palette:
 vpnSvg::circle(g,pen,ink,13.5f,6.5f,0.5f,true);
 vpnSvg::circle(g,pen,ink,17.5f,10.5f,0.5f,true);
 vpnSvg::circle(g,pen,ink,8.5f,7.5f,0.5f,true);
 vpnSvg::circle(g,pen,ink,6.5f,12.5f,0.5f,true);
 vpnSvg::path(g,pen,L"M12 2C6.5 2 2 6.5 2 12s4.5 10 10 10c.926 0 1.648-.746 1.648-1.688 0-.437-.18-.835-.437-1.125-.29-.289-.438-.652-.438-1.125a1.64 1.64 0 0 1 1.668-1.668h1.996c3.051 0 5.555-2.503 5.555-5.554C21.965 6.012 17.461 2 12 2z");
 break;
 }}
