#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QUrl>
#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_processor(new VideoProcessor(this)) {
    ui->setupUi(this);

    setWindowTitle("Video Cutter");
    setAcceptDrops(true);

    ui->mainTabWidget->setCurrentIndex(0);
    ui->cutButton->setEnabled(false);
    ui->addAudioTrackCheckBox->setEnabled(false);

    // Кнопки выбора файлов на обеих вкладках
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    connect(ui->loopBrowseButton, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);

    connect(ui->saveOutputButton, &QPushButton::clicked, this, &MainWindow::onSaveOutputClicked);
    connect(ui->loopSaveOutputButton, &QPushButton::clicked, this, &MainWindow::onSaveOutputClicked);

    // Главная кнопка действия и переключение вкладок
    connect(ui->cutButton, &QPushButton::clicked, this, &MainWindow::onActionClicked);
    connect(ui->mainTabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    // Вкладка 1: Время и таймлайн
    connect(ui->startTimeEdit, &QLineEdit::editingFinished, this, &MainWindow::onTimeEdited);
    connect(ui->endTimeEdit, &QLineEdit::editingFinished, this, &MainWindow::onTimeEdited);
    connect(ui->timelineWidget, &TimelineWidget::rangeChanged, this, &MainWindow::onTimelineRangeChanged);
    connect(ui->timelineWidget, &TimelineWidget::rangeChangeFinished, this, &MainWindow::onTimelineRangeFinished);

    // Вкладка 2: Настройки цикла
    connect(ui->loopByDurationRadio, &QRadioButton::toggled, this, &MainWindow::onLoopModeChanged);
    connect(ui->loopTargetEdit, &QLineEdit::textChanged, this, &MainWindow::updateLoopCalculation);

    // Сигналы FFmpeg
    connect(m_processor, &VideoProcessor::previewReady, this, &MainWindow::onPreviewReady);
    connect(m_processor, &VideoProcessor::progressChanged, this, &MainWindow::onProgressChanged);
    connect(m_processor, &VideoProcessor::finished, this, &MainWindow::onProcessFinished);

    // Загрузка инструкции из README.md
    loadHelpText();
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        const QString filePath = urls.first().toLocalFile();
        if (QFileInfo(filePath).isFile()) {
            loadInputVideo(filePath);
        }
    }
}

void MainWindow::onBrowseClicked() {
    const QString file = QFileDialog::getOpenFileName(
        this,
        "Выберите исходный видеофайл",
        QString(),
        "Видеофайлы (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.ts);;Все файлы (*.*)"
    );
    if (!file.isEmpty()) {
        loadInputVideo(file);
    }
}

void MainWindow::onSaveOutputClicked() {
    const bool isLoopTab = (ui->mainTabWidget->currentIndex() == 1);
    QLineEdit *targetEdit = isLoopTab ? ui->loopOutputFileEdit : ui->outputFileEdit;

    const QString file = QFileDialog::getSaveFileName(
        this,
        isLoopTab ? "Сохранить зацикленное видео" : "Сохранить видеофрагмент",
        targetEdit->text(),
        "MP4 видео (*.mp4);;MKV видео (*.mkv);;Все файлы (*.*)"
    );
    if (!file.isEmpty()) {
        targetEdit->setText(file);
    }
}

void MainWindow::loadInputVideo(const QString &filePath) {
    ui->inputFileEdit->setText(filePath);
    ui->loopInputFileEdit->setText(filePath);

    ui->outputFileEdit->setText(makeDefaultOutputName(filePath, "_cut"));
    ui->loopOutputFileEdit->setText(makeDefaultOutputName(filePath, "_loop"));

    if (ProbeVideo(filePath, m_videoInfo)) {
        m_sourceFileSize = QFileInfo(filePath).size();
        m_startTime = 0.0;
        m_endTime = m_videoInfo.duration;

        ui->startTimeEdit->setText("00:00:00");
        ui->endTimeEdit->setText(formatTime(m_endTime));
        ui->timelineWidget->setDuration(m_videoInfo.duration);

        ui->addAudioTrackCheckBox->setChecked(false);
        ui->addAudioTrackCheckBox->setEnabled(!m_videoInfo.hasAudio);

        QString info = QString("%1 | %2x%3 | %4 FPS | %5")
                           .arg(m_videoInfo.format)
                           .arg(m_videoInfo.width)
                           .arg(m_videoInfo.height)
                           .arg(m_videoInfo.fps, 0, 'f', 2)
                           .arg(formatTime(m_videoInfo.duration));
        if (m_videoInfo.hasAudio) {
            info += QString(" | Audio: %1").arg(m_videoInfo.audioCodec);
        }

        ui->dropHintLabel->setText(info);
        ui->loopInfoLabel->setText(info);

        ui->cutButton->setEnabled(true);
        ui->statusbar->showMessage("Информация о видео загружена", 4000);

        updatePreviews();
        updateLoopCalculation();
    } else {
        m_sourceFileSize = 0;
        const QString err = "Не удалось прочитать информацию о видео (проверьте bin/ffprobe.exe)";
        ui->dropHintLabel->setText(err);
        ui->loopInfoLabel->setText(err);
        ui->cutButton->setEnabled(false);
        ui->addAudioTrackCheckBox->setEnabled(false);
        updateLoopCalculation();
    }
}

