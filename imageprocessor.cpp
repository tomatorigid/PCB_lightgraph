#include "imageprocessor.h"
#include <cmath>
#include <QPainter>
#include <QDebug>

namespace {
static QColor blendColor(const QColor& base, const QColor& top, int topWeight255) {
    int baseWeight255 = 255 - topWeight255;
    return QColor(
        (base.red() * baseWeight255 + top.red() * topWeight255) / 255,
        (base.green() * baseWeight255 + top.green() * topWeight255) / 255,
        (base.blue() * baseWeight255 + top.blue() * topWeight255) / 255);
}

static int colorSimilarityPercent(const QColor& a, const QColor& b) {
    const int dr = a.red() - b.red();
    const int dg = a.green() - b.green();
    const int db = a.blue() - b.blue();
    const double dist = std::sqrt(static_cast<double>(dr * dr + dg * dg + db * db));
    const double maxDist = std::sqrt(3.0 * 255.0 * 255.0);
    const double similarity = (1.0 - dist / maxDist) * 100.0;
    return qBound(0, static_cast<int>(std::round(similarity)), 100);
}
}


ImageProcessor::ImageProcessor() {
}

ImageProcessor::~ImageProcessor() {
}

QColor ImageProcessor::getSolderMaskColor(const QString& colorName) {
    if (colorName == "蓝色") return QColor(0, 50, 150, 200);
    if (colorName == "红色") return QColor(150, 0, 0, 200);
    if (colorName == "黑色") return QColor(10, 10, 10, 245);
    if (colorName == "绿色") return QColor(0, 100, 50, 200);
    return QColor(240, 240, 240, 220); // 白色
}

QColor ImageProcessor::getSilkColor(const QString& maskColorName) {
    return (maskColorName == "白色") ? Qt::black : Qt::white;
}

QColor ImageProcessor::getMetalRenderColor(const QString& finishType) {
    bool isHASL = finishType.contains("喷锡");
    return isHASL ? QColor(200, 200, 215) : QColor(218, 165, 32);
}

QColor ImageProcessor::getBareSubstrateColor() {
    return QColor(QStringLiteral("#A07D40"));
}

bool ImageProcessor::isMetal(
    const QColor& col,
    bool isHASL,
    int goldThresh,
    int saturationThresh,
    int valueThresh) {

    if (!isHASL) {
        // 沉金：检查色相是否接近金色
        return (std::abs(col.hue() - goldThresh) < 25 &&
                col.saturation() > saturationThresh &&
                col.value() > valueThresh);
    } else {
        // 喷锡：检查是否为银色系
        bool isSilverHue = (col.saturation() < 40 || (col.hue() > 160 && col.hue() < 260));
        return isSilverHue && (col.value() > goldThresh);
    }
}

bool ImageProcessor::isBaseCacheValid(
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
    int bareSubstrateColorSimilarityPct) const {

    return !srcImage.isNull()
        && srcImage.cacheKey() == m_cachedSourceKey
        && goldThresh == m_cachedGoldThresh
        && silkThresh == m_cachedSilkThresh
        && transThresh == m_cachedTransThresh
        && copperUnderMaskThresh == m_cachedCopperUnderMaskThresh
        && maskColorName == m_cachedMaskColorName
        && finishType == m_cachedFinishType
        && isWhiteMask == m_cachedIsWhiteMask
        && enableBareSubstrate == m_cachedEnableBareSubstrate
        && bareSubstrateUseGrayBinding == m_cachedBareSubstrateUseGrayBinding
        && bareSubstrateGrayMinPct == m_cachedBareSubstrateGrayMinPct
        && bareSubstrateGrayMaxPct == m_cachedBareSubstrateGrayMaxPct
        && bareSubstrateColorSimilarityPct == m_cachedBareSubstrateColorSimilarityPct;
}

