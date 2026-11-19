#pragma once

#include <string>

struct VideoInfo {
    double duration = 0.0;
    double fps = 25.0;
    int width = 0;
    int height = 0;
    std::wstring format;
};

bool ProbeVideo(
    const std::wstring& filename,
    VideoInfo& info
);