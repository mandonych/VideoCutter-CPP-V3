#pragma once

#include <QWidget>

class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void setDuration(double duration);
    void setRange(double startTime, double endTime);

    double startTime() const { return m_startTime; }
    double endTime() const { return m_endTime; }

signals:
    // Срабатывает плавно во время перетаскивания (для обновления текстовых полей времени)
    void rangeChanged(double startTime, double endTime);
    // Срабатывает в момент отпускания кнопки мыши (для обновления тяжелых превью кадров)
    void rangeChangeFinished(double startTime, double endTime);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    int timeToX(double time) const;
    double xToTime(int x) const;
    QRect trackRect() const;
    QString formatDuration(double seconds) const;

    double m_duration = 0.0;
    double m_startTime = 0.0;
    double m_endTime = 0.0;

    int m_draggedMarker = 0; // 0 — нет, 1 — начало, 2 — конец
};
