#pragma once
#include <gdiplus.h>
#include "ui_palette.h"
inline void drawSwitch(HDC dc,int x,int y,int width,int height,bool on,bool dark,bool indicateState=true,bool enabled=true) {
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;float h=float(height-1),w=float(width-1);
    path.AddArc(float(x),float(y),h,h,90,180);path.AddArc(x+w-h,float(y),h,h,270,180);path.CloseFigure();
    auto palette=vpnPalette(dark);auto color=[](COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));};
    auto ink=color(enabled?palette.text:palette.muted),background=color(palette.background);
    Gdiplus::Pen outline(color(palette.line),1);
    if(on && indicateState) {Gdiplus::SolidBrush active(ink);graphics.FillPath(&active,&path);}
    else {Gdiplus::SolidBrush off(color(palette.hover));graphics.FillPath(&off,&path);graphics.DrawPath(&outline,&path);}
    Gdiplus::SolidBrush knob(on && indicateState?background:color(palette.muted));
    float diameter=h*.64f,margin=(h-diameter)*.5f;
    graphics.FillEllipse(&knob,x+(on?w-margin-diameter:margin),y+margin,diameter,diameter);
}
