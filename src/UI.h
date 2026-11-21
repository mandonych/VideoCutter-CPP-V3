#pragma once

#include <windows.h>

#include "Theme.h"

constexpr UINT WM_TIME_ENTER = WM_APP + 10;

// Compatibility aliases for existing UI code
constexpr COLORREF BG_COLOR = Theme::Background;
constexpr COLORREF PANEL_COLOR = Theme::Panel;
constexpr COLORREF BORDER_COLOR = Theme::Border;
constexpr COLORREF TEXT_COLOR = Theme::Text;
constexpr COLORREF MUTED_COLOR = Theme::TextMuted;
constexpr COLORREF ACCENT_COLOR = Theme::Accent;

// Control identifiers
enum {
    ID_INPUT = 101,
    ID_BROWSE,
    ID_START,
    ID_END,
    ID_OUTPUT,
    ID_SAVE,
    ID_CUT,
    ID_PREVIEW_START,
    ID_PREVIEW_END,
        ID_TAB_CUT,
    ID_TAB_LOOP,
    ID_TAB_HELP,
    ID_LOOP_DURATION,
    ID_LOOP_SIZE,
    ID_LOOP_TARGET
};

enum class AppMode {
    Cut,
    Loop,
    Help
};

struct LayoutMetrics {
  int clientW = Theme::WindowWidth;
  int clientH = Theme::WindowHeight;

  // Main panels
  RECT leftPanel{};
  RECT rightTopPanel{};
  RECT rightBottomPanel{};
  RECT bottomPanel{};

  // Header
  RECT accentBar{};
  POINT titlePos{};
  POINT subtitlePos{};
  RECT logoRect{};

    // Mode tabs
  RECT tabCutRect{};
  RECT tabLoopRect{};
    RECT tabHelpRect{};
  RECT loopDurationRect{};
RECT loopSizeRect{};
RECT loopTargetRect{};

  // Section headers
  POINT srcLabelPos{};
  POINT dstLabelPos{};
  POINT startLabelPos{};
  POINT endLabelPos{};

  // Controls
  RECT inputRect{};
  RECT browseRect{};
  RECT fileInfoRect{};
  RECT outputRect{};
  RECT saveRect{};

  // Time range controls in left panel
  RECT startEditRect{};
  RECT endEditRect{};
  RECT timelineRect{};
  POINT durationPos{};

  // Preview panels on the right
  RECT previewStartRect{};
  RECT previewEndRect{};

  RECT progressRect{};
  RECT cutBtnRect{};
  RECT statusRect{};
};

LayoutMetrics CalculateLayout(
    int clientW,
    int clientH,
    AppMode mode = AppMode::Cut
);

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
extern HWND hTabCut;
extern HWND hTabLoop;
extern HWND hTabHelp;
extern HWND hHelpText;
extern HWND hLoopDuration;
extern HWND hLoopSize;
extern HWND hLoopTarget;
extern HFONT hTitleFont;

extern HBRUSH hBgBrush;
extern HBRUSH hPanelBrush;
extern HBRUSH hEditBrush;

void SetFont(HWND hwnd, HFONT font);

HWND CreateLabel(HWND parent, const wchar_t *text);

HWND CreateEdit(HWND parent, int id, const wchar_t *value = L"");

HWND CreateButton(HWND parent, int id, const wchar_t *text);

void Layout(HWND hwnd);
void Layout(HWND hwnd, AppMode mode);

void SetActiveMode(AppMode mode);
AppMode GetActiveMode();

enum class LoopTargetMode {
    Duration,
    Size
};

void SetLoopTargetMode(LoopTargetMode mode);
LoopTargetMode GetLoopTargetMode();

void DrawButton(const DRAWITEMSTRUCT *dis);
