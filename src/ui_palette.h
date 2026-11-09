#pragma once
#include <windows.h>
// CONNECT's active frontend/src/style.css tokens, as requested by the user.
struct Palette { COLORREF background,text,muted,line,hover,active,buttonText; };
inline Palette vpnPalette(bool dark){
    return dark?Palette{RGB(8,9,11),RGB(238,238,239),RGB(150,151,157),RGB(41,42,46),RGB(32,33,37),RGB(238,238,239),RGB(8,9,11)}:
        Palette{RGB(255,255,255),RGB(36,38,40),RGB(115,118,123),RGB(230,231,233),RGB(237,237,239),RGB(36,38,40),RGB(255,255,255)};
}
