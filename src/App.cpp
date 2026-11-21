// Windows SDK
#include <objidl.h>
#include <richedit.h>
#include <unknwn.h>
#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>

// C++ standard library
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

Gdiplus::Color ToGdiPlusColor(COLORREF color) {
  ARGB argb = 0xFF000000u | (static_cast<ARGB>(GetRValue(color)) << 16) |
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

std::wstring FormatTime(double seconds) {
  int totalSeconds = static_cast<int>(seconds);

  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int secs = totalSeconds % 60;

  wchar_t buffer[32]{};

  swprintf_s(buffer, L"%02d:%02d:%02d", hours, minutes, secs);

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

    constexpr double durationTolerance = 0.1;

    double repeats =
        std::floor((targetValue + durationTolerance) / videoInfo.duration);

    if (repeats < 1.0 ||
        repeats > static_cast<double>((std::numeric_limits<int>::max)())) {
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }

    loopRepeatCount = static_cast<int>(repeats);
  } else {
    std::wstring value = GetControlText(hLoopTarget);

    for (wchar_t &ch : value) {
      if (ch == L',')
        ch = L'.';
    }

    wchar_t *end = nullptr;
    double targetMB = wcstod(value.c_str(), &end);

    if (end == value.c_str() || *end != L'\0' || !std::isfinite(targetMB) ||
        targetMB <= 0.0) {
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }

    long double targetBytes =
        static_cast<long double>(targetMB) * 1024.0L * 1024.0L;

    long double repeats = std::floor(targetBytes / sourceFileSize);

    if (repeats < 1.0L ||
        repeats > static_cast<long double>((std::numeric_limits<int>::max)())) {
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }

    loopRepeatCount = static_cast<int>(repeats);
  }

  loopResultDuration = videoInfo.duration * loopRepeatCount;
  loopResultSize =
      sourceFileSize * static_cast<std::uintmax_t>(loopRepeatCount);

  loopCalculationValid = true;

  InvalidateRect(hwnd, nullptr, FALSE);
}

int TimelineTimeToX(double time, const RECT &r) {
  if (videoInfo.duration <= 0.0)
    return r.left;

  double ratio = time / videoInfo.duration;
  ratio = (std::max)(0.0, (std::min)(1.0, ratio));

  return r.left + static_cast<int>(ratio * (r.right - r.left));
}

