// Windows SDK
#include <windows.h>
#include <unknwn.h>
#include <objidl.h>

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>

// C++ standard library
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <cmath>
#include <filesystem>
#include <string>
#include <limits>
#include <thread>

// Project headers
#include "App.h"
#include "Theme.h"
#include "UI.h"
#include "Utils.h"
#include "VideoInfo.h"
#include "VideoProcessor.h"

extern VideoInfo videoInfo;
using namespace Gdiplus;

namespace {

void DrawRoundedPanel(Graphics &graphics, const RECT &rect, float radius,
                      COLORREF fillColor, COLORREF borderColor) {
  float x = static_cast<float>(rect.left);
  float y = static_cast<float>(rect.top);
  float width = static_cast<float>(rect.right - rect.left);
  float height = static_cast<float>(rect.bottom - rect.top);

  GraphicsPath path;
  const float diameter = radius * 2.0f;

  path.AddArc(x, y, diameter, diameter, 180, 90);
  path.AddArc(x + width - diameter, y, diameter, diameter, 270, 90);
  path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter,
              0, 90);
  path.AddArc(x, y + height - diameter, diameter, diameter, 90, 90);
  path.CloseFigure();

  SolidBrush brush(Color(255, GetRValue(fillColor), GetGValue(fillColor),
                         GetBValue(fillColor)));

  Pen pen(Color(255, GetRValue(borderColor), GetGValue(borderColor),
                GetBValue(borderColor)),
          1.5f);

  graphics.FillPath(&brush, &path);
  graphics.DrawPath(&pen, &path);
}

std::wstring FormatSecondsDot(double seconds) {
  wchar_t buf[64]{};
  swprintf_s(buf, L"%.3f", seconds);
  for (wchar_t &ch : buf) {
    if (ch == L',')
      ch = L'.';
  }
  return buf;
}

HICON hAppIconSmall = nullptr;
HICON hAppIconLarge = nullptr;

Gdiplus::Bitmap *startBitmap = nullptr;
Gdiplus::Bitmap *endBitmap = nullptr;
Gdiplus::Image *appLogo = nullptr;

std::thread cutThread;
std::atomic<bool> isCutting{false};

std::atomic<uint64_t> startPreviewGen{0};
std::atomic<uint64_t> endPreviewGen{0};

Gdiplus::Color ToGdiPlusColor(COLORREF color)
{
    ARGB argb =
        0xFF000000u |
        (static_cast<ARGB>(GetRValue(color)) << 16) |
        (static_cast<ARGB>(GetGValue(color)) << 8) |
        static_cast<ARGB>(GetBValue(color));

    return Gdiplus::Color(argb);
}

double selectedStartTime = 0.0;
double selectedEndTime = 0.0;

// Loop calculation data
std::uintmax_t sourceFileSize = 0;
int loopRepeatCount = 0;
double loopResultDuration = 0.0;
std::uintmax_t loopResultSize = 0;
bool loopCalculationValid = false;

bool isDraggingTimeline = false;
int draggedMarker = 0; // 1 — начало, 2 — конец

std::wstring GetControlText(HWND control) {
  int length = GetWindowTextLengthW(control);
  std::wstring text(length + 1, L'\0');
  GetWindowTextW(control, text.data(), length + 1);
  text.resize(length);
  return text;
}

std::wstring MakeOutputName(const std::wstring &input) {
  size_t slash = input.find_last_of(L"\\/");
  size_t dot = input.find_last_of(L'.');

  if (dot == std::wstring::npos ||
      (slash != std::wstring::npos && dot < slash)) {
    return input + L"_cut";
  }

  return input.substr(0, dot) + L"_cut" + input.substr(dot);
}

bool ParseTime(const std::wstring &raw, double &result) {
  std::wstring value = raw;
  for (wchar_t &ch : value) {
    if (ch == L',')
      ch = L'.';
  }


  
  int h = 0, m = 0;
  double s = 0.0;

  if (swscanf_s(value.c_str(), L"%d:%d:%lf", &h, &m, &s) == 3) {
    if (h < 0 || m < 0 || m > 59 || s < 0.0 || s >= 60.0)
      return false;
    result = h * 3600.0 + m * 60.0 + s;
    return true;
  }

  if (swscanf_s(value.c_str(), L"%d:%lf", &m, &s) == 2) {
    if (m < 0 || s < 0.0 || s >= 60.0)
      return false;
    result = m * 60.0 + s;
    return true;
  }

  return false;
}

std::wstring FormatTime(double seconds)
{
    int totalSeconds = static_cast<int>(seconds);

    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int secs = totalSeconds % 60;

    wchar_t buffer[32]{};

    swprintf_s(
        buffer,
        L"%02d:%02d:%02d",
        hours,
        minutes,
        secs);

    return buffer;
}

