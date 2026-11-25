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

    bool open(HWND owner, HMONITOR monitor, PanelEdge edge, Palette palette, bool english,
        Recorder& recorder, LiveSettings& live, bool soundAvailable, bool firstStart) {
        close();
        owner_ = owner;
        recorder_ = &recorder;
        live_ = &live;
        soundAvailable_ = soundAvailable;
        monitor_ = monitor ? monitor : MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
        edge_ = edge;
        palette_ = palette;
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
        position_ = start_ = target_ = visible;
        hot_ = -1;
        pressed_ = -1;
        dragging_ = false;
        firstStart_ = firstStart;
        hidden_ = false;
        holdUntil_ = GetTickCount64() + 220;
        elapsed_ = -1;
        font_ = CreateFontW(-px(11), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Montserrat");
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
        place(visible, true);
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

private:
    HWND window_{}, owner_{}, tooltip_{};
    HMONITOR monitor_{};
    Recorder* recorder_{};
    LiveSettings* live_{};
    HFONT font_{};
    Palette palette_{};
    PanelEdge edge_{PanelEdge::Right};
    RECT screen_{}, position_{}, start_{}, target_{};
    POINT mouseDown_{};
    RECT dragStart_{};
    int dpi_{96}, hot_{-1}, pressed_{-1};
    int64_t elapsed_{-1};
    ULONGLONG began_{}, holdUntil_{}, hoverSince_{};
    bool english_{}, hidden_{}, dragging_{}, firstStart_{}, soundAvailable_{}, tipVisible_{};
    std::wstring tooltipText_;

    int px(int value) const { return MulDiv(value, dpi_, 96); }
    RECT dock(bool hidden) const {
        int width = px(edge_ == PanelEdge::Top ? 250 : 70);
        int height = px(edge_ == PanelEdge::Top ? 46 : 206);
        int x = screen_.left + (screen_.right - screen_.left - width) / 2;
        int y = screen_.top + (screen_.bottom - screen_.top - height) / 2;
        if (edge_ == PanelEdge::Left) x = screen_.left + (hidden ? -width : px(6));
        if (edge_ == PanelEdge::Right) x = screen_.right - (hidden ? 0 : width + px(6));
        if (edge_ == PanelEdge::Top) y = screen_.top + (hidden ? -height : px(6));
        return {x, y, x + width, y + height};
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
        position_ = r;
        int width = r.right-r.left, height = r.bottom-r.top;
        HRGN shape = CreateRoundRectRgn(0, 0, width, height, px(19), px(19));
        HRGN clip = CreateRectRgn(screen_.left-r.left, screen_.top-r.top, screen_.right-r.left, screen_.bottom-r.top);
        CombineRgn(shape, shape, clip, RGN_AND);
        DeleteObject(clip);
        if (!SetWindowRgn(window_, shape, FALSE)) DeleteObject(shape);
        SetWindowPos(window_, HWND_TOPMOST, r.left, r.top, width, height, SWP_NOACTIVATE);
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
        auto edgeArea = revealArea();
        POINT local{cursor.x-position_.left, cursor.y-position_.top};
        bool over = PtInRect(&position_, cursor) && PtInRect(&screen_, cursor);
        int nextHot = over && !dragging_ && recorder_->recording() ? hit(local) : -1;
        if (nextHot != hot_) {
            tooltip(false);
            hot_ = nextHot;
            hoverSince_ = now;
            InvalidateRect(window_, nullptr, FALSE);
        } else if (hot_ > 0 && !tipVisible_ && now-hoverSince_ > 600) tooltip(true);

        if (!dragging_ && pressed_ < 0) {
            if (firstStart_) {
                if (now >= holdUntil_ && !hidden_) aim(true);
                if (recorder_->recording()) {
                    firstStart_ = false;
                    holdUntil_ = 0;
                }
            } else {
                bool reveal = over || PtInRect(&edgeArea, cursor) || now < holdUntil_;
                if (hidden_ == reveal) aim(!reveal);
            }
            if (!EqualRect(&position_, &target_)) {
                double t = std::min(1., (now-began_)/180.);
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

    POINT center(int index) const {
        auto width = position_.right-position_.left, height = position_.bottom-position_.top;
        // Match the current aspect ratio even while changing orientation.
        double horizontal = std::clamp((double(width-height)/px(130)+1.)/2., 0., 1.);
        return {interpolate(width/2, px(index ? 86 + index*35 : 42), horizontal),
            interpolate(px(index ? 32 + index*36 : 24), height/2, horizontal)};
    }

    int hit(POINT p) const {
        for (int i=1; i<=4; ++i) {
            auto c = center(i);
            if (std::abs(p.x-c.x) <= px(16) && std::abs(p.y-c.y) <= px(16)) return i;
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
                MAKELPARAM(position_.left+c.x, position_.top+c.y+px(18)));
        }
        SendMessageW(tooltip_, TTM_TRACKACTIVATE, show, reinterpret_cast<LPARAM>(&tool));
        tipVisible_ = show;
    }

    void paint(HDC dc, RECT r) const {
        auto brush = CreateSolidBrush(palette_.background);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        auto color = [](COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));};
        for (int i=1;i<=4;++i) {
            auto c = center(i);
            if (hot_ == i || pressed_ == i) {
                Gdiplus::SolidBrush hover(color(palette_.hover));
                g.FillEllipse(&hover,c.x-px(15),c.y-px(15),px(30),px(30));
            }
            Gdiplus::Pen pen(color(i==2 && !soundAvailable_ ? palette_.muted : palette_.text),float(px(2)));
            auto line = [&](int x,int y,int a,int b) {
                g.DrawLine(&pen,c.x+px(x),c.y+px(y),c.x+px(a),c.y+px(b));
            };
            if (i==1 && recorder_->paused()) { line(-4,-6,6,0);line(6,0,-4,6);line(-4,6,-4,-6); }
            if (i==1 && !recorder_->paused()) { line(-3,-6,-3,6);line(3,-6,3,6); }
            if (i==2) {
                line(-7,-3,-3,-3);line(-3,-3,2,-7);line(2,-7,2,7);
                line(2,7,-3,3);line(-3,3,-7,3);line(-7,3,-7,-3);
                if (live_->systemMuted || !soundAvailable_) { line(5,-3,9,3);line(9,-3,5,3); }
                else g.DrawArc(&pen,c.x-px(2),c.y-px(6),px(20),px(12),-55.f,110.f);
            }
            if (i==3) { line(-8,-1,0,-8);line(0,-8,8,-1);line(-5,-2,-5,7);line(-5,7,5,7);line(5,7,5,-2); }
            if (i==4) g.DrawRectangle(&pen,c.x-px(5),c.y-px(5),px(10),px(10));
        }
        auto c = center(0);
        Gdiplus::SolidBrush dot(recorder_->paused() ? color(palette_.muted) : Gdiplus::Color(255,229,69,86));
        g.FillEllipse(&dot,c.x-px(29),c.y-px(3),px(6),px(6));
        wchar_t duration[32]{};
        auto seconds = std::max<int64_t>(0, elapsed_);
        swprintf_s(duration,L"%02lld:%02lld:%02lld",seconds/3600,seconds/60%60,seconds%60);
        RECT text{c.x-px(17),c.y-px(10),c.x+px(35),c.y+px(10)};
        auto previous = SelectObject(dc,font_);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,palette_.text);
        DrawTextW(dc,duration,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_LEFT|DT_NOPREFIX);
        SelectObject(dc,previous);
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
            if (!self->recorder_->recording()) return 0;
            self->pressed_ = self->hit(point);
            self->tooltip(false);
            self->dragStart_ = self->position_;
            GetCursorPos(&self->mouseDown_);
            SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            if (self->pressed_ == 0) {
                POINT cursor{};GetCursorPos(&cursor);
                auto dx=cursor.x-self->mouseDown_.x, dy=cursor.y-self->mouseDown_.y;
                if (self->dragging_ || std::abs(dx)+std::abs(dy)>self->px(5)) {
                    self->dragging_ = true;
                    auto r = self->dragStart_;
                    OffsetRect(&r,dx,dy);
                    self->place(r);
                }
            }
            return 0;
        case WM_LBUTTONUP: {
            int pressed = self->pressed_;
            bool dragging = self->dragging_;
            self->pressed_ = -1;
            self->dragging_ = false;
            if (GetCapture()==hwnd) ReleaseCapture();
            if (dragging) { POINT cursor{};GetCursorPos(&cursor);self->drop(cursor); }
            else if (pressed>0 && pressed==self->hit(point)) {
                PostMessageW(self->owner_,WM_RECORD_PANEL,pressed,0);
                InvalidateRect(hwnd,nullptr,FALSE);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (self->dragging_) self->aim(false);
            self->dragging_=false;
            self->pressed_=-1;
            return 0;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
};
