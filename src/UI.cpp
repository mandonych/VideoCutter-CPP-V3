#include "UI.h"
#include <algorithm>
#include <commctrl.h>

HWND hInput, hBrowse;
HWND hStart, hEnd;
HWND hOutput, hSave;
HWND hCut, hStatus;
HWND hProgress = nullptr;
HWND hPreviewStart, hPreviewEnd;
HWND hFileInfo = nullptr;
HFONT hFont = nullptr;
HFONT hTimeFont = nullptr;
HFONT hTitleFont = nullptr;
HBRUSH hBgBrush = nullptr;
HBRUSH hPanelBrush = nullptr;
HBRUSH hEditBrush = nullptr;

void SetFont(HWND hwnd, HFONT font)
{
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

HWND CreateLabel(HWND parent, const wchar_t *text)
{
    HWND h = CreateWindowW(
        L"STATIC", text,
        WS_CHILD | WS_VISIBLE,
        0, 0, 100, 25,
        parent, nullptr, nullptr, nullptr);

    SetFont(h, hFont);

    // Внутренние отступы текста
    SendMessageW(
        h,
        EM_SETMARGINS,
        EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELONG(10, 10));

    return h;
}

LRESULT CALLBACK EditSubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR refData)
{
    if (message == WM_MOUSEMOVE)
    {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        InvalidateRect(hwnd, nullptr, FALSE);
    }

    if (message == WM_MOUSELEAVE)
    {
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    if (message == WM_SETFOCUS ||
        message == WM_KILLFOCUS)
    {
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    LRESULT result = DefSubclassProc(
        hwnd, message, wParam, lParam);

    if (message == WM_PAINT)
    {
        HDC dc = GetDC(hwnd);

        RECT rect{};
        GetClientRect(hwnd, &rect);

        COLORREF borderColor = Theme::Border;

        if (GetFocus() == hwnd)
        {
            borderColor = Theme::Accent;
        }
        else
        {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(hwnd, &cursor);

            if (PtInRect(&rect, cursor))
                borderColor = Theme::AccentSecondary;
        }

        HPEN pen = CreatePen(
            PS_SOLID, 1, borderColor);

        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(
            dc, GetStockObject(NULL_BRUSH));

        Rectangle(
            dc,
            1, 1,
            rect.right - 1,
            rect.bottom - 1);

        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);

        DeleteObject(pen);
        ReleaseDC(hwnd, dc);
    }

    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(
            hwnd,
            EditSubclassProc,
            subclassId);
    }

    return result;
}

void CenterEditTextVertical(HWND h)
{
    RECT rect{};
    GetClientRect(h, &rect);

    HDC dc = GetDC(h);
    HFONT font = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    HFONT oldFont = (HFONT)SelectObject(dc, font);

    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);

    SelectObject(dc, oldFont);
    ReleaseDC(h, dc);

    int textHeight = tm.tmHeight + tm.tmExternalLeading;
    int offset = (rect.bottom - rect.top - textHeight) / 2;

    RECT textRect{
        8,
        (std::max)(0, offset),
        rect.right - 8,
        rect.bottom
    };

    SendMessageW(h, EM_SETRECTNP, 0, (LPARAM)&textRect);
}

HWND CreateEdit(HWND parent, int id, const wchar_t *value)
{
    HWND h = CreateWindowExW(
        0, L"EDIT", value,
        WS_CHILD | WS_VISIBLE |
    ES_AUTOHSCROLL |
    ES_MULTILINE |
    ES_AUTOVSCROLL,
        0, 0, 100, 35,
        parent,
        (HMENU)(INT_PTR)id,
        nullptr, nullptr);

    SetFont(h, hFont);
    SetWindowSubclass(
        h,
        EditSubclassProc,
        id,
        0);
        CenterEditTextVertical(h);
    return h;
}

HWND CreateButton(HWND parent, int id, const wchar_t *text)
{
    HWND h = CreateWindowW(
        L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 100, 38,
        parent,
        (HMENU)(INT_PTR)id,
        nullptr, nullptr);

    SetFont(h, hFont);
    return h;
}

void Layout(HWND hwnd)
{
    // Fixed client area: 1100 x 760
    constexpr int margin = 20;

    // =====================================
    // LEFT PANEL — Source and Output
    // =====================================

    // Source file
    SetWindowPos(
        hInput, nullptr,
        40, 255, 400, 44,
        SWP_NOZORDER);

    SetWindowPos(
        hBrowse, nullptr,
        450, 255, 140, 44,
        SWP_NOZORDER);

    // Video information
    SetWindowPos(
        hFileInfo, nullptr,
        40, 315, 540, 32,
        SWP_NOZORDER);

    // Output file
    SetWindowPos(
        hOutput, nullptr,
        40, 445, 400, 44,
        SWP_NOZORDER);

    SetWindowPos(
        hSave, nullptr,
        450, 445, 140, 44,
        SWP_NOZORDER);

    // =====================================
    // RIGHT PANEL — Preview
    // =====================================

    // Start frame
    SetWindowPos(
        hPreviewStart, nullptr,
        645, 205, 220, 145,
        SWP_NOZORDER);

    SetWindowPos(
        hStart, nullptr,
        895, 270, 170, 48,
        SWP_NOZORDER);

    // End frame
    SetWindowPos(
        hPreviewEnd, nullptr,
        645, 400, 220, 145,
        SWP_NOZORDER);

    SetWindowPos(
        hEnd, nullptr,
        895, 475, 170, 48,
        SWP_NOZORDER);

    // =====================================
// BOTTOM PANEL — Progress and Cut
// =====================================

SetWindowPos(
    hCut, nullptr,
    820, 610, 240, 65,
    SWP_NOZORDER);

SetWindowPos(
    hProgress, nullptr,
    40, 635, 700, 14,
    SWP_NOZORDER);

// =====================================
// FOOTER — Status message
// =====================================

SetWindowPos(
    hStatus, nullptr,
    500, 720, 585, 24,
    SWP_NOZORDER);

        CenterEditTextVertical(hInput);
CenterEditTextVertical(hOutput);
CenterEditTextVertical(hStart);
CenterEditTextVertical(hEnd);
}

void DrawButton(const DRAWITEMSTRUCT *dis)
{
    bool primary = dis->CtlID == ID_CUT;

    COLORREF background = primary
                              ? Theme::Accent
                              : Theme::PanelSecondary;

    COLORREF textColor = primary
                             ? Theme::Background
                             : Theme::Text;

    // Отключённая кнопка
    if (dis->itemState & ODS_DISABLED)
    {
        background = Theme::PanelSecondary;
        textColor = Theme::TextMuted;
    }
    // Нажатая кнопка
    else if (dis->itemState & ODS_SELECTED)
    {
        background = primary
                         ? RGB(96, 124, 181)
                         : RGB(73, 73, 105);
    }

    HBRUSH brush = CreateSolidBrush(background);

    FillRect(dis->hDC, &dis->rcItem, brush);

    DeleteObject(brush);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, textColor);

    wchar_t text[128]{};

    GetWindowTextW(dis->hwndItem, text, 128);

    RECT rect = dis->rcItem;

    DrawTextW(
        dis->hDC,
        text,
        -1,
        &rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
