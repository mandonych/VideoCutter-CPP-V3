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
HWND hTabCut = nullptr;
HWND hTabLoop = nullptr;
HWND hTabHelp = nullptr;
HWND hHelpText = nullptr;
HWND hLoopDuration = nullptr;
HWND hLoopSize = nullptr;
HWND hLoopTarget = nullptr;
HWND hAudioSilence = nullptr;
HWND hAudioSilenceLabel = nullptr;

static AppMode activeMode = AppMode::Cut;
static LoopTargetMode loopTargetMode = LoopTargetMode::Duration;

void SetActiveMode(AppMode mode) { activeMode = mode; }

AppMode GetActiveMode() { return activeMode; }

void SetLoopTargetMode(LoopTargetMode mode) { loopTargetMode = mode; }

LoopTargetMode GetLoopTargetMode() { return loopTargetMode; }

HFONT hFont = nullptr;
HFONT hTimeFont = nullptr;
HFONT hTitleFont = nullptr;
HFONT hButtonFont = nullptr;
HFONT hInfoFont = nullptr;
HFONT hPreviewFont = nullptr;
HBRUSH hBgBrush = nullptr;
HBRUSH hPanelBrush = nullptr;
HBRUSH hEditBrush = nullptr;

LayoutMetrics CalculateLayout(int clientW, int clientH, AppMode mode) {
  LayoutMetrics m{};
  m.clientW = (std::max)(clientW, Theme::WindowWidth);
  m.clientH = (std::max)(clientH, Theme::WindowHeight);

  constexpr int margin = 20;
  constexpr int gap = 12;
  constexpr int footerH = 30;
  constexpr int bottomPanelH = 70;

  // Header
  m.accentBar = {34, 30, 39, 100};
  m.titlePos = {60, 20};
  m.subtitlePos = {60, 76};
  m.logoRect = {m.clientW - 140, 8, m.clientW - 35, 113};

  // Mode tabs
  m.tabCutRect = {margin, 116, margin + 190, 148};
  m.tabLoopRect = {margin + 200, 116, margin + 390, 148};
  m.tabHelpRect = {margin + 400, 116, margin + 590, 148};

  // Main panels
  int middleTop = 158;
  int bottomPanelTop = m.clientH - footerH - bottomPanelH;

  int middleBottom =
      (mode == AppMode::Help) ? m.clientH - margin : bottomPanelTop - 18;

  int middleH = middleBottom - middleTop;

  // 68% left panel, 32% right panel
  int usableW = m.clientW - margin * 2;
  int leftW = usableW;
  int rightW = 0;

  int leftX = margin;
  int rightX = leftX + leftW + gap;

  if (mode == AppMode::Cut) {
    leftW = static_cast<int>((usableW - gap) * 0.68);
    rightW = usableW - leftW - gap;
    rightX = leftX + leftW + gap;
  }

  m.leftPanel = {leftX, middleTop, leftX + leftW, middleBottom};

  if (mode == AppMode::Cut) {
    int previewGap = 12;
    int previewH = (middleH - previewGap) / 2;

    m.rightTopPanel = {rightX, middleTop, rightX + rightW,
                       middleTop + previewH};

    m.rightBottomPanel = {rightX, middleTop + previewH + previewGap,
                          rightX + rightW, middleBottom};
  } else {
    m.rightTopPanel = {};
    m.rightBottomPanel = {};

    // Элементы управления режимом зацикливания
    int loopX = m.leftPanel.left + 20;
    int loopY = middleTop + 130;

    m.loopDurationRect = {loopX, loopY, loopX + 180, loopY + 34};

    m.loopSizeRect = {loopX + 192, loopY, loopX + 372, loopY + 34};

    m.loopTargetRect = {loopX, loopY + 80, loopX + 260, loopY + 114};
  }
  // Left panel contents
  int innerPad = 18;
  int btnW = 120;
  int editW = leftW - innerPad * 2 - btnW - 10;

  // Source file
  int srcBlockY = middleTop + 5;

  m.srcLabelPos = {leftX + innerPad + 4, srcBlockY};

  m.inputRect = {leftX + innerPad, srcBlockY + 30, leftX + innerPad + editW,
                 srcBlockY + 65};

  m.browseRect = {leftX + innerPad + editW + 10, srcBlockY + 30,
                  leftX + leftW - innerPad, srcBlockY + 65};

  m.fileInfoRect = {leftX + innerPad, srcBlockY + 72, leftX + leftW - innerPad,
                    srcBlockY + 96};

  m.audioSilenceRect = {leftX + innerPad, srcBlockY + 100,
                        leftX + innerPad + 22, srcBlockY + 126};

  // Time range
  int timeBlockY = middleTop + 130;
  int timeGap = 18;
  int timeW = (leftW - innerPad * 2 - timeGap) / 2;

  m.startLabelPos = {leftX + innerPad + 4, timeBlockY};

  m.endLabelPos = {leftX + innerPad + timeW + timeGap + 4, timeBlockY};

  m.startEditRect = {leftX + innerPad, timeBlockY + 25,
                     leftX + innerPad + timeW, timeBlockY + 65};

  m.endEditRect = {leftX + innerPad + timeW + timeGap, timeBlockY + 25,
                   leftX + leftW - innerPad, timeBlockY + 65};

  // Timeline placeholder
  m.timelineRect = {leftX + innerPad, timeBlockY + 88, leftX + leftW - innerPad,
                    timeBlockY + 102};

  m.durationPos = {leftX + innerPad + 4, timeBlockY + 108};

  // Output file
  int dstBlockY = middleBottom - 80;

  if (mode == AppMode::Loop) {
    dstBlockY += 12;
  }

  m.dstLabelPos = {leftX + innerPad + 4, dstBlockY};

  m.outputRect = {leftX + innerPad, dstBlockY + 28, leftX + innerPad + editW,
                  dstBlockY + 63};

  m.saveRect = {leftX + innerPad + editW + 10, dstBlockY + 28,
                leftX + leftW - innerPad, dstBlockY + 63};

  // Right previews occupy the entire panels
  // Right previews are used only in Cut mode
  if (mode == AppMode::Cut) {
    constexpr int previewPad = 10;

    m.previewStartRect = {rightX + previewPad, m.rightTopPanel.top + previewPad,
                          rightX + rightW - previewPad,
                          m.rightTopPanel.bottom - previewPad};

    m.previewEndRect = {
        rightX + previewPad, m.rightBottomPanel.top + previewPad,
        rightX + rightW - previewPad, m.rightBottomPanel.bottom - previewPad};
  } else {
    m.previewStartRect = {};
    m.previewEndRect = {};
  }

  // Bottom panel
  m.bottomPanel = {margin, bottomPanelTop, m.clientW - margin,
                   bottomPanelTop + bottomPanelH};

  int cutBtnW = 190;
  int cutBtnH = 52;

  int cutBtnX = m.clientW - margin - 20 - cutBtnW;
  int cutBtnY = bottomPanelTop + (bottomPanelH - cutBtnH) / 2;

  m.cutBtnRect = {cutBtnX, cutBtnY, cutBtnX + cutBtnW, cutBtnY + cutBtnH};

  // Progress bar
  int progX = margin + 20;
  int progW = cutBtnX - progX - 40;
  int progY = bottomPanelTop + (bottomPanelH - 14) / 2;

  m.progressRect = {progX, progY, progX + progW, progY + 14};

  // Footer status
  int statusY = bottomPanelTop + bottomPanelH + 4;

  m.statusRect = {m.clientW - margin - 600, statusY, m.clientW - margin - 5,
                  statusY + 22};

  return m;
}

