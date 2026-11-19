#include "App.h"
#include "UI.h"
#include "Theme.h"
#include "VideoInfo.h"
#include "VideoProcessor.h"

#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>
#include <thread>
#include <atomic>

#include <commdlg.h>
#include <filesystem>
#include <algorithm>
#include <cwchar>
#include <gdiplus.h>

extern VideoInfo videoInfo;
using namespace Gdiplus;

static void DrawRoundedPanel(
    Graphics &graphics,
    float x,
    float y,
    float width,
    float height,
    float radius,
    COLORREF fillColor,
    COLORREF borderColor)
{
    GraphicsPath path;

    const float diameter = radius * 2.0f;

    path.AddArc(x, y, diameter, diameter, 180, 90);
    path.AddArc(
        x + width - diameter,
        y,
        diameter,
        diameter,
        270,
        90);

    path.AddArc(
        x + width - diameter,
        y + height - diameter,
        diameter,
        diameter,
        0,
        90);

    path.AddArc(
        x,
        y + height - diameter,
        diameter,
        diameter,
        90,
        90);

    path.CloseFigure();

    SolidBrush brush(Color(
        255,
        GetRValue(fillColor),
        GetGValue(fillColor),
        GetBValue(fillColor)));

    Pen pen(Color(
                255,
                GetRValue(borderColor),
                GetGValue(borderColor),
                GetBValue(borderColor)),
            1.5f);

    graphics.FillPath(&brush, &path);
    graphics.DrawPath(&pen, &path);
}

// Глобальные переменные приложения
HICON hAppIconSmall = nullptr;
HICON hAppIconLarge = nullptr;

Gdiplus::Bitmap *startBitmap = nullptr;
Gdiplus::Bitmap *endBitmap = nullptr;
Gdiplus::Image *appLogo = nullptr;

std::thread cutThread;
std::atomic<bool> isCutting{false};

// Функции из main.cpp
std::wstring GetExecutablePath();

std::wstring GetControlText(HWND hwnd);

void ChooseInput(HWND hwnd);
void ChooseOutput(HWND hwnd);
bool CutVideo();

