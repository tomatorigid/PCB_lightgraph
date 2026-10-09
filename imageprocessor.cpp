#include "imageprocessor.h"
#include <cmath>
#include <limits>
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
    // 阻焊颜色可自定义（Option -> 颜色设置），这里保留各自的默认 alpha（阻焊半透明合成语义）
    if (colorName == "蓝色") return QColor(s_customMaskBlue.red(),  s_customMaskBlue.green(),  s_customMaskBlue.blue(),  200);
    if (colorName == "红色") return QColor(s_customMaskRed.red(),   s_customMaskRed.green(),   s_customMaskRed.blue(),   200);
    if (colorName == "黑色") return QColor(s_customMaskBlack.red(), s_customMaskBlack.green(), s_customMaskBlack.blue(), 245);
    if (colorName == "绿色") return QColor(s_customMaskGreen.red(), s_customMaskGreen.green(), s_customMaskGreen.blue(), 200);
    if (colorName == "黄色") return QColor(s_customMaskYellow.red(), s_customMaskYellow.green(), s_customMaskYellow.blue(), 200);
    if (colorName == "紫色") return QColor(s_customMaskPurple.red(), s_customMaskPurple.green(), s_customMaskPurple.blue(), 200);
    return QColor(s_customMaskWhite.red(), s_customMaskWhite.green(), s_customMaskWhite.blue(), 220); // 白色
}

QColor ImageProcessor::getSilkColor(const QString& maskColorName) {
    // 白色阻焊板配黑色丝印（深灰，避免死黑）；其余颜色配白色丝印
    if (maskColorName == "白色") return QColor(20, 20, 20);
    return Qt::white;
}

// 自定义色值默认值：沉金 / OSP(#F0AA93) / 喷锡 / 裸露基材 + 阻焊层五色（RGB 与旧硬编码一致）
QColor ImageProcessor::s_customEnig = QColor(240, 217, 140);
QColor ImageProcessor::s_customOsp = QColor(240, 170, 147);
QColor ImageProcessor::s_customHasl = QColor(200, 200, 215);
QColor ImageProcessor::s_customBare = QColor(153, 187, 119);
QColor ImageProcessor::s_customMaskBlue  = QColor(0, 50, 150);
QColor ImageProcessor::s_customMaskBlack = QColor(10, 10, 10);
QColor ImageProcessor::s_customMaskRed   = QColor(150, 0, 0);
QColor ImageProcessor::s_customMaskGreen = QColor(0, 100, 50);
QColor ImageProcessor::s_customMaskWhite = QColor(240, 240, 240);
QColor ImageProcessor::s_customMaskYellow  = QColor(192, 167, 13);   // #C0A70D
QColor ImageProcessor::s_customMaskPurple = QColor(26, 0, 31);       // #1A001F

QColor ImageProcessor::getCustomEnigColor()          { return s_customEnig; }
QColor ImageProcessor::getCustomOspColor()           { return s_customOsp; }
QColor ImageProcessor::getCustomHaslColor()          { return s_customHasl; }
QColor ImageProcessor::getCustomBareSubstrateColor() { return s_customBare; }
QColor ImageProcessor::getCustomMaskBlueColor()      { return s_customMaskBlue; }
QColor ImageProcessor::getCustomMaskBlackColor()     { return s_customMaskBlack; }
QColor ImageProcessor::getCustomMaskRedColor()       { return s_customMaskRed; }
QColor ImageProcessor::getCustomMaskGreenColor()     { return s_customMaskGreen; }
QColor ImageProcessor::getCustomMaskWhiteColor()     { return s_customMaskWhite; }
QColor ImageProcessor::getCustomMaskYellowColor()    { return s_customMaskYellow; }
QColor ImageProcessor::getCustomMaskPurpleColor()    { return s_customMaskPurple; }
void ImageProcessor::setCustomEnigColor(const QColor& c)          { if (c.isValid()) s_customEnig = c; }
void ImageProcessor::setCustomOspColor(const QColor& c)           { if (c.isValid()) s_customOsp = c; }
void ImageProcessor::setCustomHaslColor(const QColor& c)          { if (c.isValid()) s_customHasl = c; }
void ImageProcessor::setCustomBareSubstrateColor(const QColor& c) { if (c.isValid()) s_customBare = c; }
void ImageProcessor::setCustomMaskBlueColor(const QColor& c)      { if (c.isValid()) s_customMaskBlue = c; }
void ImageProcessor::setCustomMaskBlackColor(const QColor& c)     { if (c.isValid()) s_customMaskBlack = c; }
void ImageProcessor::setCustomMaskRedColor(const QColor& c)       { if (c.isValid()) s_customMaskRed = c; }
void ImageProcessor::setCustomMaskGreenColor(const QColor& c)     { if (c.isValid()) s_customMaskGreen = c; }
void ImageProcessor::setCustomMaskWhiteColor(const QColor& c)     { if (c.isValid()) s_customMaskWhite = c; }
void ImageProcessor::setCustomMaskYellowColor(const QColor& c)    { if (c.isValid()) s_customMaskYellow = c; }
void ImageProcessor::setCustomMaskPurpleColor(const QColor& c)    { if (c.isValid()) s_customMaskPurple = c; }

