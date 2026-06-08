#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSlider>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QGroupBox>
#include <QDir>
#include <QDirIterator>
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QProcess>
#include <cmath>
#include <QDebug>

namespace {
static QRectF calcPreviewRect(const QSize& labelSize, const QSize& imageSize, double zoom, const QPointF& pan) {
    if (!labelSize.isValid() || !imageSize.isValid() || imageSize.isEmpty()) return QRectF();

    const double sx = static_cast<double>(labelSize.width()) / imageSize.width();
    const double sy = static_cast<double>(labelSize.height()) / imageSize.height();
    const double fit = qMin(sx, sy);
    const double drawW = imageSize.width() * fit * zoom;
    const double drawH = imageSize.height() * fit * zoom;
    const double x = (labelSize.width() - drawW) * 0.5 + pan.x();
    const double y = (labelSize.height() - drawH) * 0.5 + pan.y();
    return QRectF(x, y, drawW, drawH);
}



// 统一获取导入图片的默认像素上限（可在一个地方调整）
static qint64 getMaxImportPixels() {
    return 16000000LL;
}

static QSize scaleDownToPixelLimit(const QSize& src, qint64 maxPixels) {
    if (!src.isValid() || src.isEmpty() || maxPixels <= 0) return QSize();

    const qint64 srcPixels = static_cast<qint64>(src.width()) * static_cast<qint64>(src.height());
    if (srcPixels < maxPixels) return src;

    // 目标：按比例缩小到“像素数低于 maxPixels 的最大可能值”
    const double scale = std::sqrt(static_cast<double>(maxPixels - 1) / static_cast<double>(srcPixels));
    int w = qMax(1, static_cast<int>(std::floor(src.width() * scale)));
    int h = qMax(1, static_cast<int>(std::floor(src.height() * scale)));

    // 保险校验：如果因为取整导致仍然超限，则继续微调到低于阈值
    while (static_cast<qint64>(w) * static_cast<qint64>(h) >= maxPixels && (w > 1 || h > 1)) {
        if (w >= h && w > 1) --w;
        else if (h > 1) --h;
        else break;
    }

    return QSize(w, h);
}

// 根据文件头二进制判断图片真实格式，返回常见小写扩展名（例如 "png", "jpg", "gif", "bmp", "webp"），无法判断时返回空串
static QString detectImageExtensionFromContent(const QString &filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QByteArray hdr = f.read(16);
    f.close();

    if (hdr.size() >= 8 && hdr.startsWith("\x89PNG\r\n\x1A\n")) return QStringLiteral("png");
    if (hdr.size() >= 3 && (uchar)hdr[0] == 0xFF && (uchar)hdr[1] == 0xD8 && (uchar)hdr[2] == 0xFF) return QStringLiteral("jpg");
    if (hdr.startsWith("GIF87a") || hdr.startsWith("GIF89a")) return QStringLiteral("gif");
    if (hdr.size() >= 2 && hdr[0] == 'B' && hdr[1] == 'M') return QStringLiteral("bmp");
    if (hdr.size() >= 12 && hdr.startsWith("RIFF") && hdr.mid(8,4) == "WEBP") return QStringLiteral("webp");
    return QString();
}

static bool isSupportedImageExtension(const QString& extLower) {
    return extLower == "png" || extLower == "jpg" || extLower == "jpeg" || extLower == "bmp" || extLower == "gif" || extLower == "webp";
}

static QString psSingleQuoted(const QString& value) {
    QString escaped = value;
    escaped.replace("'", "''");
    return QString("'%1'").arg(escaped);
}

static qint64 fileStampMs(const QFileInfo& fi) {
    return fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUI();
    initTempWorkspace();
    syncArgsToJson();

    m_tempReloadTimer = new QTimer(this);
    connect(m_tempReloadTimer, &QTimer::timeout, this, &MainWindow::checkTempImageUpdated);
    m_tempReloadTimer->start(1200);

    setWindowTitle("PCB 透光画拆分工具v1.3.1 https://github.com/tomatorigid/PCB_lightgraph");
}

MainWindow::~MainWindow() {
    if (m_tempReloadTimer) m_tempReloadTimer->stop();
    cleanupTempImages();
}

void MainWindow::setupUI() {
    QWidget *central = new QWidget;
    QHBoxLayout *mainLayout = new QHBoxLayout(central);

    // --- 左侧：动态效果预览 ---
    QVBoxLayout *leftLayout = new QVBoxLayout;

    l_composite = new QLabel("请导入图片...");
    l_composite->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    l_composite->setMinimumSize(400, 400); // 设置保底尺寸
    l_composite->setAlignment(Qt::AlignCenter); // 关键：居中显示，方便计算偏移
    l_composite->setStyleSheet("border: 2px solid #FFD700; background: #1a1a1a;");
    l_composite->installEventFilter(this);

    leftLayout->addWidget(new QLabel("<b>【预览区】支持中建或右键拖动缩放（滚轮），左键点击画面手动布灯</b>"));
    leftLayout->addWidget(l_composite, 5); // 权重分配

    // --- 中间：控制台 ---
    QVBoxLayout *ctrl = new QVBoxLayout;
    ctrl->setContentsMargins(10, 10, 10, 10);
    ctrl->setSpacing(10);

    ctrl->addWidget(new QLabel("<b>核心参数控制</b>"));

    QGroupBox *group_basic = new QGroupBox("基础参数");
    QVBoxLayout *basicLayout = new QVBoxLayout(group_basic);
    combo_surfaceFinish = new QComboBox();
    combo_surfaceFinish->addItems({"沉金 (亮金)", "喷锡 (银色)"});
    connect(combo_surfaceFinish, SIGNAL(currentIndexChanged(int)), this, SLOT(updateProcess()));
    basicLayout->addWidget(new QLabel("表面处理工艺:"));
    basicLayout->addWidget(combo_surfaceFinish);

    combo_maskColor = new QComboBox();
    combo_maskColor->addItems({"蓝色", "黑色", "红色", "绿色", "白色"});
    connect(combo_maskColor, SIGNAL(currentIndexChanged(int)), this, SLOT(updateProcess()));
    basicLayout->addWidget(new QLabel("阻焊颜色:"));
    basicLayout->addWidget(combo_maskColor);

    s_gold   = createSlider("金色/银色判定", 0, 359, 45, basicLayout);
    s_silk   = createSlider("丝印阈值", 0, 255, 180, basicLayout);
    s_trans  = createSlider("基材透光阈值", 0, 255, 120, basicLayout);
    s_copperDepth = createSlider("敷铜层较深阈值", 0, 255, 150, basicLayout);
    ctrl->addWidget(group_basic);

    QGroupBox *group_light = new QGroupBox("灯光 / 显示 / 布灯");
    QVBoxLayout *lightLayout = new QVBoxLayout(group_light);
    check_lightEnable = new QCheckBox("启用灯光设置");
    check_lightEnable->setChecked(false);
    lightLayout->addWidget(check_lightEnable);

    QWidget *lightDetailWidget = new QWidget(group_light);
    QVBoxLayout *lightDetailLayout = new QVBoxLayout(lightDetailWidget);
    lightDetailLayout->setContentsMargins(0, 0, 0, 0);

    check_showLEDOverlay = new QCheckBox("在预览中叠加显示灯光与范围");
    connect(check_showLEDOverlay, &QCheckBox::toggled, [=](bool){ updateProcess(); });
    lightDetailLayout->addWidget(check_showLEDOverlay);

    s_autoSense = createSlider("自动布灯敏感度(0关)", 0, 10, 1, lightDetailLayout);
    QPushButton *btn_auto = new QPushButton("自动重心布灯");
    connect(btn_auto, &QPushButton::clicked, this, &MainWindow::autoSuggestLEDs);
    lightDetailLayout->addWidget(btn_auto);

    s_ledRad = createSlider("灯光散射半径", 20, 500, 150, lightDetailLayout);
    s_ledIntensity = createSlider("灯光中心不透明度", 0, 255, 200, lightDetailLayout);
    lightLayout->addWidget(lightDetailWidget);
    ctrl->addWidget(group_light);

    auto updateLightDetailState = [=]() {
        lightDetailWidget->setVisible(check_lightEnable->isChecked());
    };
    connect(check_lightEnable, &QCheckBox::toggled, [=](bool){
        updateLightDetailState();
        updateProcess();
    });
    updateLightDetailState();

    QGroupBox *group_bareSubstrate = new QGroupBox("裸露基材绑定");
    QVBoxLayout *bareGroupLayout = new QVBoxLayout(group_bareSubstrate);
    check_bareSubstrateEnable = new QCheckBox("启用裸露基材");
    bareGroupLayout->addWidget(check_bareSubstrateEnable);

    QWidget *bareDetailWidget = new QWidget(group_bareSubstrate);
    QVBoxLayout *bareDetailLayout = new QVBoxLayout(bareDetailWidget);
    bareDetailLayout->setContentsMargins(0, 0, 0, 0);

    QButtonGroup *bareSubstrateModeGroup = new QButtonGroup(central);
    bareSubstrateModeGroup->setExclusive(true);
    radio_bareSubstrateGray = new QRadioButton("1. 绑定层次（用灰度判断）");
    radio_bareSubstrateColor = new QRadioButton("2. 绑定颜色（与基材色相似）");
    bareSubstrateModeGroup->addButton(radio_bareSubstrateGray);
    bareSubstrateModeGroup->addButton(radio_bareSubstrateColor);
    radio_bareSubstrateGray->setChecked(true);

    QVBoxLayout *grayModeLayout = new QVBoxLayout;
    grayModeLayout->setContentsMargins(0, 0, 0, 0);
    grayModeLayout->addWidget(radio_bareSubstrateGray);
    s_bareSubstrateGrayA = createSlider("灰度下限A (%)", 0, 100, 20, grayModeLayout);
    s_bareSubstrateGrayB = createSlider("灰度上限B (%)", 0, 100, 65, grayModeLayout);

    QVBoxLayout *colorModeLayout = new QVBoxLayout;
    colorModeLayout->setContentsMargins(0, 0, 0, 0);
    colorModeLayout->addWidget(radio_bareSubstrateColor);
    s_bareSubstrateColorSimilarity = createSlider("颜色相似度C (%)", 0, 100, 80, colorModeLayout);

    bareDetailLayout->addLayout(grayModeLayout);
    bareDetailLayout->addLayout(colorModeLayout);
    bareGroupLayout->addWidget(bareDetailWidget);
    ctrl->addWidget(group_bareSubstrate);

    auto updateBareSubstrateControlState = [=]() {
        const bool masterEnabled = check_bareSubstrateEnable->isChecked();
        const bool grayMode = radio_bareSubstrateGray->isChecked();

        bareDetailWidget->setVisible(masterEnabled);

        radio_bareSubstrateGray->setEnabled(masterEnabled);
        radio_bareSubstrateColor->setEnabled(masterEnabled);
        s_bareSubstrateGrayA->setEnabled(masterEnabled && grayMode);
        s_bareSubstrateGrayB->setEnabled(masterEnabled && grayMode);
        s_bareSubstrateColorSimilarity->setEnabled(masterEnabled && !grayMode);
    };

    connect(check_bareSubstrateEnable, &QCheckBox::toggled, [=](bool){
        updateBareSubstrateControlState();
        updateProcess();
    });
    connect(radio_bareSubstrateGray, &QRadioButton::toggled, [=](bool){
        updateBareSubstrateControlState();
        updateProcess();
    });
    connect(radio_bareSubstrateColor, &QRadioButton::toggled, [=](bool){
        updateBareSubstrateControlState();
        updateProcess();
    });
    updateBareSubstrateControlState();

    group_edgeOperation = new QGroupBox("边缘操作");
    QVBoxLayout *edgeOpLayout = new QVBoxLayout(group_edgeOperation);
    edgeOpLayout->setContentsMargins(8, 8, 8, 8);
    edgeOpLayout->setSpacing(6);

    check_edgeEnable = new QCheckBox("启用边缘操作");
    edgeOpLayout->addWidget(check_edgeEnable);

    QWidget *edgeDetailWidget = new QWidget(group_edgeOperation);
    QVBoxLayout *edgeDetailLayout = new QVBoxLayout(edgeDetailWidget);
    edgeDetailLayout->setContentsMargins(0, 0, 0, 0);

    QButtonGroup *edgeModeGroup = new QButtonGroup(group_edgeOperation);
    edgeModeGroup->setExclusive(true);
    radio_edgeStroke = new QRadioButton("描边");
    radio_edgeEnhance = new QRadioButton("边缘增强");
    edgeModeGroup->addButton(radio_edgeStroke);
    edgeModeGroup->addButton(radio_edgeEnhance);
    radio_edgeEnhance->setChecked(true);
    edgeDetailLayout->addWidget(radio_edgeStroke);
    edgeDetailLayout->addWidget(radio_edgeEnhance);

    s_edgeThresh = createSlider("强边缘阈值/边缘下限阈值", 0, 255, 50, edgeDetailLayout);
    s_edgeThreshMax = createSlider("弱边缘阈值/边缘上限阈值", 0, 255, 200, edgeDetailLayout); // 新增上限，默认值设高一些
    s_autoInvert = createSlider("自动反色范围", -1, 50, 10, edgeDetailLayout);
    edgeOpLayout->addWidget(edgeDetailWidget);
    ctrl->addWidget(group_edgeOperation);

    // 初始化状态控制：总开关决定模式单选和共用滑条是否启用
    edgeDetailWidget->setVisible(false);

    auto updateEdgeControlState = [=]() {
        const bool enabled = check_edgeEnable->isChecked();
        edgeDetailWidget->setVisible(enabled);
    };

    connect(check_edgeEnable, &QCheckBox::toggled, [=](bool){
        updateEdgeControlState();
        updateProcess();
    });
    connect(radio_edgeStroke, &QRadioButton::toggled, [=](bool checked){
        if (checked) updateProcess();
    });
    connect(radio_edgeEnhance, &QRadioButton::toggled, [=](bool checked){
        if (checked) updateProcess();
    });
    updateEdgeControlState();

    QGroupBox *group_actions = new QGroupBox("图纸操作");
    QVBoxLayout *actionLayout = new QVBoxLayout(group_actions);
    QHBoxLayout *actionButtonsLayout = new QHBoxLayout;

    QPushButton *btn_import = new QPushButton("导入图纸");
    btn_import->setMinimumHeight(50);
    connect(btn_import, &QPushButton::clicked, this, &MainWindow::loadAndProcess);

    btn_export = new QPushButton("导出图纸");
    btn_export->setEnabled(false);
    btn_export->setMinimumHeight(50);
    btn_export->setStyleSheet("background-color: #2d5a2d; color: white; font-weight: bold;");
    connect(btn_export, &QPushButton::clicked, this, &MainWindow::exportLayers);
    actionButtonsLayout->addWidget(btn_import);
    actionButtonsLayout->addWidget(btn_export);
    actionLayout->addLayout(actionButtonsLayout);
    ctrl->addWidget(group_actions);
    ctrl->addStretch();

    // --- 右侧：四个生产层预览 ---
    QGridLayout *rightGrid = new QGridLayout;
    auto createSubLabel = [&](QString title, QLabel*& lbl) {
        QVBoxLayout *v = new QVBoxLayout;
        v->addWidget(new QLabel(title));
        lbl = new QLabel();
        lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        lbl->setMinimumSize(200, 200);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("border: 1px dashed #555; background: #000;");
        v->addWidget(lbl);
        return v;
    };
    rightGrid->addLayout(createSubLabel("Top Copper (线路)", l_copper), 0, 0);
    rightGrid->addLayout(createSubLabel("Top Mask (阻焊)", l_mask), 0, 1);
    rightGrid->addLayout(createSubLabel("Top Silk (丝印)", l_silk), 1, 0);
    rightGrid->addLayout(createSubLabel("Bottom Mask (透光)", l_bottom), 1, 1);

    m_layerPreviewKeys[l_copper] = "Top_Copper";
    m_layerPreviewKeys[l_mask] = "Top_Mask";
    m_layerPreviewKeys[l_silk] = "Top_Silk";
    m_layerPreviewKeys[l_bottom] = "Bottom_Mask";
    l_copper->installEventFilter(this);
    l_mask->installEventFilter(this);
    l_silk->installEventFilter(this);
    l_bottom->installEventFilter(this);

    mainLayout->addLayout(leftLayout, 4);
    mainLayout->addLayout(ctrl, 1);
    mainLayout->addLayout(rightGrid, 3);

    setCentralWidget(central);
    resize(1200, 800); // 初始窗口大小

    // 顶部菜单：File / Option
    QMenu *fileMenu = menuBar()->addMenu("File");
    QAction *importImageAction = fileMenu->addAction("导入图片");
    connect(importImageAction, &QAction::triggered, this, &MainWindow::loadAndProcess);
    action_exportLayers = fileMenu->addAction("导出图纸");
    action_exportLayers->setEnabled(false);
    connect(action_exportLayers, &QAction::triggered, this, &MainWindow::exportLayers);
    QAction *saveProjectAction = fileMenu->addAction("保存工程...");
    connect(saveProjectAction, &QAction::triggered, this, &MainWindow::saveProject);
    QAction *importProjectAction = fileMenu->addAction("导入工程...");
    connect(importProjectAction, &QAction::triggered, this, &MainWindow::importProject);
    QAction *paintLiveAction = fileMenu->addAction("画图实时编辑");
    connect(paintLiveAction, &QAction::triggered, this, &MainWindow::openPaintEditor);

    QMenu *optionMenu = menuBar()->addMenu("Option");
    QMenu *edgeMenu = optionMenu->addMenu("边缘处理");
    QMenu *experimentalMenu = edgeMenu->addMenu("实验性功能");
    QAction *filterPreprocessAction = experimentalMenu->addAction("滤波预处理");
    connect(filterPreprocessAction, &QAction::triggered, this, &MainWindow::openFilterPreprocessDialog);
    QAction *dpAction = experimentalMenu->addAction("道格拉斯-普克抽稀");
    connect(dpAction, &QAction::triggered, this, &MainWindow::openDouglasPeuckerDialog);
}

QSlider* MainWindow::createSlider(QString title, int min, int max, int def, QVBoxLayout* layout) {
    QLabel *lbl = new QLabel(QString("%1: %2").arg(title).arg(def));
    QSlider* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(def);
    layout->addWidget(lbl);
    layout->addWidget(s);
    connect(s, &QSlider::valueChanged, [=](int v){
        lbl->setText(QString("%1: %2").arg(title).arg(v));
        updateProcess();
    });
    return s;
}

void MainWindow::updateProcess() {
    if (m_origin.isNull()) {
        if (!m_isApplyingArgs) syncArgsToJson();
        return;
    }

    processedOrigin = m_origin;

    // 应用边缘操作（边缘增强 / 描边Canny），共用同一组参数控件
    if (check_edgeEnable && check_edgeEnable->isChecked()) {
        const EdgeSharpener::OperationMode edgeMode =
            (radio_edgeStroke && radio_edgeStroke->isChecked())
                ? EdgeSharpener::OperationMode::StrokeCanny
                : EdgeSharpener::OperationMode::EdgeEnhance;
        processedOrigin = m_edgeSharpener.processEdgeOperation(
            processedOrigin,
            edgeMode,
            s_edgeThresh->value(),
            s_edgeThreshMax->value(),
            s_autoInvert->value(),
            m_edgePrefilterEnabled,
            m_edgePrefilterKernelSize,
            m_edgePrefilterSigma,
            m_dpEnabled,
            m_dpTolerance,
            m_dpLineWidth
        );
    }

    // 获取参数
    QString maskColorName = combo_maskColor->currentText();
    QString finishType = combo_surfaceFinish->currentText();
    bool isWhiteMask = (maskColorName == "白色");
    bool enableBareSubstrate = (check_bareSubstrateEnable && check_bareSubstrateEnable->isChecked());
    bool bareSubstrateUseGrayBinding = (radio_bareSubstrateGray && radio_bareSubstrateGray->isChecked());
    int bareSubstrateGrayMinPct = s_bareSubstrateGrayA ? s_bareSubstrateGrayA->value() : 0;
    int bareSubstrateGrayMaxPct = s_bareSubstrateGrayB ? s_bareSubstrateGrayB->value() : 0;
    int bareSubstrateColorSimilarityPct = s_bareSubstrateColorSimilarity ? s_bareSubstrateColorSimilarity->value() : 0;

    int goldThresh = s_gold->value();
    int silkThresh = s_silk->value();
    int transThresh = s_trans->value();
    int radVal = s_ledRad->value();
    // Overlay switch is only for preview composition.
    bool showOverlay = (check_lightEnable && check_lightEnable->isChecked() && check_showLEDOverlay && check_showLEDOverlay->isChecked());

    // 使用 ImageProcessor 处理图像
    QImage imgCopper, imgMask, imgSilk, imgBottom, imgComp;

    m_imageProcessor.processImage(
        processedOrigin,
        goldThresh,
        silkThresh,
        transThresh,
        s_copperDepth->value(),
        radVal,
        maskColorName,
        finishType,
        isWhiteMask,
        enableBareSubstrate,
        bareSubstrateUseGrayBinding,
        bareSubstrateGrayMinPct,
        bareSubstrateGrayMaxPct,
        bareSubstrateColorSimilarityPct,
        imgCopper,
        imgMask,
        imgSilk,
        imgBottom,
        imgComp,
        m_ledStrips,
        false // keep base composite clean; overlay is rendered by LEDLayoutEngine below
    );

    // 生成生产层
    m_layerGenerator.generateLayers(imgCopper, imgMask, imgSilk, imgBottom, m_layers);
    /*
    // 更新 UI 显示
    auto setScaled = [this](QLabel* label, const QImage& img) {
        if (img.isNull() || label->size().isEmpty()) return;
        label->setPixmap(QPixmap::fromImage(img).scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    */
    // 叠加灯光与范围（如果启用），并在渲染时尊重敷铜与底层透光遮挡规则
    if (showOverlay) {
        m_ledLayoutEngine.renderCompositeWithLEDs(
            imgComp,
            m_ledStrips,
            s_ledRad->value(),
            imgCopper,
            imgBottom,
            showOverlay,
            s_ledIntensity->value(),
            showOverlay
        );
    }
    m_previewComposite = imgComp;
    updateCompositePreview(m_previewComposite);
    updateLayerPreview(l_copper, m_layers["Top_Copper"], m_layerPreviewStates[l_copper]);
    updateLayerPreview(l_mask, m_layers["Top_Mask"], m_layerPreviewStates[l_mask]);
    updateLayerPreview(l_silk, m_layers["Top_Silk"], m_layerPreviewStates[l_silk]);
    updateLayerPreview(l_bottom, m_layers["Bottom_Mask"], m_layerPreviewStates[l_bottom]);

    // 独立的灯条预览widget已移除，灯光在主预览上叠加显示
    if (!m_isApplyingArgs) syncArgsToJson();
}

void MainWindow::clampPreviewPan(QLabel* label, const QImage& img, PreviewState& state) {
    if (!label || img.isNull() || label->size().isEmpty()) {
        state.pan = QPointF(0, 0);
        return;
    }

    const QSize labelSize = label->size();
    const QRectF r = calcPreviewRect(labelSize, img.size(), state.zoom, QPointF(0, 0));
    const double maxPanX = qMax(0.0, (r.width() - labelSize.width()) * 0.5);
    const double maxPanY = qMax(0.0, (r.height() - labelSize.height()) * 0.5);
    state.pan.setX(qBound(-maxPanX, state.pan.x(), maxPanX));
    state.pan.setY(qBound(-maxPanY, state.pan.y(), maxPanY));
}

void MainWindow::updateLayerPreview(QLabel* label, const QImage& img, PreviewState& state) {
    if (!label || img.isNull() || label->size().isEmpty()) return;

    clampPreviewPan(label, img, state);
    const QSize labelSize = label->size();
    const QRectF targetRect = calcPreviewRect(labelSize, img.size(), state.zoom, state.pan);

    QPixmap canvas(labelSize);
    canvas.fill(QColor(26, 26, 26));

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(targetRect, img);
    painter.end();

    label->setPixmap(canvas);
}

// 恢复：主预览的 clamp 与 渲染、以及 mapLabelToImage 与若干对话框/操作实现
void MainWindow::clampPreviewPan() {
    if (m_previewComposite.isNull() || !l_composite || l_composite->size().isEmpty()) {
        m_previewPan = QPointF(0, 0);
        return;
    }

    const QSize labelSize = l_composite->size();
    const QSize imgSize = m_previewComposite.size();
    const QRectF r = calcPreviewRect(labelSize, imgSize, m_previewZoom, QPointF(0, 0));

    const double maxPanX = qMax(0.0, (r.width() - labelSize.width()) * 0.5);
    const double maxPanY = qMax(0.0, (r.height() - labelSize.height()) * 0.5);
    m_previewPan.setX(qBound(-maxPanX, m_previewPan.x(), maxPanX));
    m_previewPan.setY(qBound(-maxPanY, m_previewPan.y(), maxPanY));
}

void MainWindow::updateCompositePreview(const QImage& img) {
    if (!l_composite || img.isNull() || l_composite->size().isEmpty()) return;

    clampPreviewPan();
    const QSize labelSize = l_composite->size();
    const QRectF targetRect = calcPreviewRect(labelSize, img.size(), m_previewZoom, m_previewPan);

    QPixmap canvas(labelSize);
    canvas.fill(QColor(26, 26, 26));

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(targetRect, img);
    painter.end();

    l_composite->setPixmap(canvas);
}

bool MainWindow::mapLabelToImage(const QPoint& labelPos, QPoint& imgPos) const {
    if (m_previewComposite.isNull() || !l_composite || l_composite->size().isEmpty()) return false;

    const QRectF drawRect = calcPreviewRect(l_composite->size(), m_previewComposite.size(), m_previewZoom, m_previewPan);
    if (!drawRect.contains(QPointF(labelPos))) return false;

    const double nx = (labelPos.x() - drawRect.left()) / drawRect.width();
    const double ny = (labelPos.y() - drawRect.top()) / drawRect.height();
    const int x = qBound(0, static_cast<int>(std::floor(nx * m_previewComposite.width())), m_previewComposite.width() - 1);
    const int y = qBound(0, static_cast<int>(std::floor(ny * m_previewComposite.height())), m_previewComposite.height() - 1);
    imgPos = QPoint(x, y);
    return true;
}

void MainWindow::autoSuggestLEDs() {
    if (processedOrigin.isNull()) return;

    int targetCount = s_autoSense->value();
    if (targetCount <= 0) {
        m_ledStrips.clear();
        updateProcess();
        return;
    }

    m_ledStrips = m_ledLayoutEngine.autoSuggestLEDs(
        processedOrigin,
        targetCount,
        s_ledRad->value()
    );

    updateProcess();
}

void MainWindow::openFilterPreprocessDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("滤波预处理");

