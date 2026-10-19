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
#include <shlwapi.h>
#include <commctrl.h>
#include "installer_web.h"

namespace {
constexpr UINT Completed=WM_APP+20;
constexpr UINT WebAction=WM_APP+21;
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
#include "installer_view.h"
class Setup : public InstallerView {
public:
    std::thread worker;
    InstallerWeb web;
    std::deque<std::wstring> webMessages;
    std::filesystem::path pickerTestInitial;
    void syncWeb(){
        if(!web.initialized())return;
        wchar_t edited[32768]{};GetWindowTextW(pathEdit,edited,32768);
        web.update(L"{english:"+std::to_wstring(english)+L",dark:"+std::to_wstring(dark)+L",stage:"+std::to_wstring(stage)+
            L",desktop:"+std::to_wstring(desktop)+L",startMenu:"+std::to_wstring(startMenu)+L",launch:"+std::to_wstring(launch)+
            L",progress:"+std::to_wstring(progress.load())+L",path:"+InstallerWeb::jsString(edited)+L"}");
    }
    void rebuild(){InstallerView::rebuild();syncWeb();web.resize();}
    void theme(){InstallerView::theme();syncWeb();}
    void openWeb(){
        // Leave the COM callback before opening a modal Shell dialog or closing the window.
        web.open(window,[this](std::wstring message){webMessages.push_back(std::move(message));PostMessageW(window,WebAction,0,0);});
        syncWeb();
    }
    void dispatchWeb(){
        if(webMessages.empty())return;auto message=std::move(webMessages.front());webMessages.pop_front();
        try{
            if(message.starts_with(L"path:")){SetWindowTextW(pathEdit,message.substr(5).c_str());return;}
            if(message==L"drag"){ReleaseCapture();SendMessageW(window,WM_NCLBUTTONDOWN,HTCAPTION,0);return;}
            if(message.starts_with(L"command:")){command(std::stoi(message.substr(8)));if(IsWindow(window))syncWeb();}
        }catch(...){notice(errorText());}
    }
    void snapshot(){syncWeb();web.capture(snapshotPath);}
    void webTest(std::filesystem::path const& directory){
        auto require=[&](wchar_t const* script,char const* why){if(web.evaluate(script)!=L"true")throw std::runtime_error(why);};
        require(L"!!window.chrome.webview && document.fonts.check('12px Montserrat')","WebView2/Montserrat not loaded");
        web.evaluate(L"document.getElementById('settingsButton').click()");
        require(L"document.body.dataset.stage==='settings'","HTML Settings click failed");
        web.evaluate(L"document.getElementById('en').click();document.getElementById('dark').click()");
        require(L"document.body.dataset.stage==='settings' && document.documentElement.lang==='en' && document.documentElement.dataset.theme==='dark'","HTML language/theme controls failed");
        web.evaluate(L"document.getElementById('path').value='C:\\\\Selected folder\\\\Screencast';document.getElementById('path').dispatchEvent(new Event('input'))");
        wchar_t edited[32768]{};GetWindowTextW(pathEdit,edited,32768);
        if(std::wstring(edited)!=L"C:\\Selected folder\\Screencast")throw std::runtime_error("HTML input did not update installation destination");
        require(L"['path','browse','pathLabel'].every(id=>{const s=getComputedStyle(document.getElementById(id));return s.fontFamily==='Montserrat, sans-serif'&&s.fontSize==='12px'&&s.fontWeight==='400'&&s.transform==='none'&&s.zoom==='1'&&s.filter==='none'&&s.backdropFilter==='none'&&s.opacity==='1'})","HTML path/browse font or rendering properties differ");
        web.evaluate(L"document.getElementById('ru').click();document.getElementById('light').click();document.getElementById('back').click()");
        require(L"document.body.dataset.stage==='welcome'","HTML Back failed");
        SetWindowTextW(pathEdit,L"C:\\Program Files\\Screencast");
        auto metrics=L"JSON.stringify(['pathLabel','path','browse','accept','done','success'].map(id=>{const e=document.getElementById(id),s=getComputedStyle(e),r=e.getBoundingClientRect();return {id,rect:[r.x,r.y,r.width,r.height],css:['fontFamily','fontSize','fontWeight','lineHeight','letterSpacing','borderTopWidth','borderRadius','opacity','transform','zoom','filter','backdropFilter','willChange','color','backgroundColor'].map(k=>s[k])}}))";
        for(auto page:{L"settings",L"finish"}){
            stage=std::wstring(page)==L"settings"?1:3;syncWeb();
            auto actual=web.evaluate(metrics);web.capture(directory/(std::wstring(L"connect-actual-")+page+L".png"));
            web.reference(true);web.evaluate(std::wstring(L"lang='ru';light=true;installed=true;chromeReady=true;stage='")+page+L"';render()");
            auto reference=web.evaluate(metrics);web.capture(directory/(std::wstring(L"connect-reference-")+page+L".png"));
            if(actual!=reference)throw std::runtime_error("Live WebView2 computed CSS/geometry differs from CONNECT reference");
            web.reference(false);syncWeb();
        }
        stage=0;syncWeb();
        std::ofstream file(directory/L"installer-web.txt");file<<"PASS: live Edge WebView2 HTML controls, Montserrat loaded, actual settings/language/theme/back clicks, real input bridge, exact path/browse computed font/rendering properties.\n";
    }
    void browse(std::filesystem::path const& initial={}) {
        ComPtr<IFileDialog> dialog;
        check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));
        DWORD options{};check(dialog->GetOptions(&options));
        check(dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR));
        if(!initial.empty()){
            ComPtr<IShellItem> item;check(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&item)));
            check(dialog->SetFolder(item.Get()));
        }
        auto result=dialog->Show(window);
        if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;
        check(result);
        ComPtr<IShellItem> item;check(dialog->GetResult(&item));PWSTR name{};
        check(item->GetDisplayName(SIGDN_FILESYSPATH,&name));
        std::filesystem::path selected(name);CoTaskMemFree(name);
        path=(selected/L"Screencast").wstring();SetWindowTextW(pathEdit,path.c_str());syncWeb();
    }
    void folderTest(std::filesystem::path const& directory) {
        APTTYPE type{};APTTYPEQUALIFIER qualifier{};check(CoGetApartmentType(&type,&qualifier));
        if(type!=APTTYPE_STA&&type!=APTTYPE_MAINSTA)throw std::runtime_error("Folder picker requires an STA UI thread");
        auto selected=std::filesystem::absolute(directory/L"folder picker selection");std::filesystem::create_directories(selected);
        command(3);
        auto uiThread=GetCurrentThreadId();
        for(bool cancel:{false,true,false}){
            if(cancel){path=(directory/L"manually edited destination").wstring();SetWindowTextW(pathEdit,path.c_str());}
            auto previous=path;std::atomic<bool> handled{};
            std::jthread driver([&](std::stop_token stop){
                struct Search{HWND owner,dialog;};
                while(!stop.stop_requested()){
                    Search found{window,nullptr};
                    EnumThreadWindows(uiThread,[](HWND candidate,LPARAM context)->BOOL{
                        auto& search=*reinterpret_cast<Search*>(context);wchar_t name[32]{};GetClassNameW(candidate,name,32);
                        if(GetWindow(candidate,GW_OWNER)==search.owner&&std::wstring(name)==L"#32770"&&IsWindowVisible(candidate)){
                            search.dialog=candidate;return FALSE;
                        }return TRUE;
                    },reinterpret_cast<LPARAM>(&found));
                    if(found.dialog){
                        Sleep(300);handled=true;
                        PostMessageW(found.dialog,WM_COMMAND,cancel?IDCANCEL:IDOK,0);return;
                    }
                    Sleep(25);
                }
            });
            pickerTestInitial=selected;
            web.evaluate(L"document.getElementById('browse').click()");
            web.until([&]{return handled.load()&&IsWindowEnabled(window);});
            pickerTestInitial.clear();driver.request_stop();driver.join();
            if(!handled||stage!=1||!IsWindow(window)||!IsWindowEnabled(window))throw std::runtime_error("Folder picker did not return to settings");
            auto expected=cancel?previous:(selected/L"Screencast").wstring();
            wchar_t edited[32768]{};GetWindowTextW(pathEdit,edited,32768);
            // Shell resolves junctions and other folder aliases to their filesystem target.
            bool same=cancel?path==previous:
                std::filesystem::path(path).filename()==L"Screencast"&&std::filesystem::equivalent(std::filesystem::path(path).parent_path(),selected);
            if(!same||path!=edited)throw std::runtime_error("Folder picker selection/cancellation lost destination: expected="+winrt::to_string(expected)+", actual="+winrt::to_string(path)+", field="+winrt::to_string(edited));
        }
        std::ofstream report(directory/L"installer-folder.txt");report<<"PASS: real Windows folder picker select, cancel, reopen; selected destination reaches native field; STA UI thread; settings and enabled owner preserved.\n";
        command(3);
    }
    void install() {
        std::wstring selected(32768,0);selected.resize(GetWindowTextW(pathEdit,selected.data(),int(selected.size())));auto destination=safeDestination(selected);path=destination.wstring();stage=2;progress=5;rebuild();
        const DWORD selectedTheme=dark?2:1,selectedEnglish=english?1:0;
        worker=std::thread([this,destination,selectedTheme,selectedEnglish]{
            try {
                winrt::init_apartment();extract(destination);progress=75;
                wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);std::filesystem::copy_file(self,destination/L"Screencast-Uninstall.exe",std::filesystem::copy_options::overwrite_existing);
                auto app=(destination/L"Screencast.exe").wstring();auto icon=(destination/L"assets"/L"Screencast-shell-0.2.6.ico").wstring();
                if(desktop)shortcut(folder(FOLDERID_Desktop)+L"\\Screencast.lnk",app,icon,L"SanderStripa.Screencast");
                SHChangeNotify(SHCNE_UPDATEITEM,SHCNF_PATHW,app.c_str(),nullptr);
                if(startMenu) {auto group=std::filesystem::path(folder(FOLDERID_Programs))/L"Screencast";std::filesystem::create_directories(group);shortcut((group/L"Screencast.lnk").wstring(),app,icon,L"SanderStripa.Screencast");shortcut((group/L"Uninstall.lnk").wstring(),(destination/L"Screencast-Uninstall.exe").wstring(),icon,L"SanderStripa.Screencast.Uninstall");}
                HKEY key{};check(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Screencast",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)));
                auto put=[&](wchar_t const* name,std::wstring const& value){RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<BYTE const*>(value.c_str()),DWORD((value.size()+1)*2));};
                put(L"DisplayName",L"Screencast");put(L"DisplayVersion",L"0.4.1");put(L"InstallLocation",path);put(L"DisplayIcon",icon);put(L"UninstallString",L"\""+(destination/L"Screencast-Uninstall.exe").wstring()+L"\" --uninstall");RegCloseKey(key);
                // Initialize only appearance preferences, after successful extraction/registration.
                // The application reads these same values on its first and subsequent launches.
                check(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Screencast",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)));
                auto themeResult=RegSetValueExW(key,L"Theme",0,REG_DWORD,reinterpret_cast<BYTE const*>(&selectedTheme),sizeof(selectedTheme));
                auto languageResult=RegSetValueExW(key,L"English",0,REG_DWORD,reinterpret_cast<BYTE const*>(&selectedEnglish),sizeof(selectedEnglish));
                RegCloseKey(key);check(HRESULT_FROM_WIN32(themeResult));check(HRESULT_FROM_WIN32(languageResult));
                progress=100;winrt::uninit_apartment();PostMessageW(window,Completed,1,0);
            }catch(...){error=errorText();PostMessageW(window,Completed,0,0);}
        });
    }
    void command(int id) {
        if(id==10){ShowWindow(window,SW_MINIMIZE);return;}
        if(id==11){SendMessageW(window,WM_CLOSE,0,0);return;}
        if(id==1||id==12){english=id==12;rebuild();return;}if(id==2||id==13){dark=id==13;theme();return;}
        if(id==3){if(stage==0)stage=1;else if(stage==1)stage=0;rebuild();return;}
        if(id>=5 && id<=7){if(id==5)desktop=!desktop;else if(id==6)startMenu=!startMenu;else launch=!launch;InvalidateRect(buttons[id],nullptr,TRUE);return;}
        if(id==8){browse(pickerTestInitial);return;}
        if(id==4){if(stage==0||stage==1)install();else if(stage==3){if(launch)ShellExecuteW(window,L"open",(path+L"\\Screencast.exe").c_str(),nullptr,path.c_str(),SW_SHOWNORMAL);DestroyWindow(window);}}
    }
    ~Setup(){if(worker.joinable())worker.join();}
};
Setup* setup{};
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    try {
        if(m==WM_CREATE){setup->window=h;setup->create();return 0;}
        if(m==WebAction){setup->dispatchWeb();return 0;}
        if(m==WM_COMMAND){
            if(HIWORD(w)==EN_CHANGE&&reinterpret_cast<HWND>(l)==setup->pathEdit){setup->syncWeb();return 0;}
            // BS_NOTIFY also sends focus notifications. Only clicks are commands.
            auto button=setup->buttons.find(LOWORD(w));
            if(HIWORD(w)==BN_CLICKED && button!=setup->buttons.end() &&
               (!l || reinterpret_cast<HWND>(l)==button->second))setup->command(LOWORD(w));
            return 0;
        }
        if(m==WM_DRAWITEM){auto item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);bufferedPaint(item.hDC,item.rcItem,[&](HDC back){item.hDC=back;setup->draw(item);});return TRUE;}
        if(m==WM_PAINT){PAINTSTRUCT p{};auto dc=BeginPaint(h,&p);RECT rect{};GetClientRect(h,&rect);bufferedPaint(dc,rect,[](HDC back){setup->paint(back);});EndPaint(h,&p);return 0;}
        if(m==WM_NCCALCSIZE)return 0;
        if(m==WM_SYSCOMMAND&&(w&0xfff0)==SC_MAXIMIZE)return 0;
        if(m==WM_DPICHANGED){setup->dpi=HIWORD(w);setup->fonts();auto r=reinterpret_cast<RECT*>(l);SetWindowPos(h,nullptr,r->left,r->top,setup->px(installerVisual::width),setup->px(installerVisual::height),SWP_NOZORDER|SWP_NOACTIVATE);setup->rebuild();return 0;}
        if(m==WM_NCHITTEST){auto hit=DefWindowProcW(h,m,w,l);POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&point);if(hit==HTCLIENT && point.y<setup->px(60))return HTCAPTION;return hit;}
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_SIZE){setup->web.resize();return 0;}
        if(m==WM_CTLCOLOREDIT){SetTextColor(reinterpret_cast<HDC>(w),setup->fg());SetBkColor(reinterpret_cast<HDC>(w),setup->field());return reinterpret_cast<LRESULT>(setup->fieldBrush);}
        if(m==WM_TIMER){if(w==2){KillTimer(h,2);setup->snapshot();DestroyWindow(h);return 0;}if(setup->stage==2){InvalidateRect(h,nullptr,FALSE);setup->syncWeb();}return 0;}
        if(m==Completed){if(setup->worker.joinable())setup->worker.join();if(w)setup->stage=3;else {setup->stage=1;setup->notice(setup->error);}setup->rebuild();return 0;}
        if(m==WM_CLOSE){if(setup->stage!=2)DestroyWindow(h);return 0;}
        if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    }catch(...){setup->notice(errorText());}
    return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    std::wstring diagnostics;
    ULONG_PTR graphics{};Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&graphics,&input,nullptr);
    try {
        // Windows Shell dialogs require a single-threaded UI apartment.
        // Extraction keeps its independent MTA worker.
        winrt::init_apartment(winrt::apartment_type::single_threaded);int count{};auto args=CommandLineToArgvW(GetCommandLineW(),&count);std::vector<std::wstring> parameters;for(int i=0;i<count;++i)parameters.emplace_back(args[i]);
        if(parameters.size()>=3&&parameters[1]==L"--wizard-test"){std::filesystem::create_directories(parameters[2]);diagnostics=parameters[2]+L"\\installer-ui-error.txt";}
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
            auto resource=FindResourceW(instance,MAKEINTRESOURCEW(202),RT_RCDATA);DWORD countFonts{};
            auto handle=AddFontMemResourceEx(LockResource(LoadResource(instance,resource)),SizeofResource(instance,resource),nullptr,&countFonts);
            auto face=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Montserrat");
            DWORD language{},theme{},light=1,size=sizeof(DWORD);
            RegGetValueW(HKEY_CURRENT_USER,L"Software\\Screencast",L"English",RRF_RT_REG_DWORD,nullptr,&language,&size);size=sizeof(DWORD);
            RegGetValueW(HKEY_CURRENT_USER,L"Software\\Screencast",L"Theme",RRF_RT_REG_DWORD,nullptr,&theme,&size);size=sizeof(DWORD);
            RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
            bool dark=theme==2||(theme==0&&!light);
            UiNotice confirm(nullptr,face,96,dark?RGB(12,12,13):RGB(247,247,245),dark?RGB(237,237,238):RGB(23,26,29),dark?RGB(48,50,54):RGB(218,219,220),
                language?L"Uninstall Screencast?":L"Удалить Screencast?",L"Screencast",language?L"Uninstall":L"Удалить",language?L"Cancel":L"Отмена");
            bool accepted=confirm.show();DeleteObject(face);if(handle)RemoveFontMemResourceEx(handle);
            if(accepted) {
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
        if(parameters.size()>=3 && parameters[1].starts_with(L"--snapshot-")) {state.english=parameters[1].find(L"-en-")!=std::wstring::npos;state.dark=parameters[1].ends_with(L"-dark");state.snapshotPath=parameters[2];if(parameters.size()>=4)state.stage=std::stoi(parameters[3]);state.progress=65;if(parameters.size()>=5 && parameters[4]==L"off")state.desktop=state.startMenu=state.launch=false;}
        for(size_t i=4;i<parameters.size();++i)if(parameters[i].starts_with(L"dpi="))state.snapshotDpi=std::stoi(parameters[i].substr(4));
        setup=&state;WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hIconSm=cls.hIcon;cls.lpszClassName=L"ScreencastSetup";RegisterClassExW(&cls);
        auto window=CreateWindowW(cls.lpszClassName,L"Screencast",WS_POPUP|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,installerVisual::width,installerVisual::height,nullptr,nullptr,instance,nullptr);
        if(!state.snapshotPath.empty() && (GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION))throw std::runtime_error("System caption must be absent");
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);SetWindowPos(window,nullptr,work.left+(work.right-work.left-state.px(installerVisual::width))/2,work.top+(work.bottom-work.top-state.px(installerVisual::height))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
        DWORD corners=2;DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
        state.openWeb();ShowWindow(window,show);
        if(parameters.size()>=3&&parameters[1]==L"--wizard-test"){state.webTest(parameters[2]);state.folderTest(parameters[2]);state.uiTest(parameters[2]);}
        if(!state.snapshotPath.empty())SetTimer(window,2,200,nullptr);
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
        if(privateFont)RemoveFontMemResourceEx(privateFont);
    }catch(...){if(!diagnostics.empty()){std::ofstream error(diagnostics);error<<winrt::to_string(winrt::hstring(errorText()));}else MessageBoxW(nullptr,errorText().c_str(),L"Screencast",MB_OK|MB_ICONERROR);return 1;}
    winrt::uninit_apartment();Gdiplus::GdiplusShutdown(graphics);return 0;
}
