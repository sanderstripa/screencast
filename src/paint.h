#pragma once
#include <windows.h>
// Present each repaint in one operation; the destination DC retains child clipping.
template<class Paint> void bufferedPaint(HDC destination,RECT const& rect,Paint paint) {
    int width=rect.right-rect.left,height=rect.bottom-rect.top;
    if(width<=0 || height<=0)return;
    auto dc=CreateCompatibleDC(destination);auto bitmap=CreateCompatibleBitmap(destination,width,height);
    if(!dc || !bitmap){if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);paint(destination);return;}
    auto old=SelectObject(dc,bitmap);SetViewportOrgEx(dc,-rect.left,-rect.top,nullptr);
    paint(dc);BitBlt(destination,rect.left,rect.top,width,height,dc,rect.left,rect.top,SRCCOPY);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
