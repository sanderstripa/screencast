#pragma once
#include <gdiplus.h>
inline void drawSwitch(HDC dc,int x,int y,int width,int height,bool on,bool dark,bool indicateState=true) {
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;float h=float(height-1),w=float(width-1);
    path.AddArc(float(x),float(y),h,h,90,180);path.AddArc(x+w-h,float(y),h,h,270,180);path.CloseFigure();
    Gdiplus::Pen outline(dark?Gdiplus::Color(255,55,59,65):Gdiplus::Color(255,209,210,208),1);
    if(on && indicateState) {Gdiplus::SolidBrush active(Gdiplus::Color(255,24,135,230));graphics.FillPath(&active,&path);}
    else graphics.DrawPath(&outline,&path);
    Gdiplus::SolidBrush knob(on && indicateState?Gdiplus::Color(255,255,255,255):dark?Gdiplus::Color(255,140,144,151):Gdiplus::Color(255,93,98,104));
    float diameter=h*.64f,margin=(h-diameter)*.5f;
    graphics.FillEllipse(&knob,x+(on?w-margin-diameter:margin),y+margin,diameter,diameter);
}
