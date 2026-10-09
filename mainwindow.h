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
class QPropertyAnimation;
class QMenu;
#include "imageprocessor.h"
#include "edgesharpener.h"
#include "ledlayoutengine.h"
#include "layergenerator.h"
#include "progressiverendercontroller.h"
#include "regionmodel.h"
#include "regionselection.h"
#include <QPointer>
#include <memory>
class QListWidget;
class RegionSlider;
class QVBoxLayout;
class QScrollArea;

class MainWindow : public QMainWindow {
    Q_OBJECT

    struct PreviewState {
        double zoom = 1.0;
        QPointF pan = QPointF(0, 0);
        bool isPanning = false;
        QPoint lastPanPos;
    };

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
    void openColorSettingsDialog();
    void resetAllSettings();

private:
    friend class RegionTests;
    void setupUI();
    void updateControlWidth();
    void setupRegionUI(QVBoxLayout* layout);
    void refreshRegionUI();
    Regions::Parameters globalRegionParameters() const;
    void setGlobalRegionParameters(const Regions::Parameters& values);
    void trackGlobalRegionSlider(QSlider* slider);
    void beginRegionSlider(QSlider* slider);
    void endRegionSlider(QSlider* slider);
    void undoRegionChange(bool redo);
    void cancelRegionGesture();
    void setRegionEditing(bool enabled);
    void setRegionTool(Regions::Tool tool);
    void selectRegion(quint32 id);
    void startRegionSelection(const QPoint& point, Regions::Operation operation, const QPainterPath* geometry = nullptr);
    bool handleRegionEvent(QLabel* label, QEvent* event);
    bool regionPoint(QLabel* label, const QPointF& point, QPointF& result, bool outside = false) const;
    void updateGesturePath();
    void refreshRegionPreviews();
    void scheduleRegionOverlay();
    void paintRegionOverlay(QPainter& painter, const QRectF& target);
    QJsonObject projectArgs() const;
    bool validateProjectArgs(const QJsonObject& root, const QSize& size, QVector<Regions::Region>& regions) const;
    void applyProjectArgs(const QJsonObject& root, const QVector<Regions::Region>& regions);

    /**
     * @brief 按指定阶段尺寸生成预览，并在完整分辨率阶段更新导出数据
     * @param targetSize 当前阶段的渲染尺寸
     * @param generation 当前请求所属的渲染代次
     * @param authoritative 是否允许本阶段结果更新导出数据
     */
    void renderAtSize(const QSize& targetSize, quint64 generation, bool authoritative);
    // 获取当前阶段稳定的渲染输入图像（同一源图+同一目标尺寸时复用缓存，
    // 保证 EdgeSharpener/ImageProcessor 的 cacheKey 缓存能跨帧生效）。
    QImage renderOriginForSize(const QSize& targetSize);
    // 内容容器展开/收起动画（maximumHeight/Width 动画，180ms InOutCubic；horizontal=true 时横向）
    void toggleContent(QWidget *content, bool expand, bool horizontal = false);
    QSlider* createSlider(QString title, int min, int max, int def, class QVBoxLayout* layout);
    float distanceToSegment(QPoint p, QPoint v, QPoint w);
    void updateCompositePreview(const QImage& img);
    void updateLayerPreview(QLabel* label, const QImage& img, PreviewState& state);
    bool mapLabelToImage(const QPoint& labelPos, QPoint& imgPos) const;
    void clampPreviewPan();
    void clampPreviewPan(QLabel* label, const QImage& img, PreviewState& state);
    bool handleLayerPreviewEvent(QLabel* label, QEvent* event, const QImage& img, PreviewState& state);
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
    // 生产层只保存最近一次全分辨率结果，低分辨率帧不得污染导出数据。
    QMap<QString, QImage> m_layers;
    // 预览层保存当前可见阶段，可随自适应预览、可选细化和原图逐级替换。
    QMap<QString, QImage> m_previewLayers;
    QVector<LEDStrip> m_ledStrips;
    QImage processedOrigin;
    QImage m_previewComposite;
    // 记录生产层对应的渲染代次，导出前据此判断是否需要补做完整渲染。
    quint64 m_fullResolutionGeneration = 0;

    // 渲染输入缓存：拖动滑块时目标尺寸不变，复用同一张缩放图像，
    // 使下游基于 cacheKey 的缓存（EdgeSharpener/ImageProcessor）直接命中，
    // 避免每一帧都重新缩放并因此使缓存全部失效。
    quint64 m_cachedRenderOriginSourceKey = 0;
    QImage m_cachedRenderOrigin;

