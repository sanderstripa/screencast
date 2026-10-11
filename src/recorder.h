#pragma once
#include "common.h"
#include "motion.h"
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
    bool hardware() const {return hardware_;}
    int64_t elapsed() const { auto start = epoch_.load(); return start ? (clock100ns() - start) / 10000000 : 0; }
    void join();
    std::shared_ptr<Image const> preview() const { return preview_.load(); }
    Point cameraPosition() const { return {cameraX_.load(),cameraY_.load()}; }
private:
    std::thread worker_;
    std::atomic_bool busy_{}, recording_{}, stop_{},hardware_{};
    std::atomic<int64_t> epoch_{};
    std::atomic<std::shared_ptr<Image const>> preview_;
    std::atomic<float> cameraX_{1.f},cameraY_{1.f};
};
