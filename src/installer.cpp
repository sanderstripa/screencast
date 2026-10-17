#include "common.h"
#include "switch.h"
#include "paint.h"
#include "ui_notice.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <fstream>
#include <set>
#include <map>
#include <wincodec.h>
#include <propkey.h>
#include <propsys.h>

namespace {
constexpr UINT Completed=WM_APP+20;
std::wstring folder(REFKNOWNFOLDERID id) {PWSTR value{};check(SHGetKnownFolderPath(id,0,nullptr,&value));std::wstring result=value;CoTaskMemFree(value);return result;}
void shortcut(std::wstring const& path,std::wstring const& target,std::wstring const& icon,wchar_t const* appId) {
    ComPtr<IShellLinkW> link;check(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)));
    check(link->SetPath(target.c_str()));check(link->SetWorkingDirectory(std::filesystem::path(target).parent_path().c_str()));check(link->SetIconLocation(icon.c_str(),0));
    ComPtr<IPropertyStore> properties;check(link.As(&properties));PROPVARIANT identity{};identity.vt=VT_LPWSTR;identity.pwszVal=const_cast<wchar_t*>(appId);
    check(properties->SetValue(PKEY_AppUserModel_ID,identity));check(properties->Commit());
    ComPtr<IPersistFile> persist;check(link.As(&persist));check(persist->Save(path.c_str(),TRUE));
    SHChangeNotify(SHCNE_UPDATEITEM,SHCNF_PATHW,path.c_str(),nullptr);
}
std::wstring quotedPS(std::wstring text) {size_t position{};while((position=text.find(L"'",position))!=std::wstring::npos){text.insert(position,L"'");position+=2;}return L"'"+text+L"'";}
void runHidden(std::wstring arguments) {
    std::wstring command=L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \""+arguments+L"\"";
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
    CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);DWORD code{};GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);
    if(code) throw winrt::hresult_error(E_FAIL,L"Не удалось распаковать файлы. Проверьте доступ к папке / Could not extract files. Check folder permissions");
}
std::filesystem::path safeDestination(std::wstring const& text) {
    auto path=std::filesystem::absolute(text).lexically_normal();
    if(path==path.root_path() || path==std::filesystem::path(folder(FOLDERID_Profile))) throw winrt::hresult_error(E_INVALIDARG,L"Выберите отдельную папку приложения / Select a dedicated application folder");
    if(std::filesystem::exists(path) && !std::filesystem::is_empty(path) && !std::filesystem::exists(path/L"Screencast.exe"))
        throw winrt::hresult_error(E_INVALIDARG,L"Папка содержит другие файлы. Выберите новую папку / Folder contains other files. Select a new folder");
    if(std::filesystem::exists(path)) {
        if(GetFileAttributesW(path.c_str())&FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("Installation folder must not be a link");
        for(auto const& entry:std::filesystem::recursive_directory_iterator(path))
            if(GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Installation folder contains a link");
    }
    return path;
}
void extract(std::filesystem::path const& destination) {
    auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(201),RT_RCDATA);if(!resource)check(HRESULT_FROM_WIN32(GetLastError()));
    auto loaded=LoadResource(nullptr,resource);auto bytes=LockResource(loaded);DWORD size=SizeofResource(nullptr,resource);
    wchar_t temporaryRoot[MAX_PATH]{};GetTempPathW(MAX_PATH,temporaryRoot);GUID id{};check(CoCreateGuid(&id));wchar_t suffix[40]{};StringFromGUID2(id,suffix,40);
    auto zip=std::filesystem::path(temporaryRoot)/(std::wstring(L"Screencast-")+suffix+L".zip");
    {std::ofstream file(zip,std::ios::binary);file.write(static_cast<char const*>(bytes),size);if(!file)throw std::runtime_error("Could not create temporary payload");}
    try {std::filesystem::create_directories(destination);runHidden(L"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath "+quotedPS(zip.wstring())+L" -DestinationPath "+quotedPS(destination.wstring())+L" -Force");}
    catch(...) {std::filesystem::remove(zip);throw;}std::filesystem::remove(zip);
    if(!std::filesystem::exists(destination/L"Screencast.exe")) throw std::runtime_error("Application missing from payload");
    std::ofstream manifest(destination/L"installed-files.txt");
    for(auto const& item:std::filesystem::recursive_directory_iterator(destination)) if(item.is_regular_file())
        manifest<<std::filesystem::relative(item.path(),destination).generic_string()<<'\n';
}
void uninstallFiles(std::filesystem::path const& destination) {
    std::ifstream list(destination/L"installed-files.txt");if(!list)throw std::runtime_error("Installation manifest is missing");
    std::set<std::filesystem::path,std::greater<>> directories;std::string line;std::vector<std::string> files;
    while(std::getline(list,line)) files.push_back(line);list.close();
    for(auto const& entry:files) {
        line=entry;
        auto relative=std::filesystem::path(line).lexically_normal();
        if(relative.is_absolute() || relative.empty() || *relative.begin()==L"..") throw std::runtime_error("Unsafe installation manifest");
        auto path=destination/relative;
        auto root=std::filesystem::weakly_canonical(destination),resolved=std::filesystem::weakly_canonical(path);
        auto mismatch=std::mismatch(root.begin(),root.end(),resolved.begin(),resolved.end());
        if(mismatch.first!=root.end())throw std::runtime_error("Manifest resolves outside the application folder");
        std::error_code error;std::filesystem::remove(path,error);
        for(auto dir=path.parent_path();dir!=destination && dir.has_relative_path();dir=dir.parent_path())directories.insert(dir);
    }
    for(auto name:{L"models/rvm_mobilenetv3_fp32.onnx",L"models/RVM-LICENSE.txt",L"models/rvm_mobilenetv3_fp32.onnx.download",L"installed-files.txt"}) {std::error_code error;std::filesystem::remove(destination/name,error);}
    directories.insert(destination/L"models");for(auto const& dir:directories) {std::error_code error;std::filesystem::remove(dir,error);}
}
class Setup {
public:
    HWND window{},pathEdit{};bool english{},dark{},desktop{true},startMenu{true},launch{true};int stage{};
    HFONT font{},heading{},smallFont{};HBRUSH background{};std::wstring path,error;
    std::thread worker;std::atomic<int> progress{};std::map<int,HWND> buttons;
    std::wstring snapshotPath;
    void snapshot() {
        HDC screen=GetDC(window),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=680;info.bmiHeader.biHeight=-484;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* bits{};auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);paint(dc);
        for(auto [id,b]:buttons) if(GetWindowLongPtrW(b,GWL_STYLE)&WS_VISIBLE) {
            RECT r{};GetWindowRect(b,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);auto saved=SaveDC(dc);SetViewportOrgEx(dc,r.left,r.top,nullptr);
            DRAWITEMSTRUCT item{};item.CtlID=id;item.hDC=dc;item.rcItem={0,0,r.right-r.left,r.bottom-r.top};draw(item);RestoreDC(dc,saved);
        }
        if(stage==1)text(dc,path,{44,234,524,262},font);
        for(size_t i=3;i<680*484*4;i+=4)static_cast<BYTE*>(bits)[i]=255;
        ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
        ComPtr<IWICStream> stream;check(factory->CreateStream(&stream));check(stream->InitializeFromFilename(snapshotPath.c_str(),GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder;check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> frame;check(encoder->CreateNewFrame(&frame,nullptr));check(frame->Initialize(nullptr));check(frame->SetSize(680,484));auto format=GUID_WICPixelFormat32bppBGRA;check(frame->SetPixelFormat(&format));
        check(frame->WritePixels(484,680*4,680*484*4,static_cast<BYTE*>(bits)));check(frame->Commit());check(encoder->Commit());
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);
    }
    std::wstring tr(wchar_t const* ru,wchar_t const* en) const {return english?en:ru;}
    COLORREF bg()const{return dark?RGB(11,11,12):RGB(247,247,245);}COLORREF fg()const{return dark?RGB(237,237,235):RGB(16,24,36);}
    void notice(std::wstring const& message){UiNotice(window,font,96,bg(),fg(),dark?RGB(49,52,57):RGB(218,221,221),message,tr(L"Уведомление",L"Notice"),tr(L"Закрыть",L"Close")).show();}
    void theme() {if(background)DeleteObject(background);background=CreateSolidBrush(bg());BOOL d=dark;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&d,sizeof(d));auto border=bg();DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));InvalidateRect(window,nullptr,TRUE);for(auto [id,b]:buttons)InvalidateRect(b,nullptr,TRUE);if(pathEdit)InvalidateRect(pathEdit,nullptr,TRUE);}
    void add(int id,int x,int y,int w,int h) {buttons[id]=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);}
    void create() {
        AddFontResourceExW((executableDir()+L"\\assets\\Montserrat.ttf").c_str(),FR_PRIVATE,nullptr);
        // The font is also extracted to a private temporary file before the wizard opens.
        auto make=[](int size,int weight){return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Montserrat");};
        font=make(14,FW_NORMAL);heading=make(30,FW_NORMAL);smallFont=make(12,FW_NORMAL);
        path=folder(FOLDERID_LocalAppData)+L"\\Programs\\Screencast";
        add(1,20,436,54,28);add(2,88,436,48,28);add(3,396,430,120,38);add(4,528,430,132,38);
        add(5,36,288,588,30);add(6,36,324,588,30);add(7,46,322,588,30);add(8,534,232,90,34);
        add(10,590,8,36,30);add(11,634,8,36,30);
        pathEdit=CreateWindowExW(0,L"EDIT",path.c_str(),WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,44,241,480,18,window,reinterpret_cast<HMENU>(9),GetModuleHandleW(nullptr),nullptr);SendMessageW(pathEdit,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        rebuild();theme();SetTimer(window,1,100,nullptr);
    }
    void rebuild() {
        for(int id:{5,6,8})ShowWindow(buttons[id],stage==1?SW_SHOW:SW_HIDE);
        ShowWindow(pathEdit,stage==1?SW_SHOW:SW_HIDE);ShowWindow(buttons[7],stage==3?SW_SHOW:SW_HIDE);
        ShowWindow(buttons[3],stage==3?SW_HIDE:SW_SHOW);
        EnableWindow(buttons[3],stage!=2);EnableWindow(buttons[4],stage!=2);
        InvalidateRect(window,nullptr,TRUE);for(auto [id,b]:buttons)InvalidateRect(b,nullptr,TRUE);
    }
    void text(HDC dc,std::wstring const& value,RECT rect,HFONT face,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
        SelectObject(dc,face);SetTextColor(dc,fg());SetBkMode(dc,TRANSPARENT);DrawTextW(dc,value.c_str(),-1,&rect,flags);
    }
    void paint(HDC dc) {
        RECT all{};GetClientRect(window,&all);FillRect(dc,&all,background);
        auto icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,112,112,0));DrawIconEx(dc,284,28,icon,112,112,0,nullptr,DI_NORMAL);DestroyIcon(icon);
        text(dc,L"SCREENCAST",{20,146,660,186},heading,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        text(dc,tr(L"Запись экрана · MP4",L"Screen recording · MP4"),{20,188,660,212},smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(stage==0) text(dc,tr(L"Простая и удобная запись экрана\nв высоком качестве.",L"Simple, comfortable screen recording\nin high quality."),{40,242,640,316},font,DT_CENTER|DT_WORDBREAK);
        if(stage==1) {
            text(dc,tr(L"ПАПКА УСТАНОВКИ",L"INSTALLATION FOLDER"),{36,210,530,232},smallFont);
            auto pen=CreatePen(PS_SOLID,1,dark?RGB(49,52,57):RGB(218,221,221));auto old=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,background);RoundRect(dc,36,228,630,270,14,14);SelectObject(dc,oldBrush);SelectObject(dc,old);DeleteObject(pen);
            text(dc,tr(L"Установка для текущего пользователя. Права администратора не нужны.",L"Installs for the current user. Administrator rights are not required."),{36,274,640,288},smallFont);
        }
        if(stage==2) {
            text(dc,tr(L"Установка файлов…",L"Installing files…"),{36,258,644,286},font);
            RECT track{36,304,644,308};auto b=CreateSolidBrush(dark?RGB(38,42,48):RGB(221,225,230));FillRect(dc,&track,b);DeleteObject(b);track.right=36+608*progress.load()/100;b=CreateSolidBrush(fg());FillRect(dc,&track,b);DeleteObject(b);
            text(dc,std::to_wstring(progress.load())+L"%",{36,324,644,350},smallFont);
        }
        if(stage==3) text(dc,tr(L"Установка успешно завершена!",L"Installation completed successfully!"),{36,260,644,292},font,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
    }
    void draw(DRAWITEMSTRUCT const& item) {
        auto dc=item.hDC;RECT r=item.rcItem;FillRect(dc,&r,background);int id=int(item.CtlID);
        if(id==2){drawSwitch(dc,4,4,38,20,!dark,dark,false);return;}
        if(id==10 || id==11) {
            auto pen=CreatePen(PS_SOLID,1,fg());auto old=SelectObject(dc,pen);
            if(id==10){MoveToEx(dc,12,15,nullptr);LineTo(dc,24,15);}
            else {MoveToEx(dc,13,10,nullptr);LineTo(dc,23,20);MoveToEx(dc,23,10,nullptr);LineTo(dc,13,20);}
            SelectObject(dc,old);DeleteObject(pen);return;
        }
        if(id>=5 && id<=7) {
            if(id==7) {
                auto label=tr(L"Запустить Screencast после установки",L"Launch Screencast after installation");SelectObject(dc,font);SIZE size{};GetTextExtentPoint32W(dc,label.c_str(),int(label.size()),&size);
                int x=(r.right-size.cx-30)/2;
                Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::Pen outline(Gdiplus::Color(255,140,144,151),1);
                if(launch){Gdiplus::SolidBrush fill(Gdiplus::Color(255,GetRValue(fg()),GetGValue(fg()),GetBValue(fg())));graphics.FillEllipse(&fill,x,5,20,20);Gdiplus::Pen checkmark(Gdiplus::Color(255,GetRValue(bg()),GetGValue(bg()),GetBValue(bg())),2);graphics.DrawLine(&checkmark,x+5,15,x+9,19);graphics.DrawLine(&checkmark,x+9,19,x+15,11);}
                else graphics.DrawEllipse(&outline,x,5,20,20);
                r.left=x+30;text(dc,label,r,font);return;
            }
            bool checked=id==5?desktop:startMenu;drawSwitch(dc,0,5,38,20,checked,dark);r.left=52;
            text(dc,id==5?tr(L"Создать ярлык на рабочем столе",L"Create a desktop shortcut"):id==6?tr(L"Создать ярлык в меню «Пуск»",L"Create a Start menu shortcut"):tr(L"Запустить Screencast после установки",L"Launch Screencast after installation"),r,font);return;
        }
        std::wstring label=id==1?(english?L"EN":L"RU"):id==3?tr(L"Отмена",L"Cancel"):id==8?tr(L"Обзор…",L"Browse…"):stage==0?tr(L"Далее",L"Next"):stage==1?tr(L"Установить",L"Install"):stage==2?tr(L"Установка…",L"Installing…"):tr(L"Готово",L"Finish");
        auto pen=CreatePen(PS_SOLID,1,dark?RGB(49,52,57):RGB(218,221,221));auto brush=CreateSolidBrush(id==4?fg():bg());auto a=SelectObject(dc,pen),b=SelectObject(dc,brush);RoundRect(dc,0,0,r.right-1,r.bottom-1,14,14);SelectObject(dc,a);SelectObject(dc,b);DeleteObject(pen);DeleteObject(brush);
        SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,id==4?bg():fg());DrawTextW(dc,label.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    void install() {
        std::wstring selected(32768,0);selected.resize(GetWindowTextW(pathEdit,selected.data(),int(selected.size())));auto destination=safeDestination(selected);path=destination.wstring();stage=2;progress=5;rebuild();
        worker=std::thread([this,destination]{
            try {
                winrt::init_apartment();extract(destination);progress=75;
                wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);std::filesystem::copy_file(self,destination/L"Screencast-Uninstall.exe",std::filesystem::copy_options::overwrite_existing);
                auto app=(destination/L"Screencast.exe").wstring();auto icon=(destination/L"assets"/L"Screencast-shell-0.2.6.ico").wstring();
                if(desktop)shortcut(folder(FOLDERID_Desktop)+L"\\Screencast.lnk",app,icon,L"SanderStripa.Screencast");
                SHChangeNotify(SHCNE_UPDATEITEM,SHCNF_PATHW,app.c_str(),nullptr);
                if(startMenu) {auto group=std::filesystem::path(folder(FOLDERID_Programs))/L"Screencast";std::filesystem::create_directories(group);shortcut((group/L"Screencast.lnk").wstring(),app,icon,L"SanderStripa.Screencast");shortcut((group/L"Uninstall.lnk").wstring(),(destination/L"Screencast-Uninstall.exe").wstring(),icon,L"SanderStripa.Screencast.Uninstall");}
                HKEY key{};check(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Screencast",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)));
                auto put=[&](wchar_t const* name,std::wstring const& value){RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<BYTE const*>(value.c_str()),DWORD((value.size()+1)*2));};
                put(L"DisplayName",L"Screencast");put(L"DisplayVersion",L"0.2.7");put(L"InstallLocation",path);put(L"DisplayIcon",icon);put(L"UninstallString",L"\""+(destination/L"Screencast-Uninstall.exe").wstring()+L"\" --uninstall");RegCloseKey(key);
                progress=100;winrt::uninit_apartment();PostMessageW(window,Completed,1,0);
            }catch(...){error=errorText();PostMessageW(window,Completed,0,0);}
        });
    }
    void command(int id) {
        if(id==10){ShowWindow(window,SW_MINIMIZE);return;}
        if(id==11){SendMessageW(window,WM_CLOSE,0,0);return;}
        if(id==1){english=!english;rebuild();return;}if(id==2){dark=!dark;theme();return;}
        if(id==3){if(stage!=2)DestroyWindow(window);return;}
        if(id>=5 && id<=7){if(id==5)desktop=!desktop;else if(id==6)startMenu=!startMenu;else launch=!launch;InvalidateRect(buttons[id],nullptr,TRUE);return;}
        if(id==8){ComPtr<IFileDialog> dialog;check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS);if(SUCCEEDED(dialog->Show(window))){ComPtr<IShellItem> item;check(dialog->GetResult(&item));PWSTR name{};check(item->GetDisplayName(SIGDN_FILESYSPATH,&name));path=(std::filesystem::path(name)/L"Screencast").wstring();CoTaskMemFree(name);SetWindowTextW(pathEdit,path.c_str());}return;}
        if(id==4){if(stage==0){stage=1;rebuild();}else if(stage==1)install();else if(stage==3){if(launch)ShellExecuteW(window,L"open",(path+L"\\Screencast.exe").c_str(),nullptr,path.c_str(),SW_SHOWNORMAL);DestroyWindow(window);}}
    }
    ~Setup(){if(worker.joinable())worker.join();for(auto f:{font,heading,smallFont})if(f)DeleteObject(f);if(background)DeleteObject(background);}
};
Setup* setup{};
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    try {
        if(m==WM_CREATE){setup->window=h;setup->create();return 0;}
        if(m==WM_COMMAND){setup->command(LOWORD(w));return 0;}
        if(m==WM_DRAWITEM){auto item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);bufferedPaint(item.hDC,item.rcItem,[&](HDC back){item.hDC=back;setup->draw(item);});return TRUE;}
        if(m==WM_PAINT){PAINTSTRUCT p{};auto dc=BeginPaint(h,&p);RECT rect{};GetClientRect(h,&rect);bufferedPaint(dc,rect,[](HDC back){setup->paint(back);});EndPaint(h,&p);return 0;}
        if(m==WM_NCHITTEST){auto hit=DefWindowProcW(h,m,w,l);POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&point);if(hit==HTCLIENT && point.y<40 && point.x<580)return HTCAPTION;return hit;}
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_CTLCOLOREDIT){SetTextColor(reinterpret_cast<HDC>(w),setup->fg());SetBkColor(reinterpret_cast<HDC>(w),setup->bg());return reinterpret_cast<LRESULT>(setup->background);}
        if(m==WM_TIMER){if(w==2){KillTimer(h,2);setup->snapshot();DestroyWindow(h);return 0;}if(setup->stage==2)InvalidateRect(h,nullptr,FALSE);return 0;}
        if(m==Completed){if(setup->worker.joinable())setup->worker.join();if(w)setup->stage=3;else {setup->stage=1;setup->notice(setup->error);}setup->rebuild();return 0;}
        if(m==WM_CLOSE){if(setup->stage!=2)DestroyWindow(h);return 0;}
        if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    }catch(...){setup->notice(errorText());}
    return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    ULONG_PTR graphics{};Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&graphics,&input,nullptr);
    try {
        winrt::init_apartment();int count{};auto args=CommandLineToArgvW(GetCommandLineW(),&count);std::vector<std::wstring> parameters;for(int i=0;i<count;++i)parameters.emplace_back(args[i]);
        if(count>=3 && std::wstring(args[1])==L"--install-test") {auto destination=safeDestination(args[2]);extract(destination);uninstallFiles(destination);LocalFree(args);return 0;}
        if(count>=4 && std::wstring(args[1])==L"--shortcut-test") {
            std::filesystem::path directory=args[2],appPath=args[3];LocalFree(args);std::filesystem::create_directories(directory);
            try {
                auto icon=(appPath.parent_path()/L"assets"/L"Screencast-shell-0.2.6.ico").wstring();
                if(!std::filesystem::exists(icon))throw std::runtime_error("Shortcut icon asset missing");
                shortcut((directory/L"Screencast.lnk").wstring(),appPath.wstring(),icon,L"SanderStripa.Screencast");
                shortcut((directory/L"Uninstall.lnk").wstring(),executableDir()+L"\\Screencast-Setup-x64.exe",icon,L"SanderStripa.Screencast.Uninstall");return 0;
            }catch(...){std::ofstream file(directory/L"shortcut-error.txt");file<<winrt::to_string(winrt::hstring(errorText()));return 1;}
        }
        wchar_t filename[32768]{};GetModuleFileNameW(nullptr,filename,32768);
        bool uninstall=(count>=2 && std::wstring(args[1])==L"--uninstall") || std::filesystem::path(filename).filename()==L"Screencast-Uninstall.exe";LocalFree(args);
        if(uninstall) {
            if(MessageBoxW(nullptr,L"Удалить Screencast? / Uninstall Screencast?",L"Screencast",MB_YESNO|MB_ICONQUESTION)==IDYES) {
                uninstallFiles(executableDir());RegDeleteTreeW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Screencast");
                std::error_code error;std::filesystem::remove(folder(FOLDERID_Desktop)+L"\\Screencast.lnk",error);auto group=std::filesystem::path(folder(FOLDERID_Programs))/L"Screencast";
                std::filesystem::remove(group/L"Screencast.lnk",error);std::filesystem::remove(group/L"Uninstall.lnk",error);std::filesystem::remove(group,error);
                // Remove the running uninstaller after this process exits; never recurse into user data.
                auto self=std::filesystem::path(filename);
                if(self.filename()==L"Screencast-Uninstall.exe") {
                    auto command=L"powershell.exe -NoProfile -NonInteractive -Command \"Wait-Process -Id "+std::to_wstring(GetCurrentProcessId())+
                        L" -ErrorAction SilentlyContinue; Remove-Item -LiteralPath "+quotedPS(self.wstring())+L" -Force; if (-not (Get-ChildItem -LiteralPath "+quotedPS(self.parent_path().wstring())+
                        L" -Force)) { Remove-Item -LiteralPath "+quotedPS(self.parent_path().wstring())+L" }\"";
                    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
                    if(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){CloseHandle(process.hThread);CloseHandle(process.hProcess);}
                }
            }return 0;
        }
        // Extract only the private font for the wizard, never install it into Windows.
        auto fontResource=FindResourceW(instance,MAKEINTRESOURCEW(202),RT_RCDATA);DWORD size=SizeofResource(instance,fontResource);
        auto data=LockResource(LoadResource(instance,fontResource));DWORD fonts{};auto privateFont=AddFontMemResourceEx(data,size,nullptr,&fonts);
        Setup state;
        if(parameters.size()>=3 && parameters[1].starts_with(L"--snapshot-")) {state.english=parameters[1].find(L"-en-")!=std::wstring::npos;state.dark=parameters[1].ends_with(L"-dark");state.snapshotPath=parameters[2];if(parameters.size()>=4)state.stage=std::stoi(parameters[3]);state.progress=68;if(parameters.size()>=5 && parameters[4]==L"off")state.desktop=state.startMenu=state.launch=false;}
        setup=&state;WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hIconSm=cls.hIcon;cls.lpszClassName=L"ScreencastSetup";RegisterClassExW(&cls);
        auto window=CreateWindowW(cls.lpszClassName,L"Screencast",WS_POPUP|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,680,484,nullptr,nullptr,instance,nullptr);
        if(!state.snapshotPath.empty() && (GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION))throw std::runtime_error("System caption must be absent");
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);SetWindowPos(window,nullptr,work.left+(work.right-work.left-680)/2,work.top+(work.bottom-work.top-484)/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
        DWORD corners=2;DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));ShowWindow(window,show);
        if(!state.snapshotPath.empty())SetTimer(window,2,200,nullptr);
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
        if(privateFont)RemoveFontMemResourceEx(privateFont);
    }catch(...){MessageBoxW(nullptr,errorText().c_str(),L"Screencast",MB_OK|MB_ICONERROR);return 1;}
    winrt::uninit_apartment();Gdiplus::GdiplusShutdown(graphics);return 0;
}