void SetFont(HWND hwnd, HFONT font) {
  SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

HWND CreateLabel(HWND parent, const wchar_t *text) {
  HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, 0, 0, 100, 25,
                         parent, nullptr, nullptr, nullptr);

  SetFont(h, hFont);
  return h;
}

LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                  LPARAM lParam, UINT_PTR subclassId,
                                  DWORD_PTR) {
  if (message == WM_MOUSEMOVE) {
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    TrackMouseEvent(&tme);

    InvalidateRect(hwnd, nullptr, FALSE);
  }

  if (message == WM_MOUSELEAVE) {
    InvalidateRect(hwnd, nullptr, FALSE);
  }

  if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
    InvalidateRect(hwnd, nullptr, TRUE);
  }

  if (message == WM_KEYDOWN && wParam == VK_RETURN) {
    PostMessageW(GetParent(hwnd), WM_TIME_ENTER, subclassId, (LPARAM)hwnd);
    return 0;
  }

  if (message == WM_CHAR && wParam == L'\r') {
    return 0;
  }

  LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);

  if (message == WM_PAINT) {
    HDC dc = GetDC(hwnd);

    RECT rect{};
    GetClientRect(hwnd, &rect);

    COLORREF borderColor = Theme::Border;

    if (GetFocus() == hwnd) {
      borderColor = Theme::Accent;
    } else {
      POINT cursor{};
      GetCursorPos(&cursor);
      ScreenToClient(hwnd, &cursor);

      if (PtInRect(&rect, cursor))
        borderColor = Theme::AccentSecondary;
    }

    HPEN pen = CreatePen(PS_SOLID, 1, borderColor);

    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    Rectangle(dc, 1, 1, rect.right - 1, rect.bottom - 1);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);

    DeleteObject(pen);
    ReleaseDC(hwnd, dc);
  }

  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, EditSubclassProc, subclassId);
  }

  return result;
}

LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                    LPARAM lParam, UINT_PTR subclassId,
                                    DWORD_PTR) {
  if (message == WM_MOUSEMOVE) {
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    TrackMouseEvent(&tme);

    InvalidateRect(hwnd, nullptr, FALSE);
  } else if (message == WM_MOUSELEAVE) {
    InvalidateRect(hwnd, nullptr, FALSE);
  } else if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, ButtonSubclassProc, subclassId);
  }

  return DefSubclassProc(hwnd, message, wParam, lParam);
}

void CenterEditTextVertical(HWND h) {
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

  RECT textRect{8, (std::max)(0, offset), rect.right - 8, rect.bottom};

  SendMessageW(h, EM_SETRECTNP, 0, (LPARAM)&textRect);
}

HWND CreateEdit(HWND parent, int id, const wchar_t *value) {
  HWND h = CreateWindowExW(
      0, L"EDIT", value,
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0,
      0, 100, 35, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);

  SetFont(h, hFont);
  SetWindowSubclass(h, EditSubclassProc, id, 0);
  CenterEditTextVertical(h);
  return h;
}

HWND CreateButton(HWND parent, int id, const wchar_t *text) {
  HWND h =
      CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0,
                    100, 38, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);

  SetFont(h, hFont);
  SetWindowSubclass(h, ButtonSubclassProc, id, 0);
  return h;
}

void Layout(HWND hwnd) { Layout(hwnd, activeMode); }

