#include "TimelineWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent) {
    setMinimumHeight(46);
    setCursor(Qt::PointingHandCursor);
}

void TimelineWidget::setDuration(double duration) {
    m_duration = std::max(0.0, duration);
    m_startTime = 0.0;
    m_endTime = m_duration;
    update();
}

void TimelineWidget::setRange(double startTime, double endTime) {
    if (m_duration <= 0.0)
        return;

    m_startTime = std::clamp(startTime, 0.0, m_duration);
    m_endTime = std::clamp(endTime, m_startTime, m_duration);
    update();
}

QRect TimelineWidget::trackRect() const {
    constexpr int marginX = 12;
    constexpr int trackY = 14;
    return QRect(marginX, trackY, std::max(10, width() - marginX * 2), 6);
}

int TimelineWidget::timeToX(double time) const {
    const QRect r = trackRect();
    if (m_duration <= 0.0)
        return r.left();

    double ratio = std::clamp(time / m_duration, 0.0, 1.0);
    return r.left() + static_cast<int>(ratio * r.width());
}

double TimelineWidget::xToTime(int x) const {
    const QRect r = trackRect();
    if (m_duration <= 0.0 || r.width() <= 0)
        return 0.0;

    int clampedX = std::clamp(x, r.left(), r.right());
    double ratio = static_cast<double>(clampedX - r.left()) / r.width();
    return ratio * m_duration;
}

void TimelineWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = trackRect();
    const int centerY = r.center().y();
    constexpr int radius = 7;

    // Фоновая дорожка
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(65, 68, 85));
    painter.drawRoundedRect(r, 3, 3);

    if (m_duration <= 0.0)
        return;

    const int startX = timeToX(m_startTime);
    const int endX = timeToX(m_endTime);

    // Выделенный диапазон (зеленый цвет Kanagawa)
    QRect selectedRect(startX, r.top(), std::max(0, endX - startX), r.height());
    painter.setBrush(QColor(152, 187, 108));
    painter.drawRoundedRect(selectedRect, 3, 3);

    // Маркер начала (зеленый)
    painter.setBrush(QColor(152, 187, 108));
    painter.drawEllipse(QPoint(startX, centerY), radius, radius);

    // Маркер конца (желтый)
    painter.setBrush(QColor(220, 180, 110));
    painter.drawEllipse(QPoint(endX, centerY), radius, radius);

    // Текст длительности выбранного фрагмента под шкалой
    const double selectedDuration = std::max(0.0, m_endTime - m_startTime);
    painter.setPen(QColor(147, 138, 169)); // #938AA9
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);

    QRect textRect(r.left(), centerY + 11, r.width(), 20);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                     "Длительность фрагмента: " + formatDuration(selectedDuration));
}

void TimelineWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_duration <= 0.0)
        return;

    const int clickX = static_cast<int>(event->position().x());
    const int distStart = std::abs(clickX - timeToX(m_startTime));
    const int distEnd = std::abs(clickX - timeToX(m_endTime));

    m_draggedMarker = (distStart <= distEnd) ? 1 : 2;
    mouseMoveEvent(event);
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_draggedMarker == 0 || m_duration <= 0.0)
        return;

    const double time = xToTime(static_cast<int>(event->position().x()));
    constexpr double minInterval = 1.0;

    if (m_draggedMarker == 1) {
        m_startTime = std::clamp(time, 0.0, std::max(0.0, m_endTime - minInterval));
    } else {
        m_endTime = std::clamp(time, std::min(m_duration, m_startTime + minInterval), m_duration);
    }

    update();
    emit rangeChanged(m_startTime, m_endTime);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_draggedMarker != 0) {
        m_draggedMarker = 0;
        emit rangeChangeFinished(m_startTime, m_endTime);
    }
}

QString TimelineWidget::formatDuration(double seconds) const {
    int total = static_cast<int>(std::round(seconds));
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}