void UpdateLoopCalculation(HWND hwnd) {
    loopRepeatCount = 0;
    loopResultDuration = 0.0;
    loopResultSize = 0;
    loopCalculationValid = false;

    if (videoInfo.duration <= 0.0 || sourceFileSize == 0) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    double targetValue = 0.0;

    if (GetLoopTargetMode() == LoopTargetMode::Duration) {
        if (!ParseTime(GetControlText(hLoopTarget), targetValue) ||
            targetValue <= 0.0) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        double repeats = std::floor(targetValue / videoInfo.duration);

        if (repeats < 1.0 ||
            repeats > static_cast<double>((std::numeric_limits<int>::max)())) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        loopRepeatCount = static_cast<int>(repeats);
    }
    else {
        std::wstring value = GetControlText(hLoopTarget);

        for (wchar_t& ch : value) {
            if (ch == L',')
                ch = L'.';
        }

        wchar_t* end = nullptr;
        double targetMB = wcstod(value.c_str(), &end);

        if (end == value.c_str() || *end != L'\0' ||
            !std::isfinite(targetMB) || targetMB <= 0.0) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        long double targetBytes =
            static_cast<long double>(targetMB) * 1024.0L * 1024.0L;

        long double repeats =
            std::floor(targetBytes / sourceFileSize);

        if (repeats < 1.0L ||
            repeats > static_cast<long double>((std::numeric_limits<int>::max)())) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        loopRepeatCount = static_cast<int>(repeats);
    }

    loopResultDuration = videoInfo.duration * loopRepeatCount;
    loopResultSize = sourceFileSize *
                     static_cast<std::uintmax_t>(loopRepeatCount);

    loopCalculationValid = true;

    InvalidateRect(hwnd, nullptr, FALSE);
}

int TimelineTimeToX(double time, const RECT &r)
{
    if (videoInfo.duration <= 0.0)
        return r.left;

    double ratio = time / videoInfo.duration;
    ratio = (std::max)(0.0, (std::min)(1.0, ratio));

    return r.left + static_cast<int>(
        ratio * (r.right - r.left));
}

double TimelineXToTime(int x, const RECT &r)
{
    if (videoInfo.duration <= 0.0)
        return 0.0;

    const int left = static_cast<int>(r.left);
    const int right = static_cast<int>(r.right);

    x = (std::max)(left, (std::min)(right, x));

    double ratio = static_cast<double>(x - left) /
        (right - left);

    return ratio * videoInfo.duration;
}

void RequestPreviewUpdate(HWND hwnd, int controlId,
                          const std::wstring &filePath, double timeSeconds) {
  if (filePath.empty())
    return;

  uint64_t reqId =
      (controlId == ID_PREVIEW_START) ? ++startPreviewGen : ++endPreviewGen;

  ExtractFrameAsync(hwnd, controlId, reqId, filePath, timeSeconds);
}

void LoadInputVideo(HWND hwnd, const std::wstring &filename) {
  SetWindowTextW(hInput, filename.c_str());
  SendMessageW(hInput, EM_SETSEL, static_cast<WPARAM>(-1),
               static_cast<LPARAM>(-1));
  SendMessageW(hInput, EM_SCROLLCARET, 0, 0);

  std::wstring output = MakeOutputName(filename);
  SetWindowTextW(hOutput, output.c_str());
  SendMessageW(hOutput, EM_SETSEL, static_cast<WPARAM>(-1),
               static_cast<LPARAM>(-1));
  SendMessageW(hOutput, EM_SCROLLCARET, 0, 0);

  if (ProbeVideo(filename, videoInfo)) {
    SetWindowTextW(hStatus, L"Информация о видео загружена");
    EnableWindow(hCut, TRUE);

        std::error_code fileSizeError;
    sourceFileSize = std::filesystem::file_size(filename, fileSizeError);

    if (fileSizeError) {
        sourceFileSize = 0;
    }

    loopRepeatCount = 0;
    loopResultDuration = 0.0;
    loopResultSize = 0;
    loopCalculationValid = false;

    UpdateLoopCalculation(hwnd);

    int totalSeconds = static_cast<int>(videoInfo.duration);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    wchar_t endTime[32]{};
    swprintf_s(endTime, L"%02d:%02d:%02d", hours, minutes, seconds);

    SetWindowTextW(hStart, L"00:00:00");
    SetWindowTextW(hEnd, endTime);

    selectedStartTime = 0.0;
    selectedEndTime = videoInfo.duration;

    wchar_t info[512]{};
    swprintf_s(info, L"%s | %dx%d | %.2f FPS | %02d:%02d:%02d",
               videoInfo.format.c_str(), videoInfo.width, videoInfo.height,
               videoInfo.fps, hours, minutes, seconds);

    SetWindowTextW(hFileInfo, info);

    double startPreviewTime = (std::min)(0.5, videoInfo.duration * 0.1);
    double endPreviewTime = (std::max)(0.0, videoInfo.duration - 0.5);

    RequestPreviewUpdate(hwnd, ID_PREVIEW_START, filename, startPreviewTime);
    RequestPreviewUpdate(hwnd, ID_PREVIEW_END, filename, endPreviewTime);
  } else {
    SetWindowTextW(hStatus, L"Не удалось прочитать информацию о видео");
    SetWindowTextW(hFileInfo, L"Ошибка чтения видео");
    EnableWindow(hCut, FALSE);
  }

  InvalidateRect(hwnd, nullptr, FALSE);
}