void Layout(HWND hwnd, AppMode mode) {
  RECT rc{};
  GetClientRect(hwnd, &rc);

  LayoutMetrics m =
      CalculateLayout(rc.right - rc.left, rc.bottom - rc.top, mode);

  auto place = [](HWND ctrl, const RECT &r) {
    if (!ctrl)
      return;
    SetWindowPos(ctrl, nullptr, r.left, r.top, r.right - r.left,
                 r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
  };

  place(hTabCut, m.tabCutRect);
  place(hTabLoop, m.tabLoopRect);
  place(hTabHelp, m.tabHelpRect);
  place(hLoopDuration, m.loopDurationRect);
  place(hLoopSize, m.loopSizeRect);
  place(hLoopTarget, m.loopTargetRect);

  place(hAudioSilence, m.audioSilenceRect);

  RECT audioLabelRect = m.audioSilenceRect;
  audioLabelRect.left += 24;
  audioLabelRect.right = audioLabelRect.left + 300;
  place(hAudioSilenceLabel, audioLabelRect);

  place(hInput, m.inputRect);
  place(hBrowse, m.browseRect);
  place(hFileInfo, m.fileInfoRect);
  place(hOutput, m.outputRect);
  place(hSave, m.saveRect);

  if (mode == AppMode::Help) {
    ShowWindow(hHelpText, SW_SHOW);
    ShowWindow(hInput, SW_HIDE);
    ShowWindow(hBrowse, SW_HIDE);
    ShowWindow(hFileInfo, SW_HIDE);
    ShowWindow(hOutput, SW_HIDE);
    ShowWindow(hSave, SW_HIDE);
    ShowWindow(hStart, SW_HIDE);
    ShowWindow(hEnd, SW_HIDE);
    ShowWindow(hPreviewStart, SW_HIDE);
    ShowWindow(hPreviewEnd, SW_HIDE);
    ShowWindow(hLoopDuration, SW_HIDE);
    ShowWindow(hLoopSize, SW_HIDE);
    ShowWindow(hLoopTarget, SW_HIDE);
    ShowWindow(hCut, SW_HIDE);
    ShowWindow(hProgress, SW_HIDE);
    ShowWindow(hStatus, SW_HIDE);
  } else {
    ShowWindow(hHelpText, SW_HIDE);
    ShowWindow(hInput, SW_SHOW);
    ShowWindow(hBrowse, SW_SHOW);
    ShowWindow(hFileInfo, SW_SHOW);
    ShowWindow(hOutput, SW_SHOW);
    ShowWindow(hSave, SW_SHOW);
    ShowWindow(hCut, SW_SHOW);
    ShowWindow(hProgress, SW_SHOW);
    ShowWindow(hStatus, SW_SHOW);
    ShowWindow(hAudioSilence, SW_HIDE);
    ShowWindow(hAudioSilenceLabel, SW_HIDE);

    if (mode == AppMode::Cut) {
      ShowWindow(hPreviewStart, SW_SHOW);
      ShowWindow(hPreviewEnd, SW_SHOW);
      ShowWindow(hStart, SW_SHOW);
      ShowWindow(hEnd, SW_SHOW);
      ShowWindow(hLoopDuration, SW_HIDE);
      ShowWindow(hLoopSize, SW_HIDE);
      ShowWindow(hLoopTarget, SW_HIDE);
      ShowWindow(hAudioSilence, SW_SHOW);
      ShowWindow(hAudioSilenceLabel, SW_SHOW);
    } else {
      ShowWindow(hPreviewStart, SW_HIDE);
      ShowWindow(hPreviewEnd, SW_HIDE);
      ShowWindow(hStart, SW_HIDE);
      ShowWindow(hEnd, SW_HIDE);
      ShowWindow(hLoopDuration, SW_SHOW);
      ShowWindow(hLoopSize, SW_SHOW);
      ShowWindow(hLoopTarget, SW_SHOW);
      ShowWindow(hAudioSilence, SW_HIDE);
      ShowWindow(hAudioSilenceLabel, SW_HIDE);
    }
  }

  place(hPreviewStart, m.previewStartRect);
  place(hPreviewEnd, m.previewEndRect);

  place(hStart, m.startEditRect);
  place(hEnd, m.endEditRect);

  place(hCut, m.cutBtnRect);
  place(hProgress, m.progressRect);
  place(hStatus, m.statusRect);

  RECT helpRect = {m.leftPanel.left + 20, m.leftPanel.top + 20,
                   m.leftPanel.right - 20, m.leftPanel.bottom - 20};

  place(hHelpText, helpRect);

  CenterEditTextVertical(hInput);
  CenterEditTextVertical(hOutput);
  CenterEditTextVertical(hStart);
  CenterEditTextVertical(hEnd);
}

void DrawButton(const DRAWITEMSTRUCT *dis) {
  bool primary = dis->CtlID == ID_CUT;

  bool isTab = dis->CtlID == ID_TAB_CUT || dis->CtlID == ID_TAB_LOOP ||
               dis->CtlID == ID_TAB_HELP;

  bool isActiveTab =
      (dis->CtlID == ID_TAB_CUT && activeMode == AppMode::Cut) ||
      (dis->CtlID == ID_TAB_LOOP && activeMode == AppMode::Loop) ||
      (dis->CtlID == ID_TAB_HELP && activeMode == AppMode::Help);

  // Loop calculation mode buttons
  bool isLoopOption =
      dis->CtlID == ID_LOOP_DURATION || dis->CtlID == ID_LOOP_SIZE;

  // По умолчанию активен режим расчёта по длительности
  bool isActiveLoopOption =
      (dis->CtlID == ID_LOOP_DURATION &&
       loopTargetMode == LoopTargetMode::Duration) ||
      (dis->CtlID == ID_LOOP_SIZE && loopTargetMode == LoopTargetMode::Size);

  bool highlighted =
      primary || (isTab && isActiveTab) || (isLoopOption && isActiveLoopOption);

  COLORREF background = highlighted ? Theme::Accent : Theme::PanelSecondary;

  COLORREF textColor = highlighted ? Theme::Background : Theme::Text;

  if (dis->itemState & ODS_DISABLED) {
    background = Theme::PanelSecondary;
    textColor = Theme::TextMuted;
  } else if (dis->itemState & ODS_SELECTED) {
    background = primary ? Theme::ButtonPressed : RGB(73, 73, 105);
  } else {
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(dis->hwndItem, &cursor);

    if (PtInRect(&dis->rcItem, cursor)) {
      background = primary ? Theme::ButtonHover : Theme::Border;
    }
  }

  HBRUSH brush = CreateSolidBrush(background);
  FillRect(dis->hDC, &dis->rcItem, brush);
  DeleteObject(brush);

  SetBkMode(dis->hDC, TRANSPARENT);
SetTextColor(dis->hDC, textColor);

HGDIOBJ oldFont = SelectObject(dis->hDC, hButtonFont);

wchar_t text[128]{};
GetWindowTextW(dis->hwndItem, text, 128);

RECT rect = dis->rcItem;

DrawTextW(
    dis->hDC,
    text,
    -1,
    &rect,
    DT_CENTER | DT_VCENTER | DT_SINGLELINE
);

SelectObject(dis->hDC, oldFont);
}
