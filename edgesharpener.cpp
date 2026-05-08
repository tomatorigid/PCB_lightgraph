#include "edgesharpener.h"
#include <cmath>
#include <QDebug>

EdgeSharpener::EdgeSharpener() {
}

EdgeSharpener::~EdgeSharpener() {
}

int EdgeSharpener::calculateEdgeStrength(const QImage& img, int x, int y) {
    int w = img.width();
    int h = img.height();

    if (x <= 0 || x >= w - 1 || y <= 0 || y >= h - 1) {
        return 0;
    }

    // 3x3 拉普拉斯卷积核进行边缘检测
    int center = qGray(img.pixel(x, y));
    int edgeVal = std::abs(
        4 * center - qGray(img.pixel(x - 1, y))
                   - qGray(img.pixel(x + 1, y))
                   - qGray(img.pixel(x, y - 1))
                   - qGray(img.pixel(x, y + 1))
    );

    return edgeVal;
}

QColor EdgeSharpener::determineEdgeColor(const QImage& srcImage, int x, int y, int autoInvertRange) {
    int w = srcImage.width();
    int h = srcImage.height();

    if (autoInvertRange == -1) {
        // 值为 -1：强制全黑
        return QColor(0, 0, 0);
    } else if (autoInvertRange == 0) {
        // 值为 0：强制全白
        return QColor(255, 255, 255);
    } else {
        // 值 > 0：执行自动反色逻辑
        long long sumGray = 0;
        int count = 0;

        for (int dy = -autoInvertRange; dy <= autoInvertRange; ++dy) {
            for (int dx = -autoInvertRange; dx <= autoInvertRange; ++dx) {
                int nx = x + dx;
                int ny = y + dy;
                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    sumGray += qGray(srcImage.pixel(nx, ny));
                    count++;
                }
            }
        }

        int avg = (count > 0) ? sumGray / count : 128;
        return (avg > 128) ? QColor(0, 0, 0) : QColor(255, 255, 255);
    }
}

QImage EdgeSharpener::processEdgeSharpening(
    const QImage& srcImage,
    int edgeThreshMin,
    int edgeThreshMax,
    int autoInvertRange) {

    QImage result = srcImage.copy();

    int w = srcImage.width();
    int h = srcImage.height();

    // 边缘检测和锐化处理
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            int edgeVal = calculateEdgeStrength(srcImage, x, y);

            if (edgeVal > edgeThreshMin && edgeVal < edgeThreshMax) {
                QColor edgeColor = determineEdgeColor(srcImage, x, y, autoInvertRange);
                result.setPixel(x, y, edgeColor.rgb());
            }
        }
    }

    return result;
}