double TimelineXToTime(int x, const RECT &r) {
  if (videoInfo.duration <= 0.0)
    return 0.0;

  const int left = static_cast<int>(r.left);
  const int right = static_cast<int>(r.right);

  x = (std::max)(left, (std::min)(right, x));

  double ratio = static_cast<double>(x - left) / (right - left);

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

    if (hAudioSilence) {
      SendMessageW(hAudioSilence, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(hAudioSilence, !videoInfo.hasAudio);
    }

    wchar_t info[1024]{};

    std::wstring audioInfo = L"—";

    if (videoInfo.hasAudio) {
      audioInfo = videoInfo.audioCodec;

      if (!videoInfo.audioBitrate.empty()) {
        try {
          int bitrate = std::stoi(videoInfo.audioBitrate) / 1000;
          audioInfo += L" | " + std::to_wstring(bitrate) + L" kb/s";
        } catch (...) {
        }
      }

      if (!videoInfo.audioSampleRate.empty()) {
        try {
          int sampleRate = std::stoi(videoInfo.audioSampleRate) / 1000;
          audioInfo += L" | " + std::to_wstring(sampleRate) + L" kHz";
        } catch (...) {
        }
      }

      if (!videoInfo.audioChannels.empty()) {
        audioInfo += L" | " + videoInfo.audioChannels;
      }
    }

    swprintf_s(info, L"%s | %dx%d | %.2f FPS | %02d:%02d:%02d | Audio: %s",
               videoInfo.format.c_str(), videoInfo.width, videoInfo.height,
               videoInfo.fps, hours, minutes, seconds, audioInfo.c_str());

    SetWindowTextW(hFileInfo, info);

    double startPreviewTime = (std::min)(0.5, videoInfo.duration * 0.1);
    double endPreviewTime = (std::max)(0.0, videoInfo.duration - 0.5);

    RequestPreviewUpdate(hwnd, ID_PREVIEW_START, filename, startPreviewTime);
    RequestPreviewUpdate(hwnd, ID_PREVIEW_END, filename, endPreviewTime);
  } else {
    if (hAudioSilence) {
      SendMessageW(hAudioSilence, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(hAudioSilence, FALSE);
    }

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

  bool addSilentAudio =
    hAudioSilence &&
    SendMessageW(hAudioSilence, BM_GETCHECK, 0, 0) == BST_CHECKED;

std::wstring command;

if (addSilentAudio) {
    command = L"\"" + ffmpeg +
              L"\" -y -ss " +
              FormatSecondsDot(startTime) +
              L" -i \"" + input +
              L"\" -f lavfi -i anullsrc=channel_layout=mono:sample_rate=44100"
              L" -t " +
              FormatSecondsDot(duration) +
              L" -map 0:v:0 -map 1:a:0"
              L" -c:v copy -c:a aac -b:a 64k"
              L" -progress pipe:1 -nostats \"" + output + L"\"";
} else {
    command = L"\"" + ffmpeg +
              L"\""
              L" -y -ss " +
              FormatSecondsDot(startTime) + L" -i \"" + input +
              L"\""
              L" -t " +
              FormatSecondsDot(duration) +
              L" -c copy -progress pipe:1 -nostats \"" + output +
              L"\"";
}

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
    SetWindowTextW(hStatus,
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

  auto outputPath =
      std::filesystem::absolute(output, pathError).lexically_normal();

  if (pathError) {
    SetWindowTextW(hStatus, L"Некорректный путь выходного файла");
    return false;
  }

  if (_wcsicmp(inputPath.c_str(), outputPath.c_str()) == 0) {
    SetWindowTextW(hStatus, L"Выходной файл не должен совпадать с исходным");
    return false;
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

  std::wstring ffmpeg = ffmpegPath.wstring();

  // Количество дополнительных повторов
  int additionalLoops = loopRepeatCount - 1;

  std::wstring command = L"\"" + ffmpeg + L"\"" + L" -y -stream_loop " +
                         std::to_wstring(additionalLoops) + L" -i \"" + input +
                         L"\"" + L" -c copy -progress pipe:1 -nostats \"" +
                         output + L"\"";

  isCutting = true;
  EnableWindow(hCut, FALSE);

  SendMessageW(hProgress, PBM_SETPOS, 0, 0);
  SetWindowTextW(hStatus, L"Создаётся зацикленное видео...");

  double duration = loopResultDuration;

  cutThread = std::thread([command, ffmpeg, hwnd, duration]() {
    RunCutProcess(command, ffmpeg, hwnd, duration);
  });

  return true;
}

struct RtfStreamData {
  const std::string *data;
  size_t position = 0;
};

DWORD CALLBACK ReadRtfStream(DWORD_PTR cookie, LPBYTE buffer, LONG bufferSize,
                             LONG *bytesRead) {
  auto *stream = reinterpret_cast<RtfStreamData *>(cookie);

  size_t remaining = stream->data->size() - stream->position;
  size_t count = (std::min)(remaining, static_cast<size_t>(bufferSize));

  if (count > 0) {
    memcpy(buffer, stream->data->data() + stream->position, count);
  }

  stream->position += count;
  *bytesRead = static_cast<LONG>(count);

  return 0;
}

void SetRichText(HWND control, const std::string &rtf) {
  RtfStreamData data{&rtf, 0};

  EDITSTREAM stream{};
  stream.dwCookie = reinterpret_cast<DWORD_PTR>(&data);
  stream.pfnCallback = ReadRtfStream;

  SendMessageW(control, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream));
}

std::string ConvertMarkdownToRtf(const std::wstring &markdown);

void LoadHelpText() {
  const std::filesystem::path readmePath =
      std::filesystem::path(GetExecutablePath()) / L"README.md";

  std::ifstream file(readmePath, std::ios::binary);

  if (!file.is_open()) {
    SetWindowTextW(hHelpText,
                   L"Не удалось открыть README.md.\r\n\r\n"
                   L"Убедитесь, что файл находится рядом с VideoCutter.exe.");
    return;
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
      static_cast<unsigned char>(content[1]) == 0xBB &&
      static_cast<unsigned char>(content[2]) == 0xBF) {
    content.erase(0, 3);
  }

  if (content.empty()) {
    SetWindowTextW(hHelpText, L"Файл README.md пуст.");
    return;
  }

  const int length = MultiByteToWideChar(
      CP_UTF8, 0, content.data(), static_cast<int>(content.size()), nullptr, 0);

  if (length <= 0) {
    SetWindowTextW(hHelpText, L"Не удалось прочитать текст README.md.");
    return;
  }

  std::wstring text(length, L'\0');

  MultiByteToWideChar(CP_UTF8, 0, content.data(),
                      static_cast<int>(content.size()), text.data(), length);

  std::string rtf = ConvertMarkdownToRtf(text);
  SetRichText(hHelpText, rtf);
}

std::string RtfEscape(const std::wstring &text) {
  std::string result;

  for (wchar_t ch : text) {
    if (ch == L'\\')
      result += "\\\\";
    else if (ch == L'{')
      result += "\\{";
    else if (ch == L'}')
      result += "\\}";
    else if (ch == L'\t')
      result += "\\tab ";
    else if (ch >= 32 && ch <= 126)
      result += static_cast<char>(ch);
    else {
      short value = static_cast<short>(ch);
      result += "\\u" + std::to_string(value) + "?";
    }
  }

  return result;
}

struct HelpLink {
  std::wstring label;
  std::wstring url;
};

std::vector<HelpLink> helpLinks;

void AppendInlineMarkdown(std::string &rtf, const std::wstring &text) {
  size_t i = 0;

  while (i < text.size()) {
    if (text.compare(i, 2, L"**") == 0) {
      size_t end = text.find(L"**", i + 2);

      if (end != std::wstring::npos) {
        rtf += "\\b ";
        rtf += RtfEscape(text.substr(i + 2, end - i - 2));
        rtf += "\\b0 ";
        i = end + 2;
        continue;
      }
    }

    if (text[i] == L'*') {
      size_t end = text.find(L'*', i + 1);

      if (end != std::wstring::npos) {
        rtf += "\\i ";
        rtf += RtfEscape(text.substr(i + 1, end - i - 1));
        rtf += "\\i0 ";
        i = end + 1;
        continue;
      }
    }

    if (text[i] == L'`') {
      size_t end = text.find(L'`', i + 1);

      if (end != std::wstring::npos) {
        rtf += "\\f1\\cf6 ";
        rtf += RtfEscape(text.substr(i + 1, end - i - 1));
        rtf += "\\f0\\cf1 ";
        i = end + 1;
        continue;
      }
    }

    if (text[i] == L'[') {
      size_t close = text.find(L']', i + 1);

      if (close != std::wstring::npos && close + 1 < text.size() &&
          text[close + 1] == L'(') {
        size_t end = text.find(L')', close + 2);

        if (end != std::wstring::npos) {
          std::wstring label = text.substr(i + 1, close - i - 1);
          std::wstring url = text.substr(close + 2, end - close - 2);
          helpLinks.push_back({label, url});

          rtf += "{\\field{\\*\\fldinst HYPERLINK \"";
          rtf += RtfEscape(url);
          rtf += "\"}{\\fldrslt{\\ul\\cf2 ";
          rtf += RtfEscape(label);
          rtf += "}}}";

          i = end + 1;
          continue;
        }
      }
    }

    rtf += RtfEscape(text.substr(i, 1));
    ++i;
  }
}

std::string ConvertMarkdownToRtf(const std::wstring &markdown) {
  helpLinks.clear();
  std::string rtf = "{\\rtf1\\ansi\\deff0\\uc1"
                    "{\\fonttbl"
                    "{\\f0\\fnil\\fcharset204 Segoe UI;}"
                    "{\\f1\\fmodern\\fcharset204 Consolas;}"
                    "}"
                    "{\\colortbl;"
                    "\\red220\\green215\\blue186;"
                    "\\red126\\green156\\blue216;"
                    "\\red166\\green166\\blue156;"
                    "\\red54\\green54\\blue70;"
                    "\\red127\\green180\\blue202;"
                    "\\red152\\green187\\blue108;"
                    "}"
                    "\\viewkind4\\pard\\f0\\fs22\\cf1 ";

  std::wistringstream stream(markdown);
  std::wstring line;
  bool inCodeBlock = false;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == L'\r')
      line.pop_back();

    // Начало или конец блока кода
    if (line.rfind(L"```", 0) == 0) {
      if (!inCodeBlock) {
        rtf += "\\pard\\sb120\\sa120\\li180"
               "\\f1\\fs19\\cf6 ";
        inCodeBlock = true;
      } else {
        rtf += "\\f0\\fs22\\cf1"
               "\\pard\\sa120 ";
        inCodeBlock = false;
      }

      continue;
    }

    if (inCodeBlock) {
      rtf += RtfEscape(line);
      rtf += "\\line ";
      continue;
    }

    if (line.empty()) {
      rtf += "\\par\\sa100 ";
      continue;
    }

    // Горизонтальный разделитель
    if (line.find(L"---") == 0 || line.find(L"___") == 0 ||
        line.find(L"***") == 0) {
      rtf += "\\pard\\brdrb\\brdrs\\brdrcf3"
             "\\brdrw10\\sa200\\par ";
      continue;
    }

    // Заголовки Markdown
    int headingLevel = 0;

    while (headingLevel < static_cast<int>(line.size()) && headingLevel < 6 &&
           line[headingLevel] == L'#') {
      ++headingLevel;
    }

    if (headingLevel > 0 && headingLevel < static_cast<int>(line.size()) &&
        line[headingLevel] == L' ') {
      std::wstring heading = line.substr(headingLevel + 1);

      int fontSize = headingLevel == 1   ? 34
                     : headingLevel == 2 ? 29
                     : headingLevel == 3 ? 25
                                         : 22;

      rtf += "\\pard\\sb220\\sa120\\keepn\\b"
             "\\cf2\\fs" +
             std::to_string(fontSize) + " ";

      AppendInlineMarkdown(rtf, heading);

      rtf += "\\b0\\cf1\\fs22\\par ";
      continue;
    }

    // Маркированный список
    if (line.rfind(L"-   ", 0) == 0 || line.rfind(L"- ", 0) == 0) {
      size_t offset = line.rfind(L"-   ", 0) == 0 ? 4 : 2;

      rtf += "\\pard\\li360\\fi-220\\sa60"
             "\\cf5\\bullet\\tab\\cf1 ";

      AppendInlineMarkdown(rtf, line.substr(offset));

      rtf += "\\par ";
      continue;
    }

    // Нумерованный список
    size_t numberEnd = line.find(L".  ");

    if (numberEnd != std::wstring::npos && numberEnd > 0 &&
        std::all_of(line.begin(), line.begin() + numberEnd,
                    [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) {
      rtf += "\\pard\\li360\\fi-220\\sa60\\cf5 ";
      rtf += RtfEscape(line.substr(0, numberEnd + 1));
      rtf += "\\tab\\cf1 ";

      AppendInlineMarkdown(rtf, line.substr(numberEnd + 3));

      rtf += "\\par ";
      continue;
    }

    // Обычный текст
    rtf += "\\pard\\sa100\\sl276\\slmult1\\cf1 ";
    AppendInlineMarkdown(rtf, line);
    rtf += "\\par ";
  }

  rtf += "}";

  return rtf;
}

} // namespace

static int helpScrollAccumulator = 0;

LRESULT CALLBACK HelpTextSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam, UINT_PTR, DWORD_PTR) {
  if (message == WM_MOUSEWHEEL) {
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);

    helpScrollAccumulator -= delta;

    int lines = helpScrollAccumulator / WHEEL_DELTA;
    helpScrollAccumulator %= WHEEL_DELTA;

    if (lines != 0) {
      SendMessageW(hwnd, EM_LINESCROLL, 0, lines * 3);
    }

    return 0;
  }

  return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  switch (message) {

  case WM_LBUTTONDOWN: {
    if (videoInfo.duration <= 0.0)
      return 0;

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

    LayoutMetrics m =
        CalculateLayout(clientRect.right, clientRect.bottom, GetActiveMode());

    RECT hitRect = m.timelineRect;
    hitRect.top -= 15;
    hitRect.bottom += 15;

    POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

    if (!PtInRect(&hitRect, cursor))
      return 0;

    int startX = TimelineTimeToX(selectedStartTime, m.timelineRect);

    int endX = TimelineTimeToX(selectedEndTime, m.timelineRect);

    int startDistance = std::abs(cursor.x - startX);
    int endDistance = std::abs(cursor.x - endX);

    draggedMarker = startDistance <= endDistance ? 1 : 2;

    isDraggingTimeline = true;
    SetCapture(hwnd);

    return 0;
  }

  case WM_MOUSEMOVE: {
    if (!isDraggingTimeline || !draggedMarker)
      return 0;

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

    LayoutMetrics m =
        CalculateLayout(clientRect.right, clientRect.bottom, GetActiveMode());

    double time = TimelineXToTime(GET_X_LPARAM(lParam), m.timelineRect);

    constexpr double minInterval = 1.0;

    if (draggedMarker == 1) {
      selectedStartTime = (std::min)(time, selectedEndTime - minInterval);

      selectedStartTime = (std::max)(0.0, selectedStartTime);
    } else {
      selectedEndTime = (std::max)(time, selectedStartTime + minInterval);

      selectedEndTime = (std::min)(videoInfo.duration, selectedEndTime);
    }

    SetWindowTextW(hStart, FormatTime(selectedStartTime).c_str());

    SetWindowTextW(hEnd, FormatTime(selectedEndTime).c_str());

    RECT updateRect = m.timelineRect;

    updateRect.left -= 15;
    updateRect.top -= 20;
    updateRect.right += 15;
    updateRect.bottom = m.durationPos.y + 30;

    InvalidateRect(hwnd, &updateRect, FALSE);

    return 0;
  }

  case WM_LBUTTONUP: {
    if (!isDraggingTimeline)
      return 0;

    isDraggingTimeline = false;
    draggedMarker = 0;

    ReleaseCapture();

    std::wstring inputPath = GetControlText(hInput);

    if (!inputPath.empty()) {
      RequestPreviewUpdate(hwnd, ID_PREVIEW_START, inputPath,
                           selectedStartTime);

      RequestPreviewUpdate(hwnd, ID_PREVIEW_END, inputPath,
                           (std::max)(0.0, selectedEndTime - 0.5));
    }

    SetWindowTextW(hStatus, L"Временной диапазон корректен");

    return 0;
  }

  case WM_CAPTURECHANGED: {
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

    // Help text control
    LoadLibraryW(L"Msftedit.dll");

    hHelpText = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                                WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                    ES_READONLY | ES_AUTOVSCROLL,
                                0, 0, 100, 100, hwnd, nullptr, hInst, nullptr);

    SetWindowSubclass(hHelpText, HelpTextSubclassProc, 1, 0);

    SendMessageW(hHelpText, EM_SETEVENTMASK, 0, ENM_LINK);

    SendMessageW(hHelpText, EM_SETBKGNDCOLOR, 0, Theme::Panel);

    SendMessageW(hHelpText, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    ShowWindow(hHelpText, SW_HIDE);

    LoadHelpText();

    // Форматирование текста в стиле Kanagawa Wave

    // Mode tabs
    hTabCut = CreateButton(hwnd, ID_TAB_CUT, L"Нарезка видео");
    hTabLoop = CreateButton(hwnd, ID_TAB_LOOP, L"Зацикливание");
    hTabHelp = CreateButton(hwnd, ID_TAB_HELP, L"Помощь");

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

    hAudioSilence = CreateWindowW(
    L"BUTTON", L"",
    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
    0, 0, 22, 25, hwnd,
    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_AUDIO_SILENCE)),
    hInst, nullptr);

hAudioSilenceLabel = CreateWindowW(
    L"STATIC", L"Добавить пустую аудиодорожку",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    0, 0, 300, 25, hwnd,
    nullptr, hInst, nullptr);

    

SetFont(hAudioSilence, hFont);
SetFont(hAudioSilenceLabel, hFont);


EnableWindow(hAudioSilence, FALSE);

    SetFont(hAudioSilence, hFont);
    EnableWindow(hAudioSilence, FALSE);

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

    if (hCtrl == hFileInfo || hCtrl == hAudioSilenceLabel) {
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

    if (dis->CtlID == ID_BROWSE || dis->CtlID == ID_SAVE ||
        dis->CtlID == ID_CUT || dis->CtlID == ID_TAB_CUT ||
        dis->CtlID == ID_TAB_LOOP || dis->CtlID == ID_TAB_HELP ||
        dis->CtlID == ID_LOOP_DURATION || dis->CtlID == ID_LOOP_SIZE) {
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

  case WM_NOTIFY: {
    auto *header = reinterpret_cast<NMHDR *>(lParam);

    if (header && header->hwndFrom == hHelpText && header->code == EN_LINK) {
      auto *link = reinterpret_cast<ENLINK *>(lParam);

      if (link->msg == WM_LBUTTONUP) {
        int length = link->chrg.cpMax - link->chrg.cpMin;

        if (length > 0) {
          std::wstring clickedText(length + 1, L'\0');

          TEXTRANGEW range{};
          range.chrg = link->chrg;
          range.lpstrText = clickedText.data();

          SendMessageW(hHelpText, EM_GETTEXTRANGE, 0,
                       reinterpret_cast<LPARAM>(&range));

          clickedText.resize(length);

          for (const auto &item : helpLinks) {
            if (item.url == clickedText) {
              if (item.url.rfind(L"https://", 0) == 0 ||
                  item.url.rfind(L"http://", 0) == 0) {
                SHELLEXECUTEINFOW sei{};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_FLAG_NO_UI;
                sei.lpVerb = L"open";
                sei.lpFile = item.url.c_str();
                sei.nShow = SW_SHOWNORMAL;

                if (!ShellExecuteExW(&sei)) {
                  DWORD error = GetLastError();

                  MessageBoxW(hwnd, std::to_wstring(error).c_str(),
                              L"Ошибка открытия ссылки", MB_OK | MB_ICONERROR);
                }
              }

              break;
            }
          }
        }

        return 0;
      }
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

    case ID_TAB_HELP:
      SetActiveMode(AppMode::Help);
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
      } else {
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
      if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == EN_KILLFOCUS) {
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
    DrawRoundedPanel(graphics, m.leftPanel, 18.0f, Theme::Panel, Theme::Border);

    if (mode == AppMode::Cut) {
      DrawRoundedPanel(graphics, m.rightTopPanel, 18.0f, Theme::Panel,
                       Theme::Border);

      DrawRoundedPanel(graphics, m.rightBottomPanel, 18.0f, Theme::Panel,
                       Theme::Border);
    }

    if (mode != AppMode::Help) {
      DrawRoundedPanel(graphics, m.bottomPanel, 18.0f, Theme::Panel,
                       Theme::Border);
    }

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
    } else if (mode == AppMode::Loop) {
      TextOutW(memDC, m.subtitlePos.x, m.subtitlePos.y,
               L"Создание зацикленного видео", 28);
    } else {
      TextOutW(memDC, m.subtitlePos.x, m.subtitlePos.y, L"Краткая инструкция",
               19);
    }

    // Section headings

    if (mode != AppMode::Help) {

      SetTextColor(memDC, Theme::Text);
      TextOutW(memDC, m.srcLabelPos.x, m.srcLabelPos.y, L"Исходный видеофайл",
               18);
      TextOutW(memDC, m.dstLabelPos.x, m.dstLabelPos.y, L"Выходной файл", 14);
      if (mode == AppMode::Loop) {
        SetTextColor(memDC, Theme::Text);
        TextOutW(memDC, m.leftPanel.left + 20, m.loopDurationRect.top - 32,
                 L"Параметры зацикливания", 23);

        // Estimated result
        std::wstring targetLabel =
            GetLoopTargetMode() == LoopTargetMode::Duration
                ? L"Целевая длительность (ЧЧ:ММ:СС)"
                : L"Целевой размер (МБ)";

        SetTextColor(memDC, Theme::TextMuted);
        TextOutW(memDC, m.loopTargetRect.left, m.loopTargetRect.top - 20,
                 targetLabel.c_str(), static_cast<int>(targetLabel.length()));

        // Estimated result
        // Estimated result
        int resultAreaLeft = m.loopTargetRect.right + 24;
        int resultAreaRight = m.leftPanel.right - 24;
        int resultBlockWidth = 300;
        int resultLeftX =
            resultAreaLeft +
            (resultAreaRight - resultAreaLeft - resultBlockWidth) / 2;

        int resultY = m.loopTargetRect.top - 80;

        HFONT resultFont = Theme::CreateUIFont(21, FW_NORMAL);
        HGDIOBJ oldResultFont = SelectObject(memDC, resultFont);

        SetTextColor(memDC, Theme::TextMuted);

        auto DrawCenteredResult = [&](const std::wstring &text, int y) {
          TextOutW(memDC, resultLeftX, y, text.c_str(),
                   static_cast<int>(text.length()));
        };

        std::wstring durationLine;
        std::wstring sizeLine;
        std::wstring repeatsLine;

        if (loopCalculationValid) {
          durationLine = L"Длительность: " + FormatTime(loopResultDuration);

          wchar_t sizeText[128]{};
          swprintf_s(sizeText, L"Размер файла: %.1f МБ",
                     static_cast<double>(loopResultSize) / (1024.0 * 1024.0));

          wchar_t repeatsText[128]{};
          swprintf_s(repeatsText, L"Количество повторов: %d", loopRepeatCount);

          sizeLine = sizeText;
          repeatsLine = repeatsText;
        } else {
          durationLine = L"Длительность: —";
          sizeLine = L"Размер файла: —";
          repeatsLine = L"Количество повторов: —";
        }

        SetTextColor(memDC, Theme::Text);
        DrawCenteredResult(L"Расчётный результат", resultY);

        SetTextColor(memDC, Theme::TextMuted);
        DrawCenteredResult(durationLine, resultY + 34);
        DrawCenteredResult(sizeLine, resultY + 62);
        DrawCenteredResult(repeatsLine, resultY + 90);

        SelectObject(memDC, oldResultFont);
        DeleteObject(resultFont);
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
    // Blit only the invalidated area
    BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
           ps.rcPaint.right - ps.rcPaint.left,
           ps.rcPaint.bottom - ps.rcPaint.top, memDC, ps.rcPaint.left,
           ps.rcPaint.top, SRCCOPY);

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
