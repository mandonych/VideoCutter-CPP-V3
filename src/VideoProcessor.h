#pragma once

#include <windows.h>
#include <string>
#include <cstdint>

constexpr UINT WM_CUT_FINISHED  = WM_APP + 1;
constexpr UINT WM_CUT_PROGRESS  = WM_APP + 2;
constexpr UINT WM_PREVIEW_READY = WM_APP + 3;

namespace Gdiplus {
    class Bitmap;
}

struct PreviewResult {
    int controlId = 0;
    uint64_t requestId = 0;
    Gdiplus::Bitmap* bitmap = nullptr;
};

Gdiplus::Bitmap* ExtractFrame(
    const std::wstring& filename,
    double timeSeconds
);

void ExtractFrameAsync(
    HWND hwnd,
    int controlId,
    uint64_t requestId,
    std::wstring filename,
    double timeSeconds
);

void RunCutProcess(
    const std::wstring& command,
    const std::wstring& ffmpeg,
    HWND hwnd,
    double duration
);