void MainWindow::onTimeEdited() {
    if (m_videoInfo.duration <= 0.0)
        return;

    double start = 0.0;
    double end = 0.0;

    if (!parseTime(ui->startTimeEdit->text(), start) || !parseTime(ui->endTimeEdit->text(), end)) {
        ui->statusbar->showMessage("Неверный формат времени (используйте ЧЧ:ММ:СС)", 4000);
        return;
    }

    if (start >= end || end > m_videoInfo.duration + 1.0) {
        ui->statusbar->showMessage("Некорректный временной диапазон", 4000);
        return;
    }

    m_startTime = start;
    m_endTime = std::min(end, m_videoInfo.duration);
    ui->timelineWidget->setRange(m_startTime, m_endTime);
    updatePreviews();
}

void MainWindow::onTimelineRangeChanged(double startTime, double endTime) {
    m_startTime = startTime;
    m_endTime = endTime;
    ui->startTimeEdit->setText(formatTime(m_startTime));
    ui->endTimeEdit->setText(formatTime(m_endTime));
}

void MainWindow::onTimelineRangeFinished(double startTime, double endTime) {
    m_startTime = startTime;
    m_endTime = endTime;
    updatePreviews();
}

void MainWindow::updatePreviews() {
    const QString input = ui->inputFileEdit->text();
    if (input.isEmpty() || m_videoInfo.duration <= 0.0)
        return;

    const double startPos = std::min(m_startTime + 0.2, m_videoInfo.duration * 0.1);
    const double endPos = std::max(0.0, m_endTime - 0.5);

    m_processor->extractFrameAsync(1, ++m_startPreviewGen, input, startPos);
    m_processor->extractFrameAsync(2, ++m_endPreviewGen, input, endPos);
}

