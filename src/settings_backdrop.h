#pragma once
#include <windows.h>
#include <vector>
#include <algorithm>
#include <cstdint>

// UI-only cached backdrop. Capturing it never pauses or reconfigures the recorder.
class SettingsBackdrop {
    HBITMAP bitmap{};
    int width{},height{};
public:
    SettingsBackdrop()=default;
    SettingsBackdrop(SettingsBackdrop const&)=delete;
    ~SettingsBackdrop(){reset();}
    void reset(){if(bitmap)DeleteObject(bitmap);bitmap=nullptr;}
    template<class Paint> void capture(HDC screen,int w,int h,Paint paint,bool dark){
        reset();width=w;height=h;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;
        info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* memory{};bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&memory,nullptr,0);
        if(!bitmap)return;
        auto dc=CreateCompatibleDC(screen);auto old=SelectObject(dc,bitmap);paint(dc);GdiFlush();
        auto pixels=static_cast<uint8_t*>(memory);std::vector<uint8_t> temp(size_t(w)*h*4);
        // Three separable box passes approximate a soft Gaussian with linear cost.
        constexpr int radius=6,span=radius*2+1;
        for(int pass=0;pass<3;++pass){
            for(int y=0;y<h;++y)for(int c=0;c<3;++c){
                int sum=0;for(int d=-radius;d<=radius;++d)sum+=pixels[(size_t(y)*w+std::clamp(d,0,w-1))*4+c];
                for(int x=0;x<w;++x){temp[(size_t(y)*w+x)*4+c]=uint8_t(sum/span);
                    sum+=pixels[(size_t(y)*w+std::min(w-1,x+radius+1))*4+c]-pixels[(size_t(y)*w+std::max(0,x-radius))*4+c];}
            }
            for(int x=0;x<w;++x)for(int c=0;c<3;++c){
                int sum=0;for(int d=-radius;d<=radius;++d)sum+=temp[(size_t(std::clamp(d,0,h-1))*w+x)*4+c];
                for(int y=0;y<h;++y){pixels[(size_t(y)*w+x)*4+c]=uint8_t(sum/span);
                    sum+=temp[(size_t(std::min(h-1,y+radius+1))*w+x)*4+c]-temp[(size_t(std::max(0,y-radius))*w+x)*4+c];}
            }
        }
        for(size_t i=0;i<size_t(w)*h*4;i+=4){for(int c=0;c<3;++c)pixels[i+c]=uint8_t(pixels[i+c]*(dark?.72f:.88f));pixels[i+3]=255;}
        SelectObject(dc,old);DeleteDC(dc);
    }
    void paint(HDC destination,int w,int h){
        if(!bitmap)return;auto dc=CreateCompatibleDC(destination);auto old=SelectObject(dc,bitmap);
        SetStretchBltMode(destination,HALFTONE);StretchBlt(destination,0,0,w,h,dc,0,0,width,height,SRCCOPY);
        SelectObject(dc,old);DeleteDC(dc);
    }
};