    QVBoxLayout *layout = new QVBoxLayout(&dlg);

    QCheckBox *enableCheck = new QCheckBox("是否开启图片预滤波（会消除噪点和部分细节）");
    enableCheck->setChecked(m_edgePrefilterEnabled);
    layout->addWidget(enableCheck);

    QLabel *kernelLabel = new QLabel;
    QSlider *kernelSlider = new QSlider(Qt::Horizontal);
    kernelSlider->setRange(3, 7);
    kernelSlider->setSingleStep(2);
    kernelSlider->setPageStep(2);
    kernelSlider->setTickInterval(2);
    kernelSlider->setTickPosition(QSlider::TicksBelow);
    kernelSlider->setValue(m_edgePrefilterKernelSize);
    layout->addWidget(kernelLabel);
    layout->addWidget(kernelSlider);

    auto updateKernelLabel = [kernelLabel](int v) {
        kernelLabel->setText(QString("高斯模糊矩阵大小: %1").arg(v));
    };

    auto snapOddKernel = [kernelSlider, updateKernelLabel](int v) {
        int snapped = v;
        if ((snapped % 2) == 0) {
            snapped = (snapped >= 5) ? (snapped + 1) : (snapped - 1);
        }
        snapped = qBound(3, snapped, 7);
        if (snapped != v) {
            QSignalBlocker blocker(kernelSlider);
            kernelSlider->setValue(snapped);
        }
        updateKernelLabel(snapped);
    };

