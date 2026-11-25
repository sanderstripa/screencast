#pragma once
#include "capture_exclusion.h"
#include "paint.h"
#include "recorder.h"
#include "ui_palette.h"
#include <commctrl.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <array>

enum class PanelEdge { Left, Right, Top };
enum class PanelAction { Pause = 1, Sound, Home, Stop };
constexpr UINT WM_RECORD_PANEL = WM_APP + 24;
constexpr UINT WM_RECORD_EDGE = WM_APP + 25;

class RecordingPanel {
public:
    ~RecordingPanel() { close(); }

    bool open(HWND owner, HMONITOR monitor, PanelEdge edge, Palette, bool english,
        Recorder& recorder, LiveSettings& live, bool soundAvailable, bool firstStart) {
        close();
        owner_ = owner;
        recorder_ = &recorder;
        live_ = &live;
        soundAvailable_ = soundAvailable;
        monitor_ = monitor ? monitor : MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
        edge_ = edge;
        english_ = english;
        tipVisible_ = false;
        dpi_ = GetDpiForWindow(owner);
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(monitor_, &info)) return false;
        screen_ = info.rcMonitor;
        WNDCLASSW cls{};
        cls.style = CS_DROPSHADOW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = procedure;
        cls.lpszClassName = L"ScreencastRecordingPanel";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&cls);
        auto visible = dock(false);
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
            cls.lpszClassName, L"Screencast", WS_POPUP, visible.left, visible.top,
            visible.right-visible.left, visible.bottom-visible.top, nullptr, nullptr, cls.hInstance, this);
        if (!window_ || !excludeFromCapture(window_)) { close(); return false; }
        if (!SetTimer(window_, 1, 15, nullptr)) { close(); return false; }
        dpi_ = GetDpiForWindow(window_);
        visible = dock(false);
        position_ = start_ = fromMain(visible);
        target_ = visible;
        began_ = GetTickCount64();
        travelling_ = true;
        returning_ = false;
        hot_ = -1;
        pressed_ = -1;
        dragging_ = false;
        firstStart_ = firstStart;
        hidden_ = false;
        holdUntil_ = began_ + 230;
        elapsed_ = -1;
        font_ = CreateFontW(-px(21), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&common);
        tooltip_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, cls.hInstance, nullptr);
        if (tooltip_ && !excludeFromCapture(tooltip_)) { DestroyWindow(tooltip_); tooltip_ = nullptr; }
        if (tooltip_) {
            TOOLINFOW tool{sizeof(tool)};
            tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            tool.hwnd = window_;
            tool.uId = 1;
            tool.lpszText = tooltipText_.data();
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        place(position_, true);
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        return true;
    }

    void close() {
        if (tooltip_) { DestroyWindow(tooltip_); tooltip_ = nullptr; }
        if (window_) { DestroyWindow(window_); window_ = nullptr; }
        if (font_) { DeleteObject(font_); font_ = nullptr; }
    }

    bool settledHidden() const { return window_ && hidden_ && EqualRect(&position_, &target_); }
    bool open() const { return window_ != nullptr; }

    void travelToMain() {
        if (!window_ || returning_) return;
        tooltip(false);
        start_ = position_;
        target_ = fromMain(dock(false));
        began_ = GetTickCount64();
        travelling_ = returning_ = true;
    }
    bool reachedMain() const { return window_ && returning_ && !travelling_; }

