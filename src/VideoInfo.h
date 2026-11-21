#pragma once

#include <string>

struct VideoInfo {
    double duration = 0.0;
    double fps = 25.0;
    int width = 0;
    int height = 0;
    std::wstring format;

    bool hasAudio = false;
    std::wstring audioCodec;
    std::wstring audioBitrate;
    std::wstring audioSampleRate;
    std::wstring audioChannels;
};

bool ProbeVideo(
    const std::wstring& filename,
    VideoInfo& info
);