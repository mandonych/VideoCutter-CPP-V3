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
    void onBrowseClicked();
    void onSaveOutputClicked();
    void onCutClicked();
    void onTimeEdited();
    void onTabChanged(int index);

    void onPreviewReady(int targetId, uint64_t requestId, const QImage &image);
    void onProgressChanged(int percent);
    void onCutFinished(bool success, const QString &errorMessage);

private:
    void loadInputVideo(const QString &filePath);
    void updatePreviews();
    QString makeDefaultOutputName(const QString &inputPath) const;
    bool parseTime(const QString &raw, double &seconds) const;
    QString formatTime(double seconds) const;

    Ui::MainWindow *ui = nullptr;
    VideoProcessor *m_processor = nullptr;
    VideoInfo m_videoInfo;

    double m_startTime = 0.0;
    double m_endTime = 0.0;
    uint64_t m_startPreviewGen = 0;
    uint64_t m_endPreviewGen = 0;
};
