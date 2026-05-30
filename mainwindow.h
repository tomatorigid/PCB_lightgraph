#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QMap>
#include <QVector>
#include <QPoint>
#include <QComboBox>
#include <QPainter>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
class QAction;
class QTimer;
#include "imageprocessor.h"
#include "edgesharpener.h"
#include "ledlayoutengine.h"
#include "layergenerator.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    bool eventFilter(QObject *obj, QEvent *event) override;
protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void loadAndProcess();
    void updateProcess();
    void exportLayers();
    void saveProject();
    void importProject();
    void openPaintEditor();
    void autoSuggestLEDs();
    void openFilterPreprocessDialog();
    void openDouglasPeuckerDialog();

private:
    void setupUI();
    QSlider* createSlider(QString title, int min, int max, int def, class QVBoxLayout* layout);
    float distanceToSegment(QPoint p, QPoint v, QPoint w);
    void updateCompositePreview(const QImage& img);
    bool mapLabelToImage(const QPoint& labelPos, QPoint& imgPos) const;
    void clampPreviewPan();
    void initTempWorkspace();
    void cleanupTempImages(const QString& keepImagePath = QString());
    bool loadImageFromPath(const QString& filePath, bool alreadyInTemp = false);
    QString resolveCurrentTempImagePath() const;
    void syncArgsToJson();
    bool loadArgsFromJson(const QString& argsPath);
    bool saveProjectToBlg(const QString& blgPath);
    bool importProjectFromBlg(const QString& blgPath);
    void checkTempImageUpdated();

    QImage m_origin;
    QMap<QString, QImage> m_layers;
    QVector<LEDStrip> m_ledStrips;
    QImage processedOrigin;
    QImage m_previewComposite;

    QPoint m_pendingStart;
    bool m_isPlacing = false;
    bool m_isPanningPreview = false;
    QPoint m_lastPanPos;
    QPointF m_previewPan;
    double m_previewZoom = 1.0;

    // UI 组件
    QLabel *l_copper, *l_mask, *l_silk, *l_bottom, *l_composite;
    QSlider *s_silk, *s_gold, *s_trans, *s_ledRad;
    QComboBox *combo_maskColor; // 阻焊颜色选择
    QPushButton *btn_export;
    QAction *action_exportLayers = nullptr;
    QComboBox *combo_surfaceFinish; // 表面处理：沉金 / 喷锡
    QCheckBox *check_lightEnable;
    QGroupBox *group_edgeOperation;
    QCheckBox *check_edgeEnable;
    QRadioButton *radio_edgeStroke;
    QRadioButton *radio_edgeEnhance;
    QSlider *s_edgeThresh, *s_autoInvert, *s_edgeThreshMax;
    QSlider *s_autoSense;
    QSlider *s_copperDepth; // 敷铜层较深阈值
    QSlider *s_ledIntensity; // 灯光中心不透明度控制 (0-255)
    QCheckBox *check_showLEDOverlay; // 在主预览上叠加显示灯光与范围
    QCheckBox *check_bareSubstrateEnable;
    QRadioButton *radio_bareSubstrateGray;
    QRadioButton *radio_bareSubstrateColor;
    QSlider *s_bareSubstrateGrayA;
    QSlider *s_bareSubstrateGrayB;
    QSlider *s_bareSubstrateColorSimilarity;

    // 实验性：边缘滤波预处理，同时影响描边与边缘增强
    bool m_edgePrefilterEnabled = true;
    int m_edgePrefilterKernelSize = 5;
    double m_edgePrefilterSigma = 1.1;

    // Douglas-Peucker settings (experimental)
    bool m_dpEnabled = false;
    double m_dpTolerance = 1.0;
    int m_dpLineWidth = 2;

    // 工程临时工作区
    QString m_tempDirPath;
    QString m_tempImagePath;
    QString m_tempArgsPath;
    bool m_isApplyingArgs = false;
    QTimer *m_tempReloadTimer = nullptr;
    qint64 m_tempImageMTimeMs = -1;
    qint64 m_tempImageSize = -1;

    // 子模块实例
    ImageProcessor m_imageProcessor;
    EdgeSharpener m_edgeSharpener;
    LEDLayoutEngine m_ledLayoutEngine;
    LayerGenerator m_layerGenerator;
};

#endif
