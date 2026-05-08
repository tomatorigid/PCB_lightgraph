#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include <QImage>
#include <QColor>
#include <QMap>
#include <QVector>
#include <QPoint>
#include "ledstrip.h"

/**
 * @brief 图像处理核心类
 * 负责颜色分离、物理层判定、生产数据生成
 */
class ImageProcessor {
public:
    ImageProcessor();
    ~ImageProcessor();

    /**
     * @brief 处理原始图像生成各层输出
     */
    void processImage(
        const QImage& srcImage,
        int goldThresh,
        int silkThresh,
        int transThresh,
        int copperUnderMaskThresh,
        int ledRadVal,
        const QString& maskColorName,
        const QString& finishType,
        bool isWhiteMask,
        QImage& outCopper,
        QImage& outMask,
        QImage& outSilk,
        QImage& outBottom,
        QImage& outComposite,
        const QVector<LEDStrip>& ledStrips,
        bool renderLEDs = true
    );

    /**
     * @brief 获取阻焊颜色
     */
    static QColor getSolderMaskColor(const QString& colorName);

    /**
     * @brief 获取丝印颜色
     */
    static QColor getSilkColor(const QString& maskColorName);

    /**
     * @brief 检查像素是否为金属（铜箔）
     */
    static bool isMetal(
        const QColor& col,
        bool isHASL,
        int goldThresh,
        int saturationThresh = 50,
        int valueThresh = 70
    );

    /**
     * @brief 计算铜铁颜色 (用于金属渲染)
     */
    static QColor getMetalRenderColor(const QString& finishType);

private:
    void renderLEDOverlay(
        QImage& composite,
        const QImage& bottomMask,
        const QVector<LEDStrip>& ledStrips,
        int ledRadVal) const;

    void buildBaseLayers(
        const QImage& srcImage,
        int goldThresh,
        int silkThresh,
        int transThresh,
        int copperUnderMaskThresh,
        const QString& maskColorName,
        const QString& finishType,
        bool isWhiteMask,
        QImage& outCopper,
        QImage& outMask,
        QImage& outSilk,
        QImage& outBottom,
        QImage& outCompositeBase) const;

    bool isBaseCacheValid(
        const QImage& srcImage,
        int goldThresh,
        int silkThresh,
        int transThresh,
        int copperUnderMaskThresh,
        const QString& maskColorName,
        const QString& finishType,
        bool isWhiteMask) const;

    void storeBaseCache(
        const QImage& srcImage,
        int goldThresh,
        int silkThresh,
        int transThresh,
        int copperUnderMaskThresh,
        const QString& maskColorName,
        const QString& finishType,
        bool isWhiteMask,
        const QImage& outCopper,
        const QImage& outMask,
        const QImage& outSilk,
        const QImage& outBottom,
        const QImage& outCompositeBase);

    // 基础层缓存：只要源图和参数不变，就不重复计算像素分类
    qint64 m_cachedSourceKey = 0;
    int m_cachedGoldThresh = -1;
    int m_cachedSilkThresh = -1;
    int m_cachedTransThresh = -1;
    int m_cachedCopperUnderMaskThresh = -1;
    QString m_cachedMaskColorName;
    QString m_cachedFinishType;
    bool m_cachedIsWhiteMask = false;

    QImage m_cachedCopper;
    QImage m_cachedMask;
    QImage m_cachedSilk;
    QImage m_cachedBottom;
    QImage m_cachedCompositeBase;
};

#endif // IMAGEPROCESSOR_H

