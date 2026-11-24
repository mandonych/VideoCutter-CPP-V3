#pragma once

#include <QImage>
#include <QObject>
#include <QProcess>
#include <QString>
#include <cstdint>

class VideoProcessor : public QObject {
    Q_OBJECT

public:
    explicit VideoProcessor(QObject *parent = nullptr);

    // Асинхронное извлечение кадра для превью
    void extractFrameAsync(int targetId, uint64_t requestId, const QString &filename, double timeSeconds);

    // Запуск нарезки видео
    bool startCut(const QString &input, const QString &output, double startTime, double endTime, bool addSilentAudio);

    // Запуск создания цикла
    bool startLoop(const QString &input, const QString &output, int repeatCount, double totalDuration);

    bool isBusy() const;
    void cancel();

signals:
    void previewReady(int targetId, uint64_t requestId, const QImage &image);
    void progressChanged(int percent);
    void finished(bool success, const QString &errorMessage);

private slots:
    void onReadyReadStdout();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QString ffmpegPath() const;
    void startFfmpeg(const QStringList &args, double targetDuration);

    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    double m_targetDuration = 0.0;
};
