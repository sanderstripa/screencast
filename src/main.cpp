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

int encoderSelfTest(std::wstring const& directory);
namespace {
enum Id { SourceKind = 100, Source, Refresh, OutputMode, OutputDevice, Microphone, MicDevice,
    Denoise, Webcam, CameraDevice, Corner, Background, BackgroundFile, Save, Start, ThemeSystem, ThemeLight, ThemeDark };
enum class Kind { Button, Choice, Toggle, Segment };
struct Control {
    HWND window{}; Kind kind{}; std::wstring label;
    std::vector<std::wstring> items; int selected{}; bool checked{};
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
    std::vector<Target> screens, windowList;
    std::vector<Device> microphones, outputs, cameras;
    Recorder recorder;
    std::wstring path, backgroundPath, lastFile, status{L"Готов к записи"};
    std::wstring snapshotPath;
    Palette palette{};

    int px(int value) const { return MulDiv(value, dpi, 96); }
    void scrollTo(int position) {
        RECT r{}; GetClientRect(window, &r);
        position = std::clamp(position, 0, std::max(0, px(848) - r.bottom));
        int delta = scroll - position; scroll = position;
        SetScrollPos(window, SB_VERT, scroll, TRUE);
        ScrollWindowEx(window, 0, delta, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
        UpdateWindow(window);
    }
    void scrollRange() {
        RECT r{}; GetClientRect(window, &r);
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMax = px(848) - 1; info.nPage = r.bottom; info.nPos = scroll;
        SetScrollInfo(window, SB_VERT, &info, TRUE);
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
        auto hwnd = CreateWindowExW(0, L"BUTTON", label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            px(x), px(y), px(w), px(h), window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        controls[id] = {hwnd, kind, label};
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
        auto micId = previousId(MicDevice, microphones), outputId = previousId(OutputDevice, outputs), cameraId = previousId(CameraDevice, cameras);
        screens = monitors(); windowList = windows();
        microphones = audioDevices(true); outputs = audioDevices(false); cameras = cameraDevices();
        auto labels = [](auto const& devices) { std::vector<std::wstring> values; for (auto const& d : devices) values.push_back(d.name); return values; };
        setItems(MicDevice, labels(microphones)); setItems(OutputDevice, labels(outputs)); setItems(CameraDevice, labels(cameras));
        auto restore = [&](int id, auto const& devices, std::wstring const& key) {
            controls[id].selected = 0;
            for (size_t i = 0; i < devices.size(); ++i) if (devices[i].id == key) controls[id].selected = static_cast<int>(i);
            updateText(id);
        };
        restore(MicDevice, microphones, micId); restore(OutputDevice, outputs, outputId); restore(CameraDevice, cameras, cameraId);
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
    void enabled() {
        bool idle = !recorder.busy() && !finishing;
        for (auto& [id, control] : controls) {
            if (id == Start || id >= ThemeSystem) continue;
            bool enable = idle;
            if (id == MicDevice || id == Denoise) enable &= controls[Microphone].checked;
            if (id == OutputDevice) enable &= controls[OutputMode].selected == 1;
            if (id == CameraDevice || id == Corner || id == Background) enable &= controls[Webcam].checked && !cameras.empty();
            if (id == BackgroundFile) enable &= controls[Webcam].checked && controls[Background].checked;
            EnableWindow(control.window, enable);
        }
        EnableWindow(controls[Start].window, !finishing);
        controls[Start].label = recorder.busy() ? L"Остановить запись" : L"Начать запись";
        updateText(Start); InvalidateRect(window, nullptr, FALSE);
    }
    void create() {
        dpi = GetDpiForWindow(window); fonts();
        add(ThemeSystem, Kind::Segment, L"Система", 30, 82, 156, 30);
        add(ThemeLight, Kind::Segment, L"Светлая", 192, 82, 156, 30);
        add(ThemeDark, Kind::Segment, L"Тёмная", 354, 82, 156, 30);
        add(SourceKind, Kind::Choice, L"Источник", 30, 170, 360, 34);
        setItems(SourceKind, {L"Запись экрана", L"Запись отдельного окна"});
        add(Refresh, Kind::Button, L"Обновить", 400, 170, 110, 34);
        add(Source, Kind::Choice, L"Экран / окно", 30, 214, 480, 34);
        add(OutputMode, Kind::Choice, L"Выходной звук", 30, 306, 480, 34);
        setItems(OutputMode, {L"Звук выключен", L"Системный звук", L"Звук приложения выбранного окна"});
        controls[OutputMode].selected = 1; updateText(OutputMode);
        add(OutputDevice, Kind::Choice, L"Устройство системного звука", 30, 350, 480, 34);
        add(Microphone, Kind::Toggle, L"Микрофон", 30, 394, 480, 30); setChecked(Microphone, true);
        add(MicDevice, Kind::Choice, L"Устройство микрофона", 30, 434, 480, 34);
        add(Denoise, Kind::Toggle, L"Программное шумоподавление", 30, 478, 480, 30);
        add(Webcam, Kind::Toggle, L"Круглая веб-камера", 30, 572, 480, 30);
        add(CameraDevice, Kind::Choice, L"Камера", 30, 610, 295, 34);
        add(Corner, Kind::Choice, L"Угол камеры", 335, 610, 175, 34);
        setItems(Corner, {L"Слева сверху", L"Справа сверху", L"Слева снизу", L"Справа снизу"});
        controls[Corner].selected = 3; updateText(Corner);
        add(Background, Kind::Toggle, L"Заменить фон изображением", 30, 654, 480, 30);
        add(BackgroundFile, Kind::Button, L"Выбрать изображение…", 30, 692, 480, 34);
        add(Save, Kind::Button, L"Куда сохранить MP4…", 30, 784, 235, 38);
        add(Start, Kind::Button, L"Начать запись", 275, 784, 235, 38);
        RECT client{0,0,px(540),px(848)};
        MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
        client.bottom = std::min(client.bottom, info.rcWork.bottom - info.rcWork.top - px(60));
        AdjustWindowRectExForDpi(&client, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL, FALSE, 0, dpi);
        SetWindowPos(window, nullptr, 0, 0, client.right-client.left, client.bottom-client.top, SWP_NOMOVE | SWP_NOZORDER);
        scrollRange();
        refresh(); applyTheme(); SetTimer(window, 1, 250, nullptr);
    }
    void text(HDC dc, std::wstring const& value, int x, int y, int w, int h, HFONT face, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
        RECT r{px(x),px(y)-scroll,px(x+w),px(y+h)-scroll};
        SelectObject(dc, face); SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, value.c_str(), -1, &r, flags);
    }
    void spaced(HDC dc, std::wstring const& value, int y, HFONT face, int spacing) {
        SelectObject(dc, face); SetTextColor(dc, palette.text); SetBkMode(dc, TRANSPARENT);
        int x = px(30);
        for (wchar_t c : value) { SIZE size{}; GetTextExtentPoint32W(dc, &c, 1, &size); TextOutW(dc, x, px(y)-scroll, &c, 1); x += size.cx + px(spacing); }
    }
    void line(HDC dc, int y) {
        auto pen = CreatePen(PS_SOLID, 1, palette.line); auto old = SelectObject(dc, pen);
        MoveToEx(dc, px(30), px(y)-scroll, nullptr); LineTo(dc, px(510), px(y)-scroll); SelectObject(dc, old); DeleteObject(pen);
    }
    void paint(HDC dc) {
        RECT r{}; GetClientRect(window, &r); FillRect(dc, &r, brush);
        spaced(dc, L"SCREENCAST", 24, title, 2);
        text(dc, L"Запись экрана · MP4", 30, 54, 480, 20, smallFont, palette.muted);
        line(dc, 128); spaced(dc, L"ИСТОЧНИК ВИДЕО", 144, heading, 1);
        line(dc, 268); spaced(dc, L"ЗВУК", 284, heading, 1);
        text(dc, controls[OutputMode].selected == 2 ? L"Выберите окно: записывается весь процесс и его дочерние процессы." :
            L"Микрофон и выходной звук включаются независимо.", 30, 510, 480, 18, smallFont, palette.muted);
        line(dc, 540); spaced(dc, L"КАМЕРА И ФОН", 550, heading, 1);
        text(dc, L"Для замены фона нужна дополнительная модель (отдельная загрузка).", 30, 730, 480, 18, smallFont, palette.muted);
        line(dc, 756);
        auto label = status;
        if (recorder.recording()) {
            auto t = recorder.elapsed(); wchar_t duration[64]{};
            swprintf_s(duration, L"Запись · %02lld:%02lld:%02lld", t / 3600, t / 60 % 60, t % 60);
            label = duration;
        }
        text(dc, label, 30, 759, 480, 22, smallFont, palette.muted);
    }
    void draw(DRAWITEMSTRUCT const& item) {
        auto found = controls.find(static_cast<int>(item.CtlID)); if (found == controls.end()) return;
        auto& control = found->second; auto dc = item.hDC; auto r = item.rcItem;
        bool disabled = item.itemState & ODS_DISABLED, pressed = item.itemState & ODS_SELECTED;
        COLORREF color = disabled ? palette.muted : palette.text;
        FillRect(dc, &r, brush);
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
        if (control.kind == Kind::Toggle) {
            int x = r.right - px(42), y = (r.bottom-r.top-px(20))/2;
            RoundRect(dc, x, y, r.right-1, y+px(20), px(20), px(20));
            auto knob = CreateSolidBrush(control.checked ? palette.text : palette.muted);
            auto before = SelectObject(dc, knob);
            int kx = control.checked ? r.right-px(18) : x+px(4);
            Ellipse(dc, kx, y+px(4), kx+px(12), y+px(16));
            SelectObject(dc, before); DeleteObject(knob);
        }
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(fill); DeleteObject(pen);
        if (item.itemState & ODS_FOCUS) { auto focus = r; InflateRect(&focus, -px(4), -px(4)); DrawFocusRect(dc, &focus); }
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
            if (!settings.target.window) throw winrt::hresult_error(E_INVALIDARG, L"Для звука приложения выберите запись отдельного окна");
            settings.audioPid = settings.target.pid;
        }
        settings.mic = controls[Microphone].checked; settings.denoise = controls[Denoise].checked;
        settings.micId = microphones.at(controls[MicDevice].selected).id;
        settings.outputId = outputs.at(controls[OutputDevice].selected).id;
        settings.camera = controls[Webcam].checked;
        if (settings.camera) {
            if (cameras.empty()) throw winrt::hresult_error(E_INVALIDARG, L"Камера не подключена");
            settings.cameraId = cameras.at(controls[CameraDevice].selected).id;
        }
        settings.corner = controls[Corner].selected;
        settings.replaceBackground = settings.camera && controls[Background].checked;
        settings.backgroundPath = backgroundPath;
        if (settings.replaceBackground && backgroundPath.empty()) throw winrt::hresult_error(E_INVALIDARG, L"Выберите изображение фона");
        if (path.empty()) path = choose(true, L"Видео MP4\0*.mp4\0\0");
        if (path.empty()) return;
        if (_wcsicmp(std::filesystem::path(path).extension().c_str(), L".mp4")) path += L".mp4";
        settings.path = path;
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
            if (picked) { control.selected = picked - 1; updateText(id); if (id == SourceKind) sourceItems(); }
            enabled();
        } else if (control.kind == Kind::Toggle) { setChecked(id, !control.checked); enabled(); }
        else if (id == Refresh) refresh();
        else if (id == BackgroundFile) {
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
        auto old = SelectObject(mem, bitmap); PrintWindow(window, mem, PW_CLIENTONLY);
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
    ~App() { for (auto f : {font, smallFont, heading, title}) if (f) DeleteObject(f); if (brush) DeleteObject(brush); }
};
App* app{};
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        switch (message) {
        case WM_CREATE: app->window = hwnd; app->create(); return 0;
        case WM_COMMAND: if (HIWORD(wParam) == BN_CLICKED) app->command(LOWORD(wParam)); return 0;
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
        case WM_DRAWITEM: app->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam)); return TRUE;
        case WM_PAINT: { PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps); app->paint(dc); EndPaint(hwnd, &ps); return 0; }
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
            InvalidateRect(hwnd, nullptr, FALSE);
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
    int result = 1; bool mf{};
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        check(MFStartup(MF_VERSION)); mf = true;
        int argc{}; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::wstring> args; for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]); LocalFree(argv);
        if (args.size() >= 3 && args[1] == L"--self-test") {
            result = encoderSelfTest(args[2]); MFShutdown(); winrt::uninit_apartment(); return result;
        }
        App state; app = &state; state.readTheme();
        auto fontPath = executableDir() + L"\\assets\\Montserrat.ttf";
        AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr);
        if (args.size() >= 3 && (args[1] == L"--snapshot-light" || args[1] == L"--snapshot-dark")) {
            state.theme = args[1] == L"--snapshot-dark" ? 2 : 1; state.snapshotPath = args[2];
        }
        WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = procedure; cls.hInstance = instance;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        cls.lpszClassName = L"ScreencastWindow"; RegisterClassExW(&cls);
        auto hwnd = CreateWindowExW(0, cls.lpszClassName, L"Screencast",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL, CW_USEDEFAULT, CW_USEDEFAULT, 560, 900, nullptr, nullptr, instance, nullptr);
        if (!hwnd) check(HRESULT_FROM_WIN32(GetLastError()));
        ShowWindow(hwnd, show); UpdateWindow(hwnd);
        if (!state.snapshotPath.empty()) SetTimer(hwnd, 2, 1000, nullptr);
        MSG message{}; int value{};
        while ((value = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(hwnd, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        state.recorder.stop(); state.recorder.join();
        RemoveFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr);
        result = value < 0 ? 1 : static_cast<int>(message.wParam);
    } catch (...) { MessageBoxW(nullptr, errorText().c_str(), L"Screencast", MB_OK | MB_ICONERROR); }
    if (mf) MFShutdown(); winrt::uninit_apartment(); return result;
}
