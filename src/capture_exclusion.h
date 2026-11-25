#pragma once
#include <windows.h>

inline constexpr DWORD captureExclusion = 0x11;

inline bool hasCaptureExclusion(HWND window) {
    DWORD actual{};
    return GetWindowDisplayAffinity(window, &actual) && actual == captureExclusion;
}

inline bool excludeFromCapture(HWND window) {
    return SetWindowDisplayAffinity(window, captureExclusion) && hasCaptureExclusion(window);
}