    connect(kernelSlider, &QSlider::valueChanged, &dlg, snapOddKernel);
    connect(enableCheck, &QCheckBox::toggled, kernelSlider, &QWidget::setEnabled);
    kernelSlider->setEnabled(enableCheck->isChecked());
    updateKernelLabel(kernelSlider->value());

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        m_edgePrefilterEnabled = enableCheck->isChecked();
        m_edgePrefilterKernelSize = kernelSlider->value();
        updateProcess();
    }
}

void MainWindow::openDouglasPeuckerDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("道格拉斯-普克设置");

    QVBoxLayout *layout = new QVBoxLayout(&dlg);

    QCheckBox *enableCheck = new QCheckBox("启用道格拉斯-普克抽稀");
    enableCheck->setChecked(m_dpEnabled);
    layout->addWidget(enableCheck);

    QHBoxLayout *tolLay = new QHBoxLayout;
    QLabel *tolLabel = new QLabel("抽稀容忍度 (epsilon):");
    QDoubleSpinBox *tolSpin = new QDoubleSpinBox;
    tolSpin->setRange(0.0, 1000.0);
    tolSpin->setDecimals(3);
    tolSpin->setSingleStep(0.1);
    tolSpin->setValue(m_dpTolerance);
    tolLay->addWidget(tolLabel);
    tolLay->addWidget(tolSpin);
    layout->addLayout(tolLay);

    QHBoxLayout *widthLay = new QHBoxLayout;
    QLabel *wLabel = new QLabel("抽稀后重绘线宽 (像素):");
    QSpinBox *wSpin = new QSpinBox;
    wSpin->setRange(1, 50);
    wSpin->setSingleStep(1);
    wSpin->setValue(m_dpLineWidth);
    widthLay->addWidget(wLabel);
    widthLay->addWidget(wSpin);
    layout->addLayout(widthLay);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        m_dpEnabled = enableCheck->isChecked();
        m_dpTolerance = tolSpin->value();
        if (m_dpTolerance < 0.0) m_dpTolerance = 0.0;
        m_dpLineWidth = wSpin->value();
        if (m_dpLineWidth < 1) m_dpLineWidth = 1;
        updateProcess();
    }
}