void HandleTimeChanged(HWND hwnd, int editId) {
  double startTime = 0.0;
  double endTime = 0.0;

  if (!ParseTime(GetControlText(hStart), startTime) ||
      !ParseTime(GetControlText(hEnd), endTime)) {
    SetWindowTextW(hStatus, L"Неверный формат времени (ЧЧ:ММ:СС)");
    return;
  }

  if (startTime >= endTime) {
    SetWindowTextW(hStatus, L"Начало должно быть раньше конца");
    return;
  }

  if (videoInfo.duration > 0 && endTime > videoInfo.duration + 1.0) {
    SetWindowTextW(hStatus, L"Конец выходит за длительность видео");
    return;
  }

  if (videoInfo.duration > 0 && endTime > videoInfo.duration) {
    endTime = videoInfo.duration;
  }

  selectedStartTime = startTime;
selectedEndTime = endTime;

InvalidateRect(hwnd, nullptr, FALSE);

  SetWindowTextW(hStatus, L"Временной диапазон корректен");


  std::wstring inputPath = GetControlText(hInput);
  if (inputPath.empty())
    return;

  if (editId == ID_START) {
    RequestPreviewUpdate(hwnd, ID_PREVIEW_START, inputPath, startTime);
  } else if (editId == ID_END) {
    double previewPos = (std::max)(0.0, endTime - 0.5);
    RequestPreviewUpdate(hwnd, ID_PREVIEW_END, inputPath, previewPos);
  }
}

void ChooseInput(HWND hwnd) {
  wchar_t filename[32768]{};

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd;
  ofn.lpstrFilter = L"Video files\0*.mp4;*.mkv;*.mov;*.avi;*.webm;*.m4v;*.ts\0"
                    L"All files\0*.*\0";
  ofn.lpstrFile = filename;
  ofn.nMaxFile = 32768;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  ofn.lpstrTitle = L"Выберите исходный видеофайл";

  if (GetOpenFileNameW(&ofn)) {
    LoadInputVideo(hwnd, filename);
  }
}

void ChooseOutput(HWND hwnd) {
  wchar_t filename[32768]{};
  GetWindowTextW(hOutput, filename, 32768);

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd;
  ofn.lpstrFilter = L"MP4 video\0*.mp4\0"
                    L"MKV video\0*.mkv\0"
                    L"All files\0*.*\0";
  ofn.lpstrFile = filename;
  ofn.nMaxFile = 32768;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
  ofn.lpstrTitle = L"Сохранить видеофрагмент";

  if (GetSaveFileNameW(&ofn)) {
    SetWindowTextW(hOutput, filename);
    SendMessageW(hOutput, EM_SETSEL, static_cast<WPARAM>(-1),
                 static_cast<LPARAM>(-1));
    SendMessageW(hOutput, EM_SCROLLCARET, 0, 0);
  }
}

bool CutVideo(HWND hwnd) {
  if (isCutting)
    return false;

  std::wstring input = GetControlText(hInput);
  std::wstring output = GetControlText(hOutput);

  if (input.empty() || output.empty()) {
    SetWindowTextW(hStatus, L"Не выбран входной или выходной файл");
    return false;
  }

  double startTime = 0.0;
  double endTime = 0.0;

  if (!ParseTime(GetControlText(hStart), startTime) ||
      !ParseTime(GetControlText(hEnd), endTime) || startTime >= endTime ||
      (videoInfo.duration > 0 && endTime > videoInfo.duration + 1.0)) {
    SetWindowTextW(hStatus, L"Проверьте временной диапазон");
    return false;
  }

  if (videoInfo.duration > 0 && endTime > videoInfo.duration) {
    endTime = videoInfo.duration;
  }

  std::filesystem::path ffmpegPath =
      std::filesystem::path(GetExecutablePath()) / L"bin" / L"ffmpeg.exe";

  if (!std::filesystem::exists(ffmpegPath)) {
    SetWindowTextW(hStatus, L"Не найден bin\\ffmpeg.exe");
    MessageBoxW(
        hwnd, L"Исполняемый файл bin\\ffmpeg.exe не найден рядом с программой.",
        L"Video Cutter", MB_OK | MB_ICONERROR);
    return false;
  }

  double duration = endTime - startTime;
  std::wstring ffmpeg = ffmpegPath.wstring();

  std::wstring command = L"\"" + ffmpeg +
                         L"\""
                         L" -y -ss " +
                         FormatSecondsDot(startTime) + L" -i \"" + input +
                         L"\""
                         L" -t " +
                         FormatSecondsDot(duration) +
                         L" -c copy -progress pipe:1 -nostats \"" + output +
                         L"\"";

  isCutting = true;
  EnableWindow(hCut, FALSE);

  SendMessageW(hProgress, PBM_SETPOS, 0, 0);
  SetWindowTextW(hStatus, L"Выполняется нарезка видео...");

  cutThread = std::thread([command, ffmpeg, hwnd, duration]() {
    RunCutProcess(command, ffmpeg, hwnd, duration);
  });

  return true;
}

