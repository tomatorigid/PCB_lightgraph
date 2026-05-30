#ifndef EDGESHARPENER_H
#define EDGESHARPENER_H

#include <QImage>
#include <QVector>

/**
 * @brief 边缘锐化和增强处理
 */
class EdgeSharpener {
public:
    enum class OperationMode {
        EdgeEnhance = 0,
        StrokeCanny = 1
    };

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
        int autoInvertRange,
        bool enablePreFilter = true,
        int gaussianKernelSize = 5,
        double gaussianSigma = 1.1,
        bool enableDouglasPeucker = false,
        double dpTolerance = 1.0,
        int dpLineWidth = 2
    );

    QImage processEdgeOperation(
        const QImage& srcImage,
        OperationMode mode,
        int edgeThreshMin,
        int edgeThreshMax,
        int autoInvertRange,
        bool enablePreFilter = true,
        int gaussianKernelSize = 5,
        double gaussianSigma = 1.1,
        bool enableDouglasPeucker = false,
        double dpTolerance = 1.0,
        int dpLineWidth = 2
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

    // ----- 缓存用于用空间换时间 -----
    quint64 m_cachedImageKey = 0;
    int m_cachedW = 0;
    int m_cachedH = 0;
    QVector<int> m_gray;            // 灰度缓存，每像素一个 [0..255]
    QVector<int> m_edgeStrength;    // 每像素的边缘强度（拉普拉斯结果）
    QVector<quint64> m_integral;    // 积分图 (w+1)*(h+1)，用于快速邻域求和
    QVector<int> m_sobelMagnitude;  // Sobel 梯度幅值（Canny前置缓存）
    QVector<uchar> m_sobelDirection;// 量化方向: 0/45/90/135
    QVector<int> m_cannyNms;        // 非极大值抑制后的梯度
    bool m_cannyCacheReady = false;

    // 构建或重建缓存（在源图像改变时调用）
    void buildCacheIfNeeded(const QImage& srcImage);
    void buildCannyCacheIfNeeded();
    // 使用积分图计算矩形区域平均灰度（含边界裁剪）
    int averageGrayInBox(int x0, int y0, int x1, int y1) const;
    QImage buildLaplacianMask(int edgeThreshMin, int edgeThreshMax) const;
    QImage buildCannyMask(int edgeThreshMin, int edgeThreshMax);
};

#endif // EDGESHARPENER_H

