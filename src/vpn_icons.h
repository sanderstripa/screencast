#pragma once
#include <gdiplus.h>
#include <cwctype>
#include <cwchar>
#include <cmath>
#include <stdexcept>
enum class VpnIcon { Monitor,Sun,Moon,Minus,X,Palette };
namespace vpnSvg {
inline void arc(Gdiplus::GraphicsPath& path,float x,float y,float rx,float ry,float rotation,bool large,bool sweep,float ex,float ey){
    if(rotation!=0)throw std::runtime_error("Unsupported rotation in CONNECT SVG");
    if(x==ex&&y==ey)return;
    if(rx==0||ry==0){path.AddLine(x,y,ex,ey);return;}
    double a=std::abs(rx),b=std::abs(ry),dx=(x-ex)/2.,dy=(y-ey)/2.;
    double scale=dx*dx/(a*a)+dy*dy/(b*b);if(scale>1){scale=std::sqrt(scale);a*=scale;b*=scale;}
    double denominator=a*a*dy*dy+b*b*dx*dx;
    double ratio=std::max(0.,(a*a*b*b-a*a*dy*dy-b*b*dx*dx)/denominator);
    double k=(large==sweep?-1.:1.)*std::sqrt(ratio),ox=k*a*dy/b,oy=-k*b*dx/a;
    double cx=(x+ex)/2.+ox,cy=(y+ey)/2.+oy;
    double ux=(dx-ox)/a,uy=(dy-oy)/b,vx=(-dx-ox)/a,vy=(-dy-oy)/b;
    constexpr double pi=3.14159265358979323846;
    double start=std::atan2(uy,ux),angle=std::atan2(ux*vy-uy*vx,ux*vx+uy*vy);
    if(!sweep&&angle>0)angle-=2*pi;if(sweep&&angle<0)angle+=2*pi;
    path.AddArc(float(cx-a),float(cy-b),float(2*a),float(2*b),float(start*180/pi),float(angle*180/pi));
}
inline void path(Gdiplus::Graphics& graphics,Gdiplus::Pen& pen,wchar_t const* data){
    Gdiplus::GraphicsPath shape;auto p=data;wchar_t command{},previous{};float x{},y{},sx{},sy{},lastX{},lastY{};
    auto skip=[&]{while(*p&&(iswspace(*p)||*p==L','))++p;};
    auto number=[&]{skip();wchar_t* end{};auto value=wcstod(p,&end);if(end==p)throw std::runtime_error("Invalid CONNECT SVG number");p=end;return float(value);};
    while(true){skip();if(!*p)break;if(iswalpha(*p))command=*p++;
        bool relative=iswlower(command)!=0;wchar_t op=wchar_t(towupper(command));
        if(op==L'Z'){shape.CloseFigure();x=sx;y=sy;previous=op;command=0;continue;}
        int count=op==L'H'||op==L'V'?1:op==L'M'||op==L'L'?2:op==L'S'?4:op==L'C'?6:op==L'A'?7:0;
        if(!count)throw std::runtime_error("Unsupported CONNECT SVG command");float v[7]{};for(int i=0;i<count;++i)v[i]=number();
        if(op==L'M'){x=(relative?x:0)+v[0];y=(relative?y:0)+v[1];sx=x;sy=y;shape.StartFigure();command=relative?L'l':L'L';}
        else if(op==L'L'||op==L'H'||op==L'V'){
            float ex=op==L'V'?x:(relative?x:0)+v[0],ey=op==L'H'?y:(relative?y:0)+v[op==L'V'?0:1];shape.AddLine(x,y,ex,ey);x=ex;y=ey;
        }else if(op==L'C'||op==L'S'){
            float a=op==L'S'?((previous==L'C'||previous==L'S')?2*x-lastX:x):(relative?x:0)+v[0];
            float b=op==L'S'?((previous==L'C'||previous==L'S')?2*y-lastY:y):(relative?y:0)+v[1];
            int i=op==L'S'?0:2;float c=(relative?x:0)+v[i],d=(relative?y:0)+v[i+1],ex=(relative?x:0)+v[i+2],ey=(relative?y:0)+v[i+3];
            shape.AddBezier(x,y,a,b,c,d,ex,ey);lastX=c;lastY=d;x=ex;y=ey;
        }else if(op==L'A'){float ex=(relative?x:0)+v[5],ey=(relative?y:0)+v[6];arc(shape,x,y,v[0],v[1],v[2],v[3]!=0,v[4]!=0,ex,ey);x=ex;y=ey;}
        previous=op;
    }
    graphics.DrawPath(&pen,&shape);
}
inline void circle(Gdiplus::Graphics& g,Gdiplus::Pen& pen,Gdiplus::SolidBrush& ink,float cx,float cy,float r,bool fill){
    if(fill)g.FillEllipse(&ink,cx-r,cy-r,r*2,r*2);g.DrawEllipse(&pen,cx-r,cy-r,r*2,r*2);
}
inline void rect(Gdiplus::Graphics& g,Gdiplus::Pen& pen,float x,float y,float w,float h,float r){
    Gdiplus::GraphicsPath p;float d=r*2;p.AddArc(x,y,d,d,180,90);p.AddArc(x+w-d,y,d,d,270,90);p.AddArc(x+w-d,y+h-d,d,d,0,90);p.AddArc(x,y+h-d,d,d,90,90);p.CloseFigure();g.DrawPath(&pen,&p);
}
}
#include "vpn_icon_data.h"
#include "vpn_theme_pixels.h"
// Exact browser coverage at each physical size: no native path/stroke conversion.
inline void drawVpnThemeIcon(HDC dc,VpnIcon icon,int x,int y,int size,COLORREF color){
    auto mask=icon==VpnIcon::Monitor?vpnThemePixels::monitor(size):icon==VpnIcon::Sun?vpnThemePixels::sun(size):vpnThemePixels::moon(size);
    std::vector<unsigned char> pixels(size_t(mask.size)*mask.size*4);
    for(size_t i=0;i<pixels.size()/4;++i){pixels[i*4]=GetBValue(color);pixels[i*4+1]=GetGValue(color);pixels[i*4+2]=GetRValue(color);pixels[i*4+3]=mask.alpha[i];}
    Gdiplus::Bitmap bitmap(mask.size,mask.size,mask.size*4,PixelFormat32bppARGB,pixels.data());
    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.DrawImage(&bitmap,Gdiplus::Rect(x,y,size,size),0,0,mask.size,mask.size,Gdiplus::UnitPixel);
}
