
#pragma once

#include <windows.h>

namespace Theme
{
    // =====================================
    // Kanagawa Wave — Color Palette
    // =====================================

    // Main background
    inline constexpr COLORREF Background =
        RGB(31, 31, 40);       // #1F1F28

    // Panels
    inline constexpr COLORREF Panel =
        RGB(42, 42, 55);       // #2A2A37

    inline constexpr COLORREF PanelSecondary =
        RGB(54, 54, 70);       // #363646

    // Borders
    inline constexpr COLORREF Border =
        RGB(84, 84, 109);      // #54546D

    // Text
    inline constexpr COLORREF Text =
        RGB(220, 215, 186);    // #DCD7BA

    inline constexpr COLORREF TextMuted =
        RGB(166, 166, 156);    // #A6A69C

    // Accents
    inline constexpr COLORREF Accent =
        RGB(126, 156, 216);    // #7E9CD8

    inline constexpr COLORREF AccentSecondary =
        RGB(127, 180, 202);    // #7FB4CA

    // Button colors
    inline constexpr COLORREF ButtonHover =
        RGB(143, 169, 217);

    inline constexpr COLORREF ButtonPressed =
        RGB(96, 124, 181);

    // =====================================
    // Typography
    // =====================================

    inline constexpr wchar_t FontFamily[] = L"Segoe UI";

    inline constexpr int FontSize = 17;
    inline constexpr int SectionFontSize = 20;
    inline constexpr int TitleFontSize = 29;

    // =====================================
    // Main window
    // =====================================

    inline constexpr int WindowWidth = 880;
    inline constexpr int WindowHeight = 608;

    HFONT CreateUIFont(int height, int weight = FW_NORMAL);
    HBRUSH CreateThemeBrush(COLORREF color);
}
