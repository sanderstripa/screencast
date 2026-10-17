#pragma once
#include <windows.h>
// CONNECT's active frontend/src/style.css tokens, as requested by the user.
struct Palette { COLORREF background,text,muted,line,hover,active,buttonText; };
inline Palette vpnPalette(bool dark){
    return dark?Palette{RGB(12,12,13),RGB(237,237,238),RGB(129,133,140),RGB(48,50,54),RGB(26,27,30),RGB(230,231,233),RGB(18,19,21)}:
        Palette{RGB(247,247,245),RGB(23,26,29),RGB(128,133,139),RGB(218,219,220),RGB(238,238,238),RGB(23,26,29),RGB(255,255,255)};
}
