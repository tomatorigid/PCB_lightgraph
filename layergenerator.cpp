#include "layergenerator.h"
#include <QPainter>
#include <QDebug>
#include "ledstrip.h"

LayerGenerator::LayerGenerator() {
}

LayerGenerator::~LayerGenerator() {
}

QImage LayerGenerator::getInvertedImage(const QImage& img) {
    QImage inv = img.copy();
    inv.invertPixels();
    return inv;
}

void LayerGenerator::applyEDACalibrationPatch(QImage& img) {
    int w = img.width();
    int h = img.height();

    // 左上角锚点
    img.setPixel(0, 0, 1);
    img.setPixel(0, 1, 0);
    img.setPixel(1, 0, 0);

    // 右下角锚点
    if (w > 1 && h > 1) {
        img.setPixel(w - 1, h - 1, 1);
        img.setPixel(w - 1, h - 2, 0);
        img.setPixel(w - 2, h - 1, 0);
    }
}

void LayerGenerator::generateLayers(
    const QImage& imgCopper,
    const QImage& imgMask,
    const QImage& imgSilk,
    const QImage& imgBottom,
    QMap<QString, QImage>& outLayers) {

    outLayers["Top_Copper"] = getInvertedImage(imgCopper);
    outLayers["Top_Mask"] = getInvertedImage(imgMask);
    outLayers["Top_Silk"] = getInvertedImage(imgSilk);
    outLayers["Bottom_Mask"] = getInvertedImage(imgBottom);
}

QImage LayerGenerator::generateLEDReferenceLayer(
    int width,
    int height,
    const QVector<LEDStrip>& ledStrips) {

    QImage imgLED_Export(width, height, QImage::Format_ARGB32);
    imgLED_Export.fill(Qt::transparent);

    QPainter painter(&imgLED_Export);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const auto& s : ledStrips) {
        painter.setPen(QPen(s.color, width / 60, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(s.start, s.end);
    }
    painter.end();

    return imgLED_Export;
}

bool LayerGenerator::exportLayersToFiles(
    const QMap<QString, QImage>& layers,
    const QString& directory,
    const QString& finishName,
    const QVector<LEDStrip>& ledStrips) {

    if (directory.isEmpty()) {
        return false;
    }

    // 1. 导出四个生产层 (反色 1-bit)
    QMapIterator<QString, QImage> i(layers);
    while (i.hasNext()) {
        i.next();

        // 转换为单色位图
        QImage exp = i.value().convertToFormat(QImage::Format_Mono);

        // 应用 EDA 锚点补丁
        applyEDACalibrationPatch(exp);

        QString path = QString("%1/%2_%3_Inverted.png")
            .arg(directory)
            .arg(i.key())
            .arg(finishName);

        if (!exp.save(path)) {
            qWarning() << "Failed to save:" << path;
            return false;
        }
    }

    // 2. 导出灯条参考层 (透明底色 RGBA)
    int w = (layers.isEmpty()) ? 1024 : layers.first().width();
    int h = (layers.isEmpty()) ? 768 : layers.first().height();

    QImage imgLED_Export = generateLEDReferenceLayer(w, h, ledStrips);
    QString ledPath = directory + "/LED_Placement_Reference.png";

    if (!imgLED_Export.save(ledPath)) {
        qWarning() << "Failed to save LED reference:" << ledPath;
        return false;
    }

    return true;
}

