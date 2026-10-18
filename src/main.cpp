#include "common.h"
#include "capture.h"
#include "audio.h"
#include "camera.h"
#include "recorder.h"
#include <dwmapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wincodec.h>
#include <map>
#include <fstream>
#include <commctrl.h>
#include <windowsx.h>
#include "overlay.h"
#include "switch.h"
#include "paint.h"
#include "dropdown.h"
#include "single_instance.h"
#include "ui_language.h"
#include "ui_notice.h"
#include "settings_icon.h"
#include "ui_palette.h"
#include "vpn_icons.h"
#include "window_position.h"

int encoderSelfTest(std::wstring const& directory);
int cameraSelfTest(std::wstring const& directory);
int overlaySelfTest(std::wstring const& directory);
int liveDeviceSelfTest(std::wstring const& directory);
namespace {
enum Id { SourceKind = 100, Source, Refresh, OutputMode, OutputDevice, Microphone, MicDevice,
    Denoise, Webcam, CameraDevice, Corner, Background, BackgroundFile, Save, Start, ThemeSystem, ThemeLight, ThemeDark,
    SystemVolume,MicVolume,NoiseStrength,BlurStrength,BackgroundPreset,CameraMirror,
    OpenSettings,Back,Language,SaveFolder,Minimize,Close,LanguageEnglish };
enum class Kind { Button, Choice, Toggle, Segment, Slider };
struct Control {
    HWND window{}; Kind kind{}; std::wstring label;
    std::vector<std::wstring> items; int selected{}; bool checked{}; int maximum{100}; bool hover{};
    float hot{},from{};ULONGLONG transition{};
};
class App {
public:
    HWND window{};
    int dpi{96}, theme{}, scroll{};
    bool dark{}, closing{}, finishing{};
    bool english{},settingsOpen{},settingsSnapshot{},keyboardFocusSnapshot{},testing{};
    bool windowHoverSnapshot{},windowHoverStartSnapshot{},cornersApplied{};
    bool positionReady{};
    void saveWindowPosition(){if(positionReady&&!testing)windowPosition::save(window,preferencesKey.c_str());}
    std::wstring saveFolder;
    std::wstring preferencesKey{L"Software\\Screencast"};
    unsigned noticePumps{};
    std::wstring tr(std::wstring const& s)const{return uiText(s,english);}
    void notice(std::wstring const& message){UiNotice(window,font,dpi,palette.background,palette.text,palette.line,tr(message),tr(L"Уведомление"),tr(L"Закрыть")).show();}
    HFONT font{}, smallFont{}, heading{}, title{};
    HBRUSH brush{};
    std::map<int, Control> controls;
    std::vector<Target> screens, windowList, processList;
    int previousAudioMode{1}, systemOutputIndex{}, processIndex{};
    std::vector<Device> microphones, outputs, cameras;
    Recorder recorder;
    CameraOverlay overlay;
    HANDLE modelDownload{};int pendingBackground{};
    ULONGLONG lastStatusPaint{};
    void installModel(int mode) {
        pendingBackground=mode;if(modelDownload)return;
        auto script=executableDir()+L"\\scripts\\setup-background.ps1";
        if(!std::filesystem::exists(script)) throw winrt::hresult_error(E_FAIL,L"Распакуйте приложение полностью или переустановите его: отсутствует компонент загрузки эффектов");
        auto command=L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+script+L"\" -AppDirectory \""+executableDir()+L"\"";
        STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
        check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        CloseHandle(process.hThread);modelDownload=process.hProcess;status=L"Загружаю эффекты фона (15 МБ)…";enabled();
    }
    void tick() {
        overlay.update(recorder.busy() && live->cameraEnabled?recorder.webcam():nullptr);
        if((modelDownload||finishing)&&!settingsOpen){RECT progress{px(662),px(532),px(976),px(536)};InvalidateRect(window,&progress,FALSE);}
        if(recorder.busy()) {
            RECT preview{px(24),px(244),px(310),px(510)};if(!settingsOpen)InvalidateRect(window,&preview,FALSE);
            auto now=GetTickCount64();
            if(now-lastStatusPaint>=1000) {lastStatusPaint=now;RECT statusRect{px(24),px(546),px(976),px(570)};InvalidateRect(window,&statusRect,FALSE);}
        }
        if(modelDownload && WaitForSingleObject(modelDownload,0)==WAIT_OBJECT_0) {
            DWORD code{};GetExitCodeProcess(modelDownload,&code);CloseHandle(modelDownload);modelDownload=nullptr;
            if(code==0 && std::filesystem::exists(executableDir()+L"\\models\\rvm_mobilenetv3_fp32.onnx")) {
                controls[Background].selected=pendingBackground;backgroundItems();updateText(Background);updateLive();status=L"Эффекты фона готовы";
            } else {status=L"Загрузка не удалась. Нажмите «Загрузить эффекты» ещё раз";}
            enabled();InvalidateRect(window,nullptr,FALSE);
        }
    }
    std::wstring path, backgroundPath, lastFile, status{L"Готов к записи"};
    std::wstring snapshotPath;
    bool mouseFocusSnapshot{},dropdownSnapshot{};
    std::wstring idleTestPath;int idleTestPhase{},idlePaints{};
    Palette palette{};
    std::map<int,HICON> windowIcons;
    HICON windowIcon(WPARAM kind,LPARAM requestedDpi) {
        UINT scale=requestedDpi>0 && requestedDpi<=768?UINT(requestedDpi):GetDpiForWindow(window);
        // Match the installer's full-size icon for EVERY shell request, including
        // ICON_SMALL. Some taskbar paths request SMALL rather than SMALL2.
        (void)kind;
        int size=GetSystemMetricsForDpi(SM_CXICON,scale);
        auto& icon=windowIcons[size];
        if(!icon)icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,0));
        return icon;
    }
    std::shared_ptr<LiveSettings> live=std::make_shared<LiveSettings>();
    bool dragging{}; Point dragOffset{};
    RECT previewRect{};
    std::shared_ptr<Image const> displayed;
    static LRESULT CALLBACK controlProcedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
        if(message==WM_ERASEBKGND)return 1;
        auto self=reinterpret_cast<App*>(GetWindowLongPtrW(GetParent(hwnd),GWLP_USERDATA));
        if(self && (message==WM_MOUSEMOVE||message==WM_MOUSELEAVE)){
            auto& c=self->controls.at(GetDlgCtrlID(hwnd));bool hot=message==WM_MOUSEMOVE;
            if(c.hover!=hot){c.hover=hot;c.from=c.hot;c.transition=GetTickCount64();SetTimer(self->window,4,15,nullptr);InvalidateRect(hwnd,nullptr,FALSE);}
            if(hot){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);}
        }
        if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,controlProcedure,2);
        return DefSubclassProc(hwnd,message,wp,lp);
    }
    static LRESULT CALLBACK sliderProcedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
        auto self=reinterpret_cast<App*>(data); int id=GetDlgCtrlID(hwnd); auto& c=self->controls.at(id);
        if(message==WM_LBUTTONDOWN || (message==WM_MOUSEMOVE && GetCapture()==hwnd)) {
            if(message==WM_LBUTTONDOWN) {SetCapture(hwnd);SetFocus(hwnd);}
            RECT r{};GetClientRect(hwnd,&r); int inset=self->px(14);
            c.selected=std::clamp(MulDiv(GET_X_LPARAM(lp)-inset,c.maximum,std::max(1,int(r.right)-2*inset)),0,c.maximum);
            self->updateText(id);self->updateLive();return 0;
        }
        if(message==WM_LBUTTONUP) {if(GetCapture()==hwnd)ReleaseCapture();return 0;}
        if(message==WM_KEYDOWN && (wp==VK_LEFT || wp==VK_RIGHT || wp==VK_HOME || wp==VK_END)) {
            c.selected=wp==VK_HOME?0:wp==VK_END?c.maximum:std::clamp(c.selected+(wp==VK_RIGHT?5:-5),0,c.maximum);
            self->updateText(id);self->updateLive();return 0;
        }
        if(message==WM_NCDESTROY) RemoveWindowSubclass(hwnd,sliderProcedure,1);
        return DefSubclassProc(hwnd,message,wp,lp);
    }
    void updateLive() {
        live->micEnabled=controls[Microphone].checked;live->cameraEnabled=controls[Webcam].checked;live->cameraMirror=controls[CameraMirror].checked;
        live->systemGain=controls[SystemVolume].selected/100.f; live->micGain=controls[MicVolume].selected/100.f;
        live->noiseStrength=controls[NoiseStrength].selected/100.f;live->denoise=controls[Denoise].checked;
        live->background=static_cast<BackgroundMode>(controls[Background].selected);
        live->backgroundPreset=controls[BackgroundPreset].selected;live->blurStrength=std::max(1,controls[BlurStrength].selected*40/100);
    }
    static COLORREF blend(COLORREF a,COLORREF b,float t){
        return RGB(int(GetRValue(a)+(GetRValue(b)-GetRValue(a))*t),int(GetGValue(a)+(GetGValue(b)-GetGValue(a))*t),int(GetBValue(a)+(GetBValue(b)-GetBValue(a))*t));
    }
    void hoverTick(){
        BOOL animate=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0);bool active{};
        for(auto& [id,c]:controls)if(c.transition){
            float t=animate?std::min(1.f,float(GetTickCount64()-c.transition)/135.f):1.f;
            c.hot=c.from+((c.hover?1.f:0.f)-c.from)*t;InvalidateRect(c.window,nullptr,FALSE);
            if(t>=1)c.transition=0;else active=true;
        }
        if(!active)KillTimer(window,4);
    }
    void backgroundItems() {
        int mode=controls[Background].selected;
        if(mode==4) setItems(BackgroundPreset,{L"Молочная гостиная",L"Бежевый уголок",L"Светло-серый интерьер"});
        else setItems(BackgroundPreset,{L"Графитовый",L"Молочный",L"Светло-серый",L"Тёплый бежевый"});
    }
    void dragCamera(int x,int y,bool begin) {
        if(!recorder.recording() || !controls[Webcam].checked || !displayed) return;
        float scale=float(previewRect.right-previewRect.left)/displayed->width;
        auto p=circlePlacement(displayed->width,displayed->height,recorder.cameraPosition());
        float cx=previewRect.left+(p.left+p.diameter*.5f)*scale, cy=previewRect.top+(p.top+p.diameter*.5f)*scale;
        if(begin) {
            if(std::hypot(x-cx,y-cy)>p.diameter*.5f*scale) return;
            dragging=true;dragOffset={x-cx,y-cy};SetCapture(window);
        }
        if(!dragging) return;
        float ix=(x-dragOffset.x-previewRect.left)/scale-p.diameter*.5f;
        float iy=(y-dragOffset.y-previewRect.top)/scale-p.diameter*.5f;
        live->cameraX=std::clamp((ix-p.margin)/std::max(1,displayed->width-p.diameter-2*p.margin),0.f,1.f);
        live->cameraY=std::clamp((iy-p.margin)/std::max(1,displayed->height-p.diameter-2*p.margin),0.f,1.f);
    }

    int px(int value) const { return MulDiv(value, dpi, 96); }
    void layoutChrome(){
        if(!controls.contains(Close))return;RECT r{};GetClientRect(window,&r);
        SetWindowPos(controls[Close].window,nullptr,r.right-px(50),px(16),px(28),px(30),SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(controls[Minimize].window,nullptr,r.right-px(86),px(16),px(28),px(30),SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void scrollTo(int position) {
        RECT r{}; GetClientRect(window, &r);
        position = 0;
        int delta = scroll - position; scroll = position;
        SetScrollPos(window, SB_VERT, scroll, TRUE);
        ScrollWindowEx(window, 0, delta, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
        UpdateWindow(window);
    }
    void scrollRange() {
        RECT r{}; GetClientRect(window, &r);
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMax = r.bottom-1; info.nPage = r.bottom; info.nPos = 0;
        SetScrollInfo(window, SB_VERT, &info, TRUE);
    }
    void reveal(int id) {
        auto found = controls.find(id); if (found == controls.end()) return;
        RECT r{}, client{}; GetWindowRect(found->second.window, &r); GetClientRect(window, &client);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&r), 2);
        if (r.top < 0) scrollTo(scroll + r.top - px(10));
        else if (r.bottom > client.bottom) scrollTo(scroll + r.bottom - client.bottom + px(10));
    }
    void applyTheme() {
        DWORD light = 1, size = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
        dark = theme == 2 || (theme == 0 && !light);
        palette = vpnPalette(dark);
        if (brush) DeleteObject(brush); brush = CreateSolidBrush(palette.background);
        BOOL useDark = dark; DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
        DWM_WINDOW_CORNER_PREFERENCE corners=DWMWCP_ROUND;
        cornersApplied=SUCCEEDED(DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners)));
        for (auto [id, value] : controls) InvalidateRect(value.window, nullptr, TRUE);
        InvalidateRect(window, nullptr, TRUE);
    }
    void fonts() {
        for (auto value : {font, smallFont, heading, title}) if (value) DeleteObject(value);
        auto make = [&](int size, int weight) {
            return CreateFontW(-px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Montserrat");
        };
        font = make(13, FW_NORMAL); smallFont = make(11, FW_NORMAL);
        heading = make(12, FW_MEDIUM); title = make(18, FW_MEDIUM);
    }
    void add(int id, Kind kind, std::wstring label, int x, int y, int w, int h) {
        auto hwnd = CreateWindowExW(0, L"BUTTON", label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY,
            px(x), px(y), px(w), px(h), window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        controls[id] = {hwnd, kind, label};
          SetWindowSubclass(hwnd,controlProcedure,2,0);
        if(kind==Kind::Slider) SetWindowSubclass(hwnd,sliderProcedure,1,reinterpret_cast<DWORD_PTR>(this));
    }
    void setItems(int id, std::vector<std::wstring> items) {
        auto& c = controls.at(id); c.items = std::move(items);
        if (c.selected >= static_cast<int>(c.items.size())) c.selected = 0;
        updateText(id);
    }
    void updateText(int id) {
        auto& c = controls.at(id);
        auto text = c.label;
        if (c.kind == Kind::Choice) text = c.items.empty() ? L"Нет доступных устройств" : c.items[c.selected];
        else if (c.kind == Kind::Toggle) text=tr(text)+(c.checked?(english?L": on":L": включено"):(english?L": off":L": выключено"));
        else if(c.kind==Kind::Slider) text+=L" · "+std::to_wstring(c.selected)+L"%";
        text=tr(text);SetWindowTextW(c.window, text.c_str()); InvalidateRect(c.window, nullptr, FALSE);
    }
    void setChecked(int id, bool value) { controls.at(id).checked = value; updateText(id); }
    void refresh() {
        if (recorder.busy() || finishing) return;
        // Keep selections by stable IDs / handles when devices or windows are refreshed.
        Target previous{};
        auto const& old = controls[SourceKind].selected == 0 ? screens : windowList;
        auto selected = controls[Source].selected;
        if (selected < static_cast<int>(old.size())) previous = old[selected];
        auto previousId = [&](int id, std::vector<Device> const& devices) {
            auto index = controls[id].selected;
            return index < static_cast<int>(devices.size()) ? devices[index].id : std::wstring{};
        };
        auto micId = previousId(MicDevice, microphones), cameraId = previousId(CameraDevice, cameras);
        if (previousAudioMode != 2) systemOutputIndex = controls[OutputDevice].selected;
        else processIndex = controls[OutputDevice].selected;
        auto outputId = systemOutputIndex < static_cast<int>(outputs.size()) ? outputs[systemOutputIndex].id : std::wstring{};
        Target oldProcess{};
        if (processIndex < static_cast<int>(processList.size())) oldProcess = processList[processIndex];
        screens = monitors(); windowList = windows();
        processList.clear();
        for (auto const& target : windowList) {
            if (!target.creationTime) continue;
            if (std::none_of(processList.begin(), processList.end(), [&](auto const& other) { return other.pid == target.pid; }))
                processList.push_back(target);
        }
        processIndex = 0;
        for (size_t i = 0; i < processList.size(); ++i) if (processList[i].pid == oldProcess.pid && processList[i].creationTime == oldProcess.creationTime)
            processIndex = static_cast<int>(i);
        microphones = audioDevices(true); outputs = audioDevices(false); cameras = cameraDevices();
        auto labels = [](auto const& devices) { std::vector<std::wstring> values; for (auto const& d : devices) values.push_back(d.name); return values; };
        setItems(MicDevice, labels(microphones)); setItems(OutputDevice, labels(outputs)); setItems(CameraDevice, labels(cameras));
        auto restore = [&](int id, auto const& devices, std::wstring const& key) {
            controls[id].selected = 0;
            for (size_t i = 0; i < devices.size(); ++i) if (devices[i].id == key) controls[id].selected = static_cast<int>(i);
            updateText(id);
        };
        restore(MicDevice, microphones, micId); restore(OutputDevice, outputs, outputId); restore(CameraDevice, cameras, cameraId);
        systemOutputIndex = controls[OutputDevice].selected;
        outputItems(false);
        sourceItems();
        auto const& current = controls[SourceKind].selected == 0 ? screens : windowList;
        for (size_t i = 0; i < current.size(); ++i)
            if (current[i].monitor == previous.monitor && current[i].window == previous.window && current[i].pid == previous.pid)
                controls[Source].selected = static_cast<int>(i);
        updateText(Source); enabled();
    }
    void sourceItems() {
        auto const& list = controls[SourceKind].selected == 0 ? screens : windowList;
        std::vector<std::wstring> labels; for (auto const& t : list) labels.push_back(t.name);
        controls[Source].selected = 0; setItems(Source, std::move(labels));
    }
    void outputItems(bool preserve = true) {
        if (preserve) {
            if (previousAudioMode == 2) processIndex = controls[OutputDevice].selected;
            else systemOutputIndex = controls[OutputDevice].selected;
        }
        auto mode = controls[OutputMode].selected;
        std::vector<std::wstring> names;
        if (mode == 2) for (auto const& process : processList)
            names.push_back(process.name + L" · PID " + std::to_wstring(process.pid));
        else for (auto const& device : outputs) names.push_back(device.name);
        controls[OutputDevice].selected = mode == 2 ? processIndex : systemOutputIndex;
        setItems(OutputDevice, std::move(names)); previousAudioMode = mode;
    }
    void enabled() {
        bool idle = !recorder.busy() && !finishing;
        for (auto& [id, control] : controls) {
            if(id>=OpenSettings || (id>=ThemeSystem && id<=ThemeDark))continue;
            if (id == Start || (id >= ThemeSystem && id <= ThemeDark)) continue;
            bool enable = idle || id==Microphone || id==Webcam || id==CameraMirror || id==SystemVolume || id==MicVolume || id==NoiseStrength || id==Denoise || id==Corner || id==Background || id==BackgroundPreset || id==BlurStrength;
            enable &= !finishing;
            if(id==Microphone)enable &= microphones.size()>1;
            if(id==Webcam)enable &= !cameras.empty();
            if(id==CameraMirror)enable &= controls[Webcam].checked && !cameras.empty();
            if (id == MicDevice || id == Denoise || id==MicVolume || id==NoiseStrength) enable &= controls[Microphone].checked;
            if(id==NoiseStrength) enable &= controls[Denoise].checked;
            if(id==SystemVolume) enable &= controls[OutputMode].selected!=0;
            if (id == OutputDevice) enable &= controls[OutputMode].selected != 0;
            if (id == CameraDevice || id == Corner || id == Background) enable &= controls[Webcam].checked && !cameras.empty();
            if (id == BackgroundFile) enable &= controls[Webcam].checked && !modelDownload &&
                (!std::filesystem::exists(executableDir()+L"\\models\\rvm_mobilenetv3_fp32.onnx") || controls[Background].selected==5);
            if(id==BackgroundPreset) enable &= controls[Webcam].checked && controls[Background].selected>=2 && controls[Background].selected<=4;
            if(id==BlurStrength) enable &= controls[Webcam].checked && controls[Background].selected==1;
            EnableWindow(control.window, enable);
        }
        EnableWindow(controls[Start].window, !finishing);
        controls[Start].label = recorder.busy() ? L"Остановить запись" : L"Начать запись";
        updateText(Start); InvalidateRect(window, nullptr, FALSE);
        if(!std::filesystem::exists(executableDir()+L"\\models\\rvm_mobilenetv3_fp32.onnx")) {
            controls[BackgroundFile].label=modelDownload?L"Загрузка эффектов…":L"Загрузить эффекты фона (15 МБ)";updateText(BackgroundFile);
        } else if(backgroundPath.empty()) {controls[BackgroundFile].label=L"Выбрать своё изображение…";updateText(BackgroundFile);}
        showPage();
    }
    bool settingControl(int id)const{return (id>=ThemeSystem&&id<=ThemeDark)||id==Language||id==LanguageEnglish||id==SaveFolder||id==Back;}
    void showPage(){
        for(auto& [id,c]:controls){
            bool visible=id==Minimize||id==Close||id==OpenSettings|| (settingsOpen?settingControl(id):!settingControl(id));
            ShowWindow(c.window,visible?SW_SHOW:SW_HIDE);
        }
        InvalidateRect(window,nullptr,FALSE);
    }
    void changeLanguage(bool value){english=value;for(auto& [id,c]:controls)updateText(id);savePreferences();InvalidateRect(window,nullptr,FALSE);}
    void savePreferences(){
        if(testing)return;
        HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,preferencesKey.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS){
            DWORD value=english;RegSetValueExW(key,L"English",0,REG_DWORD,reinterpret_cast<BYTE*>(&value),sizeof(value));
            RegSetValueExW(key,L"SaveFolder",0,REG_SZ,reinterpret_cast<BYTE const*>(saveFolder.c_str()),DWORD((saveFolder.size()+1)*sizeof(wchar_t)));RegCloseKey(key);
        }
    }
    void selectFolder(){
        ComPtr<IFileDialog> dialog;check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));
        DWORD flags{};check(dialog->GetOptions(&flags));check(dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST));
        auto title=tr(L"ПАПКА СОХРАНЕНИЯ");dialog->SetTitle(title.c_str());
        if(SUCCEEDED(dialog->Show(window))){ComPtr<IShellItem> item;check(dialog->GetResult(&item));PWSTR name{};check(item->GetDisplayName(SIGDN_FILESYSPATH,&name));saveFolder=name;CoTaskMemFree(name);savePreferences();InvalidateRect(window,nullptr,FALSE);}
    }
    void create() {
        dpi = GetDpiForWindow(window); fonts();
        add(OpenSettings,Kind::Segment,L"Настройки",848,16,36,36);
        add(Minimize,Kind::Segment,L"Свернуть",912,16,28,30);
        add(Close,Kind::Segment,L"Закрыть",948,16,28,30);
        add(Back,Kind::Button,L"Назад",24,580,160,36);
        add(Language,Kind::Segment,L"RU",312,140,32,28);
        add(LanguageEnglish,Kind::Segment,L"EN",348,140,32,28);
        add(ThemeSystem,Kind::Segment,L"Система",24,176,116,32);
        add(ThemeLight,Kind::Segment,L"Светлая",144,176,116,32);
        add(ThemeDark,Kind::Segment,L"Тёмная",264,176,116,32);
        add(SaveFolder,Kind::Button,L"Выбрать папку…",24,324,308,36);
        add(SourceKind,Kind::Choice,L"Источник",24,128,178,34);setItems(SourceKind,{L"Запись экрана",L"Отдельное окно"});
        add(Refresh,Kind::Button,L"Обновить",214,128,96,34);
        add(Source,Kind::Choice,L"Экран / окно",24,172,286,34);
        add(OutputMode,Kind::Choice,L"Выходной звук",336,128,300,34);setItems(OutputMode,{L"Звук выключен",L"Системный звук",L"Звук приложения / процесса"});controls[OutputMode].selected=1;
        add(OutputDevice,Kind::Choice,L"Устройство выхода",336,172,300,34);
        add(SystemVolume,Kind::Slider,L"Системный звук",336,214,300,54);
        add(Microphone,Kind::Toggle,L"Микрофон",336,280,300,30);setChecked(Microphone,true);
        add(MicDevice,Kind::Choice,L"Устройство микрофона",336,318,300,34);
        add(MicVolume,Kind::Slider,L"Громкость микрофона",336,360,300,54);
        for(int id:{SystemVolume,MicVolume}) {controls[id].maximum=200;controls[id].selected=100;updateText(id);}
        add(Denoise,Kind::Toggle,L"Шумоподавление",336,430,300,30);
        add(NoiseStrength,Kind::Slider,L"Интенсивность",336,468,300,54);controls[NoiseStrength].selected=65;updateText(NoiseStrength);
        add(Webcam,Kind::Toggle,L"Кружок на экране",662,128,314,30);
        add(CameraDevice,Kind::Choice,L"Камера",662,168,314,34);
        add(Corner,Kind::Choice,L"Положение",662,210,148,34);setItems(Corner,{L"Слева сверху",L"Справа сверху",L"Слева снизу",L"Справа снизу"});controls[Corner].selected=3;updateText(Corner);
        add(CameraMirror,Kind::Toggle,L"Зеркало",822,210,154,34);
        add(Background,Kind::Choice,L"Фон",662,252,314,34);setItems(Background,{L"Реальный фон",L"Размыть реальный фон",L"Однотонный фон",L"Спокойный градиент",L"Нейтральный интерьер",L"Своё изображение"});
        add(BackgroundPreset,Kind::Choice,L"Вариант",662,294,314,34);backgroundItems();
        add(BlurStrength,Kind::Slider,L"Размытие",662,336,314,54);controls[BlurStrength].selected=40;updateText(BlurStrength);
        add(BackgroundFile,Kind::Button,L"Выбрать своё изображение…",662,402,314,34);
        add(Save,Kind::Button,L"Куда сохранить MP4…",24,580,250,36);
        add(Start,Kind::Button,L"Начать запись",726,580,250,36);
        // Keep the dense three-column layout; inputs share a 36px visual height.
        for(auto const& [id,c]:controls)if(c.kind==Kind::Choice || (c.kind==Kind::Button&&id!=Start&&id!=Save)){
            RECT r{};GetWindowRect(c.window,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
            SetWindowPos(c.window,nullptr,r.left,r.top,r.right-r.left,px(36),SWP_NOZORDER);
        }
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
        dpi=std::min<int>(dpi,std::min<long>((monitor.rcWork.right-monitor.rcWork.left-20)*96/1000,(monitor.rcWork.bottom-monitor.rcWork.top-50)*96/640));
        fonts();
        // Rescale children if a small desktop cannot fit the preferred system scale.
        int originalDpi=GetDpiForWindow(window);
        if(dpi!=originalDpi) for(auto const& [id,c]:controls) {RECT r{};GetWindowRect(c.window,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);SetWindowPos(c.window,nullptr,MulDiv(r.left,dpi,originalDpi),MulDiv(r.top,dpi,originalDpi),MulDiv(r.right-r.left,dpi,originalDpi),MulDiv(r.bottom-r.top,dpi,originalDpi),SWP_NOZORDER);}
        // Preserve the previous outer window footprint, including its former non-client area.
        RECT client{0,0,px(1000),px(640)};AdjustWindowRectExForDpi(&client,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0,GetDpiForWindow(window));
        SetWindowPos(window,nullptr,0,0,client.right-client.left,client.bottom-client.top,SWP_NOMOVE|SWP_NOZORDER);
        layoutChrome();
        scrollRange();
        refresh(); applyTheme(); SetTimer(window, 1, 33, nullptr);
    }
    void text(HDC dc, std::wstring const& value, int x, int y, int w, int h, HFONT face, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
        RECT r{px(x),px(y)-scroll,px(x+w),px(y+h)-scroll};
        SelectObject(dc, face); SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
        auto translated=tr(value);DrawTextW(dc, translated.c_str(), -1, &r, flags);
    }
    void spaced(HDC dc, std::wstring const& value, int y, HFONT face, int spacing) {
        SelectObject(dc, face); SetTextColor(dc, palette.text); SetBkMode(dc, TRANSPARENT);
        int x = px(24);
        for (wchar_t c : value) { SIZE size{}; GetTextExtentPoint32W(dc, &c, 1, &size); TextOutW(dc, x, px(y)-scroll, &c, 1); x += size.cx + px(spacing); }
    }
    void line(HDC dc, int y) {
        auto pen = CreatePen(PS_SOLID, 1, palette.line); auto old = SelectObject(dc, pen);
        MoveToEx(dc, px(24), px(y)-scroll, nullptr); LineTo(dc, px(976), px(y)-scroll); SelectObject(dc, old); DeleteObject(pen);
    }
    void paint(HDC dc) {
        RECT r{}; GetClientRect(window, &r); FillRect(dc, &r, brush);
        spaced(dc, L"SCREENCAST.", 24, title, 2);
        line(dc,62);
        if(settingsOpen){
            text(dc,L"Настройки",24,88,600,28,title,palette.text);
            auto pen=CreatePen(PS_SOLID,1,palette.line);auto previous=SelectObject(dc,pen);
            for(int y:{128,220}){MoveToEx(dc,px(24),px(y),nullptr);LineTo(dc,px(380),px(y));}SelectObject(dc,previous);DeleteObject(pen);
            drawVpnIcon(dc,VpnIcon::Palette,px(24),px(145),px(17),palette.muted);
            text(dc,L"ВНЕШНИЙ ВИД",51,140,248,28,smallFont,palette.text);
            text(dc,L"ПАПКА СОХРАНЕНИЯ",24,244,600,20,heading,palette.muted);
            text(dc,saveFolder,24,276,952,36,font,palette.text);
            text(dc,L"Папка используется для новых записей. Файл MP4 можно выбрать перед записью.",24,372,700,48,smallFont,palette.muted,DT_WORDBREAK);
            return;
        }
        text(dc,L"ИСТОЧНИК ВИДЕО",24,100,286,20,heading,palette.text);
        text(dc,L"ЗВУК",336,100,300,20,heading,palette.text);
        text(dc,L"КАМЕРА И ФОН",662,100,314,20,heading,palette.text);
        text(dc,L"Перетащите кружок прямо на рабочем столе.",662,450,314,36,smallFont,palette.muted,DT_WORDBREAK);
        text(dc,modelDownload?L"Загрузка модели…":L"Эффекты работают локально на компьютере.",662,496,314,34,smallFont,palette.muted,DT_WORDBREAK);
        text(dc,L"ПРЕДПРОСМОТР",24,216,286,20,heading,palette.muted);
        line(dc,540);
        auto label = status;
        if (recorder.recording()) {
            auto t = recorder.elapsed(); wchar_t duration[64]{};
            swprintf_s(duration, L"Запись · %02lld:%02lld:%02lld", t / 3600, t / 60 % 60, t % 60);
            label = duration;
            label=tr(label);label+=tr(recorder.hardware()?L" · аппаратное кодирование":L" · программное кодирование");
        }
        text(dc, label, 24, 546, 952, 24, smallFont, palette.muted);
        if(modelDownload||finishing){
            auto track=CreateSolidBrush(palette.line),ink=CreateSolidBrush(palette.text);
            RECT bar{px(662),px(532),px(976),px(536)};FillRect(dc,&bar,track);
            BOOL motion=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0);
            int width=bar.right-bar.left,offset=motion?int(GetTickCount64()/15%std::max(1,width-width/3)):0;
            bar.left+=offset;bar.right=bar.left+width/3;FillRect(dc,&bar,ink);DeleteObject(track);DeleteObject(ink);
        }
        displayed=recorder.preview();
        RECT area{px(24),px(244),px(310),px(510)}; FillRect(dc,&area,brush);
        if(displayed && !displayed->empty()) {
            float scale=std::min(float(area.right-area.left)/displayed->width,float(area.bottom-area.top)/displayed->height);
            int w=int(displayed->width*scale),h=int(displayed->height*scale);
            int x=area.left+(area.right-area.left-w)/2,y=area.top+(area.bottom-area.top-h)/2;
            previewRect={x,y,x+w,y+h};
            BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=displayed->width;
            bitmap.bmiHeader.biHeight=-displayed->height;bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;
            SetStretchBltMode(dc,COLORONCOLOR);
            StretchDIBits(dc,x,y,w,h,0,0,displayed->width,displayed->height,displayed->bgra.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
        } else text(dc,L"Предпросмотр появится после начала записи",24,340,286,70,smallFont,palette.muted,DT_CENTER|DT_WORDBREAK);
    }
    void draw(DRAWITEMSTRUCT const& item) {
        auto found = controls.find(static_cast<int>(item.CtlID)); if (found == controls.end()) return;
        auto& control = found->second; auto dc = item.hDC; auto r = item.rcItem;
        bool disabled = item.itemState & ODS_DISABLED, pressed = item.itemState & ODS_SELECTED;
        COLORREF color = disabled ? palette.muted : palette.text;
        FillRect(dc, &r, brush);
        if(item.CtlID==OpenSettings){
            // Exact approved SVG contours. No tile, frame, selection ring or alternate gear.
            drawApprovedSettingsIcon(dc,(r.right-px(24))/2,(r.bottom-px(24))/2,px(24),control.hot>0?palette.text:palette.muted);
            return;
        }
        if(control.kind==Kind::Slider) {
            SelectObject(dc,smallFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
            auto label=tr(control.label)+L" · "+std::to_wstring(control.selected)+L"%";
            RECT labelRect{px(14),0,r.right-px(14),px(24)};DrawTextW(dc,label.c_str(),-1,&labelRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            int left=px(14),right=r.right-px(14),y=px(38),x=left+MulDiv(right-left,control.selected,control.maximum);
            auto pen=CreatePen(PS_SOLID,px(2),palette.line);auto before=SelectObject(dc,pen);
            MoveToEx(dc,left,y,nullptr);LineTo(dc,right,y);SelectObject(dc,before);DeleteObject(pen);
            pen=CreatePen(PS_SOLID,px(2),color);before=SelectObject(dc,pen);MoveToEx(dc,left,y,nullptr);LineTo(dc,x,y);
            SelectObject(dc,before);DeleteObject(pen);
            // Fill only: a GDI stroke on a tiny ellipse produces jagged protrusions.
            Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::SolidBrush thumb(Gdiplus::Color(255,GetRValue(palette.text),GetGValue(palette.text),GetBValue(palette.text)));
            graphics.FillEllipse(&thumb,float(x-px(6)),float(y-px(6)),float(px(12)),float(px(12)));
            return;
        }
        auto label = control.label;

        if (control.kind == Kind::Choice) label = control.items.empty() ? L"Нет доступных устройств" : control.items[control.selected];
        label=tr(label);
        if(item.CtlID==Minimize || item.CtlID==Close){
            drawVpnWindowIcon(dc,item.CtlID==Minimize?VpnIcon::Minus:VpnIcon::X,r.right,r.bottom,dpi,control.hot>0?palette.text:palette.muted);
            return;
        }
        bool themeButton=item.CtlID>=ThemeSystem&&item.CtlID<=ThemeDark;
        bool languageButton=item.CtlID==Language||item.CtlID==LanguageEnglish;
        if(themeButton||languageButton){
            bool selected=themeButton?int(item.CtlID)-ThemeSystem==theme:(item.CtlID==LanguageEnglish?english:!english);
            auto stroke=CreatePen(PS_SOLID,1,selected&&themeButton?palette.muted:palette.line);
            auto fill=CreateSolidBrush(selected?palette.hover:palette.background);auto a=SelectObject(dc,stroke),b=SelectObject(dc,fill);
            RoundRect(dc,0,0,r.right-1,r.bottom-1,px(16),px(16));SelectObject(dc,a);SelectObject(dc,b);DeleteObject(stroke);DeleteObject(fill);
            COLORREF ink=selected?palette.text:palette.muted;SelectObject(dc,smallFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ink);
            RECT textRect=r;
            if(themeButton){SIZE extent{};GetTextExtentPoint32W(dc,label.c_str(),int(label.size()),&extent);
                int left=(r.right-extent.cx-px(18))/2;drawVpnThemeIcon(dc,item.CtlID==ThemeSystem?VpnIcon::Monitor:item.CtlID==ThemeLight?VpnIcon::Sun:VpnIcon::Moon,left,(r.bottom-px(13))/2,px(13),ink);textRect.left=left+px(18);textRect.right=r.right-px(4);
                DrawTextW(dc,label.c_str(),-1,&textRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            }else DrawTextW(dc,label.c_str(),-1,&textRect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            return;
        }
        bool primary = item.CtlID == Start;
        auto pen = CreatePen(PS_SOLID, 1, palette.line);
        auto fill = CreateSolidBrush(primary && !disabled ? palette.active : blend(palette.background,palette.hover,pressed?1.f:control.hot));
        auto oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, fill);
        if(control.kind==Kind::Segment)SelectObject(dc,GetStockObject(NULL_PEN));
        if (control.kind != Kind::Toggle && (control.kind != Kind::Segment || control.hot>0||pressed))
            RoundRect(dc, r.left, r.top, r.right - 1, r.bottom - 1, px(16), px(16));
        SelectObject(dc, font); SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, primary && !disabled ? palette.buttonText : color);
        auto labelRect = r; labelRect.left += px(control.kind == Kind::Toggle ? 0 : 14); labelRect.right -= px(14);
        if (control.kind == Kind::Toggle) labelRect.right -= px(60);
        if (control.kind == Kind::Choice) labelRect.right -= px(20);
        if(pressed&&control.kind==Kind::Button)OffsetRect(&labelRect,0,px(1));
        DrawTextW(dc, label.c_str(), -1, &labelRect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
            ((control.kind == Kind::Button || control.kind == Kind::Segment) ? DT_CENTER : DT_LEFT));
        if (control.kind == Kind::Choice) {
            int x = r.right - px(20), y = (r.top+r.bottom)/2;
            MoveToEx(dc, x-px(4), y-px(2), nullptr); LineTo(dc, x, y+px(2)); LineTo(dc, x+px(4), y-px(2));
        }
        if(control.kind==Kind::Toggle) drawSwitch(dc,r.right-px(40),(r.bottom-r.top-px(23))/2,px(40),px(23),control.checked,dark,true,!disabled);

        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(fill); DeleteObject(pen);
    }
    std::wstring choose(bool save, wchar_t const* filter) {
        wchar_t buffer[32768]{};
        if (save) {
            SYSTEMTIME time{}; GetLocalTime(&time);
            swprintf_s(buffer, L"Screencast-%04u-%02u-%02u-%02u-%02u-%02u.mp4", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
            if(!saveFolder.empty()){auto initial=(std::filesystem::path(saveFolder)/buffer).wstring();wcscpy_s(buffer,initial.c_str());}
        }
        OPENFILENAMEW file{}; file.lStructSize = sizeof(file); file.hwndOwner = window;
        file.lpstrFilter = filter; file.lpstrFile = buffer; file.nMaxFile = 32768;
        file.lpstrDefExt = save ? L"mp4" : nullptr;
        if(english){file.lpstrFilter=save?L"MP4 video\0*.mp4\0\0":L"Images\0*.png;*.jpg;*.jpeg;*.bmp\0\0";file.lpstrTitle=save?L"Save recording":L"Choose image";}
        if(save&&!saveFolder.empty())file.lpstrInitialDir=saveFolder.c_str();
        file.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        bool ok = save ? GetSaveFileNameW(&file) != FALSE : GetOpenFileNameW(&file) != FALSE;
        if (!ok && CommDlgExtendedError()) throw winrt::hresult_error(E_FAIL, L"Не удалось открыть диалог выбора файла");
        return ok ? buffer : L"";
    }
    void start() {
        if (recorder.busy()) { finishing = true; status = L"Завершаю MP4…"; recorder.stop(); enabled(); return; }
        auto const& targets = controls[SourceKind].selected == 0 ? screens : windowList;
        if (targets.empty()) throw winrt::hresult_error(E_INVALIDARG, L"Нет доступного источника видео");
        Settings settings;
        settings.target = targets.at(controls[Source].selected);
        settings.audio = static_cast<AudioMode>(controls[OutputMode].selected);
        if (settings.audio == AudioMode::Process) {
            if (processList.empty()) throw winrt::hresult_error(E_INVALIDARG, L"Нет доступных приложений для захвата звука");
            auto const& process = processList.at(controls[OutputDevice].selected);
            settings.audioPid = process.pid; settings.audioCreationTime = process.creationTime;
        }
        settings.mic = controls[Microphone].checked && microphones.size()>1; settings.denoise = controls[Denoise].checked;
        settings.micAvailable=microphones.size()>1;
        settings.micId = microphones.at(controls[MicDevice].selected).id;
        if (settings.audio == AudioMode::System) settings.outputId = outputs.at(controls[OutputDevice].selected).id;
        settings.camera = controls[Webcam].checked;
        if(!cameras.empty())settings.cameraId=cameras.at(controls[CameraDevice].selected).id;
        if (settings.camera) {
            if (cameras.empty()) throw winrt::hresult_error(E_INVALIDARG, L"Камера не подключена");
            settings.cameraId = cameras.at(controls[CameraDevice].selected).id;
        }
        settings.corner = controls[Corner].selected;
        updateLive(); settings.live=live;
        live->cameraX=float(settings.corner%2); live->cameraY=float(settings.corner/2);
        settings.replaceBackground = settings.camera && controls[Background].selected==5;
        settings.backgroundPath = backgroundPath;
        if (settings.replaceBackground && backgroundPath.empty()) throw winrt::hresult_error(E_INVALIDARG, L"Выберите изображение фона");
        if (path.empty()) path = choose(true, L"Видео MP4\0*.mp4\0\0");
        if (path.empty()) return;
        if (_wcsicmp(std::filesystem::path(path).extension().c_str(), L".mp4")) path += L".mp4";
        settings.path = path;
        overlay.configure(settings.target,live);
        status = L"Подключаю источники…";
        recorder.start(settings, window); enabled();
    }
    void command(int id) {
        if(id==OpenSettings||id==Back){settingsOpen=id==OpenSettings?!settingsOpen:false;showPage();SetFocus(controls[settingsOpen?Back:OpenSettings].window);return;}
        if(id==Minimize){ShowWindow(window,SW_MINIMIZE);return;}
        if(id==Close){SendMessageW(window,WM_CLOSE,0,0);return;}
        if(id==SaveFolder){selectFolder();return;}
        if(id==Language||id==LanguageEnglish){changeLanguage(id==LanguageEnglish);return;}
        if (id >= ThemeSystem && id <= ThemeDark) {
            theme = id - ThemeSystem; applyTheme(); saveTheme(); return;
        }
        auto& control = controls.at(id);
        if (control.kind == Kind::Choice) {
            if (control.items.empty()) return;
            RECT r{}; GetWindowRect(control.window, &r);
            // Copy the choices: device refresh messages may arrive while the popup is open.
            auto choices=control.items;for(auto& value:choices)value=tr(value);
            Dropdown list(choices,font,dpi,control.selected,palette.background,palette.text,palette.line,palette.hover);
            auto picked=list.show(window,r);
            if(!IsWindow(window))return;
            if (picked) {
                if(id==Background && picked==6 && recorder.busy() && backgroundPath.empty()) {
                    notice(L"Сначала выберите своё изображение до начала записи.");return;
                }
                if(id==Background && picked>1 && !std::filesystem::exists(executableDir()+L"\\models\\rvm_mobilenetv3_fp32.onnx")) {installModel(picked-1);return;}
                control.selected = picked - 1; updateText(id);
                if (id == SourceKind) sourceItems();
                if (id == OutputMode) outputItems();
                if(id==Background) backgroundItems();
                if(id==Corner) { live->cameraX=float(control.selected%2); live->cameraY=float(control.selected/2); }
                updateLive();
            }
            enabled();
        } else if (control.kind == Kind::Toggle) { setChecked(id, !control.checked); updateLive(); enabled(); }
        else if (id == Refresh) refresh();
        else if (id == BackgroundFile) {
            if(!std::filesystem::exists(executableDir()+L"\\models\\rvm_mobilenetv3_fp32.onnx")) {installModel(1);return;}
            auto value = choose(false, L"Изображения\0*.png;*.jpg;*.jpeg;*.bmp\0\0");
            if (!value.empty()) {
                loadImage(value); backgroundPath = value;
                controls[BackgroundFile].label = std::filesystem::path(value).filename().wstring(); updateText(BackgroundFile);
            }
        } else if (id == Save) {
            auto value = choose(true, L"Видео MP4\0*.mp4\0\0");
            if (!value.empty()) { path = value; status = std::filesystem::path(path).filename().wstring(); InvalidateRect(window, nullptr, TRUE); }
        } else if (id == Start) start();
    }
    void saveTheme() {
        if(testing)return;
        HKEY key{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, preferencesKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            DWORD value = theme; RegSetValueExW(key, L"Theme", 0, REG_DWORD, reinterpret_cast<BYTE*>(&value), sizeof(value)); RegCloseKey(key);
        }
    }
    void readTheme() {
        DWORD value{}, size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, preferencesKey.c_str(), L"Theme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value <= 2) theme = value;
        size=sizeof(value);value=0;if(RegGetValueW(HKEY_CURRENT_USER,preferencesKey.c_str(),L"English",RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS)english=value==1;
        wchar_t folder[32768]{};size=sizeof(folder);if(RegGetValueW(HKEY_CURRENT_USER,preferencesKey.c_str(),L"SaveFolder",RRF_RT_REG_SZ,nullptr,folder,&size)==ERROR_SUCCESS)saveFolder=folder;
        if(saveFolder.empty()){PWSTR name{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos,0,nullptr,&name))){saveFolder=name;CoTaskMemFree(name);}}
    }
    void uiTest(std::filesystem::path const& directory){
        auto require=[](bool value,wchar_t const* why){if(!value)throw winrt::hresult_error(E_FAIL,why);};
        RECT outer{};GetWindowRect(window,&outer);
        RECT expected{0,0,px(1000),px(640)};AdjustWindowRectExForDpi(&expected,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0,GetDpiForWindow(window));
        require(outer.right-outer.left==expected.right-expected.left && outer.bottom-outer.top==expected.bottom-expected.top,L"Outer window footprint changed");
        auto style=GetWindowLongPtrW(window,GWL_STYLE);require(!(style&WS_CAPTION)&&!(style&WS_MAXIMIZEBOX),L"Native caption or maximize remains");
        if(cornersApplied){DWM_WINDOW_CORNER_PREFERENCE preference{};require(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&preference,sizeof(preference)))&&preference==DWMWCP_ROUND,L"CONNECT window rounding did not apply");}
        auto before=outer;SendMessageW(window,WM_SYSCOMMAND,SC_MAXIMIZE,0);GetWindowRect(window,&outer);require(EqualRect(&before,&outer),L"Maximize changed the fixed window");
        auto header=MAKELPARAM(outer.left+px(300),outer.top+px(24));require(SendMessageW(window,WM_NCHITTEST,0,header)==HTCAPTION,L"Custom header cannot drag");
        command(Minimize);require(IsIconic(window),L"Custom minimize failed");SingleInstance::activate(window);require(!IsIconic(window),L"Restore failed");
        command(OpenSettings);require(settingsOpen,L"Settings did not open");
        require((GetWindowLongPtrW(controls[Language].window,GWL_STYLE)&WS_VISIBLE)!=0 && !(GetWindowLongPtrW(controls[Source].window,GWL_STYLE)&WS_VISIBLE),L"Settings visibility is incorrect");
        auto source=controls[SourceKind].selected;auto mic=live->micEnabled.load();auto camera=live->cameraEnabled.load();
        preferencesKey=L"Software\\ScreencastUITest-"+std::to_wstring(GetCurrentProcessId());testing=false;
        try{
            require(!windowPosition::restore(window,preferencesKey.c_str()),L"Missing window position must preserve first-launch placement");
            MONITORINFO monitor{sizeof(monitor)};require(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE,L"Position test monitor unavailable");
            RECT current{};GetWindowRect(window,&current);SIZE size{current.right-current.left,current.bottom-current.top};
            auto target=windowPosition::fit(POINT{monitor.rcWork.left+20,monitor.rcWork.top+20},size,monitor.rcWork);
            SetWindowPos(window,nullptr,target.x,target.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            SendMessageW(window,WM_EXITSIZEMOVE,0,0);
            SetWindowPos(window,nullptr,target.x+10,target.y+10,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            require(windowPosition::restore(window,preferencesKey.c_str()),L"Saved window position did not restore");
            GetWindowRect(window,&current);require(current.left==target.x&&current.top==target.y,L"Window reopened at the wrong position");
            command(Minimize);require(!windowPosition::save(window,preferencesKey.c_str()),L"Minimized coordinates overwrote window position");
            SingleInstance::activate(window);require(windowPosition::restore(window,preferencesKey.c_str()),L"Minimized close lost window position");
            GetWindowRect(window,&current);require(current.left==target.x&&current.top==target.y,L"Minimized position changed saved placement");
            auto shutdownTarget=windowPosition::fit(POINT{target.x-4,target.y-4},size,monitor.rcWork);
            SetWindowPos(window,nullptr,shutdownTarget.x,shutdownTarget.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            require(SendMessageW(window,WM_QUERYENDSESSION,0,0)==TRUE,L"Shutdown position save blocked Windows");
            SetWindowPos(window,nullptr,target.x,target.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            require(windowPosition::restore(window,preferencesKey.c_str()),L"Shutdown position did not persist");
            GetWindowRect(window,&current);require(current.left==shutdownTarget.x&&current.top==shutdownTarget.y,L"Shutdown saved the wrong position");
            HKEY positionKey{};require(RegOpenKeyExW(HKEY_CURRENT_USER,preferencesKey.c_str(),0,KEY_SET_VALUE,&positionKey)==ERROR_SUCCESS,L"Position test registry unavailable");
            POINT missingMonitor{2147483647L,(-2147483647L-1L)};RegSetValueExW(positionKey,L"WindowPosition",0,REG_BINARY,reinterpret_cast<BYTE const*>(&missingMonitor),sizeof(missingMonitor));RegCloseKey(positionKey);
            require(windowPosition::restore(window,preferencesKey.c_str()),L"Disconnected monitor fallback failed");
            GetWindowRect(window,&current);monitor={sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            require(current.left>=monitor.rcWork.left&&current.top>=monitor.rcWork.top&&current.right<=std::max(monitor.rcWork.right,monitor.rcWork.left+size.cx)&&current.bottom<=std::max(monitor.rcWork.bottom,monitor.rcWork.top+size.cy),L"Restored window is outside the work area");
            auto negative=windowPosition::fit(POINT{-1700,100},SIZE{1000,640},RECT{-1920,0,0,1080});require(negative.x==-1700&&negative.y==100,L"Negative secondary-monitor coordinates were discarded");
            auto smallDisplay=windowPosition::fit(POINT{900,700},SIZE{1000,640},RECT{0,0,800,600});require(smallDisplay.x==0&&smallDisplay.y==0,L"Small display must keep the titlebar reachable without resizing");
            require(RegOpenKeyExW(HKEY_CURRENT_USER,preferencesKey.c_str(),0,KEY_SET_VALUE,&positionKey)==ERROR_SUCCESS,L"Position test registry unavailable");
            DWORD invalid=7;RegSetValueExW(positionKey,L"WindowPosition",0,REG_BINARY,reinterpret_cast<BYTE const*>(&invalid),sizeof(invalid));RegCloseKey(positionKey);
            require(!windowPosition::restore(window,preferencesKey.c_str()),L"Malformed saved position was accepted");
            KillTimer(window,5);
            for(int id:{ThemeLight,ThemeDark,ThemeSystem}){command(id);require(theme==id-ThemeSystem,L"Theme control is inert");if(id!=ThemeSystem)require(dark==(id==ThemeDark),L"Explicit theme did not apply");}
            command(LanguageEnglish);require(tr(L"Начать запись")==L"Start recording",L"Language does not translate");
            saveFolder=(directory/L"recordings").wstring();savePreferences();App loaded;loaded.preferencesKey=preferencesKey;loaded.readTheme();
            require(loaded.english&&loaded.theme==0&&loaded.saveFolder==saveFolder,L"Settings do not persist");
            command(Language);require(tr(L"Настройки")==L"Настройки",L"Language cannot return to RU");
            command(Back);require(!settingsOpen && controls[SourceKind].selected==source&&live->micEnabled.load()==mic&&live->cameraEnabled.load()==camera,L"UI settings changed capture state");
            require(!(GetWindowLongPtrW(controls[ThemeDark].window,GWL_STYLE)&WS_VISIBLE),L"Theme controls remain on the main screen");
        }catch(...){RegDeleteTreeW(HKEY_CURRENT_USER,preferencesKey.c_str());testing=true;throw;}
        RegDeleteTreeW(HKEY_CURRENT_USER,preferencesKey.c_str());testing=true;
        UiNotice note(window,font,dpi,palette.background,palette.text,palette.line,L"Background effects run locally.",L"Notice",L"Close");
        require(note.show([](HWND h){PostMessageW(GetWindow(h,GW_OWNER),WM_APP+105,0,0);PostMessageW(h,WM_COMMAND,IDOK,0);}),L"Notice confirmation failed");
        require(noticePumps==1&&IsWindowEnabled(window),L"Notice blocked owner messages or left it disabled");
        UiNotice confirm(window,font,dpi,palette.background,palette.text,palette.line,L"Confirm?",L"Notice",L"Continue",L"Cancel");
        require(!confirm.show([](HWND h){PostMessageW(h,WM_KEYDOWN,VK_ESCAPE,0);})&&IsWindowEnabled(window),L"Notice Escape cancellation failed");
        for(int scale:{144,192,96}){
            RECT r{};GetWindowRect(window,&r);r.right=r.left+MulDiv(r.right-r.left,scale,dpi);r.bottom=r.top+MulDiv(r.bottom-r.top,scale,dpi);
            SendMessageW(window,WM_DPICHANGED,MAKELONG(scale,scale),reinterpret_cast<LPARAM>(&r));
            RECT area{};GetClientRect(window,&area);for(auto const& [id,c]:controls){RECT child{};GetWindowRect(c.window,&child);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&child),2);
                require(child.left>=0&&child.top>=0&&child.right<=area.right&&child.bottom<=area.bottom,L"DPI scaling clips a control");}
            LOGFONTW face{};GetObjectW(font,sizeof(face),&face);require(std::wstring(face.lfFaceName)==L"Montserrat",L"Font changed during DPI handling");
        }
        std::ofstream report(directory/L"ui-system.txt");report<<"PASS: window position persists after moving and shutdown, minimized coordinates ignored, missing/corrupt position safe, disconnected/negative/small monitor placement handled; fixed outer size, custom header/drag/minimize/restore, maximize blocked; settings visibility; RU/EN and all theme modes; isolated registry persistence for language/theme/save folder; unchanged capture selections and live settings; production DPI handler at 96/144/192 with no clipped controls and Montserrat retained; modal owner-message delivery, confirmation, Escape and owner re-enable.\n";
    }
    void snapshot() {
        for(int scale:{96,144,192}) for(int kind:{ICON_SMALL,ICON_SMALL2,ICON_BIG}) {
            auto icon=reinterpret_cast<HICON>(SendMessageW(window,WM_GETICON,kind,scale));
            ICONINFO info{};check(GetIconInfo(icon,&info)?S_OK:E_FAIL);
            BITMAP bitmap{};GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap);
            if(info.hbmColor)DeleteObject(info.hbmColor);if(info.hbmMask)DeleteObject(info.hbmMask);
            check(bitmap.bmWidth==GetSystemMetricsForDpi(SM_CXICON,scale)?S_OK:E_FAIL);
        }
        RECT r{}; GetClientRect(window, &r); int w = r.right, h = r.bottom;
        HDC dc = GetDC(window), mem = CreateCompatibleDC(dc);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* bits{}; HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        auto old = SelectObject(mem, bitmap);
        // Use the exact production paint routines even on a hidden CI window.
        // PrintWindow returns black for hidden windows on some Windows runners.
        if(windowHoverSnapshot)for(int id:{Minimize,Close})controls[id].hot=windowHoverStartSnapshot?.1f:1.f;
        paint(mem);
        for (auto const& [id, c] : controls) {
            if(!(GetWindowLongPtrW(c.window,GWL_STYLE)&WS_VISIBLE))continue;
            RECT rect{}; GetWindowRect(c.window, &rect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
            auto saved = SaveDC(mem); SetViewportOrgEx(mem, rect.left, rect.top, nullptr);
            IntersectClipRect(mem, 0, 0, rect.right-rect.left, rect.bottom-rect.top);
            DRAWITEMSTRUCT item{};
            item.CtlID = id; item.hwndItem = c.window; item.hDC = mem; item.itemAction = ODA_DRAWENTIRE;
            item.itemState = IsWindowEnabled(c.window) ? 0 : ODS_DISABLED;
            if(mouseFocusSnapshot)item.itemState|=ODS_FOCUS|ODS_NOFOCUSRECT;
            if(keyboardFocusSnapshot)item.itemState|=ODS_FOCUS;
            if(windowHoverSnapshot&&(id==Minimize||id==Close))item.itemState|=ODS_SELECTED;
            item.rcItem = {0,0,rect.right-rect.left,rect.bottom-rect.top};
            draw(item); RestoreDC(mem, saved);
        }
        if(dropdownSnapshot) {
            auto const& c=controls[OutputMode];
            Dropdown list(c.items,font,dpi,c.selected,palette.background,palette.text,palette.line,palette.hover);
            list.width=px(300);list.rows=int(c.items.size());list.height=px(12)+list.rows*px(34);
            auto saved=SaveDC(mem);SetViewportOrgEx(mem,px(336),px(160),nullptr);list.paint(mem);RestoreDC(mem,saved);
        }
        ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        ComPtr<IWICStream> stream; check(factory->CreateStream(&stream)); check(stream->InitializeFromFilename(snapshotPath.c_str(), GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder; check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
        check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> frame; check(encoder->CreateNewFrame(&frame, nullptr)); check(frame->Initialize(nullptr));
        check(frame->SetSize(w, h)); auto format = GUID_WICPixelFormat32bppBGRA; check(frame->SetPixelFormat(&format));
        for (size_t i = 3; i < static_cast<size_t>(w) * h * 4; i += 4) static_cast<uint8_t*>(bits)[i] = 255;
        check(frame->WritePixels(h, w*4, w*h*4, static_cast<BYTE*>(bits))); check(frame->Commit()); check(encoder->Commit());
        SelectObject(mem, old); DeleteObject(bitmap); DeleteDC(mem); ReleaseDC(window, dc);
    }
    ~App() { for(auto const& [size,icon]:windowIcons)if(icon)DestroyIcon(icon);if(modelDownload)CloseHandle(modelDownload);for (auto f : {font, smallFont, heading, title}) if (f) DeleteObject(f); if (brush) DeleteObject(brush); }
};
App* app{};
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        if(message==SingleInstance::activationMessage()){SingleInstance::activate(hwnd);return 1;}
        if(message==WM_APP+105&&app->testing){++app->noticePumps;return 0;}
        switch (message) {
        case WM_CREATE: app->window = hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));app->create(); return 0;
        case WM_NCCALCSIZE:return 0;
        case WM_SIZE:app->layoutChrome();break;
        case WM_MOVE:
            if(app->positionReady&&!app->testing&&!IsIconic(hwnd))SetTimer(hwnd,5,400,nullptr);
            break;
        case WM_EXITSIZEMOVE:KillTimer(hwnd,5);app->saveWindowPosition();break;
        case WM_QUERYENDSESSION:app->saveWindowPosition();return TRUE;
        case WM_ENDSESSION:if(wParam)app->saveWindowPosition();break;
        case WM_NCHITTEST:{auto hit=DefWindowProcW(hwnd,message,wParam,lParam);POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(hwnd,&p);return hit==HTCLIENT&&p.y<app->px(62)?HTCAPTION:hit;}
        case WM_SYSCOMMAND:if((wParam&0xfff0)==SC_MAXIMIZE)return 0;break;
        case WM_GETICON:return reinterpret_cast<LRESULT>(app->windowIcon(wParam,lParam));
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL) { SendMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
            if (!app->controls.contains(LOWORD(wParam))) return 0;
            if (HIWORD(wParam) == BN_CLICKED) app->command(LOWORD(wParam));
            else if (HIWORD(wParam) == BN_SETFOCUS) app->reveal(LOWORD(wParam));
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(hwnd, SB_VERT, &info);
            int position = app->scroll;
            switch (LOWORD(wParam)) {
            case SB_LINEUP: position -= app->px(30); break;
            case SB_LINEDOWN: position += app->px(30); break;
            case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK: position = info.nTrackPos; break;
            }
            app->scrollTo(position); return 0;
        }
        case WM_MOUSEWHEEL: app->scrollTo(app->scroll - GET_WHEEL_DELTA_WPARAM(wParam) * app->px(60) / WHEEL_DELTA); return 0;
        case WM_LBUTTONDOWN: app->dragCamera(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam),true);return 0;
        case WM_MOUSEMOVE: if(app->dragging) app->dragCamera(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam),false);return 0;
        case WM_LBUTTONUP: if(app->dragging) {app->dragging=false;ReleaseCapture();}return 0;
        case WM_CAPTURECHANGED: app->dragging=false;return 0;
        case WM_DRAWITEM: {auto item=*reinterpret_cast<DRAWITEMSTRUCT*>(lParam);bufferedPaint(item.hDC,item.rcItem,[&](HDC back){item.hDC=back;app->draw(item);});return TRUE;}
        case WM_PAINT: {if(app->idleTestPhase==1)++app->idlePaints; PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps);bufferedPaint(dc,ps.rcPaint,[](HDC back){app->paint(back);}); EndPaint(hwnd, &ps); return 0; }
        case WM_ERASEBKGND: return 1;
        case WM_SETTINGCHANGE: if (!app->theme) app->applyTheme(); return 0;
        case WM_DPICHANGED: {
            int old = app->dpi; app->dpi = HIWORD(wParam); app->fonts();
            app->scroll = MulDiv(app->scroll, app->dpi, old);
            for (auto const& [id, c] : app->controls) {
                RECT r{}; GetWindowRect(c.window, &r); MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
                SetWindowPos(c.window, nullptr, MulDiv(r.left, app->dpi, old), MulDiv(r.top, app->dpi, old),
                    MulDiv(r.right-r.left, app->dpi, old), MulDiv(r.bottom-r.top, app->dpi, old), SWP_NOZORDER);
            }
            auto r = reinterpret_cast<RECT*>(lParam); SetWindowPos(hwnd, nullptr, r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER); app->scrollRange(); return 0;
        }
        case WM_TIMER:
            if(wParam==5){KillTimer(hwnd,5);app->saveWindowPosition();return 0;}
            if(wParam==4){app->hoverTick();return 0;}
            if(wParam==3) {
                if(app->idleTestPhase==0){app->idleTestPhase=1;app->idlePaints=0;SetTimer(hwnd,3,2000,nullptr);return 0;}
                KillTimer(hwnd,3);std::filesystem::create_directories(app->idleTestPath);
                bool passed=IsWindowVisible(hwnd) && app->idlePaints<=1 && (GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_CLIPCHILDREN);
                std::ofstream report(std::filesystem::path(app->idleTestPath)/L"idle-paint.txt");report<<"Idle parent repaints over 2 seconds: "<<app->idlePaints<<"\nChild clipping enabled; visible production window tested.\n"<<(passed?"PASS":"FAIL")<<'\n';report.close();
                DestroyWindow(hwnd);PostQuitMessage(passed?0:1);return 0;
            }
            app->tick();
            if (wParam == 2) { KillTimer(hwnd, 2); app->snapshot(); DestroyWindow(hwnd); }
            return 0;
        case WM_RECORD_STATUS: {
            std::unique_ptr<RecordingResult> result(reinterpret_cast<RecordingResult*>(lParam));
            app->recorder.join(); app->finishing = false;
            app->status = result->success ? L"Запись сохранена · " + std::filesystem::path(result->path).filename().wstring() : L"Запись остановлена";
            app->lastFile = result->path;
            if (result->success) app->path.clear();
            app->enabled();
            if (app->closing) DestroyWindow(hwnd);
            else if (!result->success && !result->cancelled) app->notice(result->message + (result->path.empty() ? L"" : L"\n" + result->path));
            return 0;
        }
        case WM_CLOSE:
            app->saveWindowPosition();
            if (app->recorder.busy() || app->finishing) {
                app->closing = true; app->finishing = true; app->status = L"Завершаю запись перед выходом…";
                app->recorder.stop(); app->enabled(); return 0;
            }
            DestroyWindow(hwnd); return 0;
        case WM_DESTROY: KillTimer(hwnd, 1);KillTimer(hwnd,4);KillTimer(hwnd,5); PostQuitMessage(0); return 0;
        }
    } catch (...) {
        app->notice(errorText());
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int result = 1; bool mf{}; ULONG_PTR gdiplus{};Gdiplus::GdiplusStartupInput graphicsStartup;Gdiplus::GdiplusStartup(&gdiplus,&graphicsStartup,nullptr);
    std::wstring diagnostics;
    try {
        int argc{}; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::wstring> args; for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]); LocalFree(argv);
        std::unique_ptr<SingleInstance> instanceGuard;
        bool diagnosticMode=args.size()>1 && (args[1]==L"--ui-test" || args[1]==L"--self-test" || args[1]==L"--model-test" || args[1]==L"--camera-test" || args[1]==L"--overlay-test" || args[1]==L"--live-device-test" || args[1]==L"--dropdown-test" || args[1]==L"--idle-paint-test" || args[1]==L"--snapshot-light" || args[1]==L"--snapshot-dark");
        if(!diagnosticMode){instanceGuard=std::make_unique<SingleInstance>();if(!instanceGuard->acquire()){Gdiplus::GdiplusShutdown(gdiplus);return 0;}}
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        if(!diagnosticMode)check(SetCurrentProcessExplicitAppUserModelID(L"SanderStripa.Screencast"));
        check(MFStartup(MF_VERSION)); mf = true;
        if (args.size() >= 3 && (args[1] == L"--self-test" || args[1] == L"--model-test" || args[1]==L"--camera-test" || args[1]==L"--overlay-test" || args[1]==L"--live-device-test")) {
            std::filesystem::create_directories(args[2]); diagnostics = args[2] + L"\\test-error.txt";
            if (args[1] == L"--self-test") result = encoderSelfTest(args[2]);
            else if(args[1]==L"--camera-test") result=cameraSelfTest(args[2]);
            else if(args[1]==L"--overlay-test") result=overlaySelfTest(args[2]);
            else if(args[1]==L"--live-device-test") result=liveDeviceSelfTest(args[2]);
            else { Camera::testModel(); result = 0; }
            MFShutdown(); winrt::uninit_apartment(); return result;
        }
        App state; app = &state;state.testing=diagnosticMode; state.readTheme();
        if(args.size()>=3&&args[1]==L"--ui-test"){std::filesystem::create_directories(args[2]);diagnostics=args[2]+L"\\ui-error.txt";show=SW_SHOWNOACTIVATE;}
        if(args.size()>=3 && args[1]==L"--idle-paint-test"){state.idleTestPath=args[2];state.theme=2;show=SW_SHOWNOACTIVATE;}
        auto fontPath = executableDir() + L"\\assets\\Montserrat.ttf";
        AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr);
        if(args.size()>=2 && args[1]==L"--dropdown-test") {
            if(args.size()>=3){std::filesystem::create_directories(args[2]);diagnostics=args[2]+L"\\dropdown-error.txt";}
            auto font=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Montserrat");
            auto owner=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,10,10,nullptr,nullptr,instance,nullptr);
            int dispatched{};
            SetWindowSubclass(owner,[](HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT {
                if(m==WM_APP+100){++*reinterpret_cast<int*>(data);return 0;}
                return DefSubclassProc(h,m,w,l);
            },1,reinterpret_cast<DWORD_PTR>(&dispatched));
            std::vector<std::wstring> values;for(int i=0;i<40;++i)values.push_back(L"Источник "+std::to_wstring(i));
            auto run=[&](void (*script)(HWND),int expected) {
                Dropdown list(values,font,96,0,RGB(11,11,12),RGB(240,240,240),RGB(52,54,58),RGB(30,31,33));
                if(list.show(owner,RECT{100,100,400,132},script)!=expected)throw winrt::hresult_error(E_FAIL,L"Dropdown interaction failed");
            };
            run([](HWND h){PostMessageW(GetWindow(h,GW_OWNER),WM_APP+100,0,0);PostMessageW(h,WM_KEYDOWN,VK_DOWN,0);PostMessageW(h,WM_KEYDOWN,VK_RETURN,0);},2);
            if(dispatched!=1)throw winrt::hresult_error(E_FAIL,L"Dropdown blocked owner messages");
            run([](HWND h){PostMessageW(h,WM_KEYDOWN,VK_END,0);PostMessageW(h,WM_KEYDOWN,VK_RETURN,0);},40);
            run([](HWND h){PostMessageW(h,WM_KEYDOWN,VK_ESCAPE,0);},0);
            run([](HWND h){PostMessageW(h,WM_LBUTTONUP,0,MAKELPARAM(50,57));},2);
            run([](HWND h){PostMessageW(h,WM_LBUTTONDOWN,0,MAKELPARAM(-10,-10));},0);
            run([](HWND h){PostMessageW(h,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);PostMessageW(h,WM_KEYDOWN,VK_RETURN,0);},4);
            DestroyWindow(owner);DeleteObject(font);RemoveFontResourceExW(fontPath.c_str(),FR_PRIVATE,nullptr);
            MFShutdown();winrt::uninit_apartment();Gdiplus::GdiplusShutdown(gdiplus);return 0;
        }
        if (args.size() >= 3 && (args[1] == L"--snapshot-light" || args[1] == L"--snapshot-dark")) {
            state.theme = args[1] == L"--snapshot-dark" ? 2 : 1; state.snapshotPath = args[2];
            state.mouseFocusSnapshot=args.size()>=4 && args[3]==L"mouse-focus";
            state.dropdownSnapshot=args.size()>=4 && args[3]==L"dropdown";
            state.settingsSnapshot=args.size()>=4 && (args[3]==L"settings"||args[3]==L"settings-focus");
            state.keyboardFocusSnapshot=args.size()>=4 && (args[3]==L"keyboard-focus"||args[3]==L"settings-focus");
            state.windowHoverSnapshot=args.size()>=4&&(args[3]==L"window-hover"||args[3]==L"window-hover-start");
            state.windowHoverStartSnapshot=args.size()>=4&&args[3]==L"window-hover-start";
            state.english=args.size()>=5 && args[4]==L"en";
        }
        WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = procedure; cls.hInstance = instance;
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hIconSm=cls.hIcon;
        cls.lpszClassName = L"ScreencastWindow"; RegisterClassExW(&cls);
        auto hwnd = CreateWindowExW(0, cls.lpszClassName, L"Screencast",
            WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1040, 700, nullptr, nullptr, instance, nullptr);
        if (!hwnd) check(HRESULT_FROM_WIN32(GetLastError()));
        if(!diagnosticMode)windowPosition::restore(hwnd,state.preferencesKey.c_str());
        state.positionReady=true;
        if(state.settingsSnapshot){state.settingsOpen=true;state.showPage();}
        SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(state.windowIcon(ICON_BIG,0)));
        SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(state.windowIcon(ICON_SMALL2,0)));
        ShowWindow(hwnd, show); UpdateWindow(hwnd);
        if(args.size()>=3&&args[1]==L"--ui-test"){
            if(args.size()>=5){
                if(state.theme!=std::stoi(args[3]) || state.english!=(args[4]==L"1"))
                    throw std::runtime_error("Installed application did not load installer theme/language");
                std::ofstream initial(std::filesystem::path(args[2])/L"startup-preferences.txt");
                initial<<"PASS: loaded installer Theme="<<state.theme<<", English="<<state.english<<" before application UI tests.\n";
            }
            state.uiTest(args[2]);DestroyWindow(hwnd);
        }
        if(instanceGuard)instanceGuard->markReady();
        if (!state.snapshotPath.empty()) SetTimer(hwnd, 2, 1000, nullptr);
        if(!state.idleTestPath.empty())SetTimer(hwnd,3,1000,nullptr);
        MSG message{}; int value{};
        while ((value = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(hwnd, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        state.recorder.stop(); state.recorder.join();
        RemoveFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr);
        result = value < 0 ? 1 : static_cast<int>(message.wParam);
    } catch (...) {
        auto message = errorText();
        if (!diagnostics.empty()) { std::ofstream file{std::filesystem::path(diagnostics)}; file << winrt::to_string(winrt::hstring(message)); }
        else MessageBoxW(nullptr, message.c_str(), L"Screencast", MB_OK | MB_ICONERROR);
    }
    if (mf) MFShutdown(); winrt::uninit_apartment();Gdiplus::GdiplusShutdown(gdiplus); return result;
}