QColor ImageProcessor::getMetalRenderColor(const QString& finishType) {
    if (finishType.contains("喷锡")) return getCustomHaslColor();
    if (finishType.contains("OSP")) return getCustomOspColor();
    return getCustomEnigColor();
}

QColor ImageProcessor::getBareSubstrateColor() {
    // 原先的基材颜色: QColor(QStringLiteral("#A07D40"));
    // 使用 HSL(60, 30%, 62%) 显示: QColor::fromHsl(60, 77, 158);
    // 现在改为 rgb(153, 187, 119)
    return getCustomBareSubstrateColor();
}

bool ImageProcessor::isMetal(
    const QColor& col,
    const QString& finishType,
    int goldThresh,
    int saturationThresh,
    int valueThresh) {

    if (finishType.contains("喷锡")) {
        // 喷锡：检查是否为银色系
        bool isSilverHue = (col.saturation() < 40 || (col.hue() > 160 && col.hue() < 260));
        return isSilverHue && (col.value() > goldThresh);
    }

    if (finishType.contains("OSP")) {
        // OSP：色相接近自定义 OSP 色值（默认 #F0AA93，hue≈15°），逻辑与沉金一致
        const int ospHue = getCustomOspColor().hue();
        return (std::abs(col.hue() - ospHue) < 25 &&
                col.saturation() > saturationThresh &&
                col.value() > valueThresh);
    }

    // 沉金：检查色相是否接近金色
    return (std::abs(col.hue() - goldThresh) < 25 &&
            col.saturation() > saturationThresh &&
            col.value() > valueThresh);
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
        && m_cachedMaskColor == getSolderMaskColor(maskColorName)
        && m_cachedMetalColor == getMetalRenderColor(finishType)
        && m_cachedBareColor == getBareSubstrateColor()
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
    m_cachedMaskColor = getSolderMaskColor(maskColorName);
    m_cachedMetalColor = getMetalRenderColor(finishType);
    m_cachedBareColor = getBareSubstrateColor();
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
    QImage& outCompositeBase,
    const Regions::RenderContext* regions) const {

    int w = srcImage.width();
    int h = srcImage.height();

    QColor maskColor = getSolderMaskColor(maskColorName);
    QColor silkColor = getSilkColor(maskColorName);
    QColor metalRenderColor = getMetalRenderColor(finishType);
    QColor bareSubstrateColor = getBareSubstrateColor();

    const int grayMinPct = qBound(0, qMin(bareSubstrateGrayMinPct, bareSubstrateGrayMaxPct), 100);
    const int grayMaxPct = qBound(0, qMax(bareSubstrateGrayMinPct, bareSubstrateGrayMaxPct), 100);
    const int similarityThreshold = qBound(0, bareSubstrateColorSimilarityPct, 100);

    // Allow copper-under-mask threshold to vary across the full slider range (0..254).
    // Previous code clamped this to transThresh+1 which made increases past that point have no effect.
    int effectiveCopperThresh = qBound(0, copperUnderMaskThresh, 254);

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

        quint32 previousOwner = std::numeric_limits<quint32>::max();
        const Regions::Parameters* local = nullptr;
        for (int x = 0; x < w; ++x) {
            if (regions) {
                const quint32 owner = regions->owners[y * w + x];
                if (owner != previousOwner) {
                    previousOwner = owner;
                    auto it = regions->parameters.constFind(owner);
                    local = it == regions->parameters.constEnd() ? nullptr : &it.value();
                }
            }
            const int pixelGold = local ? (*local)[Regions::Gold] : goldThresh;
            const int pixelSilk = local ? (*local)[Regions::Silk] : silkThresh;
            const int pixelTrans = local ? (*local)[Regions::Trans] : transThresh;
            const int pixelCopper = local ? qBound(0, (*local)[Regions::Copper], 254) : effectiveCopperThresh;
            const int pixelBareMin = local ? qMin((*local)[Regions::BareMin], (*local)[Regions::BareMax]) : grayMinPct;
            const int pixelBareMax = local ? qMax((*local)[Regions::BareMin], (*local)[Regions::BareMax]) : grayMaxPct;
            const int pixelSimilarity = local ? (*local)[Regions::BareSimilarity] : similarityThreshold;
            QColor col(lineSrc[x]);
            int gray = qGray(lineSrc[x]);
            int grayPct = qRound(gray * 100.0 / 255.0);

            bool isMetalPixel = isMetal(col, finishType, pixelGold);
            // 丝印判定：深色阻焊 = 源图亮像素（白墨印深色板）；
            // 白色阻焊 = 源图暗像素（黑墨印白板）——色彩逻辑与其他阻焊相反，
            // 保证输出明暗与源图一致（该白的地方白、该黑的地方黑）。
            bool silk = !isMetalPixel && (isWhiteMask
                ? (gray < (255 - pixelSilk))
                : (gray > pixelSilk));
            // 敷铜判定：深色阻焊 = 灰度较亮处；白色阻焊相反 = 灰度较深处
            // （还没到黑色丝印的那一段），有铜的白油显浅灰、无铜的白油显白。
            bool copperUnderMask = !isMetalPixel && !silk && (isWhiteMask
                ? (gray < pixelCopper)
                : (gray > pixelCopper));
            bool bareSubstratePixel = false;

            if (enableBareSubstrate && !isMetalPixel) {
                if (bareSubstrateUseGrayBinding) {
                    bareSubstratePixel = (grayPct >= pixelBareMin && grayPct <= pixelBareMax);
                } else {
                    bareSubstratePixel = (colorSimilarityPercent(col, bareSubstrateColor) >= pixelSimilarity);
                }
            }

            // 如果该像素被判定为裸露基材，则该处不应被视为敷铜（阻断铜层输出）
            if (bareSubstratePixel) {
                copperUnderMask = false;
            }

            // 裸露基材同时作用于丝印层剔除和阻焊开窗：启用裸露基材时，相应位置应当被视为阻焊开窗（即不覆盖阻焊）。
            bool bottomOpen = (gray > pixelTrans);
            bool maskOpen = isMetalPixel || (silk && !isWhiteMask) || bareSubstratePixel;

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
                    if (isWhiteMask) {
                        // 白色阻焊特例：无铜（白油盖基材）显白、有铜（白油盖铜）显浅灰，
                        // 与深色阻焊相反——深色靠敷铜 lighter() 提亮，白色靠敷铜向金属色
                        // 靠拢压灰（介于纯白与喷锡之间、更接近纯白）。
                        // 注意：不能用 metalRenderColor 混色——沉金时金属色是金色，
                        // 会让浅灰发黄；这里固定用中性金属灰，不受表面处理影响。
                        // 混色权重整体提高（250 vs 205），避免深棕底色把白色压成灰白。
                        QColor appliedMask = copperUnderMask
                            ? blendColor(maskColor, QColor(200, 200, 210), 60)   // 敷铜：固定中性浅灰
                            : maskColor;                                          // 无敷铜：白色
                        pixelRes = blendColor(pixelRes, appliedMask, 250);
                    } else {
                        QColor appliedMask = copperUnderMask ? maskColor.lighter(135) : maskColor;
                        pixelRes = blendColor(pixelRes, appliedMask, copperUnderMask ? 170 : 205);
                    }
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
    bool renderLEDs,
    const Regions::RenderContext* regions) {
    if (srcImage.isNull()) {
        outCopper = QImage();
        outMask = QImage();
        outSilk = QImage();
        outBottom = QImage();
        outComposite = QImage();
        return;
    }

    const quint64 regionRevision = regions ? regions->revision : 0;
    if (m_cachedRegionRevision != regionRevision || !isBaseCacheValid(srcImage, goldThresh, silkThresh, transThresh, copperUnderMaskThresh, maskColorName, finishType, isWhiteMask,
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
            outComposite,
            regions);

        m_cachedRegionRevision = regionRevision;
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