bool MainWindow::handleLayerPreviewEvent(QLabel* label, QEvent* event, const QImage& img, PreviewState& state) {
    if (!label || img.isNull()) return false;

    if (event->type() == QEvent::Wheel) {
        QWheelEvent* we = static_cast<QWheelEvent*>(event);
        const int delta = we->angleDelta().y();
        if (delta == 0) return true;

        const double factor = std::pow(1.12, delta / 120.0);
        const double oldZoom = state.zoom;
        const double newZoom = qBound(0.2, oldZoom * factor, 8.0);
        if (std::abs(newZoom - oldZoom) < 1e-6) return true;

        const QPoint cursorPos = we->pos();
        const QRectF oldRect = calcPreviewRect(label->size(), img.size(), oldZoom, state.pan);
        state.zoom = newZoom;

        if (oldRect.contains(QPointF(cursorPos))) {
            const double u = (cursorPos.x() - oldRect.left()) / oldRect.width();
            const double v = (cursorPos.y() - oldRect.top()) / oldRect.height();
            const QRectF newRect = calcPreviewRect(label->size(), img.size(), state.zoom, state.pan);
            const QPointF mapped(newRect.left() + u * newRect.width(), newRect.top() + v * newRect.height());
            state.pan += QPointF(cursorPos.x() - mapped.x(), cursorPos.y() - mapped.y());
        }

        clampPreviewPan(label, img, state);
        updateLayerPreview(label, img, state);
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::MiddleButton || me->button() == Qt::RightButton) {
            state.isPanning = true;
            state.lastPanPos = me->pos();
            label->setCursor(Qt::ClosedHandCursor);
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove && state.isPanning) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        const QPoint delta = me->pos() - state.lastPanPos;
        state.lastPanPos = me->pos();
        state.pan += QPointF(delta.x(), delta.y());
        clampPreviewPan(label, img, state);
        updateLayerPreview(label, img, state);
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if ((me->button() == Qt::MiddleButton || me->button() == Qt::RightButton) && state.isPanning) {
            state.isPanning = false;
            label->unsetCursor();
            return true;
        }
    }

    return false;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == l_composite && !processedOrigin.isNull()) {
        if (event->type() == QEvent::Wheel) {
            QWheelEvent *we = static_cast<QWheelEvent*>(event);
            const int delta = we->angleDelta().y();
            if (delta == 0 || m_previewComposite.isNull()) return true;

            const double factor = std::pow(1.12, delta / 120.0);
            const double oldZoom = m_previewZoom;
            const double newZoom = qBound(0.2, oldZoom * factor, 8.0);
            if (std::abs(newZoom - oldZoom) < 1e-6) return true;

            const QPoint cursorPos = we->pos();
            const QRectF oldRect = calcPreviewRect(l_composite->size(), m_previewComposite.size(), oldZoom, m_previewPan);

            m_previewZoom = newZoom;

            if (oldRect.contains(QPointF(cursorPos))) {
                const double u = (cursorPos.x() - oldRect.left()) / oldRect.width();
                const double v = (cursorPos.y() - oldRect.top()) / oldRect.height();
                const QRectF newRect = calcPreviewRect(l_composite->size(), m_previewComposite.size(), m_previewZoom, m_previewPan);
                const QPointF mapped(newRect.left() + u * newRect.width(), newRect.top() + v * newRect.height());
                m_previewPan += QPointF(cursorPos.x() - mapped.x(), cursorPos.y() - mapped.y());
            }

            clampPreviewPan();
            updateCompositePreview(m_previewComposite);
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);

            if (me->button() == Qt::MiddleButton || me->button() == Qt::RightButton) {
                m_isPanningPreview = true;
                m_lastPanPos = me->pos();
                l_composite->setCursor(Qt::ClosedHandCursor);
                return true;
            }

            if (me->button() == Qt::LeftButton) {
                QPoint imgPos;
                if (mapLabelToImage(me->pos(), imgPos)) {
                    if (!m_isPlacing) {
                        m_pendingStart = imgPos;
                        m_isPlacing = true;
                    } else {
                        LEDStrip s;
                        s.start = m_pendingStart;
                        s.end = imgPos;
                        s.radius = s_ledRad->value();
                        s.color = processedOrigin.pixelColor(imgPos.x(), imgPos.y());
                        if (s.color.value() < 50) s.color = Qt::white;

                        m_ledStrips.append(s);
                        m_isPlacing = false;
                        updateProcess();
                    }
                }
                return true;
            }
        }

        if (event->type() == QEvent::MouseMove && m_isPanningPreview) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            const QPoint delta = me->pos() - m_lastPanPos;
            m_lastPanPos = me->pos();
            m_previewPan += QPointF(delta.x(), delta.y());
            clampPreviewPan();
            updateCompositePreview(m_previewComposite);
            return true;
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if ((me->button() == Qt::MiddleButton || me->button() == Qt::RightButton) && m_isPanningPreview) {
                m_isPanningPreview = false;
                l_composite->unsetCursor();
                return true;
            }
        }
    }

    QLabel* label = qobject_cast<QLabel*>(obj);
    if (label && m_layerPreviewKeys.contains(label) && !m_layers.isEmpty()) {
        const QString key = m_layerPreviewKeys.value(label);
        if (m_layers.contains(key)) {
            return handleLayerPreviewEvent(label, event, m_layers.value(key), m_layerPreviewStates[label]);
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// --- 加载与导出 ---
void MainWindow::loadAndProcess() {
    QString f = QFileDialog::getOpenFileName(this, "选择图片", "", "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)");
    if (f.isEmpty()) return;

    if (!loadImageFromPath(f, false)) return;
}

void MainWindow::saveProject() {
    if (m_tempImagePath.isEmpty() || !QFile::exists(m_tempImagePath)) {
        QMessageBox::warning(this, "保存失败", "请先导入图片，再保存工程。");
        return;
    }

    QString pcblgPath = QFileDialog::getSaveFileName(this, "保存工程", "", "PCB Lightgraph Project (*.pcblg)");
    if (pcblgPath.isEmpty()) return;
    if (!pcblgPath.endsWith(".pcblg", Qt::CaseInsensitive)) pcblgPath += ".pcblg";

    syncArgsToJson();
    if (!saveProjectToBlg(pcblgPath)) {
        QMessageBox::warning(this, "保存失败", "工程打包失败，请检查目录权限或路径。 ");
        return;
    }

    QMessageBox::information(this, "保存成功", "工程已保存为 .pcblg 文件。");
}

void MainWindow::importProject() {
    QString pcblgPath = QFileDialog::getOpenFileName(this, "导入工程", "", "PCB Lightgraph Project (*.pcblg)");
    if (pcblgPath.isEmpty()) return;

    if (!importProjectFromBlg(pcblgPath)) {
        QMessageBox::warning(this, "导入失败", "无法导入工程，请确认 .pcblg 文件有效。 ");
        return;
    }

    QMessageBox::information(this, "导入成功", "工程已加载。 ");
}

// --- 加载与导出 ---
bool MainWindow::loadImageFromPath(const QString& filePath, bool alreadyInTemp) {
    if (filePath.isEmpty()) return false;

    // 根据二进制头判断真实格式，防止因错误后缀引起的误导
    QString actualExt = detectImageExtensionFromContent(filePath);
    QString providedExt = QFileInfo(filePath).suffix().toLower();
    if (!actualExt.isEmpty() && !providedExt.isEmpty() && actualExt != providedExt) {
        QMessageBox::warning(this, "Warning",
            QString("[Warning] 此图片格式应为.%1而非.%2\n本程序兼容，但请修改").arg(actualExt).arg(providedExt));
    }

    initTempWorkspace();

    QString sourcePath = filePath;
    if (!alreadyInTemp) {
        QString ext = actualExt.isEmpty() ? providedExt : actualExt;
        if (ext.isEmpty()) ext = QStringLiteral("png");

        const QString tempImageTarget = QDir(m_tempDirPath).filePath(QString("source.%1").arg(ext));
        cleanupTempImages(tempImageTarget);

        if (QFile::exists(tempImageTarget)) QFile::remove(tempImageTarget);
        if (!QFile::copy(filePath, tempImageTarget)) {
            QMessageBox::warning(this, "加载失败", "复制图片到 temp 目录失败。 ");
            return false;
        }
        sourcePath = tempImageTarget;
    } else {
        cleanupTempImages(sourcePath);
    }

    // 先读取尺寸做预判，避免一次性解码过大图片导致卡死/闪退
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);

    if (!reader.canRead()) {
        if (!actualExt.isEmpty()) {
            QByteArray fmt = actualExt.toLatin1();
            if (fmt == "jpg") fmt = "jpeg";
            reader.setFormat(fmt);
        }
        if (!reader.canRead()) {
            QMessageBox::warning(this, "加载失败", QString("无法读取图片文件：%1").arg(reader.errorString()));
            return false;
        }
    }

    const QSize srcSize = reader.size();
    if (srcSize.isValid() && !srcSize.isEmpty()) {
        const qint64 maxPixels = getMaxImportPixels();
        const qint64 srcPixels = static_cast<qint64>(srcSize.width()) * static_cast<qint64>(srcSize.height());
        if (srcPixels >= maxPixels) {
            QSize targetSize = scaleDownToPixelLimit(srcSize, maxPixels);
            if (targetSize.isValid() && !targetSize.isEmpty() && targetSize != srcSize) {
                reader.setScaledSize(targetSize);
                QMessageBox::information(
                    this,
                    "提示",
                    QString("图片像素数过大（%1 x %2 = %3），已自动按比例缩放到低于 %4 像素后导入。\n如需完整分辨率，请先在外部软件缩小图片。")
                        .arg(srcSize.width())
                        .arg(srcSize.height())
                        .arg(srcPixels)
                        .arg(maxPixels));
            }
        }
    }

    QImage loaded = reader.read();
    if (loaded.isNull()) {
        QMessageBox::warning(this, "加载失败", QString("无法读取图片文件：%1").arg(reader.errorString()));
        return false;
    }

    m_origin = loaded.convertToFormat(QImage::Format_RGB32);
    m_tempImagePath = sourcePath;
    m_previewZoom = 1.0;
    m_previewPan = QPointF(0, 0);
    m_isPanningPreview = false;
    btn_export->setEnabled(true);
    if (action_exportLayers) action_exportLayers->setEnabled(true);
    m_tempImageMTimeMs = fileStampMs(QFileInfo(m_tempImagePath));
    m_tempImageSize = QFileInfo(m_tempImagePath).size();
    m_ledStrips.clear();
    updateProcess();
    syncArgsToJson();
    return true;
}

void MainWindow::initTempWorkspace() {
    if (m_tempDirPath.isEmpty()) {
        m_tempDirPath = QDir(QCoreApplication::applicationDirPath()).filePath("temp");
    }
    QDir tempDir(m_tempDirPath);
    if (!tempDir.exists()) {
        tempDir.mkpath(".");
    }
    m_tempArgsPath = tempDir.filePath("args.json");
}

void MainWindow::cleanupTempImages(const QString& keepImagePath) {
    initTempWorkspace();

    const QString keepAbs = keepImagePath.isEmpty() ? QString() : QFileInfo(keepImagePath).absoluteFilePath();
    QDir dir(m_tempDirPath);
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : files) {
        const QString ext = fi.suffix().toLower();
        if (!isSupportedImageExtension(ext)) continue;

        const QString currentAbs = fi.absoluteFilePath();
        if (!keepAbs.isEmpty() && QString::compare(currentAbs, keepAbs, Qt::CaseInsensitive) == 0) {
            continue;
        }
        QFile::remove(currentAbs);
        if (QString::compare(m_tempImagePath, currentAbs, Qt::CaseInsensitive) == 0) {
            m_tempImagePath.clear();
        }
    }
}

