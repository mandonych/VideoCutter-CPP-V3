#pragma once

#include <QMainWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <cstdint>

#include "video/VideoInfo.h"
#include "video/VideoProcessor.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    // Общие слоты
    void onBrowseClicked();
    void onSaveOutputClicked();
    void onActionClicked();
    void onTabChanged(int index);

    // Вкладка 1: Нарезка
    void onTimeEdited();
    void onTimelineRangeChanged(double startTime, double endTime);
    void onTimelineRangeFinished(double startTime, double endTime);

    // Вкладка 2: Зацикливание
    void onLoopModeChanged();
    void updateLoopCalculation();

    // Сигналы от FFmpeg
    void onPreviewReady(int targetId, uint64_t requestId, const QImage &image);
    void onProgressChanged(int percent);
    void onProcessFinished(bool success, const QString &errorMessage);

private:
    void loadInputVideo(const QString &filePath);
    void updatePreviews();
    void loadHelpText();

    QString makeDefaultOutputName(const QString &inputPath, const QString &suffixTag) const;
    bool parseTime(const QString &raw, double &seconds) const;
    QString formatTime(double seconds) const;

    Ui::MainWindow *ui = nullptr;
    VideoProcessor *m_processor = nullptr;
    VideoInfo m_videoInfo;

    // Данные для нарезки
    double m_startTime = 0.0;
    double m_endTime = 0.0;
    uint64_t m_startPreviewGen = 0;
    uint64_t m_endPreviewGen = 0;

    // Данные для зацикливания
    qint64 m_sourceFileSize = 0;
    int m_loopRepeatCount = 0;
    double m_loopResultDuration = 0.0;
    bool m_loopValid = false;
};
