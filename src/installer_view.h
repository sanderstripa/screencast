#pragma once
#include "../installer/generated/layout.h"

// Presentation only. PNG coverage is generated from the approved Montserrat UI.
// Native controls, the editable path and the existing installation worker remain live.
class InstallerView {
    struct Visual { ComPtr<IStream> stream;std::unique_ptr<Gdiplus::Bitmap> bitmap; };
    std::map<int,Visual> images;
    Visual& visual(int id){
        auto& image=images[id];if(image.bitmap)return image;
        auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource)check(HRESULT_FROM_WIN32(GetLastError()));
        auto data=static_cast<BYTE const*>(LockResource(LoadResource(nullptr,resource)));
        image.stream.Attach(SHCreateMemStream(data,SizeofResource(nullptr,resource)));check(image.stream?S_OK:E_OUTOFMEMORY);
        image.bitmap.reset(Gdiplus::Bitmap::FromStream(image.stream.Get()));check(image.bitmap&&image.bitmap->GetLastStatus()==Gdiplus::Ok?S_OK:E_FAIL);return image;
    }
    static LRESULT CALLBACK cursor(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
        if(m==WM_SETCURSOR&&LOWORD(l)==HTCLIENT){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
        if(m==WM_NCDESTROY)RemoveWindowSubclass(h,cursor,1);return DefSubclassProc(h,m,w,l);
    }
    void region(HDC dc,int resource,RECT destination,RECT source,bool tile=false){
        auto& image=visual(resource);Gdiplus::Graphics g(dc);g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        float scale=float(image.bitmap->GetWidth())/(tile?float(source.right-source.left):float(px(installerVisual::width)));
        g.SetInterpolationMode(scale==1?Gdiplus::InterpolationModeNearestNeighbor:Gdiplus::InterpolationModeHighQualityBicubic);
        if(tile)g.DrawImage(image.bitmap.get(),Gdiplus::Rect(destination.left,destination.top,destination.right-destination.left,destination.bottom-destination.top),0,0,int(image.bitmap->GetWidth()),int(image.bitmap->GetHeight()),Gdiplus::UnitPixel);
        else g.DrawImage(image.bitmap.get(),Gdiplus::Rect(destination.left,destination.top,destination.right-destination.left,destination.bottom-destination.top),source.left*scale,source.top*scale,(source.right-source.left)*scale,(source.bottom-source.top)*scale,Gdiplus::UnitPixel);
    }
public:
    HWND window{},pathEdit{};bool english{},dark{},desktop{true},startMenu{true},launch{true};int stage{},dpi{96},snapshotDpi{};
    HFONT font{},heading{},smallFont{};HBRUSH background{},fieldBrush{};std::wstring path,error,snapshotPath;
    std::atomic<int> progress{};std::map<int,HWND> buttons;
    int px(int value)const{return MulDiv(value,dpi,96);}
    RECT box(int control)const{auto b=installerVisual::box(english,stage,control);return {LONG(std::floor(b.left*dpi/96)),LONG(std::floor(b.top*dpi/96)),LONG(std::ceil(b.right*dpi/96)),LONG(std::ceil(b.bottom*dpi/96))};}
    std::wstring tr(wchar_t const* ru,wchar_t const* en)const{return english?en:ru;}
    COLORREF bg()const{return dark?RGB(32,33,34):RGB(242,240,235);}COLORREF fg()const{return dark?RGB(237,237,233):RGB(32,38,36);}
    COLORREF line()const{return dark?RGB(65,67,68):RGB(217,216,210);}COLORREF field()const{return dark?RGB(41,43,44):RGB(235,233,228);}
    void notice(std::wstring const& message){UiNotice(window,font,dpi,bg(),fg(),line(),message,tr(L"Уведомление",L"Notice"),tr(L"Закрыть",L"Close")).show();}
    void fonts(){
        for(auto f:{font,heading,smallFont})if(f)DeleteObject(f);
        auto make=[&](int size,int weight){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Montserrat");};
        font=make(14,FW_NORMAL);heading=make(23,FW_MEDIUM);smallFont=make(12,FW_NORMAL);
        if(pathEdit)SendMessageW(pathEdit,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);
    }
    void theme(){
        if(background)DeleteObject(background);if(fieldBrush)DeleteObject(fieldBrush);background=CreateSolidBrush(bg());fieldBrush=CreateSolidBrush(field());
        BOOL d=dark;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&d,sizeof(d));auto border=bg();DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));
        InvalidateRect(window,nullptr,FALSE);for(auto [id,b]:buttons)InvalidateRect(b,nullptr,FALSE);if(pathEdit)InvalidateRect(pathEdit,nullptr,FALSE);
    }
    std::wstring label(int id)const{
        switch(id){
        case 1:return L"RU";case 12:return L"EN";case 2:return tr(L"Светлая",L"Light");case 13:return tr(L"Тёмная",L"Dark");
        case 3:return stage==0?tr(L"Настройки установки",L"Installation settings"):tr(L"Назад",L"Back");
        case 4:return stage==0?tr(L"Установить",L"Install"):stage==1?tr(L"Принять и установить",L"Accept and install"):tr(L"Готово",L"Done");
        case 5:return tr(L"Ярлык на рабочем столе",L"Desktop shortcut");case 6:return tr(L"Ярлык в меню «Пуск»",L"Start menu shortcut");case 7:return tr(L"Запустить Screencast после установки",L"Launch Screencast after installation");
        case 8:return tr(L"Выбрать…",L"Browse…");case 10:return tr(L"Свернуть",L"Minimize");case 11:return tr(L"Закрыть",L"Close");default:return L"";}
    }
    void rebuild(){
        for(auto [id,b]:buttons){auto r=box(id);bool visible=r.right>r.left&&r.bottom>r.top;ShowWindow(b,visible?SW_SHOW:SW_HIDE);
            if(visible)SetWindowPos(b,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
            auto name=label(id);SetWindowTextW(b,name.c_str());EnableWindow(b,!(stage==2&&id==11));InvalidateRect(b,nullptr,FALSE);}
        auto r=box(9);ShowWindow(pathEdit,stage==1?SW_SHOW:SW_HIDE);if(stage==1)SetWindowPos(pathEdit,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
        InvalidateRect(window,nullptr,FALSE);
    }
    void create(){
        dpi=snapshotDpi?snapshotDpi:int(GetDpiForWindow(window));fonts();path=folder(FOLDERID_LocalAppData)+L"\\Programs\\Screencast";
        for(int id:{10,11,1,12,2,13,8,5,6,7,3,4}){
            auto b=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW|BS_NOTIFY,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);buttons[id]=b;SetWindowSubclass(b,cursor,1,0);
        }
        pathEdit=CreateWindowExW(0,L"EDIT",path.c_str(),WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(9),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(pathEdit,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);SendMessageW(pathEdit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
        SetWindowPos(window,nullptr,0,0,px(installerVisual::width),px(installerVisual::height),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);rebuild();theme();SetTimer(window,1,100,nullptr);
    }
    void text(HDC dc,std::wstring const& value,RECT r,HFONT face){SelectObject(dc,face);SetTextColor(dc,fg());SetBkMode(dc,TRANSPARENT);DrawTextW(dc,value.c_str(),-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}
    void paint(HDC dc){
        RECT all{0,0,px(installerVisual::width),px(installerVisual::height)};FillRect(dc,&all,background);region(dc,installerVisual::base(dpi,english,dark,stage),all,all);
        if(stage==2){auto r=box(15),clear=r;InflateRect(&clear,px(1),px(1));FillRect(dc,&clear,background);
            Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            auto track=[&](float width,COLORREF ink){if(width<=0)return;float x=float(r.left),y=float(r.top),height=float(r.bottom-r.top),radius=std::min(height,width);Gdiplus::GraphicsPath shape;
                shape.AddArc(x,y,radius,height,90,180);shape.AddArc(x+width-radius,y,radius,height,270,180);shape.CloseFigure();Gdiplus::SolidBrush brush(Gdiplus::Color(255,GetRValue(ink),GetGValue(ink),GetBValue(ink)));g.FillPath(&brush,&shape);};
            track(float(r.right-r.left),line());track((r.right-r.left)*progress.load()/100.f,fg());
        }
    }
    void draw(DRAWITEMSTRUCT const& item){
        auto id=int(item.CtlID);auto r=box(id);bool off=id==5?!desktop:id==6?!startMenu:id==7?!launch:false;
        region(item.hDC,off?installerVisual::off(dpi,english,dark,id):installerVisual::base(dpi,english,dark,stage),item.rcItem,r,off);
    }
    void snapshot(){
        int width=px(installerVisual::width),height=px(installerVisual::height);HDC screen=GetDC(window),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* bits{};auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);paint(dc);
        for(auto [id,b]:buttons)if(GetWindowLongPtrW(b,GWL_STYLE)&WS_VISIBLE){auto r=box(id);auto saved=SaveDC(dc);SetViewportOrgEx(dc,r.left,r.top,nullptr);DRAWITEMSTRUCT item{};item.CtlID=id;item.hDC=dc;item.rcItem={0,0,r.right-r.left,r.bottom-r.top};draw(item);RestoreDC(dc,saved);}
        if(stage==1)text(dc,path,box(9),smallFont);for(size_t i=3;i<size_t(width)*height*4;i+=4)static_cast<BYTE*>(bits)[i]=255;
        ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;check(factory->CreateStream(&stream));check(stream->InitializeFromFilename(snapshotPath.c_str(),GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder;check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;check(encoder->CreateNewFrame(&frame,nullptr));check(frame->Initialize(nullptr));check(frame->SetSize(width,height));auto format=GUID_WICPixelFormat32bppBGRA;check(frame->SetPixelFormat(&format));check(frame->WritePixels(height,width*4,width*height*4,static_cast<BYTE*>(bits)));check(frame->Commit());check(encoder->Commit());
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);
    }
    void uiTest(std::filesystem::path const& directory){
        auto require=[](bool value,char const* message){if(!value)throw std::runtime_error(message);};auto click=[&](int id){SendMessageW(window,WM_COMMAND,id,0);};
        require(!(GetWindowLongPtrW(window,GWL_STYLE)&(WS_CAPTION|WS_MAXIMIZEBOX)),"Installer must remain frameless");
        click(3);require(stage==1,"Settings did not open");click(12);require(english,"English control failed");click(13);require(dark,"Dark control failed");
        click(5);click(6);require(!desktop&&!startMenu,"Shortcut switches failed");click(3);require(stage==0,"Back did not return home");click(3);require(!desktop&&!startMenu,"Navigation reset shortcut choices");
        wchar_t edited[32768]{};auto selected=(directory/L"chosen path").wstring();SetWindowTextW(pathEdit,selected.c_str());click(1);click(2);GetWindowTextW(pathEdit,edited,32768);require(selected==edited,"Theme/language changes reset the edited path");
        for(int scale:{144,192,96}){dpi=scale;fonts();SetWindowPos(window,nullptr,0,0,px(installerVisual::width),px(installerVisual::height),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);rebuild();
            for(auto [id,b]:buttons)if(GetWindowLongPtrW(b,GWL_STYLE)&WS_VISIBLE){auto r=box(id);require(r.left>=0&&r.top>=0&&r.right<=px(installerVisual::width)&&r.bottom<=px(installerVisual::height),"Installer DPI clips a control");}paintToTest();}
        stage=2;rebuild();SendMessageW(window,WM_CLOSE,0,0);require(IsWindow(window)&&!IsWindowEnabled(buttons[11]),"Installer can close during extraction");
        stage=3;rebuild();require(!(GetWindowLongPtrW(buttons[3],GWL_STYLE)&WS_VISIBLE),"Completion has an extra cancel/settings action");click(7);require(!launch,"Launch switch failed");click(10);require(IsIconic(window),"Minimize failed");ShowWindow(window,SW_RESTORE);
        std::filesystem::create_directories(directory);std::ofstream report(directory/L"installer-ui.txt");report<<"PASS: approved native installer presentation; working RU/EN/light/dark, shortcut and launch switches, path edits preserved, settings/back navigation, DPI resources at 96/144/192, busy-close guard, minimize, completion without cancel. No installation was performed by this UI-only check.\n";report.close();click(4);
    }
    void paintToTest(){HDC screen=GetDC(window),dc=CreateCompatibleDC(screen);auto bitmap=CreateCompatibleBitmap(screen,px(installerVisual::width),px(installerVisual::height));auto old=SelectObject(dc,bitmap);paint(dc);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);}
    ~InstallerView(){for(auto f:{font,heading,smallFont})if(f)DeleteObject(f);if(background)DeleteObject(background);if(fieldBrush)DeleteObject(fieldBrush);}
};
