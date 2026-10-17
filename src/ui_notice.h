#pragma once
#include "paint.h"
#include <windowsx.h>

// Owned modal with a live message pump: recording/status messages still reach the owner.
class UiNotice {
    HWND window{},button{}; HWND owner{}; HFONT font{}; int dpi{}; bool done{},accepted{};
    COLORREF bg,ink,line; std::wstring message,caption,close,secondary;
    int px(int n)const{return MulDiv(n,dpi,96);}
    void paint(HDC dc){
        RECT r{};GetClientRect(window,&r);auto brush=CreateSolidBrush(bg);FillRect(dc,&r,brush);DeleteObject(brush);
        SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ink);
        RECT heading{px(56),px(16),px(496),px(48)};DrawTextW(dc,caption.c_str(),-1,&heading,DT_SINGLELINE|DT_VCENTER);
        auto iconPen=CreatePen(PS_SOLID,px(1),ink);auto a=SelectObject(dc,iconPen),b=SelectObject(dc,GetStockObject(NULL_BRUSH));
        Ellipse(dc,px(24),px(22),px(44),px(42));RECT icon{px(24),px(22),px(44),px(42)};DrawTextW(dc,L"i",1,&icon,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc,a);SelectObject(dc,b);DeleteObject(iconPen);
        auto pen=CreatePen(PS_SOLID,1,line);auto old=SelectObject(dc,pen);MoveToEx(dc,px(24),px(56),nullptr);LineTo(dc,px(496),px(56));SelectObject(dc,old);DeleteObject(pen);
        RECT text{px(24),px(72),px(496),px(240)};DrawTextW(dc,message.c_str(),-1,&text,DT_WORDBREAK|DT_END_ELLIPSIS);
    }
    static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<UiNotice*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<UiNotice*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(h,m,w,l);
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_PAINT){PAINTSTRUCT p{};auto dc=BeginPaint(h,&p);bufferedPaint(dc,p.rcPaint,[&](HDC b){self->paint(b);});EndPaint(h,&p);return 0;}
        if(m==WM_DRAWITEM){auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);auto dc=item.hDC;auto r=item.rcItem;
            bool primary=item.CtlID==IDOK;
            auto fill=CreateSolidBrush(primary?self->ink:self->bg);auto pen=CreatePen(PS_SOLID,1,self->line);auto a=SelectObject(dc,fill),b=SelectObject(dc,pen);
            RoundRect(dc,0,0,r.right-1,r.bottom-1,self->px(16),self->px(16));SelectObject(dc,self->font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,primary?self->bg:self->ink);
            auto const& label=primary?self->close:self->secondary;DrawTextW(dc,label.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,a);SelectObject(dc,b);DeleteObject(fill);DeleteObject(pen);return TRUE;}
        if(m==WM_COMMAND||m==WM_CLOSE){self->accepted=m==WM_COMMAND&&LOWORD(w)==IDOK;self->done=true;return 0;}
        if(m==WM_NCHITTEST){auto hit=DefWindowProcW(h,m,w,l);POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);return hit==HTCLIENT&&p.y<self->px(56)?HTCAPTION:hit;}
        return DefWindowProcW(h,m,w,l);
    }
public:
    UiNotice(HWND parent,HFONT face,int scale,COLORREF background,COLORREF text,COLORREF border,std::wstring body,std::wstring title,std::wstring action,std::wstring cancel={})
        :owner(parent),font(face),dpi(scale),bg(background),ink(text),line(border),message(std::move(body)),caption(std::move(title)),close(std::move(action)),secondary(std::move(cancel)){}
    bool show(){
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"ScreencastNotice";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
        RECT r{};GetWindowRect(owner,&r);window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,caption.c_str(),WS_POPUP|WS_BORDER|WS_SYSMENU|WS_CLIPCHILDREN,
            r.left+(r.right-r.left-px(520))/2,r.top+(r.bottom-r.top-px(316))/2,px(520),px(316),owner,nullptr,cls.hInstance,this);
        if(!window)return false;
        button=CreateWindowW(L"BUTTON",close.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,px(360),px(264),px(136),px(36),window,reinterpret_cast<HMENU>(IDOK),cls.hInstance,nullptr);
        if(!secondary.empty())CreateWindowW(L"BUTTON",secondary.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,px(212),px(264),px(136),px(36),window,reinterpret_cast<HMENU>(IDCANCEL),cls.hInstance,nullptr);
        EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetFocus(button);
        MSG msg{};bool quit{};while(!done&&IsWindow(owner)&&IsWindow(window)){
            auto result=GetMessageW(&msg,nullptr,0,0);if(result<=0){quit=result==0;break;}
            if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){done=true;continue;}
            if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        if(IsWindow(owner)){EnableWindow(owner,TRUE);SetActiveWindow(owner);}if(IsWindow(window))DestroyWindow(window);if(quit)PostQuitMessage(int(msg.wParam));
        return accepted;
    }
};
