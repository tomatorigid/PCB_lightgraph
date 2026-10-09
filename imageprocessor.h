#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include <QImage>
#include <QColor>
#include <QMap>
#include <QVector>
#include <QPoint>
#include "ledstrip.h"
#include "regionmodel.h"

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
        bool enableBareSubstrate,
        bool bareSubstrateUseGrayBinding,
        int bareSubstrateGrayMinPct,
        int bareSubstrateGrayMaxPct,
        int bareSubstrateColorSimilarityPct,
        QImage& outCopper,
        QImage& outMask,
        QImage& outSilk,
        QImage& outBottom,
        QImage& outComposite,
        const QVector<LEDStrip>& ledStrips,
        bool renderLEDs = true,
        const Regions::RenderContext* regions = nullptr
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
        const QString& finishType,
        int goldThresh,
        int saturationThresh = 50,
        int valueThresh = 70
    );

    /**
     * @brief 计算铜铁颜色 (用于金属渲染)
     */
    static QColor getMetalRenderColor(const QString& finishType);

    /**
     * @brief 获取裸露基材的默认显示色
     */
    static QColor getBareSubstrateColor();

    // ---- 自定义色值（本地持久化于 QSettings，与 .pcblg 工程无关）----
    static QColor getCustomEnigColor();              // 沉金
    static QColor getCustomOspColor();               // OSP
    static QColor getCustomHaslColor();              // 喷锡
    static QColor getCustomBareSubstrateColor();     // 裸露基材
    static QColor getCustomMaskBlueColor();          // 阻焊：蓝色
    static QColor getCustomMaskBlackColor();         // 阻焊：黑色
    static QColor getCustomMaskRedColor();           // 阻焊：红色
    static QColor getCustomMaskGreenColor();         // 阻焊：绿色
    static QColor getCustomMaskWhiteColor();         // 阻焊：白色
    static QColor getCustomMaskYellowColor();        // 阻焊：黄色
    static QColor getCustomMaskPurpleColor();        // 阻焊：紫色
    static void setCustomEnigColor(const QColor& c);
    static void setCustomOspColor(const QColor& c);
    static void setCustomHaslColor(const QColor& c);
    static void setCustomBareSubstrateColor(const QColor& c);
    static void setCustomMaskBlueColor(const QColor& c);
    static void setCustomMaskBlackColor(const QColor& c);
    static void setCustomMaskRedColor(const QColor& c);
    static void setCustomMaskGreenColor(const QColor& c);
    static void setCustomMaskWhiteColor(const QColor& c);
    static void setCustomMaskYellowColor(const QColor& c);
    static void setCustomMaskPurpleColor(const QColor& c);

private:
    // 可自定义色值的静态存储（默认值定义见 imageprocessor.cpp）
    static QColor s_customEnig;
    static QColor s_customOsp;
    static QColor s_customHasl;
    static QColor s_customBare;
    static QColor s_customMaskBlue;
    static QColor s_customMaskBlack;
    static QColor s_customMaskRed;
    static QColor s_customMaskGreen;
    static QColor s_customMaskWhite;
    static QColor s_customMaskYellow;
    static QColor s_customMaskPurple;

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
        bool enableBareSubstrate,
        bool bareSubstrateUseGrayBinding,
        int bareSubstrateGrayMinPct,
        int bareSubstrateGrayMaxPct,
        int bareSubstrateColorSimilarityPct,
        QImage& outCopper,
        QImage& outMask,
        QImage& outSilk,
        QImage& outBottom,
        QImage& outCompositeBase,
        const Regions::RenderContext* regions = nullptr) const;

    bool isBaseCacheValid(
        const QImage& srcImage,
        int goldThresh,
        int silkThresh,
        int transThresh,
        int copperUnderMaskThresh,
        const QString& maskColorName,
        const QString& finishType,
        bool isWhiteMask,
        bool enableBareSubstrate,
        bool bareSubstrateUseGrayBinding,
        int bareSubstrateGrayMinPct,
        int bareSubstrateGrayMaxPct,
        int bareSubstrateColorSimilarityPct) const;

    void storeBaseCache(
        const QImage& srcImage,
        int goldThresh,
        int silkThresh,
        int transThresh,
        int copperUnderMaskThresh,
        const QString& maskColorName,
        const QString& finishType,
        bool isWhiteMask,
        bool enableBareSubstrate,
        bool bareSubstrateUseGrayBinding,
        int bareSubstrateGrayMinPct,
        int bareSubstrateGrayMaxPct,
        int bareSubstrateColorSimilarityPct,
        const QImage& outCopper,
        const QImage& outMask,
        const QImage& outSilk,
        const QImage& outBottom,
        const QImage& outCompositeBase);

    // 基础层缓存：只要源图和参数不变，就不重复计算像素分类
    qint64 m_cachedSourceKey = 0;
    quint64 m_cachedRegionRevision = 0;
    QColor m_cachedMaskColor, m_cachedMetalColor, m_cachedBareColor;
    int m_cachedGoldThresh = -1;
    int m_cachedSilkThresh = -1;
    int m_cachedTransThresh = -1;
    int m_cachedCopperUnderMaskThresh = -1;
    QString m_cachedMaskColorName;
    QString m_cachedFinishType;
    bool m_cachedIsWhiteMask = false;
    bool m_cachedEnableBareSubstrate = false;
    bool m_cachedBareSubstrateUseGrayBinding = false;
    int m_cachedBareSubstrateGrayMinPct = -1;
    int m_cachedBareSubstrateGrayMaxPct = -1;
    int m_cachedBareSubstrateColorSimilarityPct = -1;

    QImage m_cachedCopper;
    QImage m_cachedMask;
    QImage m_cachedSilk;
    QImage m_cachedBottom;
    QImage m_cachedCompositeBase;
};

#endif // IMAGEPROCESSOR_H

