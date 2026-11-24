#include "VideoInfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

namespace {

QString GetProbeValue(const QString &output, const QString &key) {
    const QString prefix = key + "=";
    qsizetype pos = output.indexOf(prefix);
    if (pos == -1)
        return {};

    pos += prefix.length();
    qsizetype end = output.indexOf('\n', pos);
    QString value = (end == -1) ? output.mid(pos) : output.mid(pos, end - pos);
    value = value.trimmed();

    if (value.startsWith('"') && value.endsWith('"') && value.length() >= 2) {
        value = value.mid(1, value.length() - 2);
    }
    return value;
}

double ParseFps(const QString &value) {
    qsizetype slash = value.indexOf('/');
    bool ok = false;

    if (slash != -1) {
        double num = value.left(slash).toDouble(&ok);
        bool okDen = false;
        double den = value.mid(slash + 1).toDouble(&okDen);
        if (ok && okDen && den != 0.0)
            return num / den;
    }

    double fps = value.toDouble(&ok);
    return ok ? fps : 25.0;
}

QString GetStreamProbeValue(const QString &output, const QString &streamType, const QString &key) {
    const QString typeMarker = QStringLiteral(".codec_type=\"%1\"").arg(streamType);
    qsizetype typePos = output.indexOf(typeMarker);
    if (typePos == -1)
        return {};

    qsizetype lineStart = output.lastIndexOf('\n', typePos);
    lineStart = (lineStart == -1) ? 0 : lineStart + 1;

    qsizetype prefixEnd = output.indexOf(QStringLiteral(".codec_type="), lineStart);
    if (prefixEnd == -1)
        return {};

    QString prefix = output.mid(lineStart, prefixEnd - lineStart);
    return GetProbeValue(output, prefix + "." + key);
}

QString GetDisplayFormat(const QString &filename, const QString &originalFormat) {
    QString ext = QFileInfo(filename).suffix().toUpper();
    return ext.isEmpty() ? originalFormat : ext;
}

} // namespace

bool ProbeVideo(const QString &filename, VideoInfo &info) {
    const QString probePath = QDir(QCoreApplication::applicationDirPath()).filePath("bin/ffprobe.exe");
    if (!QFileInfo::exists(probePath))
        return false;

    QProcess process;
    process.setProgram(probePath);
    process.setArguments({
        "-v", "error",
        "-show_entries",
        "format=duration,format_name:stream=codec_type,codec_name,width,height,avg_frame_rate,bit_rate,sample_rate,channels,channel_layout",
        "-of", "flat",
        filename
    });

    process.start();
    if (!process.waitForFinished(10000) || process.exitCode() != 0)
        return false;

    const QString output = QString::fromUtf8(process.readAllStandardOutput());

    VideoInfo result;
    result.duration = GetProbeValue(output, "format.duration").toDouble();
    result.width = GetStreamProbeValue(output, "video", "width").toInt();
    result.height = GetStreamProbeValue(output, "video", "height").toInt();
    result.fps = ParseFps(GetStreamProbeValue(output, "video", "avg_frame_rate"));
    result.format = GetDisplayFormat(filename, GetProbeValue(output, "format.format_name"));

    result.audioCodec = GetStreamProbeValue(output, "audio", "codec_name");
    result.hasAudio = !result.audioCodec.isEmpty();

    if (result.hasAudio) {
        result.audioBitrate = GetStreamProbeValue(output, "audio", "bit_rate");
        result.audioSampleRate = GetStreamProbeValue(output, "audio", "sample_rate");
        result.audioChannels = GetStreamProbeValue(output, "audio", "channel_layout");
        if (result.audioChannels.isEmpty()) {
            result.audioChannels = GetStreamProbeValue(output, "audio", "channels");
        }
    }

    if (result.duration <= 0.0 || result.width <= 0 || result.height <= 0)
        return false;

    info = result;
    return true;
}