bool ParseTime(const std::wstring &value, double &result)
{
    int h = 0, m = 0, s = 0;

    if (swscanf_s(value.c_str(), L"%d:%d:%d", &h, &m, &s) != 3)
        return false;

    if (h < 0 || m < 0 || m > 59 || s < 0 || s > 59)
        return false;

    result = h * 3600.0 + m * 60.0 + s;
    return true;
}

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_CUT_PROGRESS:
    {
        SendMessageW(
            hProgress,
            PBM_SETPOS,
            (WPARAM)wParam,
            0);

        return 0;
    }
    case WM_CUT_FINISHED:
    {
        DWORD exitCode = (DWORD)wParam;

        if (cutThread.joinable())
            cutThread.join();

        isCutting = false;
        EnableWindow(hCut, TRUE);

        if (exitCode == 0)
        {
            SendMessageW(hProgress, PBM_SETPOS, 100, 0);
            SetWindowTextW(hStatus, L"Нарезка успешно завершена");

            MessageBoxW(
                hwnd,
                L"Нарезка видео успешно завершена!",
                L"Video Cutter",
                MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            SetWindowTextW(hStatus, L"Ошибка при нарезке видео");

            MessageBoxW(
                hwnd,
                L"При нарезке видео произошла ошибка.",
                L"Video Cutter",
                MB_OK | MB_ICONERROR);
        }

        SetWindowTextW(hStatus, L"");
        SendMessageW(hProgress, PBM_SETPOS, 0, 0);

        return 0;
    }

    case WM_CREATE:
    {
        std::wstring logoPath =
            GetExecutablePath() + L"\\VideoCutter_icon_256.png";

        appLogo = Gdiplus::Image::FromFile(logoPath.c_str());
        // Иконка приложения
        std::wstring iconPath = GetExecutablePath() + L"\\VideoCutter.ico";

        hAppIconSmall = (HICON)LoadImageW(
            nullptr,
            iconPath.c_str(),
            IMAGE_ICON,
            24, 24,
            LR_LOADFROMFILE);

        hAppIconLarge = (HICON)LoadImageW(
            nullptr,
            iconPath.c_str(),
            IMAGE_ICON,
            48, 48,
            LR_LOADFROMFILE);

        if (hAppIconSmall)
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIconSmall);

        if (hAppIconLarge)
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIconLarge);
        INITCOMMONCONTROLSEX icex{};
        icex.dwSize = sizeof(icex);
        icex.dwICC = ICC_PROGRESS_CLASS;
        InitCommonControlsEx(&icex);
        hBgBrush = CreateSolidBrush(BG_COLOR);
        hPanelBrush = CreateSolidBrush(PANEL_COLOR);
        hEditBrush = CreateSolidBrush(PANEL_COLOR);

        hFont = CreateFontW(
            20, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI");

        hTimeFont = CreateFontW(
            37, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI");

        hTitleFont = CreateFontW(
            46, 0, 0, 0, FW_BOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI");

        // Source file

        hInput = CreateEdit(hwnd, ID_INPUT);
        hFileInfo = CreateLabel(hwnd, L"");
        hBrowse = CreateButton(hwnd, ID_BROWSE, L"Обзор...");

        // Time range

        hStart = CreateEdit(hwnd, ID_START, L"00:00:00");
        hEnd = CreateEdit(hwnd, ID_END, L"00:00:10");
        SetFont(hStart, hTimeFont);
        SetFont(hEnd, hTimeFont);

        // Preview placeholders
        hPreviewStart = CreateWindowW(
            L"BUTTON", L"Начальный кадр",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 100,
            hwnd,
            (HMENU)(INT_PTR)ID_PREVIEW_START,
            nullptr, nullptr);

        hPreviewEnd = CreateWindowW(
            L"BUTTON", L"Конечный кадр",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 100, 100,
            hwnd,
            (HMENU)(INT_PTR)ID_PREVIEW_END,
            nullptr, nullptr);

        // Output

        hOutput = CreateEdit(hwnd, ID_OUTPUT);
        hSave = CreateButton(hwnd, ID_SAVE, L"Сохранить...");

        // Bottom
        hStatus = CreateLabel(
            hwnd,
            L"Выберите исходное видео");
            SetWindowLongPtrW(
    hStatus,
    GWL_STYLE,
    GetWindowLongPtrW(hStatus, GWL_STYLE) | SS_RIGHT);

        hCut = CreateButton(
            hwnd,
            ID_CUT,
            L"Обрезать видео");

        hProgress = CreateWindowExW(
            0,
            PROGRESS_CLASSW,
            nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            0, 0, 100, 20,
            hwnd,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);

        SendMessageW(
            hProgress,
            PBM_SETBKCOLOR,
            0,
            (LPARAM)Theme::PanelSecondary);

        SendMessageW(
            hProgress,
            PBM_SETBARCOLOR,
            0,
            (LPARAM)Theme::Accent);

        SendMessageW(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(hProgress, PBM_SETPOS, 0, 0);

        EnableWindow(hCut, FALSE);

        Layout(hwnd);
        return 0;
    }

    case WM_SIZE:
        Layout(hwnd);
        return 0;

 case WM_CTLCOLORSTATIC:
{
    HDC dc = (HDC)wParam;
    HWND hCtrl = (HWND)lParam;

    if (hCtrl == hStatus)
{
    SetTextColor(dc, RGB(255, 160, 102));
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, BG_COLOR);

    return (LRESULT)hBgBrush;
}

    if (hCtrl == hFileInfo)
    {
        SetTextColor(dc, RGB(118, 148, 106)); // Autumn Green #76946A
        SetBkMode(dc, TRANSPARENT);

        return (LRESULT)hPanelBrush;
    }

    SetTextColor(dc, TEXT_COLOR);
    SetBkMode(dc, TRANSPARENT);

    return (LRESULT)hPanelBrush;
}

    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, TEXT_COLOR);
        SetBkColor(dc, PANEL_COLOR);
        return (LRESULT)hEditBrush;
    }

    case WM_CTLCOLORBTN:
        return (LRESULT)hBgBrush;

    case WM_DRAWITEM:
    {
        auto dis = (DRAWITEMSTRUCT *)lParam;

        if (dis->CtlID == ID_BROWSE ||
            dis->CtlID == ID_SAVE ||
            dis->CtlID == ID_CUT)
        {
            DrawButton(dis);
            return TRUE;
        }

        if (dis->CtlID == ID_PREVIEW_START ||
            dis->CtlID == ID_PREVIEW_END)
        {
            // Фон области предпросмотра
            FillRect(dis->hDC, &dis->rcItem, hPanelBrush);

            Gdiplus::Bitmap *bitmap =
                dis->CtlID == ID_PREVIEW_START
                    ? startBitmap
                    : endBitmap;

            if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok)
            {
                Gdiplus::Graphics graphics(dis->hDC);

                // Внутренний отступ от рамки
                constexpr int inset = 3;

                int width =
                    dis->rcItem.right - dis->rcItem.left - inset * 2;

                int height =
                    dis->rcItem.bottom - dis->rcItem.top - inset * 2;

                float scaleX = (float)width / bitmap->GetWidth();
                float scaleY = (float)height / bitmap->GetHeight();
                float scale = (std::min)(scaleX, scaleY);

                int drawWidth = (int)(bitmap->GetWidth() * scale);
                int drawHeight = (int)(bitmap->GetHeight() * scale);

                int x = dis->rcItem.left + inset +
                        (width - drawWidth) / 2;

                int y = dis->rcItem.top + inset +
                        (height - drawHeight) / 2;

                graphics.DrawImage(bitmap, x, y, drawWidth, drawHeight);
            }
            else
            {
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, MUTED_COLOR);

                RECT textRect = dis->rcItem;

                DrawTextW(
                    dis->hDC,
                    dis->CtlID == ID_PREVIEW_START
                        ? L"Начальный кадр"
                        : L"Конечный кадр",
                    -1,
                    &textRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            // Рамка поверх изображения
            HPEN pen = CreatePen(
                PS_SOLID,
                2,
                Theme::AccentSecondary);

            HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
            HGDIOBJ oldBrush = SelectObject(
                dis->hDC,
                GetStockObject(NULL_BRUSH));

            Rectangle(
                dis->hDC,
                dis->rcItem.left + 1,
                dis->rcItem.top + 1,
                dis->rcItem.right - 1,
                dis->rcItem.bottom - 1);

            SelectObject(dis->hDC, oldPen);
            SelectObject(dis->hDC, oldBrush);

            DeleteObject(pen);

            return TRUE;
        }

        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_BROWSE:
            ChooseInput(hwnd);
            return 0;

        case ID_SAVE:
            ChooseOutput(hwnd);
            return 0;

        case ID_CUT:
            CutVideo();
            return 0;

        case ID_START:
        case ID_END:
            if (HIWORD(wParam) == EN_KILLFOCUS)
            {
                wchar_t timeText[32]{};
                GetWindowTextW(
                    (HWND)lParam,
                    timeText,
                    32);

                int hours = 0;
                int minutes = 0;
                int seconds = 0;

                if (swscanf_s(
                        timeText,
                        L"%d:%d:%d",
                        &hours,
                        &minutes,
                        &seconds) != 3)
                {
                    return 0;
                }

                if (hours < 0 || minutes < 0 ||
                    minutes > 59 || seconds < 0 ||
                    seconds > 59)
                {
                    return 0;
                }

                double timeSeconds =
                    hours * 3600.0 +
                    minutes * 60.0 +
                    seconds;

                double startTime = 0.0;
                double endTime = 0.0;

                if (!ParseTime(GetControlText(hStart), startTime) ||
                    !ParseTime(GetControlText(hEnd), endTime))
                {
                    SetWindowTextW(hStatus, L"Неверный формат времени");
                    return 0;
                }

                if (startTime >= endTime)
                {
                    SetWindowTextW(hStatus, L"Начало должно быть раньше конца");
                    return 0;
                }

                if (videoInfo.duration > 0 &&
                    endTime > videoInfo.duration)
                {
                    SetWindowTextW(hStatus, L"Конец выходит за длительность видео");
                    return 0;
                }

                SetWindowTextW(hStatus, L"Временной диапазон корректен");

                if (LOWORD(wParam) == ID_END)
                {
                    timeSeconds = (std::max)(0.0,
                                             timeSeconds - 0.5);
                }

                Gdiplus::Bitmap *newBitmap =
                    ExtractFrame(
                        GetControlText(hInput),
                        timeSeconds);

                if (newBitmap)
                {
                    if (LOWORD(wParam) == ID_START)
                    {
                        delete startBitmap;
                        startBitmap = newBitmap;
                        InvalidateRect(hPreviewStart, nullptr, TRUE);
                    }
                    else
                    {
                        delete endBitmap;
                        endBitmap = newBitmap;
                        InvalidateRect(hPreviewEnd, nullptr, TRUE);
                    }
                }
            }
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        RECT r;
        GetClientRect(hwnd, &r);

        Graphics graphics(dc);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);

        graphics.Clear(Color(
            255,
            GetRValue(Theme::Background),
            GetGValue(Theme::Background),
            GetBValue(Theme::Background)));

        // =====================================
        // Main panels
        // =====================================

        // Left panel: source and output
        DrawRoundedPanel(
            graphics,
            20, 185,
            590, 375,
            18,
            Theme::Panel,
            Theme::Border);

        // Right panel: start preview
        DrawRoundedPanel(
            graphics,
            625, 185,
            455, 180,
            18,
            Theme::Panel,
            Theme::Border);

        // Right panel: end preview
        DrawRoundedPanel(
            graphics,
            625, 380,
            455, 180,
            18,
            Theme::Panel,
            Theme::Border);

        // Bottom panel
        DrawRoundedPanel(graphics, 20, 595, 1060, 95, 18, Theme::Panel, Theme::Border);

        // =====================================
        // Header
        // =====================================

        SolidBrush accentBrush(Color(
            255,
            GetRValue(Theme::Accent),
            GetGValue(Theme::Accent),
            GetBValue(Theme::Accent)));

        graphics.FillRectangle(
            &accentBrush,
            34, 52, 5, 75);

        SelectObject(dc, hTitleFont);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, Theme::Text);

        TextOutW(
            dc,
            60, 38,
            L"Video Cutter",
            12);

        SelectObject(dc, hFont);

        SetTextColor(dc, Theme::TextMuted);

        TextOutW(
            dc,
            60, 112,
            L"Быстрая обрезка без перекодирования",
            35);

        // =====================================
        // Section headings
        // =====================================

        SetTextColor(dc, Theme::Text);

        TextOutW(dc, 90, 205, L"Исходный видеофайл", 18);
        TextOutW(dc, 90, 415, L"Выходной файл", 14);

        TextOutW(dc, 895, 215, L"Начало", 6);
        TextOutW(dc, 895, 420, L"Конец", 5);

        // =====================================
        // Logo
        // =====================================

        if (appLogo &&
            appLogo->GetLastStatus() == Gdiplus::Ok)
        {
            graphics.DrawImage(
                appLogo,
                900, 15,
                145, 125);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        delete appLogo;
        appLogo = nullptr;
        DeleteObject(hBgBrush);
        DeleteObject(hPanelBrush);
        DeleteObject(hEditBrush);
        DeleteObject(hFont);
        DeleteObject(hTitleFont);
        DeleteObject(hTimeFont);

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(
        hwnd, message, wParam, lParam);
}

