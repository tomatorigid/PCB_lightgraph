#ifndef LEDLAYOUTENGINE_H
#define LEDLAYOUTENGINE_H

#include <QImage>
#include <QColor>
#include <QVector>
#include <QPoint>
#include <QMap>
#include "ledstrip.h"

/**
 * @brief LED 灯条布置引擎
 * 负责自动建议灯条位置和手动布置逻辑
 */
class LEDLayoutEngine {
public:
    LEDLayoutEngine();
    ~LEDLayoutEngine();

    /**
     * @brief 自动建议灯条位置
     * @param srcImage 处理后的图像
     * @param targetCount 目标灯条数量
     * @param ledRadius LED 散射半径
     * @return 建议的灯条列表
     */
    QVector<LEDStrip> autoSuggestLEDs(
        const QImage& srcImage,
        int targetCount,
        int ledRadius
    );

    /**
     * @brief 计算点到线段的距离
     */
    static float distanceToSegment(QPoint p, QPoint v, QPoint w);

    /**
     * @brief 获取 LED 预览图
     */
    static QImage generateLEDPreviewImage(
        int width,
        int height,
        const QVector<LEDStrip>& ledStrips
    );

    /**
     * @brief 绘制带灯光效果的合成图
     */
    void renderCompositeWithLEDs(
        QImage& composite,
        const QVector<LEDStrip>& ledStrips,
        int ledRadius,
        const QImage& copperMask,
        const QImage& bottomMask,
        bool showOverlay,
        int centerAlpha,
        bool forceShowOutlines
    );

private:
    struct ColorBucket {
        QVector<QPoint> points;
        long long totalBrightness;
    };
};

#endif // LEDLAYOUTENGINE_H

