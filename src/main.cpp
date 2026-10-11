#include "common.h"
#include "capture.h"
#include "audio.h"
#include "camera.h"
#include "recorder.h"
#include <dwmapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>
#include <map>
#include <fstream>
#include <commctrl.h>
#include <windowsx.h>
#include "overlay.h"
#include "switch.h"
#include "paint.h"

int encoderSelfTest(std::wstring const& directory);
int cameraSelfTest(std::wstring const& directory);
int overlaySelfTest(std::wstring const& directory);
int liveDeviceSelfTest(std::wstring const& directory);
namespace {
enum Id { SourceKind = 100, Source, Refresh, OutputMode, OutputDevice, Microphone, MicDevice,
    Denoise, Webcam, CameraDevice, Corner, Background, BackgroundFile, Save, Start, ThemeSystem, ThemeLight, ThemeDark,
    SystemVolume,MicVolume,NoiseStrength,BlurStrength,BackgroundPreset,CameraMirror };
enum class Kind { Button, Choice, Toggle, Segment, Slider };
struct Control {
    HWND window{}; Kind kind{}; std::wstring label;
    std::vector<std::wstring> items; int selected{}; bool checked{}; int maximum{100};
};
struct Palette { COLORREF background, text, muted, line, hover, active, buttonText; };
class App {
public:
    HWND window{};
    int dpi{96}, theme{}, scroll{};
    bool dark{}, closing{}, finishing{};
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
        if(recorder.busy()) {
            RECT preview{px(24),px(244),px(310),px(510)};InvalidateRect(window,&preview,FALSE);
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
    bool mouseFocusSnapshot{};
    std::wstring idleTestPath;int idleTestPhase{},idlePaints{};
    Palette palette{};
    std::shared_ptr<LiveSettings> live=std::make_shared<LiveSettings>();
    bool dragging{}; Point dragOffset{};
    RECT previewRect{};
    std::shared_ptr<Image const> displayed;
    static LRESULT CALLBACK controlProcedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
        if(message==WM_ERASEBKGND)return 1;
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
        palette = dark ? Palette{RGB(11,11,12), RGB(237,237,235), RGB(146,150,158), RGB(49,52,57), RGB(23,24,27), RGB(237,237,235), RGB(11,11,12)} :
            Palette{RGB(247,247,245), RGB(16,24,36), RGB(119,126,137), RGB(218,221,221), RGB(237,238,236), RGB(16,24,36), RGB(247,247,245)};
        if (brush) DeleteObject(brush); brush = CreateSolidBrush(palette.background);
        BOOL useDark = dark; DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
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
        heading = make(12, FW_MEDIUM); title = make(23, FW_MEDIUM);
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
        else if (c.kind == Kind::Toggle) text += c.checked ? L": включено" : L": выключено";
        else if(c.kind==Kind::Slider) text+=L" · "+std::to_wstring(c.selected)+L"%";
        SetWindowTextW(c.window, text.c_str()); InvalidateRect(c.window, nullptr, TRUE);
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
    }
    void create() {
        dpi = GetDpiForWindow(window); fonts();
        add(ThemeSystem,Kind::Segment,L"Система",610,30,112,30);
        add(ThemeLight,Kind::Segment,L"Светлая",730,30,112,30);
        add(ThemeDark,Kind::Segment,L"Тёмная",850,30,126,30);
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
        add(Save,Kind::Button,L"Куда сохранить MP4…",24,580,250,38);
        add(Start,Kind::Button,L"Начать запись",726,580,250,38);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
        dpi=std::min<int>(dpi,std::min<long>((monitor.rcWork.right-monitor.rcWork.left-20)*96/1000,(monitor.rcWork.bottom-monitor.rcWork.top-50)*96/640));
        fonts();
        // Rescale children if a small desktop cannot fit the preferred system scale.
        int originalDpi=GetDpiForWindow(window);
        if(dpi!=originalDpi) for(auto const& [id,c]:controls) {RECT r{};GetWindowRect(c.window,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);SetWindowPos(c.window,nullptr,MulDiv(r.left,dpi,originalDpi),MulDiv(r.top,dpi,originalDpi),MulDiv(r.right-r.left,dpi,originalDpi),MulDiv(r.bottom-r.top,dpi,originalDpi),SWP_NOZORDER);}
        RECT client{0,0,px(1000),px(640)};AdjustWindowRectExForDpi(&client,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0,GetDpiForWindow(window));
        SetWindowPos(window,nullptr,0,0,client.right-client.left,client.bottom-client.top,SWP_NOMOVE|SWP_NOZORDER);
        scrollRange();
        refresh(); applyTheme(); SetTimer(window, 1, 33, nullptr);
    }
    void text(HDC dc, std::wstring const& value, int x, int y, int w, int h, HFONT face, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
        RECT r{px(x),px(y)-scroll,px(x+w),px(y+h)-scroll};
        SelectObject(dc, face); SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, value.c_str(), -1, &r, flags);
    }
    void spaced(HDC dc, std::wstring const& value, int y, HFONT face, int spacing) {
        SelectObject(dc, face); SetTextColor(dc, palette.text); SetBkMode(dc, TRANSPARENT);
        int x = px(76);
        for (wchar_t c : value) { SIZE size{}; GetTextExtentPoint32W(dc, &c, 1, &size); TextOutW(dc, x, px(y)-scroll, &c, 1); x += size.cx + px(spacing); }
    }
    void line(HDC dc, int y) {
        auto pen = CreatePen(PS_SOLID, 1, palette.line); auto old = SelectObject(dc, pen);
        MoveToEx(dc, px(24), px(y)-scroll, nullptr); LineTo(dc, px(976), px(y)-scroll); SelectObject(dc, old); DeleteObject(pen);
    }
    void paint(HDC dc) {
        RECT r{}; GetClientRect(window, &r); FillRect(dc, &r, brush);
        spaced(dc, L"SCREENCAST", 24, title, 2);
        auto icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,px(40),px(40),0));
        DrawIconEx(dc,px(24),px(22),icon,px(40),px(40),0,nullptr,DI_NORMAL);DestroyIcon(icon);
        text(dc, L"Запись экрана · MP4", 76, 54, 480, 20, smallFont, palette.muted);
        line(dc,86);
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
            label+=recorder.hardware()?L" · аппаратное кодирование":L" · программное кодирование";
        }
        text(dc, label, 24, 546, 952, 24, smallFont, palette.muted);
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
        if(control.kind==Kind::Slider) {
            SelectObject(dc,smallFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
            auto label=control.label+L" · "+std::to_wstring(control.selected)+L"%";
            RECT labelRect{px(14),0,r.right-px(14),px(24)};DrawTextW(dc,label.c_str(),-1,&labelRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            int left=px(14),right=r.right-px(14),y=px(38),x=left+MulDiv(right-left,control.selected,control.maximum);
            auto pen=CreatePen(PS_SOLID,px(2),palette.line);auto before=SelectObject(dc,pen);
            MoveToEx(dc,left,y,nullptr);LineTo(dc,right,y);SelectObject(dc,before);DeleteObject(pen);
            pen=CreatePen(PS_SOLID,px(3),color);before=SelectObject(dc,pen);MoveToEx(dc,left,y,nullptr);LineTo(dc,x,y);
            auto fill=CreateSolidBrush(color);auto old=SelectObject(dc,fill);Ellipse(dc,x-px(5),y-px(5),x+px(5),y+px(5));
            SelectObject(dc,old);SelectObject(dc,before);DeleteObject(fill);DeleteObject(pen);return;
        }
        auto label = control.label;
        bool selectedTheme = item.CtlID >= ThemeSystem && static_cast<int>(item.CtlID) - ThemeSystem == theme;
        if (control.kind == Kind::Choice) label = control.items.empty() ? L"Нет доступных устройств" : control.items[control.selected];
        bool primary = item.CtlID == Start;
        auto pen = CreatePen(PS_SOLID, 1, palette.line);
        auto fill = CreateSolidBrush(primary && !disabled ? palette.active : pressed ? palette.hover : palette.background);
        auto oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, fill);
        if (control.kind != Kind::Toggle && (control.kind != Kind::Segment || selectedTheme))
            RoundRect(dc, r.left, r.top, r.right - 1, r.bottom - 1, px(20), px(20));
        SelectObject(dc, font); SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, primary && !disabled ? palette.buttonText : color);
        auto labelRect = r; labelRect.left += px(control.kind == Kind::Toggle ? 0 : 14); labelRect.right -= px(14);
        if (control.kind == Kind::Toggle) labelRect.right -= px(60);
        if (control.kind == Kind::Choice) labelRect.right -= px(20);
        DrawTextW(dc, label.c_str(), -1, &labelRect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
            ((control.kind == Kind::Button || control.kind == Kind::Segment) ? DT_CENTER : DT_LEFT));
        if (control.kind == Kind::Choice) {
            int x = r.right - px(20), y = (r.top+r.bottom)/2;
            MoveToEx(dc, x-px(4), y-px(2), nullptr); LineTo(dc, x, y+px(2)); LineTo(dc, x+px(4), y-px(2));
        }
        if(control.kind==Kind::Toggle) drawSwitch(dc,r.right-px(38),(r.bottom-r.top-px(20))/2,px(38),px(20),control.checked,dark);
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(fill); DeleteObject(pen);
    }
    std::wstring choose(bool save, wchar_t const* filter) {
        wchar_t buffer[32768]{};
        if (save) {
            SYSTEMTIME time{}; GetLocalTime(&time);
            swprintf_s(buffer, L"Screencast-%04u-%02u-%02u-%02u-%02u-%02u.mp4", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
        }
        OPENFILENAMEW file{}; file.lStructSize = sizeof(file); file.hwndOwner = window;
        file.lpstrFilter = filter; file.lpstrFile = buffer; file.nMaxFile = 32768;
        file.lpstrDefExt = save ? L"mp4" : nullptr;
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
        if (id >= ThemeSystem && id <= ThemeDark) {
            theme = id - ThemeSystem; applyTheme(); saveTheme(); return;
        }
        auto& control = controls.at(id);
        if (control.kind == Kind::Choice) {
            if (control.items.empty()) return;
            HMENU menu = CreatePopupMenu();
            for (size_t i = 0; i < control.items.size(); ++i) AppendMenuW(menu, MF_STRING |
                (static_cast<int>(i) == control.selected ? MF_CHECKED : 0), i+1, control.items[i].c_str());
            RECT r{}; GetWindowRect(control.window, &r);
            auto picked = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, r.left, r.bottom, 0, window, nullptr);
            DestroyMenu(menu);
            if (picked) {
                if(id==Background && picked==6 && recorder.busy() && backgroundPath.empty()) {
                    MessageBoxW(window,L"Сначала выберите своё изображение до начала записи.",L"Screencast",MB_OK);return;
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
        HKEY key{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Screencast", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
            DWORD value = theme; RegSetValueExW(key, L"Theme", 0, REG_DWORD, reinterpret_cast<BYTE*>(&value), sizeof(value)); RegCloseKey(key);
        }
    }
    void readTheme() {
        DWORD value{}, size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Screencast", L"Theme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value <= 2) theme = value;
    }
    void snapshot() {
        RECT r{}; GetClientRect(window, &r); int w = r.right, h = r.bottom;
        HDC dc = GetDC(window), mem = CreateCompatibleDC(dc);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* bits{}; HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        auto old = SelectObject(mem, bitmap);
        // Use the exact production paint routines even on a hidden CI window.
        // PrintWindow returns black for hidden windows on some Windows runners.
        paint(mem);
        for (auto const& [id, c] : controls) {
            RECT rect{}; GetWindowRect(c.window, &rect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
            auto saved = SaveDC(mem); SetViewportOrgEx(mem, rect.left, rect.top, nullptr);
            IntersectClipRect(mem, 0, 0, rect.right-rect.left, rect.bottom-rect.top);
            DRAWITEMSTRUCT item{};
            item.CtlID = id; item.hwndItem = c.window; item.hDC = mem; item.itemAction = ODA_DRAWENTIRE;
            item.itemState = IsWindowEnabled(c.window) ? 0 : ODS_DISABLED;
            if(mouseFocusSnapshot)item.itemState|=ODS_FOCUS;
            item.rcItem = {0,0,rect.right-rect.left,rect.bottom-rect.top};
            draw(item); RestoreDC(mem, saved);
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
    ~App() { if(modelDownload)CloseHandle(modelDownload);for (auto f : {font, smallFont, heading, title}) if (f) DeleteObject(f); if (brush) DeleteObject(brush); }
};
App* app{};
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        switch (message) {
        case WM_CREATE: app->window = hwnd; app->create(); return 0;
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
            else if (!result->success && !result->cancelled) MessageBoxW(hwnd, (result->message + (result->path.empty() ? L"" : L"\n" + result->path)).c_str(), L"Screencast", MB_OK | MB_ICONERROR);
            return 0;
        }
        case WM_CLOSE:
            if (app->recorder.busy() || app->finishing) {
                app->closing = true; app->finishing = true; app->status = L"Завершаю запись перед выходом…";
                app->recorder.stop(); app->enabled(); return 0;
            }
            DestroyWindow(hwnd); return 0;
        case WM_DESTROY: KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
        }
    } catch (...) {
        MessageBoxW(hwnd, errorText().c_str(), L"Screencast", MB_OK | MB_ICONERROR);
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int result = 1; bool mf{}; ULONG_PTR gdiplus{};Gdiplus::GdiplusStartupInput graphicsStartup;Gdiplus::GdiplusStartup(&gdiplus,&graphicsStartup,nullptr);
    std::wstring diagnostics;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        check(MFStartup(MF_VERSION)); mf = true;
        int argc{}; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::wstring> args; for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]); LocalFree(argv);
        if (args.size() >= 3 && (args[1] == L"--self-test" || args[1] == L"--model-test" || args[1]==L"--camera-test" || args[1]==L"--overlay-test" || args[1]==L"--live-device-test")) {
            std::filesystem::create_directories(args[2]); diagnostics = args[2] + L"\\test-error.txt";
            if (args[1] == L"--self-test") result = encoderSelfTest(args[2]);
            else if(args[1]==L"--camera-test") result=cameraSelfTest(args[2]);
            else if(args[1]==L"--overlay-test") result=overlaySelfTest(args[2]);
            else if(args[1]==L"--live-device-test") result=liveDeviceSelfTest(args[2]);
            else { Camera::testModel(); result = 0; }
            MFShutdown(); winrt::uninit_apartment(); return result;
        }
        App state; app = &state; state.readTheme();
        if(args.size()>=3 && args[1]==L"--idle-paint-test"){state.idleTestPath=args[2];state.theme=2;show=SW_SHOWNOACTIVATE;}
        auto fontPath = executableDir() + L"\\assets\\Montserrat.ttf";
        AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr);
        if (args.size() >= 3 && (args[1] == L"--snapshot-light" || args[1] == L"--snapshot-dark")) {
            state.theme = args[1] == L"--snapshot-dark" ? 2 : 1; state.snapshotPath = args[2];
            state.mouseFocusSnapshot=args.size()>=4 && args[3]==L"mouse-focus";
        }
        WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = procedure; cls.hInstance = instance;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,16,16,0));
        cls.lpszClassName = L"ScreencastWindow"; RegisterClassExW(&cls);
        auto hwnd = CreateWindowExW(0, cls.lpszClassName, L"Screencast",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1040, 700, nullptr, nullptr, instance, nullptr);
        if (!hwnd) check(HRESULT_FROM_WIN32(GetLastError()));
        ShowWindow(hwnd, show); UpdateWindow(hwnd);
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
