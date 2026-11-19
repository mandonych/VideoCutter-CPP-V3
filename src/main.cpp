#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "VideoInfo.h"
#include "App.h"
#include "Theme.h"


VideoInfo videoInfo;
ULONG_PTR gdiplusToken = 0;

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show)
{
    Gdiplus::GdiplusStartupInput gdiplusInput;

    if (Gdiplus::GdiplusStartup(
            &gdiplusToken,
            &gdiplusInput,
            nullptr) != Gdiplus::Ok)
    {
        return 1;
    }
    const wchar_t CLASS_NAME[] = L"VideoCutterWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    if (!RegisterClassW(&wc))
        return 1;

    
    // Fixed window size: 1100 x 760 client area
    const DWORD windowStyle =
        WS_OVERLAPPED |
        WS_CAPTION |
        WS_SYSMENU |
        WS_MINIMIZEBOX;

    RECT windowRect{
    0, 0,
    Theme::WindowWidth,
    Theme::WindowHeight
};

    AdjustWindowRectEx(
        &windowRect,
        windowStyle,
        FALSE,
        0
    );

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Video Cutter",
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        nullptr
    );


    if (!hwnd)
        return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};

    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

        Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}

