#pragma once
#include "camera.h"
#include <windowsx.h>

class CameraOverlay {
    HWND window_{}; std::shared_ptr<LiveSettings> live_;
    Target target_; SmoothMotion motion_{{1,1}};
    bool dragging_{}; POINT offset_{}; int64_t previous_{};
    RECT bounds() const {
        RECT r{};
        if(target_.monitor) {MONITORINFO info{sizeof(info)};GetMonitorInfoW(target_.monitor,&info);r=info.rcMonitor;}
        else if(target_.window) {GetWindowRect(target_.window,&r);}
        return r;
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<CameraOverlay*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE) {self=static_cast<CameraOverlay*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self) return DefWindowProcW(h,m,w,l);
        if(m==WM_LBUTTONDOWN) {self->dragging_=true;self->offset_={GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetCapture(h);return 0;}
        if(m==WM_MOUSEMOVE && self->dragging_) {
            POINT p{};GetCursorPos(&p);auto r=self->bounds();auto c=circlePlacement(r.right-r.left,r.bottom-r.top,{0,0});
            self->live_->cameraX=std::clamp(float(p.x-self->offset_.x-r.left-c.margin)/std::max<long>(1,r.right-r.left-c.diameter-2*c.margin),0.f,1.f);
            self->live_->cameraY=std::clamp(float(p.y-self->offset_.y-r.top-c.margin)/std::max<long>(1,r.bottom-r.top-c.diameter-2*c.margin),0.f,1.f);return 0;
        }
        if(m==WM_LBUTTONUP) {self->dragging_=false;ReleaseCapture();return 0;}
        if(m==WM_CAPTURECHANGED) {self->dragging_=false;return 0;}
        if(m==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        return DefWindowProcW(h,m,w,l);
    }
public:
    void configure(Target target,std::shared_ptr<LiveSettings> live) {
        target_=target;live_=std::move(live);motion_=SmoothMotion({live_->cameraX,live_->cameraY});previous_=clock100ns();
        if(!window_) {
            WNDCLASSW c{};c.lpfnWndProc=proc;c.hInstance=GetModuleHandleW(nullptr);c.hCursor=LoadCursorW(nullptr,IDC_HAND);c.lpszClassName=L"ScreencastCameraOverlay";RegisterClassW(&c);
            window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,c.lpszClassName,L"Screencast camera",WS_POPUP,0,0,1,1,nullptr,nullptr,c.hInstance,this);
            if(!window_) check(HRESULT_FROM_WIN32(GetLastError()));
        }
    }
    void update(std::shared_ptr<Image const> const& frame) {
        if(!window_ || !frame || frame->empty()) {hide();return;}
        auto r=bounds();if(r.right<=r.left || r.bottom<=r.top) {hide();return;}
        auto now=clock100ns();auto p=motion_.step({live_->cameraX,live_->cameraY},float(now-previous_)/10000000);previous_=now;
        auto c=circlePlacement(r.right-r.left,r.bottom-r.top,p);if(c.diameter<8) return;
        HDC desktop=GetDC(nullptr),dc=CreateCompatibleDC(desktop);BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth=c.diameter;bi.bmiHeader.biHeight=-c.diameter;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
        void* bits{};auto bitmap=CreateDIBSection(desktop,&bi,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);
        int crop=std::min(frame->width,frame->height),ox=(frame->width-crop)/2,oy=(frame->height-crop)/2;
        for(int y=0;y<c.diameter;++y) for(int x=0;x<c.diameter;++x) {
            float dx=x+.5f-c.diameter*.5f,dy=y+.5f-c.diameter*.5f;
            float alpha=std::clamp(c.diameter*.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f);
            auto src=frame->bgra.data()+(static_cast<size_t>(oy+y*crop/c.diameter)*frame->width+ox+x*crop/c.diameter)*4;
            auto out=static_cast<uint8_t*>(bits)+(static_cast<size_t>(y)*c.diameter+x)*4;
            for(int channel=0;channel<3;++channel) out[channel]=static_cast<uint8_t>(src[channel]*alpha);out[3]=static_cast<uint8_t>(255*alpha);
        }
        POINT destination{r.left+c.left,r.top+c.top},source{};SIZE size{c.diameter,c.diameter};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        BOOL ok=UpdateLayeredWindow(window_,desktop,&destination,&size,dc,&source,0,&blend,ULW_ALPHA);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,desktop);
        if(ok) {ShowWindow(window_,SW_SHOWNOACTIVATE);if(!live_->overlayReady) live_->overlayReady=now;}
    }
    void hide() {if(window_)ShowWindow(window_,SW_HIDE);}
    ~CameraOverlay() {if(window_)DestroyWindow(window_);}
};
