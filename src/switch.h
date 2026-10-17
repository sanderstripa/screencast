#pragma once
#include <gdiplus.h>
inline void drawSwitch(HDC dc,int x,int y,int width,int height,bool on,bool dark,bool indicateState=true) {
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;float h=float(height-1),w=float(width-1);
    path.AddArc(float(x),float(y),h,h,90,180);path.AddArc(x+w-h,float(y),h,h,270,180);path.CloseFigure();
    auto ink=dark?Gdiplus::Color(255,237,237,235):Gdiplus::Color(255,16,24,36);
    auto background=dark?Gdiplus::Color(255,11,11,12):Gdiplus::Color(255,247,247,245);
    Gdiplus::Pen outline(dark?Gdiplus::Color(255,49,52,57):Gdiplus::Color(255,218,221,221),1);
    if(on && indicateState) {Gdiplus::SolidBrush active(ink);graphics.FillPath(&active,&path);}
    else graphics.DrawPath(&outline,&path);
    Gdiplus::SolidBrush knob(on && indicateState?background:dark?Gdiplus::Color(255,146,150,158):Gdiplus::Color(255,119,126,137));
    float diameter=h*.64f,margin=(h-diameter)*.5f;
    graphics.FillEllipse(&knob,x+(on?w-margin-diameter:margin),y+margin,diameter,diameter);
}
