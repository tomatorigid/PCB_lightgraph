#ifndef LAYERGENERATOR_H
#define LAYERGENERATOR_H

#include <QImage>
#include <QMap>
#include <QString>
#include <QVector>
#include "ledstrip.h"

/**
 * @brief 生产层生成和导出
 * 负责生成反色位图和灯条参考图
 */
class LayerGenerator {
public:
    LayerGenerator();
    ~LayerGenerator();

    /**
     * @brief 从处理后的图像生成各生产层
     */
    void generateLayers(
        const QImage& imgCopper,
        const QImage& imgMask,
        const QImage& imgSilk,
        const QImage& imgBottom,
        QMap<QString, QImage>& outLayers
    );

    /**
     * @brief 导出所有层为文件
     * @param layers 生产层集合
     * @param directory 导出目录
     * @param finishName 表面处理工艺名称 (ENIG/HASL)
     * @param ledStrips LED 灯条数据
     * @return 导出成功返回 true
     */
    bool exportLayersToFiles(
        const QMap<QString, QImage>& layers,
        const QString& directory,
        const QString& finishName,
        const QVector<LEDStrip>& ledStrips
    );

    /**
     * @brief 获取反色图像
     */
    static QImage getInvertedImage(const QImage& img);

    /**
     * @brief 生成 LED 参考层图像
     */
    static QImage generateLEDReferenceLayer(
        int width,
        int height,
        const QVector<LEDStrip>& ledStrips
    );

private:
    /**
     * @brief 为 EDA 锚点补丁
     */
    static void applyEDACalibrationPatch(QImage& img);
};

#endif // LAYERGENERATOR_H