std::wstring GetExecutablePath()
{
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);

    return std::filesystem::path(buffer)
        .parent_path()
        .wstring();
}

std::wstring GetControlText(HWND control)
{
    int length = GetWindowTextLengthW(control);

    std::wstring text(length + 1, L'\0');

    GetWindowTextW(
        control,
        text.data(),
        length + 1);

    text.resize(length);

    return text;
}

std::wstring MakeOutputName(const std::wstring &input)
{
    size_t slash = input.find_last_of(L"\\/");
    size_t dot = input.find_last_of(L'.');

    if (dot == std::wstring::npos ||
        (slash != std::wstring::npos && dot < slash))
    {
        return input + L"_cut";
    }

    return input.substr(0, dot) +
           L"_cut" + input.substr(dot);
}

void ChooseInput(HWND hwnd)
{
    wchar_t filename[32768] = {};

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        L"Video files\0*.mp4;*.mkv;*.mov;*.avi;*.webm;*.m4v;*.ts\0"
        L"All files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Выберите исходный видеофайл";

    if (GetOpenFileNameW(&ofn))
    {
        SetWindowTextW(hInput, filename);
        SendMessageW(hInput, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(hInput, EM_SCROLLCARET, 0, 0);

        std::wstring output = MakeOutputName(filename);
        SetWindowTextW(hOutput, output.c_str());
        SendMessageW(hOutput, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(hOutput, EM_SCROLLCARET, 0, 0);

        if (ProbeVideo(filename, videoInfo))
        {
            SetWindowTextW(
                hStatus,
                L"Информация о видео загружена");

            EnableWindow(hCut, TRUE);

            int totalSeconds = static_cast<int>(videoInfo.duration + 0.5);

            int hours = totalSeconds / 3600;
            int minutes = (totalSeconds % 3600) / 60;
            int seconds = totalSeconds % 60;

            wchar_t endTime[32];

            swprintf_s(
                endTime,
                L"%02d:%02d:%02d",
                hours,
                minutes,
                seconds);

            SetWindowTextW(hEnd, endTime);
            wchar_t info[512]{};

            swprintf_s(
                info,
                L"%s | %dx%d | %.2f FPS | %02d:%02d:%02d",
                videoInfo.format.c_str(),
                videoInfo.width,
                videoInfo.height,
                videoInfo.fps,
                hours,
                minutes,
                seconds);

            SetWindowTextW(hFileInfo, info);
            delete startBitmap;
            startBitmap = ExtractFrame(filename, 0.5);

            delete endBitmap;
            endBitmap = ExtractFrame(
                filename,
                (std::max)(0.0, videoInfo.duration - 1.0));

            InvalidateRect(hPreviewStart, nullptr, TRUE);
            InvalidateRect(hPreviewEnd, nullptr, TRUE);
        }
        else
        {
            SetWindowTextW(
                hStatus,
                L"Не удалось прочитать информацию о видео");

            SetWindowTextW(hFileInfo, L"Ошибка чтения видео");
            EnableWindow(hCut, FALSE);
        }

        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void ChooseOutput(HWND hwnd)
{
    wchar_t filename[32768] = {};

    GetWindowTextW(hOutput, filename, 32768);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        L"MP4 video\0*.mp4\0"
        L"MKV video\0*.mkv\0"
        L"All files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrTitle = L"Сохранить видеофрагмент";

    if (GetSaveFileNameW(&ofn))
    {
        SetWindowTextW(hOutput, filename);
        SendMessageW(hOutput, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageW(hOutput, EM_SCROLLCARET, 0, 0);
    }
}

bool CutVideo()
{
    if (isCutting)
        return false;

    std::wstring input = GetControlText(hInput);
    std::wstring output = GetControlText(hOutput);

    if (input.empty() || output.empty())
    {
        SetWindowTextW(hStatus, L"Не выбран входной или выходной файл");
        return false;
    }

    double startTime = 0.0;
    double endTime = 0.0;

    if (!ParseTime(GetControlText(hStart), startTime) ||
        !ParseTime(GetControlText(hEnd), endTime) ||
        startTime >= endTime ||
        endTime > videoInfo.duration)
    {
        SetWindowTextW(hStatus, L"Проверьте временной диапазон");
        return false;
    }

    double duration = endTime - startTime;

    std::wstring ffmpeg = GetExecutablePath() + L"\\bin\\ffmpeg.exe";

    std::wstring command =
        L"\"" + ffmpeg + L"\""
                         L" -y -ss " +
        std::to_wstring(startTime) +
        L" -i \"" + input + L"\"" +
        L" -t " + std::to_wstring(duration) +
        L" -c copy -progress pipe:1 -nostats \"" + output + L"\"";

    HWND hwnd = GetParent(hStatus);

    isCutting = true;
    EnableWindow(hCut, FALSE);

    SendMessageW(hProgress, PBM_SETPOS, 0, 0);

    SetWindowTextW(hStatus, L"Выполняется нарезка видео...");

    cutThread = std::thread(
        [command, ffmpeg, hwnd, duration]()
        {
            RunCutProcess(command, ffmpeg, hwnd, duration);
        });

    return true;
}