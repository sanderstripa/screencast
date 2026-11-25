#pragma once
#include <windows.h>

inline constexpr DWORD captureExclusion = 0x11;
inline constexpr wchar_t captureExclusionProperty[] = L"Screencast.CaptureExclusion";

inline bool hasCaptureExclusion(HWND window) {
    return GetPropW(window, captureExclusionProperty) != nullptr;
}

inline bool excludeFromCapture(HWND window) {
    if (!SetWindowDisplayAffinity(window, captureExclusion)) return false;
    if (SetPropW(window, captureExclusionProperty, window)) return true;
    SetWindowDisplayAffinity(window, WDA_NONE);
    return false;
}

inline void resetCaptureExclusion(HWND window) {
    SetWindowDisplayAffinity(window, WDA_NONE);
    RemovePropW(window, captureExclusionProperty);
}
