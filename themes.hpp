#pragma once
#include <windows.h>
struct Palette { COLORREF bg, panel, fg, button, selection, selected_text; bool dark; };
inline constexpr Palette palettes[] = {
    {RGB(246,247,249), RGB(255,255,255), RGB(20,25,35), RGB(224,230,237), RGB(0,65,120), RGB(255,255,255), false},
    {RGB(18,20,24), RGB(29,32,38), RGB(247,248,250), RGB(48,54,64), RGB(242,213,105), RGB(15,19,25), true},
    {RGB(221,237,255), RGB(245,250,255), RGB(7,35,72), RGB(190,216,246), RGB(12,59,120), RGB(255,255,255), false},
    {RGB(9,23,44), RGB(16,37,63), RGB(243,248,255), RGB(31,57,89), RGB(174,216,255), RGB(6,23,44), true},
    {RGB(240,229,244), RGB(255,248,238), RGB(46,25,56), RGB(215,222,245), RGB(79,43,99), RGB(255,255,255), false}
};
