#pragma once
#include <windows.h>
#include <wrl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>
#include <deque>

using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr) { winrt::check_hresult(hr); }
inline int64_t clock100ns() {
    LARGE_INTEGER value{}, frequency{};
    QueryPerformanceCounter(&value); QueryPerformanceFrequency(&frequency);
    return (value.QuadPart / frequency.QuadPart) * 10000000 +
        (value.QuadPart % frequency.QuadPart) * 10000000 / frequency.QuadPart;
}
inline std::wstring executableDir() {
    std::wstring path(32768, L'\0');
    auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(n); return std::filesystem::path(path).parent_path().wstring();
}
inline std::wstring errorText() {
    try { throw; }
    catch (winrt::hresult_error const& e) { return std::wstring(e.message()) + L" (HRESULT " + std::to_wstring(static_cast<uint32_t>(e.code())) + L")"; }
    catch (std::exception const& e) {
        int len = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, nullptr, 0);
        std::wstring s(static_cast<size_t>(std::max(len, 1)), 0);
        MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, s.data(), len);
        if (!s.empty() && s.back() == 0) s.pop_back(); return s;
    }
    catch (...) { return L"Неизвестная ошибка"; }
}
struct Image {
    int width{}, height{};
    std::vector<uint8_t> bgra;
    int64_t time100ns{};
    bool empty() const { return bgra.empty(); }
};
struct Target {
    std::wstring name;
    HMONITOR monitor{};
    HWND window{};
    DWORD pid{};
    uint64_t creationTime{};
};
struct Device { std::wstring name, id; };
enum class AudioMode { Off, System, Process };
enum class BackgroundMode { Original, Blur, Solid, Gradient, Interior, Custom };
struct LiveSettings {
    std::atomic<float> systemGain{1.f}, micGain{1.f}, noiseStrength{.65f};
    std::atomic_bool denoise{};
    std::atomic_bool micEnabled{true}, cameraEnabled{}, cameraMirror{};
    std::atomic<float> cameraX{1.f}, cameraY{1.f};
    std::atomic<BackgroundMode> background{BackgroundMode::Original};
    std::atomic<int> backgroundPreset{}, blurStrength{16};
    std::atomic<int64_t> overlayReady{};
};
struct Settings {
    Target target;
    AudioMode audio{AudioMode::System};
    DWORD audioPid{};
    uint64_t audioCreationTime{};
    bool mic{true}, denoise{}, camera{}, replaceBackground{};
    bool micAvailable{};
    std::wstring micId, outputId, cameraId, backgroundPath;
    int corner{3};
    int fps{30};
    std::wstring path;
    std::shared_ptr<LiveSettings> live{std::make_shared<LiveSettings>()};
};
