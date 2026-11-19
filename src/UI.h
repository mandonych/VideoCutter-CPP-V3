#pragma once

#include <windows.h>

#include "Theme.h"

// Сообщение о нажатии Enter в поле ввода времени
constexpr UINT WM_TIME_ENTER = WM_APP + 10;

// Compatibility aliases for existing UI code
constexpr COLORREF BG_COLOR = Theme::Background;
constexpr COLORREF PANEL_COLOR = Theme::Panel;
constexpr COLORREF BORDER_COLOR = Theme::Border;
constexpr COLORREF TEXT_COLOR = Theme::Text;
constexpr COLORREF MUTED_COLOR = Theme::TextMuted;
constexpr COLORREF ACCENT_COLOR = Theme::Accent;

// Control identifiers
enum
{
    ID_INPUT = 101,
    ID_BROWSE,
    ID_START,
    ID_END,
    ID_OUTPUT,
    ID_SAVE,
    ID_CUT,
    ID_PREVIEW_START,
    ID_PREVIEW_END
};

extern HFONT hFont;
extern HFONT hTimeFont;
extern HWND hInput;
extern HWND hBrowse;
extern HWND hStart;
extern HWND hEnd;
extern HWND hOutput;
extern HWND hSave;
extern HWND hCut;
extern HWND hStatus;
extern HWND hProgress;
extern HWND hPreviewStart;
extern HWND hPreviewEnd;
extern HWND hFileInfo;
extern HFONT hTitleFont;

extern HBRUSH hBgBrush;
extern HBRUSH hPanelBrush;
extern HBRUSH hEditBrush;

void SetFont(HWND hwnd, HFONT font);

HWND CreateLabel(
    HWND parent,
    const wchar_t *text);

HWND CreateEdit(
    HWND parent,
    int id,
    const wchar_t *value = L"");

HWND CreateButton(
    HWND parent,
    int id,
    const wchar_t *text);

void Layout(HWND hwnd);

void DrawButton(const DRAWITEMSTRUCT *dis);