QString MainWindow::resolveCurrentTempImagePath() const {
    if (m_tempDirPath.isEmpty()) return QString();

    const QString tempDirAbs = QDir(m_tempDirPath).absolutePath();
    const QString tempName = QFileInfo(m_tempImagePath).fileName();
    if (!tempName.isEmpty()) {
        const QString tempPath = QDir(tempDirAbs).filePath(tempName);
        if (QFile::exists(tempPath)) return tempPath;
    }

    QDir dir(tempDirAbs);
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
    for (const QFileInfo& fi : files) {
        if (isSupportedImageExtension(fi.suffix().toLower())) {
            return fi.absoluteFilePath();
        }
    }
    return QString();
}

void MainWindow::syncArgsToJson() {
    initTempWorkspace();

    QJsonObject root;
    root["schemaVersion"] = 1;

    QJsonObject controls;
    controls["surfaceFinishIndex"] = combo_surfaceFinish ? combo_surfaceFinish->currentIndex() : 0;
    controls["maskColorIndex"] = combo_maskColor ? combo_maskColor->currentIndex() : 0;
    controls["goldThresh"] = s_gold ? s_gold->value() : 0;
    controls["silkThresh"] = s_silk ? s_silk->value() : 0;
    controls["transThresh"] = s_trans ? s_trans->value() : 0;
    controls["copperDepth"] = s_copperDepth ? s_copperDepth->value() : 0;

    controls["lightEnable"] = check_lightEnable ? check_lightEnable->isChecked() : false;
    controls["showLEDOverlay"] = check_showLEDOverlay ? check_showLEDOverlay->isChecked() : false;
    controls["autoSense"] = s_autoSense ? s_autoSense->value() : 0;
    controls["ledRadius"] = s_ledRad ? s_ledRad->value() : 0;
    controls["ledIntensity"] = s_ledIntensity ? s_ledIntensity->value() : 0;

    controls["bareSubstrateEnable"] = check_bareSubstrateEnable ? check_bareSubstrateEnable->isChecked() : false;
    controls["bareSubstrateGrayMode"] = radio_bareSubstrateGray ? radio_bareSubstrateGray->isChecked() : true;
    controls["bareSubstrateGrayA"] = s_bareSubstrateGrayA ? s_bareSubstrateGrayA->value() : 0;
    controls["bareSubstrateGrayB"] = s_bareSubstrateGrayB ? s_bareSubstrateGrayB->value() : 0;
    controls["bareSubstrateColorSimilarity"] = s_bareSubstrateColorSimilarity ? s_bareSubstrateColorSimilarity->value() : 0;

    controls["edgeEnable"] = check_edgeEnable ? check_edgeEnable->isChecked() : false;
    controls["edgeMode"] = (radio_edgeStroke && radio_edgeStroke->isChecked()) ? "stroke" : "enhance";
    controls["edgeThreshMin"] = s_edgeThresh ? s_edgeThresh->value() : 0;
    controls["edgeThreshMax"] = s_edgeThreshMax ? s_edgeThreshMax->value() : 0;
    controls["autoInvert"] = s_autoInvert ? s_autoInvert->value() : 0;

    root["controls"] = controls;

    QJsonObject experimental;
    experimental["edgePrefilterEnabled"] = m_edgePrefilterEnabled;
    experimental["edgePrefilterKernelSize"] = m_edgePrefilterKernelSize;
    experimental["edgePrefilterSigma"] = m_edgePrefilterSigma;
    experimental["dpEnabled"] = m_dpEnabled;
    experimental["dpTolerance"] = m_dpTolerance;
    experimental["dpLineWidth"] = m_dpLineWidth;
    root["experimental"] = experimental;

    QJsonArray strips;
    for (const LEDStrip& s : m_ledStrips) {
        QJsonObject o;
        o["startX"] = s.start.x();
        o["startY"] = s.start.y();
        o["endX"] = s.end.x();
        o["endY"] = s.end.y();
        o["radius"] = s.radius;
        o["r"] = s.color.red();
        o["g"] = s.color.green();
        o["b"] = s.color.blue();
        o["a"] = s.color.alpha();
        strips.append(o);
    }
    root["ledStrips"] = strips;

    if (!m_tempImagePath.isEmpty()) {
        root["imageFileName"] = QFileInfo(m_tempImagePath).fileName();
    }

    QSaveFile sf(m_tempArgsPath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    sf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    sf.commit();
}

bool MainWindow::loadArgsFromJson(const QString& argsPath) {
    QFile f(argsPath);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

    const QJsonObject root = doc.object();
    const QJsonObject controls = root.value("controls").toObject();
    const QJsonObject experimental = root.value("experimental").toObject();

    auto setSlider = [](QSlider* s, int value) {
        if (!s) return;
        const int clamped = qBound(s->minimum(), value, s->maximum());
        QSignalBlocker blocker(s);
        s->setValue(clamped);
    };
    auto setCheck = [](QCheckBox* c, bool checked) {
        if (!c) return;
        QSignalBlocker blocker(c);
        c->setChecked(checked);
    };
    auto setRadio = [](QRadioButton* r, bool checked) {
        if (!r) return;
        QSignalBlocker blocker(r);
        r->setChecked(checked);
    };
    auto setCombo = [](QComboBox* c, int index) {
        if (!c) return;
        int i = index;
        if (i < 0 || i >= c->count()) i = 0;
        QSignalBlocker blocker(c);
        c->setCurrentIndex(i);
    };

    m_isApplyingArgs = true;

    setCombo(combo_surfaceFinish, controls.value("surfaceFinishIndex").toInt(combo_surfaceFinish ? combo_surfaceFinish->currentIndex() : 0));
    setCombo(combo_maskColor, controls.value("maskColorIndex").toInt(combo_maskColor ? combo_maskColor->currentIndex() : 0));

    setSlider(s_gold, controls.value("goldThresh").toInt(s_gold ? s_gold->value() : 0));
    setSlider(s_silk, controls.value("silkThresh").toInt(s_silk ? s_silk->value() : 0));
    setSlider(s_trans, controls.value("transThresh").toInt(s_trans ? s_trans->value() : 0));
    setSlider(s_copperDepth, controls.value("copperDepth").toInt(s_copperDepth ? s_copperDepth->value() : 0));

    setCheck(check_lightEnable, controls.value("lightEnable").toBool(check_lightEnable ? check_lightEnable->isChecked() : false));
    setCheck(check_showLEDOverlay, controls.value("showLEDOverlay").toBool(check_showLEDOverlay ? check_showLEDOverlay->isChecked() : false));
    setSlider(s_autoSense, controls.value("autoSense").toInt(s_autoSense ? s_autoSense->value() : 0));
    setSlider(s_ledRad, controls.value("ledRadius").toInt(s_ledRad ? s_ledRad->value() : 0));
    setSlider(s_ledIntensity, controls.value("ledIntensity").toInt(s_ledIntensity ? s_ledIntensity->value() : 0));

    setCheck(check_bareSubstrateEnable, controls.value("bareSubstrateEnable").toBool(check_bareSubstrateEnable ? check_bareSubstrateEnable->isChecked() : false));
    const bool grayMode = controls.value("bareSubstrateGrayMode").toBool(radio_bareSubstrateGray ? radio_bareSubstrateGray->isChecked() : true);
    setRadio(radio_bareSubstrateGray, grayMode);
    setRadio(radio_bareSubstrateColor, !grayMode);
    setSlider(s_bareSubstrateGrayA, controls.value("bareSubstrateGrayA").toInt(s_bareSubstrateGrayA ? s_bareSubstrateGrayA->value() : 0));
    setSlider(s_bareSubstrateGrayB, controls.value("bareSubstrateGrayB").toInt(s_bareSubstrateGrayB ? s_bareSubstrateGrayB->value() : 0));
    setSlider(s_bareSubstrateColorSimilarity, controls.value("bareSubstrateColorSimilarity").toInt(s_bareSubstrateColorSimilarity ? s_bareSubstrateColorSimilarity->value() : 0));

    setCheck(check_edgeEnable, controls.value("edgeEnable").toBool(check_edgeEnable ? check_edgeEnable->isChecked() : false));
    const QString edgeMode = controls.value("edgeMode").toString((radio_edgeStroke && radio_edgeStroke->isChecked()) ? "stroke" : "enhance");
    setRadio(radio_edgeStroke, edgeMode == "stroke");
    setRadio(radio_edgeEnhance, edgeMode != "stroke");
    setSlider(s_edgeThresh, controls.value("edgeThreshMin").toInt(s_edgeThresh ? s_edgeThresh->value() : 0));
    setSlider(s_edgeThreshMax, controls.value("edgeThreshMax").toInt(s_edgeThreshMax ? s_edgeThreshMax->value() : 0));
    setSlider(s_autoInvert, controls.value("autoInvert").toInt(s_autoInvert ? s_autoInvert->value() : 0));

    m_edgePrefilterEnabled = experimental.value("edgePrefilterEnabled").toBool(m_edgePrefilterEnabled);
    m_edgePrefilterKernelSize = experimental.value("edgePrefilterKernelSize").toInt(m_edgePrefilterKernelSize);
    m_edgePrefilterSigma = experimental.value("edgePrefilterSigma").toDouble(m_edgePrefilterSigma);
    m_dpEnabled = experimental.value("dpEnabled").toBool(m_dpEnabled);
    m_dpTolerance = experimental.value("dpTolerance").toDouble(m_dpTolerance);
    m_dpLineWidth = experimental.value("dpLineWidth").toInt(m_dpLineWidth);

    m_ledStrips.clear();
    const QJsonArray strips = root.value("ledStrips").toArray();
    for (const QJsonValue& v : strips) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        LEDStrip s;
        s.start = QPoint(o.value("startX").toInt(), o.value("startY").toInt());
        s.end = QPoint(o.value("endX").toInt(), o.value("endY").toInt());
        s.radius = o.value("radius").toInt(s_ledRad ? s_ledRad->value() : 150);
        s.color = QColor(
            o.value("r").toInt(255),
            o.value("g").toInt(255),
            o.value("b").toInt(255),
            o.value("a").toInt(255)
        );
        m_ledStrips.append(s);
    }

    m_isApplyingArgs = false;
    updateProcess();
    return true;
}

