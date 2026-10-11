#pragma once
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>
#include <string>
#include <vector>
#include "paint.h"

// An owned popup keeps the recorder's messages running while the list is open.
class Dropdown {
    HWND window{}; bool done{}; int result{};
public:
    std::vector<std::wstring> const& items;
    HFONT font; COLORREF background,text,line,hover;
    int dpi,selected,highlight,first{},rows{},width{},height{};
    Dropdown(std::vector<std::wstring> const& values,HFONT face,int scale,int current,
        COLORREF bg,COLORREF fg,COLORREF border,COLORREF hot)
        :items(values),font(face),background(bg),text(fg),line(border),hover(hot),
         dpi(scale),selected(current),highlight(current) {
        LOGFONTW attributes{};GetObjectW(face,sizeof(attributes),&attributes);font=CreateFontIndirectW(&attributes);
    }
    ~Dropdown(){if(font)DeleteObject(font);}
    Dropdown(Dropdown const&)=delete;
    Dropdown& operator=(Dropdown const&)=delete;
    int px(int value) const {return MulDiv(value,dpi,96);}
    int hit(int y) const {int row=(y-px(6))/px(34);return y>=px(6) && row<rows && first+row<int(items.size())?first+row:-1;}
    void reveal() {first=std::clamp(first,0,std::max(0,int(items.size())-rows));
        if(highlight<first)first=highlight;if(highlight>=first+rows)first=highlight-rows+1;}
    void paint(HDC dc) const {
        RECT all{0,0,width,height};auto bg=CreateSolidBrush(background);FillRect(dc,&all,bg);
        auto pen=CreatePen(PS_SOLID,1,line);auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,bg);
        RoundRect(dc,0,0,width,height,px(16),px(16));
        SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,text);
        for(int row=0;row<rows && first+row<int(items.size());++row) {
            int index=first+row,y=px(6)+row*px(34);
            if(index==highlight) {auto fill=CreateSolidBrush(hover);auto before=SelectObject(dc,fill);
                auto beforePen=SelectObject(dc,GetStockObject(NULL_PEN));
                RoundRect(dc,px(5),y,width-px(5),y+px(34),px(12),px(12));
                SelectObject(dc,beforePen);SelectObject(dc,before);DeleteObject(fill);}
            if(index==selected) {auto tick=CreatePen(PS_SOLID,px(2),text);auto before=SelectObject(dc,tick);
                MoveToEx(dc,px(13),y+px(17),nullptr);LineTo(dc,px(17),y+px(21));LineTo(dc,px(24),y+px(13));
                SelectObject(dc,before);DeleteObject(tick);}
            RECT label{px(36),y,width-px(items.size()>size_t(rows)?20:14),y+px(34)};
            DrawTextW(dc,items[index].c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        }
        if(int(items.size())>rows) {int top=px(8),track=height-px(16);
            int thumb=std::max(px(16),MulDiv(track,rows,int(items.size())));
            int y=top+MulDiv(track-thumb,first,int(items.size())-rows);
            auto fill=CreateSolidBrush(line);RECT r{width-px(8),y,width-px(5),y+thumb};FillRect(dc,&r,fill);DeleteObject(fill);}
        SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);DeleteObject(bg);
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Dropdown*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE) {self=static_cast<Dropdown*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,w,l);
        switch(message) {
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);bufferedPaint(dc,ps.rcPaint,[&](HDC buffer){self->paint(buffer);});EndPaint(hwnd,&ps);return 0;}
        case WM_MOUSEMOVE:{int index=GET_X_LPARAM(l)>=0 && GET_X_LPARAM(l)<self->width?self->hit(GET_Y_LPARAM(l)):-1;
            if(index>=0 && self->highlight!=index){self->highlight=index;InvalidateRect(hwnd,nullptr,FALSE);}return 0;}
        case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:
            if(GET_X_LPARAM(l)<0 || GET_X_LPARAM(l)>=self->width || self->hit(GET_Y_LPARAM(l))<0)self->done=true;
            return 0;
        case WM_LBUTTONUP:{int index=self->hit(GET_Y_LPARAM(l));
            if(GET_X_LPARAM(l)>=0 && GET_X_LPARAM(l)<self->width && index>=0)self->result=index+1;
            self->done=true;return 0;}
        case WM_MOUSEWHEEL:self->first=std::clamp(self->first-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*3,0,std::max(0,int(self->items.size())-self->rows));self->highlight=std::clamp(self->highlight,self->first,self->first+self->rows-1);InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_KEYDOWN:
            if(w==VK_ESCAPE || w==VK_TAB){self->done=true;return 0;}
            if(w==VK_RETURN || w==VK_SPACE){self->result=self->highlight+1;self->done=true;return 0;}
            if(w==VK_UP)self->highlight=std::max(0,self->highlight-1);
            else if(w==VK_DOWN)self->highlight=std::min(int(self->items.size())-1,self->highlight+1);
            else if(w==VK_HOME)self->highlight=0;
            else if(w==VK_END)self->highlight=int(self->items.size())-1;
            else if(w==VK_NEXT)self->highlight=std::min(int(self->items.size())-1,self->highlight+self->rows);
            else if(w==VK_PRIOR)self->highlight=std::max(0,self->highlight-self->rows);
            self->reveal();InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_KILLFOCUS:case WM_CAPTURECHANGED:case WM_CLOSE:self->done=true;return 0;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
    int show(HWND owner,RECT anchor,void (*onOpened)(HWND)=nullptr) {
        if(items.empty())return 0;
        auto previousFocus=GetFocus();
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&anchor,MONITOR_DEFAULTTONEAREST),&monitor);
        auto work=monitor.rcWork;
        width=anchor.right-anchor.left;
        auto dc=GetDC(owner);auto old=SelectObject(dc,font);
        for(auto const& value:items){SIZE extent{};GetTextExtentPoint32W(dc,value.c_str(),int(value.size()),&extent);width=std::max(width,int(extent.cx)+px(58));}
        SelectObject(dc,old);ReleaseDC(owner,dc);
        width=std::min(width,std::min(px(560),int(work.right-work.left)));
        int below=work.bottom-anchor.bottom,above=anchor.top-work.top;
        int available=std::max(below,above);
        rows=std::min(int(items.size()),std::max(1,(std::min(px(352),available)-px(12))/px(34)));
        height=px(12)+rows*px(34);reveal();
        int x=std::clamp(anchor.left,work.left,work.right-width);
        int y=below>=height?anchor.bottom:std::max(work.top,anchor.top-height);
        WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"ScreencastDropdown";RegisterClassExW(&cls);
        window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"",WS_POPUP,x,y,width,height,owner,nullptr,cls.hInstance,this);
        if(!window)return 0;
        SetWindowRgn(window,CreateRoundRectRgn(0,0,width+1,height+1,px(16),px(16)),FALSE);
        ShowWindow(window,SW_SHOW);SetFocus(window);SetCapture(window);
        if(onOpened)onOpened(window);
        MSG msg{};
        while(!done && IsWindow(owner)) {
            int value=GetMessageW(&msg,nullptr,0,0);
            if(value<=0){if(value==0)PostQuitMessage(int(msg.wParam));break;}
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
        if(GetCapture()==window)ReleaseCapture();DestroyWindow(window);
        if(IsWindow(owner)){SetActiveWindow(owner);SetFocus(IsWindow(previousFocus)?previousFocus:owner);}
        return result;
    }
};
