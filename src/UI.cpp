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

LayoutMetrics CalculateLayout(int clientW, int clientH) {
  LayoutMetrics m{};
  m.clientW = (std::max)(clientW, Theme::WindowWidth);
  m.clientH = (std::max)(clientH, Theme::WindowHeight);

  constexpr int margin = 20;
  constexpr int gap = 12;
  constexpr int headerH = 110;
  constexpr int footerH = 30;
  constexpr int bottomPanelH = 70;

  // Header
  m.accentBar = {34, 30, 39, 100};
  m.titlePos = {60, 20};
  m.subtitlePos = {60, 76};
  m.logoRect = {m.clientW - 140, 8, m.clientW - 35, 113};

  // Main panels
  int middleTop = headerH;
  int bottomPanelTop = m.clientH - footerH - bottomPanelH;
  int middleBottom = bottomPanelTop - 18;
  int middleH = middleBottom - middleTop;

  // 68% left panel, 32% right panel
  int usableW = m.clientW - margin * 2 - gap;
  int leftW = static_cast<int>(usableW * 0.68);
  int rightW = usableW - leftW;

  int leftX = margin;
  int rightX = leftX + leftW + gap;

  m.leftPanel = {leftX, middleTop, leftX + leftW, middleBottom};

  int previewGap = 12;
  int previewH = (middleH - previewGap) / 2;

  m.rightTopPanel = {rightX, middleTop, rightX + rightW, middleTop + previewH};

  m.rightBottomPanel = {rightX, middleTop + previewH + previewGap,
                        rightX + rightW, middleBottom};

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

  // Time range
  int timeBlockY = middleTop + 125;
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
                    timeBlockY + 122};

  m.durationPos = {leftX + innerPad + 4, timeBlockY + 132};

  // Output file
  int dstBlockY = middleBottom - 80;

  m.dstLabelPos = {leftX + innerPad + 4, dstBlockY};

  m.outputRect = {leftX + innerPad, dstBlockY + 28, leftX + innerPad + editW,
                  dstBlockY + 63};

  m.saveRect = {leftX + innerPad + editW + 10, dstBlockY + 28,
                leftX + leftW - innerPad, dstBlockY + 63};

  // Right previews occupy the entire panels
  constexpr int previewPad = 10;

  m.previewStartRect = {rightX + previewPad, m.rightTopPanel.top + previewPad,
                        rightX + rightW - previewPad,
                        m.rightTopPanel.bottom - previewPad};

  m.previewEndRect = {rightX + previewPad, m.rightBottomPanel.top + previewPad,
                      rightX + rightW - previewPad,
                      m.rightBottomPanel.bottom - previewPad};

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

void Layout(HWND hwnd) {
  RECT rc{};
  GetClientRect(hwnd, &rc);

  LayoutMetrics m = CalculateLayout(rc.right - rc.left, rc.bottom - rc.top);

  auto place = [](HWND ctrl, const RECT &r) {
    if (!ctrl)
      return;
    SetWindowPos(ctrl, nullptr, r.left, r.top, r.right - r.left,
                 r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
  };

  place(hInput, m.inputRect);
  place(hBrowse, m.browseRect);
  place(hFileInfo, m.fileInfoRect);
  place(hOutput, m.outputRect);
  place(hSave, m.saveRect);

  place(hPreviewStart, m.previewStartRect);
  place(hPreviewEnd, m.previewEndRect);

  place(hStart, m.startEditRect);
  place(hEnd, m.endEditRect);

  place(hCut, m.cutBtnRect);
  place(hProgress, m.progressRect);
  place(hStatus, m.statusRect);

  CenterEditTextVertical(hInput);
  CenterEditTextVertical(hOutput);
  CenterEditTextVertical(hStart);
  CenterEditTextVertical(hEnd);
}

void DrawButton(const DRAWITEMSTRUCT *dis) {
  bool primary = dis->CtlID == ID_CUT;

  COLORREF background = primary ? Theme::Accent : Theme::PanelSecondary;

  COLORREF textColor = primary ? Theme::Background : Theme::Text;

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

  wchar_t text[128]{};
  GetWindowTextW(dis->hwndItem, text, 128);

  RECT rect = dis->rcItem;

  DrawTextW(dis->hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
