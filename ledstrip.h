#ifndef LEDSTRIP_H
#define LEDSTRIP_H

#include <QPoint>
#include <QColor>

/**
 * @brief LED 灯条数据结构
 * 用于存储单条LED灯条的位置和颜色信息
 */
struct LEDStrip {
    QPoint start;      // 灯条起点
    QPoint end;        // 灯条终点
    int radius;        // 散射半径
    QColor color;      // 灯条颜色
};

#endif // LEDSTRIP_H

