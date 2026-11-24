#include "VideoProcessor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent> // Потребуется добавить Qt6::Concurrent в CMakeLists.txt
#include <algorithm>

VideoProcessor::VideoProcessor(QObject *parent)
    : QObject(parent), m_process(new QProcess(this)) {
    connect(m_process, &QProcess::readyReadStandardOutput, this, &VideoProcessor::onReadyReadStdout);
    connect(m_process, &QProcess::finished, this, &VideoProcessor::onProcessFinished);
}

QString VideoProcessor::ffmpegPath() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("bin/ffmpeg.exe");
}

bool VideoProcessor::isBusy() const {
    return m_process->state() != QProcess::NotRunning;
}

void VideoProcessor::cancel() {
    if (isBusy()) {
        m_process->kill();
    }
}

void VideoProcessor::extractFrameAsync(int targetId, uint64_t requestId, const QString &filename, double timeSeconds) {
    const QString bin = ffmpegPath();
    if (!QFileInfo::exists(bin)) {
        emit previewReady(targetId, requestId, QImage());
        return;
    }

    // Выполняем в фоновом пуле потоков Qt, чтобы не тормозить UI
    (void)QtConcurrent::run([this, bin, targetId, requestId, filename, timeSeconds]() {
        QProcess proc;
        proc.setProgram(bin);
        proc.setArguments({
            "-ss", QString::number(timeSeconds, 'f', 3),
            "-i", filename,
            "-frames:v", "1",
            "-f", "image2pipe",
            "-vcodec", "bmp",
            "pipe:1"
        });

        proc.start();
        if (!proc.waitForFinished(5000) || proc.exitCode() != 0) {
            emit previewReady(targetId, requestId, QImage());
            return;
        }

        QByteArray data = proc.readAllStandardOutput();
        QImage img = QImage::fromData(data, "BMP");
        emit previewReady(targetId, requestId, img);
    });
}

bool VideoProcessor::startCut(const QString &input, const QString &output, double startTime, double endTime, bool addSilentAudio) {
    if (isBusy())
        return false;

    const double duration = endTime - startTime;
    QStringList args;
    args << "-y"
         << "-ss" << QString::number(startTime, 'f', 3)
         << "-i" << input;

    if (addSilentAudio) {
        args << "-f" << "lavfi"
             << "-i" << "anullsrc=channel_layout=mono:sample_rate=44100"
             << "-t" << QString::number(duration, 'f', 3)
             << "-map" << "0:v:0"
             << "-map" << "1:a:0"
             << "-c:v" << "copy"
             << "-c:a" << "aac"
             << "-b:a" << "64k";
    } else {
        args << "-t" << QString::number(duration, 'f', 3)
             << "-c" << "copy";
    }

    args << "-progress" << "pipe:1"
         << "-nostats"
         << output;

    startFfmpeg(args, duration);
    return true;
}

bool VideoProcessor::startLoop(const QString &input, const QString &output, int repeatCount, double totalDuration) {
    if (isBusy() || repeatCount < 1)
        return false;

    int additionalLoops = repeatCount - 1;
    QStringList args{
        "-y",
        "-stream_loop", QString::number(additionalLoops),
        "-i", input,
        "-c", "copy",
        "-progress", "pipe:1",
        "-nostats",
        output
    };

    startFfmpeg(args, totalDuration);
    return true;
}

void VideoProcessor::startFfmpeg(const QStringList &args, double targetDuration) {
    const QString bin = ffmpegPath();
    if (!QFileInfo::exists(bin)) {
        emit finished(false, "Не найден bin/ffmpeg.exe");
        return;
    }

    m_stdoutBuffer.clear();
    m_targetDuration = targetDuration;

    m_process->setProgram(bin);
    m_process->setArguments(args);
    m_process->start();
}

void VideoProcessor::onReadyReadStdout() {
    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    qsizetype pos;
    while ((pos = m_stdoutBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_stdoutBuffer.left(pos).trimmed();
        m_stdoutBuffer.remove(0, pos + 1);

        if (line.startsWith("out_time_ms=") && m_targetDuration > 0.0) {
            bool ok = false;
            long long microseconds = line.mid(12).toLongLong(&ok);
            if (ok) {
                int percent = static_cast<int>(microseconds / (m_targetDuration * 10000.0));
                percent = std::clamp(percent, 0, 99);
                emit progressChanged(percent);
            }
        }
    }
}

void VideoProcessor::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        emit progressChanged(100);
        emit finished(true, QString());
    } else {
        emit finished(false, "Ошибка при выполнении FFmpeg");
    }
}