    QPoint m_pendingStart;
    bool m_isPlacing = false;
    bool m_isPanningPreview = false;
    QPoint m_lastPanPos;
    QPointF m_previewPan;
    double m_previewZoom = 1.0;

    QMap<QLabel*, QString> m_layerPreviewKeys;
    QMap<QLabel*, PreviewState> m_layerPreviewStates;

    Regions::Model m_regions;
    Regions::Tool m_regionTool = Regions::Tool::Select;
    Regions::Operation m_gestureOperation = Regions::Operation::New;
    bool m_regionEditing = false, m_regionUIReady = false, m_gestureActive = false;
    QScrollArea* m_controlScroll = nullptr;
    bool m_controlWidthQueued = false;
    bool m_regionSelectPanning = false, m_regionSelectDragged = false, m_overlayQueued = false;
    QPoint m_regionPressPos;
    QPointer<QLabel> m_regionPressLabel;
    Qt::MouseButton m_regionStartButton = Qt::NoButton;
    QVector<QPointF> m_gesturePoints;
    QPainterPath m_gesturePath;
    QPointer<Regions::SelectionJob> m_regionJob;
    std::unique_ptr<Regions::Snapshot> m_sliderBefore;
    QPointer<QSlider> m_historySlider;
    Regions::Parameters m_knownGlobals;
    std::array<QSlider*, Regions::ParameterCount> m_globalRegionSliders{{nullptr}};
    std::array<RegionSlider*, Regions::ParameterCount> m_localRegionSliders{{nullptr}};
    std::array<QWidget*, Regions::ParameterCount> m_localRegionRows{{nullptr}};
    QWidget *m_regionContent = nullptr, *m_localRegionGroup = nullptr;
    QWidget *m_regionBrushControls = nullptr, *m_regionWandControls = nullptr;
    QCheckBox *m_regionEnabled = nullptr, *m_regionHighlight = nullptr, *m_wandGlobal = nullptr;
    QListWidget* m_regionList = nullptr;
    QComboBox* m_regionOperation = nullptr;
    QSlider *m_regionBrushSize = nullptr, *m_wandTolerance = nullptr;
    QLabel* m_regionBusy = nullptr;
    QComboBox* m_regionToolCombo = nullptr;
    QPushButton *m_regionRename = nullptr, *m_regionDelete = nullptr, *m_regionReset = nullptr;
    QPushButton *m_regionCopy = nullptr, *m_regionUndo = nullptr, *m_regionRedo = nullptr;

    // UI 组件
    QLabel *l_copper, *l_mask, *l_silk, *l_bottom, *l_composite;
    QSlider *s_silk, *s_gold, *s_trans, *s_ledRad;
    QComboBox *combo_maskColor; // 阻焊颜色选择
    QPushButton *btn_export;
    QAction *action_exportLayers = nullptr;
    QComboBox *combo_surfaceFinish; // 表面处理：沉金 / 喷锡
    QCheckBox *check_lightEnable;
    QCheckBox *check_expandPreviews; // 控制右侧四个预览是否展开
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
    QCheckBox *check_useMetalEdge; // 在边缘操作中：使用金属勾线
    QCheckBox *check_exposeMetalEdge; // 裸露金属勾线（当使用金属时，是否在阻焊开窗以露出铜），默认勾选

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

    // 可折叠分组框：标题点击展开/收起（带动画）
    QMap<QGroupBox*, QWidget*> m_collapsibleGroups;       // 组框 -> 内容容器
    QMap<QGroupBox*, QCheckBox*> m_groupToggleCheckbox;   // 组框 -> 主开关（标题点击等价于点开关）
    QMap<QWidget*, QPropertyAnimation*> m_collapseAnims;  // 进行中的折叠动画
    QMenu *m_scaleMenu = nullptr;                         // 界面缩放菜单（重置设置后同步勾选态）

    // 子模块实例
    ImageProcessor m_imageProcessor;
    EdgeSharpener m_edgeSharpener;
    LEDLayoutEngine m_ledLayoutEngine;
    LayerGenerator m_layerGenerator;
    // 调度器只负责代次和阶段顺序，实际图像处理仍由主窗口同步执行。
    ProgressiveRenderController m_progressiveRenderController;
};

#endif