bool LoopVideo(HWND hwnd) {
    if (isCutting)
        return false;

    std::wstring input = GetControlText(hInput);
    std::wstring output = GetControlText(hOutput);

    if (input.empty() || output.empty()) {
        SetWindowTextW(hStatus, L"Не выбран входной или выходной файл");
        return false;
    }

    if (!loopCalculationValid || loopRepeatCount < 1) {
        SetWindowTextW(
            hStatus,
            L"Невозможно создать цикл: проверьте целевое значение");
        return false;
    }

    if (!std::filesystem::exists(input)) {
        SetWindowTextW(hStatus, L"Исходный файл не найден");
        return false;
    }

    std::error_code pathError;
    auto inputPath = std::filesystem::weakly_canonical(input, pathError);

    if (pathError) {
        SetWindowTextW(hStatus, L"Не удалось определить путь исходного файла");
        return false;
    }

    auto outputPath = std::filesystem::absolute(output, pathError).lexically_normal();

    if (pathError) {
        SetWindowTextW(hStatus, L"Некорректный путь выходного файла");
        return false;
    }

    if (_wcsicmp(
            inputPath.c_str(),
            outputPath.c_str()) == 0) {
        SetWindowTextW(
            hStatus,
            L"Выходной файл не должен совпадать с исходным");
        return false;
    }

    std::filesystem::path ffmpegPath =
        std::filesystem::path(GetExecutablePath()) /
        L"bin" / L"ffmpeg.exe";

    if (!std::filesystem::exists(ffmpegPath)) {
        SetWindowTextW(hStatus, L"Не найден bin\\ffmpeg.exe");

        MessageBoxW(
            hwnd,
            L"Исполняемый файл bin\\ffmpeg.exe не найден рядом с программой.",
            L"Video Cutter",
            MB_OK | MB_ICONERROR);

        return false;
    }

    std::wstring ffmpeg = ffmpegPath.wstring();

    // Количество дополнительных повторов
    int additionalLoops = loopRepeatCount - 1;

    std::wstring command =
        L"\"" + ffmpeg + L"\"" +
        L" -y -stream_loop " +
        std::to_wstring(additionalLoops) +
        L" -i \"" + input + L"\"" +
        L" -c copy -progress pipe:1 -nostats \"" +
        output + L"\"";

    isCutting = true;
    EnableWindow(hCut, FALSE);

    SendMessageW(hProgress, PBM_SETPOS, 0, 0);
    SetWindowTextW(hStatus, L"Создаётся зацикленное видео...");

    double duration = loopResultDuration;

    cutThread = std::thread(
        [command, ffmpeg, hwnd, duration]() {
            RunCutProcess(command, ffmpeg, hwnd, duration);
        });

    return true;
}

} // namespace

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  switch (message) {

    case WM_LBUTTONDOWN:
{
    if (videoInfo.duration <= 0.0)
        return 0;

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

        LayoutMetrics m = CalculateLayout(
        clientRect.right,
        clientRect.bottom,
        GetActiveMode());

    RECT hitRect = m.timelineRect;
    hitRect.top -= 15;
    hitRect.bottom += 15;

    POINT cursor{
        GET_X_LPARAM(lParam),
        GET_Y_LPARAM(lParam)
    };

    if (!PtInRect(&hitRect, cursor))
        return 0;

    int startX = TimelineTimeToX(
        selectedStartTime,
        m.timelineRect);

    int endX = TimelineTimeToX(
        selectedEndTime,
        m.timelineRect);

    int startDistance = std::abs(cursor.x - startX);
int endDistance = std::abs(cursor.x - endX);

draggedMarker =
    startDistance <= endDistance ? 1 : 2;

    isDraggingTimeline = true;
    SetCapture(hwnd);

    return 0;
}

case WM_MOUSEMOVE:
{
    if (!isDraggingTimeline || !draggedMarker)
        return 0;

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

        LayoutMetrics m = CalculateLayout(
        clientRect.right,
        clientRect.bottom,
        GetActiveMode());

    double time = TimelineXToTime(
        GET_X_LPARAM(lParam),
        m.timelineRect);

    constexpr double minInterval = 1.0;

    if (draggedMarker == 1)
    {
        selectedStartTime = (std::min)(
            time,
            selectedEndTime - minInterval);

        selectedStartTime = (std::max)(
            0.0,
            selectedStartTime);
    }
    else
    {
        selectedEndTime = (std::max)(
            time,
            selectedStartTime + minInterval);

        selectedEndTime = (std::min)(
            videoInfo.duration,
            selectedEndTime);
    }

    SetWindowTextW(
        hStart,
        FormatTime(selectedStartTime).c_str());

    SetWindowTextW(
        hEnd,
        FormatTime(selectedEndTime).c_str());

    InvalidateRect(hwnd, nullptr, FALSE);

    return 0;
}

case WM_LBUTTONUP:
{
    if (!isDraggingTimeline)
        return 0;

    isDraggingTimeline = false;
    draggedMarker = 0;

    ReleaseCapture();

    std::wstring inputPath = GetControlText(hInput);

    if (!inputPath.empty())
    {
        RequestPreviewUpdate(
            hwnd,
            ID_PREVIEW_START,
            inputPath,
            selectedStartTime);

        RequestPreviewUpdate(
            hwnd,
            ID_PREVIEW_END,
            inputPath,
            (std::max)(0.0, selectedEndTime - 0.5));
    }

    SetWindowTextW(
        hStatus,
        L"Временной диапазон корректен");

    return 0;
}

case WM_CAPTURECHANGED:
{
    isDraggingTimeline = false;
    draggedMarker = 0;
    return 0;
}
  case WM_GETMINMAXINFO: {
    auto *mmi = reinterpret_cast<MINMAXINFO *>(lParam);
    RECT minRect{0, 0, Theme::WindowWidth, Theme::WindowHeight};
    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    AdjustWindowRectEx(&minRect, style ? style : WS_OVERLAPPEDWINDOW, FALSE,
                       exStyle);

    mmi->ptMinTrackSize.x = minRect.right - minRect.left;
    mmi->ptMinTrackSize.y = minRect.bottom - minRect.top;
    return 0;
  }

  case WM_PREVIEW_READY: {
    auto *res = reinterpret_cast<PreviewResult *>(lParam);
    if (res) {
      if (res->controlId == ID_PREVIEW_START) {
        if (res->requestId == startPreviewGen.load() && res->bitmap) {
          delete startBitmap;
          startBitmap = res->bitmap;
          InvalidateRect(hPreviewStart, nullptr, FALSE);
        } else {
          delete res->bitmap;
        }
      } else if (res->controlId == ID_PREVIEW_END) {
        if (res->requestId == endPreviewGen.load() && res->bitmap) {
          delete endBitmap;
          endBitmap = res->bitmap;
          InvalidateRect(hPreviewEnd, nullptr, FALSE);
        } else {
          delete res->bitmap;
        }
      } else {
        delete res->bitmap;
      }
      delete res;
    }
    return 0;
  }

  case WM_TIME_ENTER: {
    int editId = static_cast<int>(wParam);
    if (editId == ID_START || editId == ID_END) {
      HandleTimeChanged(hwnd, editId);
    }
    return 0;
  }

  case WM_DROPFILES: {
    HDROP hDrop = reinterpret_cast<HDROP>(wParam);
    UINT len = DragQueryFileW(hDrop, 0, nullptr, 0);

    if (len > 0) {
      std::wstring filePath(len + 1, L'\0');
      DragQueryFileW(hDrop, 0, filePath.data(), len + 1);
      filePath.resize(len);

      if (!std::filesystem::is_directory(filePath)) {
        LoadInputVideo(hwnd, filePath);
      }
    }

    DragFinish(hDrop);
    return 0;
  }

  case WM_CUT_PROGRESS: {
    SendMessageW(hProgress, PBM_SETPOS, wParam, 0);
    return 0;
  }

  case WM_CUT_FINISHED: {
    DWORD exitCode = static_cast<DWORD>(wParam);

    if (cutThread.joinable())
      cutThread.join();

    isCutting = false;
    EnableWindow(hCut, TRUE);

    if (exitCode == 0) {
      SendMessageW(hProgress, PBM_SETPOS, 100, 0);
      SetWindowTextW(hStatus, L"Нарезка успешно завершена");

      MessageBoxW(hwnd, L"Нарезка видео успешно завершена!", L"Video Cutter",
                  MB_OK | MB_ICONINFORMATION);
    } else {
      SetWindowTextW(hStatus, L"Ошибка при нарезке видео");

      MessageBoxW(hwnd, L"При нарезке видео произошла ошибка.", L"Video Cutter",
                  MB_OK | MB_ICONERROR);
    }

    SetWindowTextW(hStatus, L"");
    SendMessageW(hProgress, PBM_SETPOS, 0, 0);
    return 0;
  }

  case WM_CREATE: {
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode,
                          sizeof(useDarkMode));

    std::wstring logoPath = GetExecutablePath() + L"\\VideoCutter_icon_256.png";
    appLogo = Gdiplus::Image::FromFile(logoPath.c_str());

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    hAppIconSmall = static_cast<HICON>(LoadImageW(
        hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 24, 24, LR_DEFAULTCOLOR));
    hAppIconLarge = static_cast<HICON>(LoadImageW(
        hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));

    if (hAppIconSmall)
      SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                   reinterpret_cast<LPARAM>(hAppIconSmall));
    if (hAppIconLarge)
      SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                   reinterpret_cast<LPARAM>(hAppIconLarge));

    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    hBgBrush = Theme::CreateThemeBrush(BG_COLOR);
    hPanelBrush = Theme::CreateThemeBrush(PANEL_COLOR);
    hEditBrush = Theme::CreateThemeBrush(PANEL_COLOR);

    hFont = Theme::CreateUIFont(17, FW_NORMAL);
    hTimeFont = Theme::CreateUIFont(29, FW_NORMAL);
    hTitleFont = Theme::CreateUIFont(38, FW_BOLD);
        // Mode tabs
    hTabCut = CreateButton(hwnd, ID_TAB_CUT, L"Нарезка видео");
    hTabLoop = CreateButton(hwnd, ID_TAB_LOOP, L"Зацикливание");

    // Loop settings