private:
    HWND window_{}, owner_{}, tooltip_{};
    HMONITOR monitor_{};
    Recorder* recorder_{};
    LiveSettings* live_{};
    HFONT font_{};
    PanelEdge edge_{PanelEdge::Right};
    RECT screen_{}, position_{}, start_{}, target_{};
    POINT mouseDown_{};
    RECT dragStart_{};
    int dpi_{96}, hot_{-1}, pressed_{-1};
    int64_t elapsed_{-1};
    ULONGLONG began_{}, holdUntil_{}, hoverSince_{};
    bool english_{}, hidden_{}, dragging_{}, firstStart_{}, soundAvailable_{}, tipVisible_{}, travelling_{}, returning_{};
    std::wstring tooltipText_;

    static constexpr int verticalWidth = 126, verticalHeight = 334;
    static constexpr int horizontalWidth = 396, horizontalHeight = 81;

    int px(int value) const { return MulDiv(value, dpi_, 96); }
    RECT dock(bool hidden) const {
        int width = px(edge_ == PanelEdge::Top ? horizontalWidth : verticalWidth);
        int height = px(edge_ == PanelEdge::Top ? horizontalHeight : verticalHeight);
        int x = screen_.left + (screen_.right - screen_.left - width) / 2;
        int y = screen_.top + (screen_.bottom - screen_.top - height) / 2;
        if (edge_ == PanelEdge::Left) x = screen_.left + (hidden ? -width : px(6));
        if (edge_ == PanelEdge::Right) x = screen_.right - (hidden ? 0 : width + px(6));
        if (edge_ == PanelEdge::Top) y = screen_.top + (hidden ? -height : px(6));
        return {x, y, x + width, y + height};
    }

    RECT fromMain(RECT size) const {
        RECT main{};
        GetWindowRect(owner_, &main);
        auto width = size.right-size.left;
        auto height = size.bottom-size.top;
        auto x = std::clamp((main.left+main.right-width)/2,screen_.left,std::max(screen_.left,screen_.right-width));
        auto y = std::clamp((main.top+main.bottom-height)/2,screen_.top,std::max(screen_.top,screen_.bottom-height));
        return {x,y,x+width,y+height};
    }

    RECT revealArea() const {
        RECT r = dock(false);
        if (edge_ == PanelEdge::Top) { r.top = screen_.top; r.bottom = r.top + px(8); }
        if (edge_ == PanelEdge::Left) { r.left = screen_.left; r.right = r.left + px(8); }
        if (edge_ == PanelEdge::Right) { r.right = screen_.right; r.left = r.right - px(8); }
        return r;
    }

    void place(RECT r, bool force = false) {
        if (!force && EqualRect(&position_, &r)) return;
        bool resized = r.right-r.left != position_.right-position_.left ||
            r.bottom-r.top != position_.bottom-position_.top;
        position_ = r;
        int width = r.right-r.left, height = r.bottom-r.top;
        HRGN shape = CreateRoundRectRgn(0, 0, width, height, px(54), px(54));
        HRGN clip = CreateRectRgn(screen_.left-r.left, screen_.top-r.top, screen_.right-r.left, screen_.bottom-r.top);
        CombineRgn(shape, shape, clip, RGN_AND);
        DeleteObject(clip);
        if (!SetWindowRgn(window_, shape, FALSE)) DeleteObject(shape);
        UINT flags = SWP_NOACTIVATE | (resized ? SWP_NOCOPYBITS : 0);
        SetWindowPos(window_, HWND_TOPMOST, r.left, r.top, width, height, flags);
        if (resized) InvalidateRect(window_, nullptr, FALSE);
    }

    void aim(bool hidden) {
        hidden_ = hidden;
        start_ = position_;
        target_ = dock(hidden);
        began_ = GetTickCount64();
    }

    static LONG interpolate(LONG a, LONG b, double progress) {
        return static_cast<LONG>(std::lround(a + (b-a)*progress));
    }

    void tick() {
        auto now = GetTickCount64();
        POINT cursor{};
        GetCursorPos(&cursor);
        if (pressed_ >= 0) {
            // A non-activating window gets mouse capture only inside its visible
            // bounds. Poll while pressed so dragging can cross the desktop.
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) moveDrag(cursor);
            else finishPress(cursor);
        }
        auto edgeArea = revealArea();
        POINT local{cursor.x-position_.left, cursor.y-position_.top};
        bool over = PtInRect(&position_, cursor) && PtInRect(&screen_, cursor);
        RECT tipBounds{};
        bool overTip = tipVisible_ && GetWindowRect(tooltip_, &tipBounds) && PtInRect(&tipBounds, cursor);
        int nextHot = overTip ? hot_ : over && !dragging_ && !travelling_ && !returning_ &&
            recorder_->recording() ? hit(local) : -1;
        if (nextHot != hot_) {
            tooltip(false);
            hot_ = nextHot;
            hoverSince_ = now;
            InvalidateRect(window_, nullptr, FALSE);
        } else if (hot_ > 0 && !tipVisible_ && now-hoverSince_ > 600) tooltip(true);

        if (!dragging_ && pressed_ < 0) {
            if (travelling_ && EqualRect(&position_, &target_)) travelling_ = false;
            if (travelling_ || returning_) {
                // Keep a single target until the handoff between the two windows.
            } else if (firstStart_) {
                if (now >= holdUntil_ && !hidden_) aim(true);
                if (recorder_->recording()) {
                    firstStart_ = false;
                    holdUntil_ = 0;
                }
            } else {
                bool reveal = over || overTip || PtInRect(&edgeArea, cursor) || now < holdUntil_;
                if (hidden_ == reveal) aim(!reveal);
            }
            if (!EqualRect(&position_, &target_)) {
                BOOL animations=TRUE;
                SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
                double t = animations ? std::min(1., (now-began_)/180.) : 1.;
                t = 1.-(1.-t)*(1.-t)*(1.-t);
                place({interpolate(start_.left,target_.left,t),interpolate(start_.top,target_.top,t),
                    interpolate(start_.right,target_.right,t),interpolate(start_.bottom,target_.bottom,t)});
            }
        }
        auto elapsed = recorder_->elapsed();
        if (elapsed != elapsed_) {
            elapsed_ = elapsed;
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    double horizontalProgress() const {
        return std::clamp(double(position_.right-position_.left-px(verticalWidth)) /
            (px(horizontalWidth)-px(verticalWidth)), 0., 1.);
    }

    POINT center(int index) const {
        // Index zero is the timer. The remaining centers follow PanelAction.
        constexpr std::array<int, 5> horizontalX{78, 167, 233, 293, 355};
        constexpr std::array<int, 5> verticalY{47, 106, 172, 232, 293};
        auto progress = horizontalProgress();
        return {interpolate(px(63), px(horizontalX[index]), progress),
            interpolate(px(verticalY[index]), px(40), progress)};
    }

    int actionRadius(int index) const { return px(index == 4 ? 23 : 18); }

    int hit(POINT p) const {
        for (int i=1; i<=4; ++i) {
            auto c = center(i);
            auto dx = p.x-c.x, dy = p.y-c.y;
            auto radius = actionRadius(i);
            if (dx*dx+dy*dy <= radius*radius) return i;
        }
        return 0;
    }

    wchar_t const* hint() const {
        switch (static_cast<PanelAction>(hot_)) {
        case PanelAction::Pause:
            return recorder_->paused() ? (english_?L"Resume":L"Продолжить") : (english_?L"Pause":L"Пауза");
        case PanelAction::Sound:
            if (!soundAvailable_) return english_?L"Select an audio source before recording":L"Выберите источник звука до записи";
            return live_->systemMuted ? (english_?L"Unmute audio":L"Включить звук") : (english_?L"Mute audio":L"Выключить звук");
        case PanelAction::Home: return english_?L"Main window":L"Домой";
        default: return english_?L"Stop recording":L"Остановить запись";
        }
    }

    void tooltip(bool show) {
        if (!tooltip_) return;
        TOOLINFOW tool{sizeof(tool)};
        tool.hwnd = window_;
        tool.uId = 1;
        if (show) {
            tooltipText_ = hint();
            tool.lpszText = tooltipText_.data();
            SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
            auto c = center(hot_);
            SendMessageW(tooltip_, TTM_TRACKPOSITION, 0,
                MAKELPARAM(position_.left+c.x, position_.top+c.y+actionRadius(hot_)));
        }
        SendMessageW(tooltip_, TTM_TRACKACTIVATE, show, reinterpret_cast<LPARAM>(&tool));
        tipVisible_ = show;
    }

    void paint(HDC dc, RECT r) const {
        using namespace Gdiplus;
        const Color ink(255, 237, 237, 237);
        const Color red(255, 210, 63, 72);
        auto background = CreateSolidBrush(RGB(15, 15, 15));
        FillRect(dc, &r, background);
        DeleteObject(background);

        for (int i=1; i<=4; ++i) {
            auto c = center(i);
            Graphics g(dc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.TranslateTransform(static_cast<REAL>(c.x), static_cast<REAL>(c.y));
            const auto scale = static_cast<REAL>(dpi_)/96.f;
            g.ScaleTransform(scale, scale);

            const bool highlighted = hot_ == i || pressed_ == i;
            if (i != 4 && highlighted) {
                SolidBrush hover(Color(255, 42, 42, 42));
                g.FillEllipse(&hover, -18, -18, 36, 36);
            }
            const auto iconColor = i == 2 && !soundAvailable_ ? Color(255, 139, 139, 139) : ink;
            SolidBrush fill(iconColor);
            Pen stroke(iconColor, 2.4f);
            stroke.SetStartCap(LineCapRound);
            stroke.SetEndCap(LineCapRound);
            stroke.SetLineJoin(LineJoinRound);

            switch (static_cast<PanelAction>(i)) {
            case PanelAction::Pause: {
                g.DrawEllipse(&stroke, -11.25f, -11.25f, 22.5f, 22.5f);
                if (recorder_->paused()) {
                    const PointF play[]{{-2.5f, -4.5f}, {4.5f, 0.f}, {-2.5f, 4.5f}};
                    g.FillPolygon(&fill, play, 3);
                } else {
                    g.DrawLine(&stroke, -2.4f, -3.f, -2.4f, 3.f);
                    g.DrawLine(&stroke, 2.4f, -3.f, 2.4f, 3.f);
                }
                break;
            }
            case PanelAction::Sound: {
                GraphicsPath speaker;
                speaker.StartFigure();
                speaker.AddBezier(-10.5f, -3.5f, -10.5f, -6.f, -9.f, -6.f, -6.5f, -6.f);
                speaker.AddLine(-6.5f, -6.f, -1.5f, -10.5f);
                speaker.AddBezier(-1.5f, -10.5f, 0.f, -12.f, 2.f, -11.f, 2.f, -9.f);
                speaker.AddLine(2.f, -9.f, 2.f, 9.f);
                speaker.AddBezier(2.f, 9.f, 2.f, 11.f, 0.f, 12.f, -1.5f, 10.5f);
                speaker.AddLine(-1.5f, 10.5f, -6.5f, 6.f);
                speaker.AddBezier(-6.5f, 6.f, -9.f, 6.f, -10.5f, 6.f, -10.5f, 3.5f);
                speaker.CloseFigure();
                g.DrawPath(&stroke, &speaker);
                if (live_->systemMuted || !soundAvailable_) {
                    g.DrawLine(&stroke, 6.f, -2.5f, 11.f, 2.5f);
                    g.DrawLine(&stroke, 11.f, -2.5f, 6.f, 2.5f);
                } else {
                    g.DrawBezier(&stroke, 6.5f, -4.f, 8.f, -2.f, 8.f, 2.f, 6.5f, 4.f);
                    g.DrawBezier(&stroke, 11.f, -6.f, 13.f, -3.f, 13.f, 3.f, 11.f, 6.f);
                }
                break;
            }
            case PanelAction::Home: {
                const PointF house[]{{-11.f, 10.5f}, {-11.f, -2.5f}, {0.f, -12.f},
                    {11.f, -2.5f}, {11.f, 10.5f}};
                g.DrawPolygon(&stroke, house, 5);
                const PointF door[]{{-3.5f, 10.5f}, {-3.5f, .5f}, {3.5f, .5f}, {3.5f, 10.5f}};
                g.DrawLines(&stroke, door, 4);
                break;
            }
            case PanelAction::Stop: {
                SolidBrush ring(highlighted ? Color(255, 255, 255, 255) : ink);
                SolidBrush stop(red);
                g.FillEllipse(&ring, -22.5f, -22.5f, 45.f, 45.f);
                g.FillEllipse(&stop, -9.f, -9.f, 18.f, 18.f);
                break;
            }
            }
        }

        const auto progress = horizontalProgress();
        auto timer = center(0);
        wchar_t duration[32]{};
        auto seconds = std::max<int64_t>(0, elapsed_);
        swprintf_s(duration,L"%02lld:%02lld:%02lld",seconds/3600,seconds/60%60,seconds%60);
        auto previous = SelectObject(dc, font_);
        SIZE textSize{};
        GetTextExtentPoint32W(dc, duration, lstrlenW(duration), &textSize);
        RECT text{timer.x-textSize.cx/2, timer.y-px(15), timer.x+(textSize.cx+1)/2, timer.y+px(15)};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(237, 237, 237));
        DrawTextW(dc, duration, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        SelectObject(dc, previous);

        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        SolidBrush dot(recorder_->paused() ? Color(255, 139, 139, 139) : Color(255, 229, 75, 88));
        const auto dotX = interpolate(px(63), text.left-px(12), progress);
        const auto dotY = interpolate(px(22), px(40), progress);
        g.FillEllipse(&dot, static_cast<INT>(dotX-px(4)), static_cast<INT>(dotY-px(4)), px(9), px(9));
    }

    void moveDrag(POINT cursor) {
        if (pressed_ != 0) return;
        auto dx = cursor.x-mouseDown_.x, dy = cursor.y-mouseDown_.y;
        if (!dragging_ && std::abs(dx)+std::abs(dy)<=px(5)) return;
        dragging_ = true;
        auto rect = dragStart_;
        OffsetRect(&rect,dx,dy);
        place(rect);
    }

    void finishPress(POINT cursor) {
        int pressed = pressed_;
        bool dragging = dragging_;
        pressed_ = -1;
        dragging_ = false;
        if (GetCapture()==window_) ReleaseCapture();
        if (dragging) drop(cursor);
        else if (pressed>0 && PtInRect(&position_,cursor) &&
            pressed==hit({cursor.x-position_.left,cursor.y-position_.top})) {
            PostMessageW(owner_,WM_RECORD_PANEL,pressed,0);
            InvalidateRect(window_,nullptr,FALSE);
        }
    }

    void drop(POINT cursor) {
        auto left = std::abs(cursor.x-screen_.left);
        auto right = std::abs(cursor.x-screen_.right);
        auto top = std::abs(cursor.y-screen_.top);
        edge_ = top < std::min(left,right) ? PanelEdge::Top : left < right ? PanelEdge::Left : PanelEdge::Right;
        holdUntil_ = GetTickCount64()+500;
        aim(false);
        PostMessageW(owner_,WM_RECORD_EDGE,static_cast<WPARAM>(edge_),0);
    }

    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        if (message == WM_NCCREATE) {
            auto value = reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams;
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(value));
        }
        auto self = reinterpret_cast<RecordingPanel*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd,message,w,l);
        POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_TIMER: self->tick(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            auto dc = BeginPaint(hwnd,&ps);
            RECT r{};GetClientRect(hwnd,&r);
            bufferedPaint(dc,r,[&](HDC back){self->paint(back,r);});
            EndPaint(hwnd,&ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (!self->recorder_->recording() || self->travelling_ || self->returning_) return 0;
            self->pressed_ = self->hit(point);
            self->tooltip(false);
            self->dragStart_ = self->position_;
            GetCursorPos(&self->mouseDown_);
            SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE: {
            POINT cursor{};GetCursorPos(&cursor);
            self->moveDrag(cursor);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT cursor{};GetCursorPos(&cursor);
            self->finishPress(cursor);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return 0;
            if (self->dragging_) self->aim(false);
            self->dragging_=false;
            self->pressed_=-1;
            return 0;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
};