void ImageProcessor::storeBaseCache(
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
    const QImage& outCompositeBase) {

    m_cachedSourceKey = srcImage.cacheKey();
    m_cachedGoldThresh = goldThresh;
    m_cachedSilkThresh = silkThresh;
    m_cachedTransThresh = transThresh;
    m_cachedCopperUnderMaskThresh = copperUnderMaskThresh;
    m_cachedMaskColorName = maskColorName;
    m_cachedFinishType = finishType;
    m_cachedIsWhiteMask = isWhiteMask;
    m_cachedEnableBareSubstrate = enableBareSubstrate;
    m_cachedBareSubstrateUseGrayBinding = bareSubstrateUseGrayBinding;
    m_cachedBareSubstrateGrayMinPct = bareSubstrateGrayMinPct;
    m_cachedBareSubstrateGrayMaxPct = bareSubstrateGrayMaxPct;
    m_cachedBareSubstrateColorSimilarityPct = bareSubstrateColorSimilarityPct;

    m_cachedCopper = outCopper;
    m_cachedMask = outMask;
    m_cachedSilk = outSilk;
    m_cachedBottom = outBottom;
    m_cachedCompositeBase = outCompositeBase;
}

void ImageProcessor::buildBaseLayers(
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
    QImage& outCompositeBase) const {

    int w = srcImage.width();
    int h = srcImage.height();

    QColor maskColor = getSolderMaskColor(maskColorName);
    QColor silkColor = getSilkColor(maskColorName);
    bool isHASL = finishType.contains("喷锡");
    QColor metalRenderColor = getMetalRenderColor(finishType);
    QColor bareSubstrateColor = getBareSubstrateColor();

    const int grayMinPct = qBound(0, qMin(bareSubstrateGrayMinPct, bareSubstrateGrayMaxPct), 100);
    const int grayMaxPct = qBound(0, qMax(bareSubstrateGrayMinPct, bareSubstrateGrayMaxPct), 100);
    const int similarityThreshold = qBound(0, bareSubstrateColorSimilarityPct, 100);

    int effectiveCopperThresh = qBound(qMax(0, qMin(transThresh + 1, 254)), copperUnderMaskThresh, 254);

    outCopper = QImage(w, h, QImage::Format_RGB32);
    outMask = QImage(w, h, QImage::Format_RGB32);
    outSilk = QImage(w, h, QImage::Format_RGB32);
    outBottom = QImage(w, h, QImage::Format_RGB32);
    outCompositeBase = QImage(w, h, QImage::Format_RGB32);

    for (int y = 0; y < h; ++y) {
        QRgb *lineCopper = (QRgb *)outCopper.scanLine(y);
        QRgb *lineMask = (QRgb *)outMask.scanLine(y);
        QRgb *lineSilk = (QRgb *)outSilk.scanLine(y);
        QRgb *lineBottom = (QRgb *)outBottom.scanLine(y);
        QRgb *lineComp = (QRgb *)outCompositeBase.scanLine(y);
        const QRgb *lineSrc = (const QRgb *)srcImage.constScanLine(y);

        for (int x = 0; x < w; ++x) {
            QColor col(lineSrc[x]);
            int gray = qGray(lineSrc[x]);
            int grayPct = qRound(gray * 100.0 / 255.0);

            bool isMetalPixel = isMetal(col, isHASL, goldThresh);
            bool silk = (gray > silkThresh) && !isMetalPixel;
            bool copperUnderMask = !isMetalPixel && !silk && (gray > effectiveCopperThresh);
            bool bareSubstratePixel = false;

            if (enableBareSubstrate && !isMetalPixel) {
                if (bareSubstrateUseGrayBinding) {
                    bareSubstratePixel = (grayPct >= grayMinPct && grayPct <= grayMaxPct);
                } else {
                    bareSubstratePixel = (colorSimilarityPercent(col, bareSubstrateColor) >= similarityThreshold);
                }
            }

            // 裸露基材只作用于对应位置的丝印层剔除，不影响阻焊层本身。
            bool bottomOpen = (gray > transThresh);
            bool maskOpen = isMetalPixel || (silk && !isWhiteMask);

            lineCopper[x] = (isMetalPixel || copperUnderMask) ? 0xFFFFFFFF : 0xFF000000;
            lineMask[x] = maskOpen ? 0xFFFFFFFF : 0xFF000000;
            lineSilk[x] = (silk && !bareSubstratePixel) ? 0xFFFFFFFF : 0xFF000000;
            lineBottom[x] = bottomOpen ? 0xFFFFFFFF : 0xFF000000;

            QColor pixelRes(40, 35, 25);

            if (isMetalPixel) {
                pixelRes = metalRenderColor;
            } else if (bareSubstratePixel) {
                pixelRes = bareSubstrateColor;
            } else {
                if (!maskOpen) {
                    QColor appliedMask = copperUnderMask ? maskColor.lighter(135) : maskColor;
                    pixelRes = blendColor(pixelRes, appliedMask, copperUnderMask ? 170 : 205);
                }

                if (silk) {
                    pixelRes = silkColor;
                }
            }

            lineComp[x] = pixelRes.rgb();
        }
    }
}