hLoopDuration = CreateButton(hwnd, ID_LOOP_DURATION, L"По длительности");
hLoopSize = CreateButton(hwnd, ID_LOOP_SIZE, L"По размеру");
hLoopTarget = CreateEdit(hwnd, ID_LOOP_TARGET, L"00:10:00");
SetFont(hLoopTarget, hTimeFont);

    // Source file
    hInput = CreateEdit(hwnd, ID_INPUT);
    hFileInfo =
        CreateLabel(hwnd, L"Перетащите видео сюда или нажмите «Обзор...»");
    hBrowse = CreateButton(hwnd, ID_BROWSE, L"Обзор...");

    // Time range
    hStart = CreateEdit(hwnd, ID_START, L"00:00:00");
    hEnd = CreateEdit(hwnd, ID_END, L"00:00:10");
    SetFont(hStart, hTimeFont);
    SetFont(hEnd, hTimeFont);

    // Preview placeholders
    hPreviewStart = CreateWindowW(
        L"BUTTON", L"Начальный кадр", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,
        0, 100, 100, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PREVIEW_START)),
        nullptr, nullptr);

    hPreviewEnd = CreateWindowW(
        L"BUTTON", L"Конечный кадр", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0,
        100, 100, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PREVIEW_END)), nullptr,
        nullptr);

    // Output
    hOutput = CreateEdit(hwnd, ID_OUTPUT);
    hSave = CreateButton(hwnd, ID_SAVE, L"Сохранить...");

    // Bottom
    hStatus = CreateLabel(hwnd, L"Выберите исходное видео");
    SetWindowLongPtrW(hStatus, GWL_STYLE,
                      GetWindowLongPtrW(hStatus, GWL_STYLE) | SS_RIGHT);

    hCut = CreateButton(hwnd, ID_CUT, L"Обрезать видео");

    hProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 100,
                                20, hwnd, nullptr, hInst, nullptr);

    SendMessageW(hProgress, PBM_SETBKCOLOR, 0,
                 static_cast<LPARAM>(Theme::PanelSecondary));
    SendMessageW(hProgress, PBM_SETBARCOLOR, 0,
                 static_cast<LPARAM>(Theme::Accent));
    SendMessageW(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(hProgress, PBM_SETPOS, 0, 0);

    EnableWindow(hCut, FALSE);

    Layout(hwnd);
    return 0;
  }

  case WM_SIZE:
    Layout(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    return 0;

  case WM_CTLCOLORSTATIC: {
    HDC dc = reinterpret_cast<HDC>(wParam);
    HWND hCtrl = reinterpret_cast<HWND>(lParam);

    if (hCtrl == hStatus) {
      SetTextColor(dc, RGB(255, 160, 102));
      SetBkMode(dc, OPAQUE);
      SetBkColor(dc, BG_COLOR);
      return reinterpret_cast<LRESULT>(hBgBrush);
    }

    if (hCtrl == hFileInfo) {
      SetTextColor(dc, RGB(118, 148, 106));
      SetBkMode(dc, TRANSPARENT);
      return reinterpret_cast<LRESULT>(hPanelBrush);
    }

    SetTextColor(dc, TEXT_COLOR);
    SetBkMode(dc, TRANSPARENT);
    return reinterpret_cast<LRESULT>(hPanelBrush);
  }

  case WM_CTLCOLOREDIT: {
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, TEXT_COLOR);
    SetBkColor(dc, PANEL_COLOR);
    return reinterpret_cast<LRESULT>(hEditBrush);
  }

  case WM_CTLCOLORBTN:
    return reinterpret_cast<LRESULT>(hBgBrush);

  case WM_DRAWITEM: {
    auto dis = reinterpret_cast<DRAWITEMSTRUCT *>(lParam);

        if (dis->CtlID == ID_BROWSE ||
    dis->CtlID == ID_SAVE ||
    dis->CtlID == ID_CUT ||
    dis->CtlID == ID_TAB_CUT ||
    dis->CtlID == ID_TAB_LOOP ||
    dis->CtlID == ID_LOOP_DURATION ||
    dis->CtlID == ID_LOOP_SIZE) {
    DrawButton(dis);
    return TRUE;
}

    if (dis->CtlID == ID_PREVIEW_START || dis->CtlID == ID_PREVIEW_END) {
      FillRect(dis->hDC, &dis->rcItem, hPanelBrush);

      Gdiplus::Bitmap *bitmap =
          dis->CtlID == ID_PREVIEW_START ? startBitmap : endBitmap;

      if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Graphics graphics(dis->hDC);
        constexpr int inset = 3;

        int width = dis->rcItem.right - dis->rcItem.left - inset * 2;
        int height = dis->rcItem.bottom - dis->rcItem.top - inset * 2;

        float scaleX = static_cast<float>(width) / bitmap->GetWidth();
        float scaleY = static_cast<float>(height) / bitmap->GetHeight();
        float scale = (std::min)(scaleX, scaleY);

        int drawWidth = static_cast<int>(bitmap->GetWidth() * scale);
        int drawHeight = static_cast<int>(bitmap->GetHeight() * scale);

        int x = dis->rcItem.left + inset + (width - drawWidth) / 2;
        int y = dis->rcItem.top + inset + (height - drawHeight) / 2;

        graphics.DrawImage(bitmap, x, y, drawWidth, drawHeight);
      } else {
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, MUTED_COLOR);

        RECT textRect = dis->rcItem;
        DrawTextW(dis->hDC,
                  dis->CtlID == ID_PREVIEW_START ? L"Начальный кадр"
                                                 : L"Конечный кадр",
                  -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }

      HPEN pen = CreatePen(PS_SOLID, 2, Theme::AccentSecondary);
      HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
      HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));

      Rectangle(dis->hDC, dis->rcItem.left + 1, dis->rcItem.top + 1,
                dis->rcItem.right - 1, dis->rcItem.bottom - 1);

      SelectObject(dis->hDC, oldPen);
      SelectObject(dis->hDC, oldBrush);
      DeleteObject(pen);

      return TRUE;
    }
    break;
  }

  case WM_COMMAND:
    switch (LOWORD(wParam)) {

            case ID_TAB_CUT:
    SetActiveMode(AppMode::Cut);
    SetWindowTextW(hCut, L"Обрезать видео");
    Layout(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
    return 0;

case ID_TAB_LOOP:
    SetActiveMode(AppMode::Loop);
    SetWindowTextW(hCut, L"Создать цикл");
    Layout(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
    return 0;

        case ID_LOOP_DURATION:
    SetLoopTargetMode(LoopTargetMode::Duration);
    SetWindowTextW(hLoopTarget, L"00:10:00");
    UpdateLoopCalculation(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
    return 0;

case ID_LOOP_SIZE:
    SetLoopTargetMode(LoopTargetMode::Size);
    SetWindowTextW(hLoopTarget, L"1000");
    UpdateLoopCalculation(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
    return 0;

    case ID_BROWSE:
      ChooseInput(hwnd);
      return 0;

    case ID_SAVE:
      ChooseOutput(hwnd);
      return 0;

    case ID_CUT:
    if (GetActiveMode() == AppMode::Loop) {
        LoopVideo(hwnd);
    }
    else {
        CutVideo(hwnd);
    }
    return 0;

    case ID_START:
case ID_END:
    if (HIWORD(wParam) == EN_KILLFOCUS) {
        HandleTimeChanged(hwnd, LOWORD(wParam));
    }
    return 0;

case ID_LOOP_TARGET:
    if (HIWORD(wParam) == EN_CHANGE ||
        HIWORD(wParam) == EN_KILLFOCUS) {
        UpdateLoopCalculation(hwnd);
    }
    return 0;
    }
    break;

  case WM_ERASEBKGND:
    return 1;

  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    int cw = clientRect.right - clientRect.left;
    int ch = clientRect.bottom - clientRect.top;

    if (cw <= 0 || ch <= 0) {
      EndPaint(hwnd, &ps);
      return 0;
    }

    // Double buffering via memory DC to prevent flicker during resize
    HDC memDC = CreateCompatibleDC(dc);
    HBITMAP memBmp = CreateCompatibleBitmap(dc, cw, ch);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        AppMode mode = GetActiveMode();
    LayoutMetrics m = CalculateLayout(cw, ch, mode);

    Graphics graphics(memDC);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);

    graphics.Clear(ToGdiPlusColor(Theme::Background));

    // Panels
    DrawRoundedPanel(graphics, m.leftPanel, 18.0f,
                     Theme::Panel, Theme::Border);

    if (mode == AppMode::Cut) {
      DrawRoundedPanel(graphics, m.rightTopPanel, 18.0f,
                       Theme::Panel, Theme::Border);

      DrawRoundedPanel(graphics, m.rightBottomPanel, 18.0f,
                       Theme::Panel, Theme::Border);
    }

    DrawRoundedPanel(graphics, m.bottomPanel, 18.0f,
                     Theme::Panel, Theme::Border);

    // Header accent bar
    SolidBrush accentBrush(ToGdiPlusColor(Theme::Accent));

    graphics.FillRectangle(
        &accentBrush, static_cast<INT>(m.accentBar.left),
        static_cast<INT>(m.accentBar.top),
        static_cast<INT>(m.accentBar.right - m.accentBar.left),
        static_cast<INT>(m.accentBar.bottom - m.accentBar.top));

    HGDIOBJ oldFont = SelectObject(memDC, hTitleFont);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, Theme::Text);
        TextOutW(memDC, m.titlePos.x, m.titlePos.y, L"Video Cutter", 12);

    SelectObject(memDC, hFont);
    SetTextColor(memDC, Theme::TextMuted);

    if (mode == AppMode::Cut) {
      TextOutW(memDC, m.subtitlePos.x, m.subtitlePos.y,
               L"Быстрая обрезка без перекодирования", 35);
    } else {
      TextOutW(memDC, m.subtitlePos.x, m.subtitlePos.y,
               L"Создание зацикленного видео", 28);
    }

    // Section headings
    SetTextColor(memDC, Theme::Text);
    TextOutW(memDC, m.srcLabelPos.x, m.srcLabelPos.y, L"Исходный видеофайл",
             18);
    TextOutW(memDC, m.dstLabelPos.x, m.dstLabelPos.y, L"Выходной файл", 14);
    if (mode == AppMode::Loop) {
    SetTextColor(memDC, Theme::Text);
    TextOutW(memDC, m.leftPanel.left + 20, m.loopDurationRect.top - 32,
             L"Параметры зацикливания", 23);

    SetTextColor(memDC, Theme::TextMuted);
    TextOutW(memDC, m.loopTargetRect.left, m.loopTargetRect.top - 20,
             L"Целевое значение", 17);

    // Estimated result
    std::wstring targetLabel =
    GetLoopTargetMode() == LoopTargetMode::Duration
        ? L"Целевая длительность (ЧЧ:ММ:СС)"
        : L"Целевой размер (МБ)";

SetTextColor(memDC, Theme::TextMuted);
TextOutW(
    memDC,
    m.loopTargetRect.left,
    m.loopTargetRect.top - 20,
    targetLabel.c_str(),
    static_cast<int>(targetLabel.length()));

// Estimated result
int resultX = m.loopTargetRect.right + 24;
int resultY = m.loopTargetRect.top - 20;

SetTextColor(memDC, Theme::Text);
TextOutW(memDC, resultX, resultY,
         L"Расчётный результат", 19);

SetTextColor(memDC, Theme::TextMuted);

if (loopCalculationValid) {
    std::wstring durationLine =
        L"Длительность: " + FormatTime(loopResultDuration);

    wchar_t sizeText[128]{};
    swprintf_s(
        sizeText,
        L"Размер файла: %.1f МБ",
        static_cast<double>(loopResultSize) / (1024.0 * 1024.0));

    wchar_t repeatsText[128]{};
    swprintf_s(
        repeatsText,
        L"Количество повторов: %d",
        loopRepeatCount);

    TextOutW(
        memDC, resultX, resultY + 28,
        durationLine.c_str(),
        static_cast<int>(durationLine.length()));

    TextOutW(memDC, resultX, resultY + 50,
             sizeText, static_cast<int>(wcslen(sizeText)));

    TextOutW(memDC, resultX, resultY + 72,
             repeatsText, static_cast<int>(wcslen(repeatsText)));
}
else {
    TextOutW(memDC, resultX, resultY + 28,
             L"Длительность: —", 16);

    TextOutW(memDC, resultX, resultY + 50,
             L"Размер файла: —", 16);

    TextOutW(memDC, resultX, resultY + 72,
             L"Количество повторов: —", 22);
}
}

    if (mode == AppMode::Cut) {
    TextOutW(memDC, m.startLabelPos.x, m.startLabelPos.y, L"Начало", 6);
    TextOutW(memDC, m.endLabelPos.x, m.endLabelPos.y, L"Конец", 5);

    // Timeline
    if (videoInfo.duration > 0.0) {
      const RECT &r = m.timelineRect;

      const int centerY = (r.top + r.bottom) / 2;
      const int markerRadius = 7;

      const double duration = videoInfo.duration;

      auto timeToX = [&](double time) {
        double ratio = time / duration;
        ratio = (std::max)(0.0, (std::min)(1.0, ratio));

        return r.left + static_cast<int>(ratio * (r.right - r.left));
      };

      int startX = timeToX(selectedStartTime);
      int endX = timeToX(selectedEndTime);

      // Background track
      Gdiplus::Pen trackPen(Gdiplus::Color(255, 65, 68, 85), 6.0f);

      graphics.DrawLine(&trackPen, r.left, centerY, r.right, centerY);

      // Selected range
      Gdiplus::Pen rangePen(Gdiplus::Color(255, 152, 187, 108), 6.0f);

      graphics.DrawLine(&rangePen, startX, centerY, endX, centerY);

      // Start marker
      Gdiplus::SolidBrush startBrush(Gdiplus::Color(255, 152, 187, 108));

      graphics.FillEllipse(&startBrush, startX - markerRadius,
                           centerY - markerRadius, markerRadius * 2,
                           markerRadius * 2);

      // End marker
      Gdiplus::SolidBrush endBrush(Gdiplus::Color(255, 220, 180, 110));

      graphics.FillEllipse(&endBrush, endX - markerRadius,
                           centerY - markerRadius, markerRadius * 2,
                           markerRadius * 2);

      // Selected duration
      double selectedDuration =
          (std::max)(0.0, selectedEndTime - selectedStartTime);

      int totalSeconds = static_cast<int>(selectedDuration);

      wchar_t durationText[128]{};

      swprintf_s(durationText, L"Длительность фрагмента: %02d:%02d:%02d",
                 totalSeconds / 3600, (totalSeconds % 3600) / 60,
                 totalSeconds % 60);

      SetTextColor(memDC, Theme::TextMuted);
              TextOutW(memDC, m.durationPos.x, m.durationPos.y, durationText,
                 static_cast<int>(wcslen(durationText)));
    }
}

    // Logo
    if (appLogo && appLogo->GetLastStatus() == Gdiplus::Ok) {
      graphics.DrawImage(appLogo, static_cast<INT>(m.logoRect.left),
                         static_cast<INT>(m.logoRect.top),
                         static_cast<INT>(m.logoRect.right - m.logoRect.left),
                         static_cast<INT>(m.logoRect.bottom - m.logoRect.top));
    }

    SelectObject(memDC, oldFont);

    // Blit buffer to screen
    BitBlt(dc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_DESTROY:
    if (cutThread.joinable())
      cutThread.join();

    delete appLogo;
    appLogo = nullptr;

    delete startBitmap;
    startBitmap = nullptr;

    delete endBitmap;
    endBitmap = nullptr;

    if (hAppIconSmall)
      DestroyIcon(hAppIconSmall);
    if (hAppIconLarge)
      DestroyIcon(hAppIconLarge);

    DeleteObject(hBgBrush);
    DeleteObject(hPanelBrush);
    DeleteObject(hEditBrush);
    DeleteObject(hFont);
    DeleteObject(hTitleFont);
    DeleteObject(hTimeFont);

    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcW(hwnd, message, wParam, lParam);
}
