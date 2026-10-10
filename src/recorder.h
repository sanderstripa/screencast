#pragma once
#include "common.h"
constexpr UINT WM_RECORD_STATUS = WM_APP + 1;
struct RecordingResult {
    bool success{}, cancelled{};
    std::wstring message, path;
};
class Recorder {
public:
    ~Recorder();
    void start(Settings settings, HWND notify);
    void stop();
    bool busy() const { return busy_; }
    bool recording() const { return recording_; }
    int64_t elapsed() const { auto start = epoch_.load(); return start ? (clock100ns() - start) / 10000000 : 0; }
    void join();
private:
    std::thread worker_;
    std::atomic_bool busy_{}, recording_{}, stop_{};
    std::atomic<int64_t> epoch_{};
};