void MainWindow::onPreviewReady(int targetId, uint64_t requestId, const QImage &image) {
    if (image.isNull())
        return;

    if (targetId == 1 && requestId == m_startPreviewGen) {
        ui->startFrameLabel->setPixmap(QPixmap::fromImage(image).scaled(
            ui->startFrameLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else if (targetId == 2 && requestId == m_endPreviewGen) {
        ui->endFrameLabel->setPixmap(QPixmap::fromImage(image).scaled(
            ui->endFrameLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void MainWindow::onLoopModeChanged() {
    if (ui->loopByDurationRadio->isChecked()) {
        ui->loopTargetLabel->setText("Целевая длительность (ЧЧ:ММ:СС)");
        ui->loopTargetEdit->setText("00:10:00");
    } else {
        ui->loopTargetLabel->setText("Целевой размер (МБ)");
        ui->loopTargetEdit->setText("1000");
    }
    updateLoopCalculation();
}

void MainWindow::updateLoopCalculation() {
    m_loopRepeatCount = 0;
    m_loopResultDuration = 0.0;
    m_loopValid = false;

    if (m_videoInfo.duration > 0.0 && m_sourceFileSize > 0) {
        if (ui->loopByDurationRadio->isChecked()) {
            double targetSeconds = 0.0;
            if (parseTime(ui->loopTargetEdit->text(), targetSeconds) && targetSeconds > 0.0) {
                double repeats = std::floor((targetSeconds + 0.1) / m_videoInfo.duration);
                if (repeats >= 1.0 && repeats <= 100000.0) {
                    m_loopRepeatCount = static_cast<int>(repeats);
                    m_loopValid = true;
                }
            }
        } else {
            QString raw = ui->loopTargetEdit->text().trimmed();
            raw.replace(',', '.');
            bool ok = false;
            double targetMB = raw.toDouble(&ok);
            if (ok && targetMB > 0.0) {
                double targetBytes = targetMB * 1024.0 * 1024.0;
                double repeats = std::floor(targetBytes / static_cast<double>(m_sourceFileSize));
                if (repeats >= 1.0 && repeats <= 100000.0) {
                    m_loopRepeatCount = static_cast<int>(repeats);
                    m_loopValid = true;
                }
            }
        }
    }

    if (m_loopValid) {
        m_loopResultDuration = m_videoInfo.duration * m_loopRepeatCount;
        double resultMB = (static_cast<double>(m_sourceFileSize) * m_loopRepeatCount) / (1024.0 * 1024.0);

        ui->loopResultDurationLabel->setText("Длительность: " + formatTime(m_loopResultDuration));
        ui->loopResultSizeLabel->setText(QString("Размер файла: %1 МБ").arg(resultMB, 0, 'f', 1));
        ui->loopResultRepeatsLabel->setText(QString("Количество повторов: %1").arg(m_loopRepeatCount));
    } else {
        ui->loopResultDurationLabel->setText("Длительность: —");
        ui->loopResultSizeLabel->setText("Размер файла: —");
        ui->loopResultRepeatsLabel->setText("Количество повторов: —");
    }
}

void MainWindow::onActionClicked() {
    if (m_processor->isBusy())
        return;

    const int tabIndex = ui->mainTabWidget->currentIndex();

    if (tabIndex == 0) {
        // Режим нарезки
        const QString input = ui->inputFileEdit->text();
        const QString output = ui->outputFileEdit->text();
        if (input.isEmpty() || output.isEmpty())
            return;

        ui->cutButton->setEnabled(false);
        ui->progressBar->setValue(0);
        ui->statusbar->showMessage("Выполняется обрезка видео...");

        m_processor->startCut(input, output, m_startTime, m_endTime, ui->addAudioTrackCheckBox->isChecked());
    } else if (tabIndex == 1) {
        // Режим зацикливания
        const QString input = ui->loopInputFileEdit->text();
        const QString output = ui->loopOutputFileEdit->text();

        if (input.isEmpty() || output.isEmpty())
            return;

        if (!m_loopValid || m_loopRepeatCount < 1) {
            QMessageBox::warning(this, "Video Cutter", "Невозможно создать цикл: проверьте целевое значение.");
            return;
        }

        if (QFileInfo(input).canonicalFilePath() == QFileInfo(output).canonicalFilePath()) {
            QMessageBox::warning(this, "Video Cutter", "Выходной файл не должен совпадать с исходным.");
            return;
        }

        ui->cutButton->setEnabled(false);
        ui->progressBar->setValue(0);
        ui->statusbar->showMessage("Создаётся зацикленное видео...");

        m_processor->startLoop(input, output, m_loopRepeatCount, m_loopResultDuration);
    }
}

void MainWindow::onProgressChanged(int percent) {
    ui->progressBar->setValue(percent);
}

void MainWindow::onProcessFinished(bool success, const QString &errorMessage) {
    ui->cutButton->setEnabled(true);
    if (success) {
        ui->progressBar->setValue(100);
        ui->statusbar->showMessage("Операция успешно завершена!", 5000);
        QMessageBox::information(this, "Video Cutter", "Обработка видео успешно завершена!");
    } else {
        ui->statusbar->showMessage("Ошибка при обработке видео", 5000);
        QMessageBox::critical(this, "Video Cutter", errorMessage.isEmpty() ? "Ошибка FFmpeg." : errorMessage);
    }
    ui->progressBar->setValue(0);
}

void MainWindow::onTabChanged(int index) {
    const bool isHelpTab = (index == 2);

    ui->progressBar->setVisible(!isHelpTab);
    ui->cutButton->setVisible(!isHelpTab);

    if (isHelpTab) {
        ui->mainTabWidget->resize(ui->mainTabWidget->width(), 470);
        ui->subtitleLabel->setText("Краткая инструкция");
    } else {
        ui->mainTabWidget->resize(ui->mainTabWidget->width(), 391);
        if (index == 0) {
            ui->subtitleLabel->setText("Быстрая обрезка без перекодирования");
            ui->cutButton->setText("Обрезать видео");
        } else if (index == 1) {
            ui->subtitleLabel->setText("Создание зацикленного видео");
            ui->cutButton->setText("Создать цикл");
        }
    }
}

void MainWindow::loadHelpText() {
    QString readmePath = QDir(QCoreApplication::applicationDirPath()).filePath("README.md");
    if (!QFile::exists(readmePath)) {
        readmePath = QDir(QCoreApplication::applicationDirPath()).filePath("../README.md");
    }

    QFile file(readmePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui->helpTextBrowser->setMarkdown(QString::fromUtf8(file.readAll()));
    } else {
        ui->helpTextBrowser->setPlainText("Файл README.md не найден рядом с программой.");
    }
}

QString MainWindow::makeDefaultOutputName(const QString &inputPath, const QString &suffixTag) const {
    const QFileInfo fi(inputPath);
    const QString suffix = fi.completeSuffix();
    if (suffix.isEmpty()) {
        return fi.absolutePath() + "/" + fi.baseName() + suffixTag;
    }
    return fi.absolutePath() + "/" + fi.completeBaseName() + suffixTag + "." + suffix;
}

bool MainWindow::parseTime(const QString &raw, double &seconds) const {
    QString val = raw.trimmed();
    val.replace(',', '.');
    const QStringList parts = val.split(':');

    bool ok1 = false, ok2 = false, ok3 = false;
    if (parts.size() == 3) {
        int h = parts[0].toInt(&ok1);
        int m = parts[1].toInt(&ok2);
        double s = parts[2].toDouble(&ok3);
        if (ok1 && ok2 && ok3 && h >= 0 && m >= 0 && m < 60 && s >= 0.0 && s < 60.0) {
            seconds = h * 3600.0 + m * 60.0 + s;
            return true;
        }
    } else if (parts.size() == 2) {
        int m = parts[0].toInt(&ok1);
        double s = parts[1].toDouble(&ok2);
        if (ok1 && ok2 && m >= 0 && s >= 0.0 && s < 60.0) {
            seconds = m * 60.0 + s;
            return true;
        }
    }
    return false;
}

QString MainWindow::formatTime(double seconds) const {
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}