bool MainWindow::saveProjectToBlg(const QString& blgPath) {
    initTempWorkspace();
    syncArgsToJson();

    QString sourceImagePath = m_tempImagePath;
    if (sourceImagePath.isEmpty() || !QFile::exists(sourceImagePath)) {
        QDirIterator it(m_tempDirPath, QDir::Files, QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            const QString p = it.next();
            if (isSupportedImageExtension(QFileInfo(p).suffix().toLower())) {
                sourceImagePath = p;
                break;
            }
        }
    }
    if (sourceImagePath.isEmpty() || !QFile::exists(sourceImagePath)) return false;
    if (!QFile::exists(m_tempArgsPath)) return false;

    QTemporaryDir stageDir;
    if (!stageDir.isValid()) return false;

    const QString stageImagePath = QDir(stageDir.path()).filePath(QFileInfo(sourceImagePath).fileName());
    const QString stageArgsPath = QDir(stageDir.path()).filePath("args.json");
    if (!QFile::copy(sourceImagePath, stageImagePath)) return false;
    if (!QFile::copy(m_tempArgsPath, stageArgsPath)) return false;

    const QString zipTempPath = QDir(stageDir.path()).filePath("project.zip");
    if (QFile::exists(zipTempPath)) QFile::remove(zipTempPath);

    const QString command = QString(
        "$ErrorActionPreference='Stop'; Compress-Archive -LiteralPath @(%1, %2) -DestinationPath %3 -Force")
        .arg(psSingleQuoted(stageImagePath), psSingleQuoted(stageArgsPath), psSingleQuoted(zipTempPath));

    QProcess proc;
    proc.start("powershell", QStringList() << "-NoProfile" << "-Command" << command);
    if (!proc.waitForFinished(60000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return false;
    }

    if (QFile::exists(blgPath)) QFile::remove(blgPath);
    return QFile::copy(zipTempPath, blgPath);
}

bool MainWindow::importProjectFromBlg(const QString& blgPath) {
    if (blgPath.isEmpty() || !QFile::exists(blgPath)) return false;

    initTempWorkspace();

    QTemporaryDir unpackDir;
    if (!unpackDir.isValid()) return false;

    const QString zipPath = QDir(unpackDir.path()).filePath("project.zip");
    if (!QFile::copy(blgPath, zipPath)) return false;

    const QString extractRoot = QDir(unpackDir.path()).filePath("content");
    QDir().mkpath(extractRoot);

    const QString command = QString(
        "$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force")
        .arg(psSingleQuoted(zipPath), psSingleQuoted(extractRoot));

    QProcess proc;
    proc.start("powershell", QStringList() << "-NoProfile" << "-Command" << command);
    if (!proc.waitForFinished(60000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return false;
    }

    QString extractedImagePath;
    QString extractedArgsPath;
    QDirIterator it(extractRoot, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        const QFileInfo fi(p);
        const QString nameLower = fi.fileName().toLower();
        if (nameLower == "args.json") {
            extractedArgsPath = p;
            continue;
        }
        if (isSupportedImageExtension(fi.suffix().toLower()) && extractedImagePath.isEmpty()) {
            extractedImagePath = p;
        }
    }

    if (extractedImagePath.isEmpty() || extractedArgsPath.isEmpty()) return false;
    if (!loadImageFromPath(extractedImagePath, false)) return false;

    if (QFile::exists(m_tempArgsPath)) QFile::remove(m_tempArgsPath);
    if (!QFile::copy(extractedArgsPath, m_tempArgsPath)) return false;
    const bool ok = loadArgsFromJson(m_tempArgsPath);
    if (ok && !m_tempImagePath.isEmpty()) {
        m_tempImageMTimeMs = fileStampMs(QFileInfo(m_tempImagePath));
        m_tempImageSize = QFileInfo(m_tempImagePath).size();
    }
    return ok;
}

void MainWindow::checkTempImageUpdated() {
    if (m_tempImagePath.isEmpty() || !QFile::exists(m_tempImagePath)) return;

    const QFileInfo fi(m_tempImagePath);
    const qint64 stamp = fileStampMs(fi);
    const qint64 size = fi.size();
    if (m_tempImageMTimeMs < 0 && m_tempImageSize < 0) {
        m_tempImageMTimeMs = stamp;
        m_tempImageSize = size;
        return;
    }

    if (stamp == m_tempImageMTimeMs && size == m_tempImageSize) return;

    m_tempImageMTimeMs = stamp;
    m_tempImageSize = size;

    if (m_isApplyingArgs) return;

    if (!loadImageFromPath(m_tempImagePath, true)) {
        QMessageBox::warning(this, "提示", "检测到画图图片已变更，但重新载入失败。");
    }
}

void MainWindow::exportLayers() {
    if (processedOrigin.isNull()) return;
    QString d = QFileDialog::getExistingDirectory(this, "选择导出目录");
    if (d.isEmpty()) return;

    QString finishName = (combo_surfaceFinish->currentText().contains("沉金")) ? "ENIG" : "HASL";

    bool success = m_layerGenerator.exportLayersToFiles(m_layers, d, finishName, m_ledStrips);

    if (success) {
        QMessageBox::information(this, "导出成功", "图纸已导出，灯条参考图为透明底色。");
    } else {
        QMessageBox::warning(this, "导出失败", "导出过程中发生错误，请检查目录权限。");
    }
}

float MainWindow::distanceToSegment(QPoint p, QPoint v, QPoint w) {
    float l2 = std::pow(v.x()-w.x(), 2) + std::pow(v.y()-w.y(), 2);
    if (l2 == 0.0) return std::sqrt(std::pow(p.x()-v.x(), 2) + std::pow(p.y()-v.y(), 2));
    float t = std::max(0.0f, std::min(1.0f, (float)((p.x()-v.x())*(w.x()-v.x()) + (p.y()-v.y())*(w.y()-v.y())) / l2));
    QPoint proj(v.x() + t*(w.x()-v.x()), v.y() + t*(w.y()-v.y()));
    return std::sqrt(std::pow(p.x()-proj.x(), 2) + std::pow(p.y()-proj.y(), 2));
}
void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (!m_previewComposite.isNull()) {
        clampPreviewPan();
        updateCompositePreview(m_previewComposite);
    } else if (!processedOrigin.isNull()) {
        updateProcess();
    }

    if (l_copper && m_layers.contains("Top_Copper")) updateLayerPreview(l_copper, m_layers["Top_Copper"], m_layerPreviewStates[l_copper]);
    if (l_mask && m_layers.contains("Top_Mask")) updateLayerPreview(l_mask, m_layers["Top_Mask"], m_layerPreviewStates[l_mask]);
    if (l_silk && m_layers.contains("Top_Silk")) updateLayerPreview(l_silk, m_layers["Top_Silk"], m_layerPreviewStates[l_silk]);
    if (l_bottom && m_layers.contains("Bottom_Mask")) updateLayerPreview(l_bottom, m_layers["Bottom_Mask"], m_layerPreviewStates[l_bottom]);
}

void MainWindow::openPaintEditor() {
    initTempWorkspace();
    // 尝试优先使用 temp 目录下与当前 m_tempImagePath 同名的文件
    QString tempImage;
    QString name = QFileInfo(m_tempImagePath).fileName();
    if (!name.isEmpty()) {
        tempImage = QDir(m_tempDirPath).filePath(name);
        if (!QFile::exists(tempImage)) tempImage.clear();
    }

    // 若未能通过文件名定位到 temp 下的文件，使用 resolveCurrentTempImagePath() 兜底（会查找 temp 目录最新图片）
    if (tempImage.isEmpty()) tempImage = resolveCurrentTempImagePath();

    if (tempImage.isEmpty() || !QFile::exists(tempImage)) {
        QString probe = QDir(m_tempDirPath).absolutePath();
        QMessageBox::warning(this, "提示", QString("未能在 temp 目录找到当前图片。\n尝试的路径: %1\ntemp 目录: %2").arg(QFileInfo(m_tempImagePath).absoluteFilePath()).arg(probe));
        return;
    }

    QMessageBox::information(this, "提示", "将打开画图，请在画图中按下 Ctrl+S 进行更新渲染");
    QProcess::startDetached("mspaint", QStringList() << QDir::toNativeSeparators(QFileInfo(tempImage).absoluteFilePath()));
}