void ImageProcessor::renderLEDOverlay(
    QImage& composite,
    const QImage& bottomMask,
    const QVector<LEDStrip>& ledStrips,
    int ledRadVal) const {

    if (composite.isNull() || bottomMask.isNull() || ledStrips.isEmpty() || ledRadVal <= 0) {
        return;
    }

    int w = composite.width();
    int h = composite.height();

    for (int y = 0; y < h; ++y) {
        QRgb *lineComp = (QRgb *)composite.scanLine(y);
        const QRgb *lineBottom = (const QRgb *)bottomMask.constScanLine(y);

        for (int x = 0; x < w; ++x) {
            if (qGray(lineBottom[x]) < 128) {
                continue;
            }

            int rAcc = 0, gAcc = 0, bAcc = 0;

            for (const auto& s : ledStrips) {
                if (std::abs(x - s.end.x()) > ledRadVal && std::abs(x - s.start.x()) > ledRadVal)
                    continue;

                float d = 0;
                float l2 = std::pow(s.start.x() - s.end.x(), 2) + std::pow(s.start.y() - s.end.y(), 2);
                if (l2 == 0.0) {
                    d = std::sqrt(std::pow(x - s.end.x(), 2) + std::pow(y - s.end.y(), 2));
                } else {
                    float t = std::max(0.0f, std::min(1.0f,
                        (float)((x - s.start.x()) * (s.end.x() - s.start.x()) +
                               (y - s.start.y()) * (s.end.y() - s.start.y())) / l2));
                    QPoint proj(s.start.x() + t * (s.end.x() - s.start.x()),
                                s.start.y() + t * (s.end.y() - s.start.y()));
                    d = std::sqrt(std::pow(x - proj.x(), 2) + std::pow(y - proj.y(), 2));
                }

                if (d < ledRadVal) {
                    float factor = std::pow(1.0f - (d / ledRadVal), 1.8f);
                    rAcc += s.color.red() * factor;
                    gAcc += s.color.green() * factor;
                    bAcc += s.color.blue() * factor;
                }
            }

            if (rAcc + gAcc + bAcc > 10) {
                lineComp[x] = QColor(qMin(255, rAcc), qMin(255, gAcc), qMin(255, bAcc)).rgb();
            }
        }
    }
}

void ImageProcessor::processImage(
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
    bool renderLEDs) {
    if (srcImage.isNull()) {
        outCopper = QImage();
        outMask = QImage();
        outSilk = QImage();
        outBottom = QImage();
        outComposite = QImage();
        return;
    }

    if (!isBaseCacheValid(srcImage, goldThresh, silkThresh, transThresh, copperUnderMaskThresh, maskColorName, finishType, isWhiteMask,
                          enableBareSubstrate, bareSubstrateUseGrayBinding, bareSubstrateGrayMinPct, bareSubstrateGrayMaxPct, bareSubstrateColorSimilarityPct)) {
        buildBaseLayers(
            srcImage,
            goldThresh,
            silkThresh,
            transThresh,
            copperUnderMaskThresh,
            maskColorName,
            finishType,
            isWhiteMask,
            enableBareSubstrate,
            bareSubstrateUseGrayBinding,
            bareSubstrateGrayMinPct,
            bareSubstrateGrayMaxPct,
            bareSubstrateColorSimilarityPct,
            outCopper,
            outMask,
            outSilk,
            outBottom,
            outComposite);

        storeBaseCache(
            srcImage,
            goldThresh,
            silkThresh,
            transThresh,
            copperUnderMaskThresh,
            maskColorName,
            finishType,
            isWhiteMask,
            enableBareSubstrate,
            bareSubstrateUseGrayBinding,
            bareSubstrateGrayMinPct,
            bareSubstrateGrayMaxPct,
            bareSubstrateColorSimilarityPct,
            outCopper,
            outMask,
            outSilk,
            outBottom,
            outComposite);
    } else {
        outCopper = m_cachedCopper;
        outMask = m_cachedMask;
        outSilk = m_cachedSilk;
        outBottom = m_cachedBottom;
        outComposite = m_cachedCompositeBase;
    }

    if (renderLEDs) {
        renderLEDOverlay(outComposite, outBottom, ledStrips, ledRadVal);
    }
}

