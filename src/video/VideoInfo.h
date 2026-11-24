#pragma once

#include <QString>

struct VideoInfo {
    double duration = 0.0;
    double fps = 25.0;
    int width = 0;
    int height = 0;
    QString format;

    bool hasAudio = false;
    QString audioCodec;
    QString audioBitrate;
    QString audioSampleRate;
    QString audioChannels;
};

bool ProbeVideo(const QString &filename, VideoInfo &info);
