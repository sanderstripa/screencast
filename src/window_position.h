#pragma once
#include <windows.h>
#include <algorithm>

namespace windowPosition {
// Screen coordinates, including negative positions on secondary displays.
inline POINT fit(POINT point,SIZE size,RECT work){
    point.x=std::clamp(point.x,work.left,std::max(work.left,work.right-size.cx));
    point.y=std::clamp(point.y,work.top,std::max(work.top,work.bottom-size.cy));
    return point;
}
inline bool save(HWND window,wchar_t const* key){
    if(IsIconic(window)||IsZoomed(window))return false;
    RECT rect{};if(!GetWindowRect(window,&rect))return false;
    POINT point{rect.left,rect.top};HKEY handle{};
    if(RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,0,KEY_SET_VALUE,nullptr,&handle,nullptr)!=ERROR_SUCCESS)return false;
    auto result=RegSetValueExW(handle,L"WindowPosition",0,REG_BINARY,reinterpret_cast<BYTE const*>(&point),sizeof(point));
    RegCloseKey(handle);return result==ERROR_SUCCESS;
}
inline bool restore(HWND window,wchar_t const* key){
    POINT point{};DWORD size=sizeof(point);
    if(RegGetValueW(HKEY_CURRENT_USER,key,L"WindowPosition",RRF_RT_REG_BINARY,nullptr,&point,&size)!=ERROR_SUCCESS||size!=sizeof(point))return false;
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&monitor))return false;
    // Moving to a different monitor can synchronously change the window's DPI.
    // Clamp again using the resulting size; never restore an old window size.
    for(int pass=0;pass<2;++pass){
        RECT rect{};if(!GetWindowRect(window,&rect))return false;
        auto target=fit(point,SIZE{rect.right-rect.left,rect.bottom-rect.top},monitor.rcWork);
        if(!SetWindowPos(window,nullptr,target.x,target.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE))return false;
    }
    return true;
}
}
