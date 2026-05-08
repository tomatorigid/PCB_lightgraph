#ifndef EDGESHARPENER_H
#define EDGESHARPENER_H

#include <QImage>

/**
 * @brief 边缘锐化和增强处理
 */
class EdgeSharpener {
public:
    EdgeSharpener();
    ~EdgeSharpener();

    /**
     * @brief 执行边缘检测和锐化
     * @param srcImage 输入图像
     * @param edgeThreshMin 边缘下限阈值
     * @param edgeThreshMax 边缘上限阈值
     * @param autoInvertRange 自动反色范围
     * @return 处理后的图像
     */
    QImage processEdgeSharpening(
        const QImage& srcImage,
        int edgeThreshMin,
        int edgeThreshMax,
        int autoInvertRange
    );

private:
    /**
     * @brief 计算像素点的边缘强度（使用拉普拉斯卷积核）
     */
    int calculateEdgeStrength(const QImage& img, int x, int y);

    /**
     * @brief 根据周围像素确定边缘颜色
     */
    QColor determineEdgeColor(const QImage& srcImage, int x, int y, int autoInvertRange);
};

#endif // EDGESHARPENER_H

