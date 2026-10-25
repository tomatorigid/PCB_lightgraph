#include "regionslider.h"
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
void RegionSlider::paintEvent(QPaintEvent* event) {
    QSlider::paintEvent(event);
    QStyleOptionSlider option;
    initStyleOption(&option);
    option.sliderPosition = m_baseline; option.sliderValue = m_baseline;
    // 与 Fluent 滑轨绘制一致地使用浮点中心，避免 QRect 整数中心偏上半个像素。
    const QRectF handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
    const QRectF groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(palette().color(QPalette::Highlight));
    const qreal radius = qMax(2.0, qMin(4.0, groove.height() / 2.0));
    painter.drawEllipse(QPointF(handle.center().x(), groove.center().y()), radius, radius);
}
