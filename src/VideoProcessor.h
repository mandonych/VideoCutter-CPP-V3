#pragma once

#include <windows.h>
#include <string>

constexpr UINT WM_CUT_FINISHED = WM_APP + 1;
constexpr UINT WM_CUT_PROGRESS = WM_APP + 2;

namespace Gdiplus {
    class Bitmap;
}

Gdiplus::Bitmap* ExtractFrame(
    const std::wstring& filename,
    double timeSeconds
);

void RunCutProcess(
    const std::wstring& command,
    const std::wstring& ffmpeg,
    HWND hwnd,
    double duration
);