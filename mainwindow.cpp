#include "mainwindow.h"
#include "progressiverenderutils.h"
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
#include <QApplication>
#include <QTimer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QIcon>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QStyleOptionGroupBox>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QPixmap>
#include <QScrollArea>
#include <QScroller>
#include <QColorDialog>
#include <cmath>
#include <QDebug>
#include <QSet>
#include <QUuid>

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
    // 每个阶段完成后再通知调度器，保证自适应预览、可选细化和原图逐帧显示。
    connect(
        &m_progressiveRenderController,
        &ProgressiveRenderController::renderRequested,
        this,
        [this](const QSize& targetSize, quint64 generation, bool authoritative) {
            renderAtSize(targetSize, generation, authoritative);
            m_progressiveRenderController.renderFinished(generation);
        });
    initTempWorkspace();
    syncArgsToJson();

    m_tempReloadTimer = new QTimer(this);
    connect(m_tempReloadTimer, &QTimer::timeout, this, &MainWindow::checkTempImageUpdated);
    m_tempReloadTimer->start(1200);

    setWindowTitle("PCB_lightgraphv1.5.1");
    // 运行时窗口图标：使用随程序打包的圆角 logo（多尺寸 ICO 资源）
    setWindowIcon(QIcon(QStringLiteral(":/icons/logo.ico")));

    // 加载本地自定义色值（QSettings 持久化，与 .pcblg 工程无关）
    QSettings s;
    ImageProcessor::setCustomEnigColor(s.value(QStringLiteral("colors/enig"), ImageProcessor::getCustomEnigColor()).value<QColor>());
    ImageProcessor::setCustomOspColor(s.value(QStringLiteral("colors/osp"), ImageProcessor::getCustomOspColor()).value<QColor>());
    ImageProcessor::setCustomHaslColor(s.value(QStringLiteral("colors/hasl"), ImageProcessor::getCustomHaslColor()).value<QColor>());
    ImageProcessor::setCustomBareSubstrateColor(s.value(QStringLiteral("colors/bare"), ImageProcessor::getCustomBareSubstrateColor()).value<QColor>());
    // 阻焊层五色
    ImageProcessor::setCustomMaskBlueColor(s.value(QStringLiteral("colors/maskBlue"), ImageProcessor::getCustomMaskBlueColor()).value<QColor>());
    ImageProcessor::setCustomMaskBlackColor(s.value(QStringLiteral("colors/maskBlack"), ImageProcessor::getCustomMaskBlackColor()).value<QColor>());
    ImageProcessor::setCustomMaskRedColor(s.value(QStringLiteral("colors/maskRed"), ImageProcessor::getCustomMaskRedColor()).value<QColor>());
    ImageProcessor::setCustomMaskGreenColor(s.value(QStringLiteral("colors/maskGreen"), ImageProcessor::getCustomMaskGreenColor()).value<QColor>());
    ImageProcessor::setCustomMaskWhiteColor(s.value(QStringLiteral("colors/maskWhite"), ImageProcessor::getCustomMaskWhiteColor()).value<QColor>());
    ImageProcessor::setCustomMaskYellowColor(s.value(QStringLiteral("colors/maskYellow"), ImageProcessor::getCustomMaskYellowColor()).value<QColor>());
    ImageProcessor::setCustomMaskPurpleColor(s.value(QStringLiteral("colors/maskPurple"), ImageProcessor::getCustomMaskPurpleColor()).value<QColor>());
}

MainWindow::~MainWindow() {
    cancelRegionGesture();
    if (m_tempReloadTimer) m_tempReloadTimer->stop();
    cleanupTempImages();
    // 改动：退出时删除本实例专属的临时目录（temp/<PID>/）。
    // 原因：临时目录已按进程隔离，若不清理会残留孤儿目录；
    // 目的：保证每次退出后只留下干净的 temp 根目录，不互相干扰其他实例。
    if (!m_tempDirPath.isEmpty()) {
        QDir(m_tempDirPath).removeRecursively();
    }
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
    // Fluent 深色卡片样式（内联 QSS 会覆盖 QStyle 绘制，仅对纯容器控件使用）
    l_composite->setStyleSheet("QLabel { background-color: #232323; border-radius: 10px; }");
    l_composite->installEventFilter(this);

    leftLayout->addWidget(new QLabel("<b>【预览区】滚轮缩放，中键/右键平移；区域编辑关闭时左键布灯</b>"));
    leftLayout->addWidget(l_composite, 5); // 权重分配

    // --- 中间：控制台 ---
    QVBoxLayout *ctrl = new QVBoxLayout;
    ctrl->setContentsMargins(10, 10, 10, 10);
    ctrl->setSpacing(10);

    ctrl->addWidget(new QLabel("<b>核心参数控制</b>"));

    QGroupBox *group_basic = new QGroupBox("基础参数：全局");
    // 内容包进容器，便于整块收起/展开动画
    QWidget *basicContent = new QWidget(group_basic);
    QVBoxLayout *basicContentLayout = new QVBoxLayout(basicContent);
    basicContentLayout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *basicLayout = new QVBoxLayout(group_basic);
    basicLayout->setContentsMargins(9, 4, 9, 9);
    basicLayout->addWidget(basicContent);
    combo_surfaceFinish = new QComboBox();
    combo_surfaceFinish->addItems({"沉金 (金黄色)", "喷锡 (银色)", "OSP (玫瑰金)"});
    // 当表面处理变更时，除了更新处理外，还要确保边缘面板的金属勾线状态被正确同步
    connect(combo_surfaceFinish, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int){
        if (check_useMetalEdge && s_autoInvert) s_autoInvert->setEnabled(!check_useMetalEdge->isChecked());
        updateProcess();
    });
    basicContentLayout->addWidget(new QLabel("表面处理工艺:"));
    basicContentLayout->addWidget(combo_surfaceFinish);

    combo_maskColor = new QComboBox();
    combo_maskColor->addItems({"蓝色", "黑色", "红色", "绿色", "白色", "黄色", "紫色"});
    connect(combo_maskColor, SIGNAL(currentIndexChanged(int)), this, SLOT(updateProcess()));
    basicContentLayout->addWidget(new QLabel("阻焊颜色:"));
    basicContentLayout->addWidget(combo_maskColor);

    s_gold   = createSlider("金色/银色判定", 0, 359, 45, basicContentLayout);
    s_silk   = createSlider("丝印阈值", 0, 255, 180, basicContentLayout);
    s_trans  = createSlider("基材透光阈值", 0, 255, 120, basicContentLayout);
    s_copperDepth = createSlider("敷铜层较深阈值", 0, 255, 150, basicContentLayout);
    ctrl->addWidget(group_basic);
    m_collapsibleGroups.insert(group_basic, basicContent);
    group_basic->installEventFilter(this);

    QGroupBox *group_light = new QGroupBox("灯光 / 显示 / 布灯");
    QVBoxLayout *lightLayout = new QVBoxLayout(group_light);
    check_lightEnable = new QCheckBox(group_light);
    check_lightEnable->setChecked(false);
    check_lightEnable->hide(); // 功能开关已并入分组展开状态：展开=启用，收起=关闭

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
        toggleContent(lightDetailWidget, check_lightEnable->isChecked());
    };
    connect(check_lightEnable, &QCheckBox::toggled, [=](bool){
        updateLightDetailState();
        updateProcess();
    });
    // 初始状态：直接收起（不播动画）
    lightDetailWidget->setMaximumHeight(0);
    lightDetailWidget->hide();
    m_collapsibleGroups.insert(group_light, lightDetailWidget);
    m_groupToggleCheckbox.insert(group_light, check_lightEnable);
    group_light->installEventFilter(this);

    QGroupBox *group_bareSubstrate = new QGroupBox("裸露基材绑定");
    QVBoxLayout *bareGroupLayout = new QVBoxLayout(group_bareSubstrate);
    check_bareSubstrateEnable = new QCheckBox(group_bareSubstrate);
    check_bareSubstrateEnable->hide(); // 功能开关已并入分组展开状态：展开=启用，收起=关闭

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

        toggleContent(bareDetailWidget, masterEnabled);

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
    // 初始状态：直接收起（不播动画）
    bareDetailWidget->setMaximumHeight(0);
    bareDetailWidget->hide();
    m_collapsibleGroups.insert(group_bareSubstrate, bareDetailWidget);
    m_groupToggleCheckbox.insert(group_bareSubstrate, check_bareSubstrateEnable);
    group_bareSubstrate->installEventFilter(this);
    updateBareSubstrateControlState();

    group_edgeOperation = new QGroupBox("边缘操作");
    QVBoxLayout *edgeOpLayout = new QVBoxLayout(group_edgeOperation);
    edgeOpLayout->setContentsMargins(8, 8, 8, 8);
    edgeOpLayout->setSpacing(6);

    check_edgeEnable = new QCheckBox(group_edgeOperation);
    check_edgeEnable->hide(); // 功能开关已并入分组展开状态：展开=启用，收起=关闭

    QWidget *edgeDetailWidget = new QWidget(group_edgeOperation);
    QVBoxLayout *edgeDetailLayout = new QVBoxLayout(edgeDetailWidget);
    edgeDetailLayout->setContentsMargins(0, 0, 0, 0);

    QButtonGroup *edgeModeGroup = new QButtonGroup(group_edgeOperation);
    edgeModeGroup->setExclusive(true);
    radio_edgeStroke = new QRadioButton("描边");
    radio_edgeEnhance = new QRadioButton("边缘增强");
    edgeModeGroup->addButton(radio_edgeStroke);
    edgeModeGroup->addButton(radio_edgeEnhance);
    radio_edgeStroke->setChecked(true); // 默认描边（Canny）
    edgeDetailLayout->addWidget(radio_edgeStroke);
    edgeDetailLayout->addWidget(radio_edgeEnhance);

    s_edgeThresh = createSlider("强边缘阈值/边缘下限阈值", 0, 255, 50, edgeDetailLayout);
    s_edgeThreshMax = createSlider("弱边缘阈值/边缘上限阈值", 0, 255, 200, edgeDetailLayout); // 新增上限，默认值设高一些

    // 使用金属（勾选时用金属色勾线，自动反色不可用）
    check_useMetalEdge = new QCheckBox("使用金属");
    check_useMetalEdge->setChecked(false);
    edgeDetailLayout->addWidget(check_useMetalEdge);

    // 裸露金属勾线（当使用金属时，将在阻焊层开窗以露出铜）
    check_exposeMetalEdge = new QCheckBox("裸露金属勾线");
    check_exposeMetalEdge->setChecked(true);
    edgeDetailLayout->addWidget(check_exposeMetalEdge);

    s_autoInvert = createSlider("自动反色范围", -1, 50, 10, edgeDetailLayout);
    edgeOpLayout->addWidget(edgeDetailWidget);
    ctrl->addWidget(group_edgeOperation);

    // 初始化状态控制：总开关决定模式单选和共用滑条是否启用
    edgeDetailWidget->setMaximumHeight(0);
    edgeDetailWidget->hide();
    m_collapsibleGroups.insert(group_edgeOperation, edgeDetailWidget);
    m_groupToggleCheckbox.insert(group_edgeOperation, check_edgeEnable);
    group_edgeOperation->installEventFilter(this);

    auto updateEdgeControlState = [=]() {
        const bool enabled = check_edgeEnable->isChecked();
        toggleContent(edgeDetailWidget, enabled);
        // 当使用金属勾线时，自动反色范围不可用；裸露金属勾线仅在使用金属时显示
        if (check_useMetalEdge) {
            const bool useMetal = check_useMetalEdge->isChecked();
            s_autoInvert->setEnabled(!useMetal);
            if (check_exposeMetalEdge) check_exposeMetalEdge->setVisible(useMetal);
        }
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
    connect(check_useMetalEdge, &QCheckBox::toggled, [=](bool){
        updateEdgeControlState();
        updateProcess();
    });
    connect(check_exposeMetalEdge, &QCheckBox::toggled, [=](bool){
        updateProcess();
    });
    updateEdgeControlState();

    setupRegionUI(ctrl);

    QGroupBox *group_actions = new QGroupBox("图纸操作");
    // 内容包进容器，便于整块收起/展开动画
    QWidget *actionsContent = new QWidget(group_actions);
    QVBoxLayout *actionsContentLayout = new QVBoxLayout(actionsContent);
    actionsContentLayout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *actionLayout = new QVBoxLayout(group_actions);
    actionLayout->setContentsMargins(9, 4, 9, 9);
    QHBoxLayout *actionButtonsLayout = new QHBoxLayout;

    QPushButton *btn_import = new QPushButton("导入图纸");
    btn_import->setMinimumHeight(36); // 与导出按钮等高
    connect(btn_import, &QPushButton::clicked, this, &MainWindow::loadAndProcess);

    btn_export = new QPushButton("导出图纸");
    btn_export->setEnabled(false);
    btn_export->setMinimumHeight(36);
    // Fluent 强调色按钮（accent 属性由 FluentUI3Style 绘制，QSS 会覆盖样式）
    btn_export->setProperty("accent", true);
    connect(btn_export, &QPushButton::clicked, this, &MainWindow::exportLayers);
    actionButtonsLayout->addWidget(btn_import);
    actionButtonsLayout->addWidget(btn_export);
    actionsContentLayout->addLayout(actionButtonsLayout);
    actionLayout->addWidget(actionsContent);
    ctrl->addWidget(group_actions);
    m_collapsibleGroups.insert(group_actions, actionsContent);
    group_actions->installEventFilter(this);
    ctrl->addStretch();

    // Checkbox: 展开右侧预览（默认不勾选）
    check_expandPreviews = new QCheckBox("展开生产层预览");
    check_expandPreviews->setChecked(false);
    ctrl->addWidget(check_expandPreviews);

    // --- 右侧：四个生产层预览 ---
    QGridLayout *rightGrid = new QGridLayout;
    auto createSubLabel = [&](QString title, QLabel*& lbl) {
        QVBoxLayout *v = new QVBoxLayout;
        v->addWidget(new QLabel(title));
        lbl = new QLabel();
        lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        lbl->setMinimumSize(200, 200);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("QLabel { border: 1px dashed #3a3a3a; background-color: #1e1e1e; border-radius: 8px; }");
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

    // 使用一个包装 widget 包含右侧预览，以便通过 show/hide 控制展示
    QWidget *rightPanel = new QWidget;
    rightPanel->setLayout(rightGrid);

    // 初始不展示（复选框默认不勾选）；展开/收起带宽度动画
    rightPanel->setMaximumWidth(0);
    rightPanel->setMinimumWidth(0);
    rightPanel->hide();

    mainLayout->addLayout(leftLayout, 4);
    // 中间控制台放入可滚动区域：全部分组展开时一屏放不下，
    // 支持鼠标滚轮 / 触控板 / 触摸屏上下滚动（类似编辑器）。
    QWidget *ctrlContainer = new QWidget;
    ctrlContainer->setLayout(ctrl);
    QScrollArea *ctrlScroll = new QScrollArea;
    ctrlScroll->setWidgetResizable(true);
    ctrlScroll->setWidget(ctrlContainer);
    ctrlScroll->setFrameShape(QFrame::NoFrame);
    ctrlScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_controlScroll = ctrlScroll;
    ctrlScroll->installEventFilter(this); ctrlContainer->installEventFilter(this);
    updateControlWidth();
    QScroller::grabGesture(ctrlScroll->viewport(), QScroller::TouchGesture); // 触摸屏/触控板拖拽滚动
    mainLayout->addWidget(ctrlScroll, 1);
    mainLayout->addWidget(rightPanel, 3);

    // 切换展开时直接显示/隐藏右侧面板（带横向展开动画）
    connect(check_expandPreviews, &QCheckBox::toggled, [=](bool checked){
        toggleContent(rightPanel, checked, true);
    });

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

    // 界面缩放：按比例缩放全局字体（立即生效、无需重启），
    // 防止小屏幕/高分屏下界面显示不全；选择会持久化，下次启动自动应用。
    m_scaleMenu = optionMenu->addMenu("界面缩放");
    const double currentScale = QSettings().value(QStringLiteral("ui/scale"), 1.0).toDouble();
    const double scales[] = {0.75, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5};
    for (double s : scales) {
        QAction *act = m_scaleMenu->addAction(QStringLiteral("%1%").arg(qRound(s * 100)));
        act->setCheckable(true);
        act->setChecked(qFuzzyCompare(s, currentScale));
        connect(act, &QAction::triggered, this, [s]() {
            QSettings().setValue(QStringLiteral("ui/scale"), s);
            QFont f;
            f.setFamily(QStringLiteral("Microsoft YaHei"));
            f.setPixelSize(qMax(8, qRound(13.0 * s)));
            f.setHintingPreference(QFont::PreferNoHinting);
            qApp->setFont(f);
        });
    }

    // 颜色设置：自定义沉金/OSP/喷锡/裸露基材显示色值（本地持久化，与 .pcblg 工程无关）
    QAction *colorAction = optionMenu->addAction("颜色设置...");
    connect(colorAction, &QAction::triggered, this, &MainWindow::openColorSettingsDialog);

    // 重置所有本地设置（界面缩放 / 自定义色值）
    QAction *resetAction = optionMenu->addAction("重置所有设置...");
    connect(resetAction, &QAction::triggered, this, &MainWindow::resetAllSettings);
}

void MainWindow::toggleContent(QWidget *content, bool expand, bool horizontal) {
    if (!content) return;

    // 取消进行中的动画，避免新旧动画竞争（如快速连点）
    if (QPropertyAnimation *old = m_collapseAnims.value(content, nullptr)) {
        old->stop();
        m_collapseAnims.remove(content);
        old->deleteLater();
    }

    const QByteArray propName = horizontal ? "maximumWidth" : "maximumHeight";
    const int targetSize = horizontal ? content->sizeHint().width() : content->sizeHint().height();

    if (expand) {
        // 已展开（或正在展开）则无需处理
        if (content->isVisible() && content->maximumWidth() != 0 && content->maximumHeight() != 0) return;
        if (horizontal) { content->setMaximumWidth(0); content->setMinimumWidth(0); }
        else { content->setMaximumHeight(0); }
        content->show();
        QPropertyAnimation *anim = new QPropertyAnimation(content, propName, this);
        anim->setDuration(180);
        anim->setEasingCurve(QEasingCurve::InOutCubic);
        anim->setStartValue(0);
        anim->setEndValue(targetSize);
        connect(anim, &QPropertyAnimation::finished, this, [this, content, anim, horizontal, targetSize]() {
            // 横向面板保持展开宽度（有 stretch 参与布局，复位会跳变）；纵向复位上限避免内容变化被裁剪
            if (horizontal) content->setMaximumWidth(targetSize);
            else content->setMaximumHeight(QWIDGETSIZE_MAX);
            m_collapseAnims.remove(content);
            anim->deleteLater();
        });
        m_collapseAnims.insert(content, anim);
        anim->start();
    } else {
        if (content->isHidden()) return; // 已收起
        const int startSize = horizontal ? content->width() : content->height();
        if (startSize <= 0) return;
        QPropertyAnimation *anim = new QPropertyAnimation(content, propName, this);
        anim->setDuration(180);
        anim->setEasingCurve(QEasingCurve::InOutCubic);
        anim->setStartValue(startSize);
        anim->setEndValue(0);
        connect(anim, &QPropertyAnimation::finished, this, [this, content, anim, horizontal]() {
            if (horizontal) content->setMaximumWidth(0);
            else content->setMaximumHeight(0);
            content->hide();
            m_collapseAnims.remove(content);
            anim->deleteLater();
        });
        m_collapseAnims.insert(content, anim);
        anim->start();
    }
}

void MainWindow::openColorSettingsDialog() {
    struct ColorRow { QString name; QString key; QColor value; QPushButton *btn; };
    // 与 imageprocessor.cpp 中的默认值保持一致
    const QVector<QColor> defaults = {
        QColor(240, 217, 140),   // 沉金
        QColor(240, 170, 147),   // OSP #F0AA93
        QColor(200, 200, 215),   // 喷锡
        QColor(153, 187, 119),   // 裸露基材
        QColor(0, 50, 150),      // 蓝色阻焊
        QColor(10, 10, 10),      // 黑色阻焊
        QColor(150, 0, 0),       // 红色阻焊
        QColor(0, 100, 50),      // 绿色阻焊
        QColor(240, 240, 240),   // 白色阻焊
        QColor(192, 167, 13),    // 黄色阻焊 #C0A70D
        QColor(26, 0, 31)        // 紫色阻焊 #1A001F
    };

    QVector<ColorRow> rows;
    rows.append(ColorRow{QStringLiteral("沉金"),        QStringLiteral("colors/enig"),      ImageProcessor::getCustomEnigColor(),          nullptr});
    rows.append(ColorRow{QStringLiteral("OSP"),         QStringLiteral("colors/osp"),       ImageProcessor::getCustomOspColor(),           nullptr});
    rows.append(ColorRow{QStringLiteral("喷锡"),        QStringLiteral("colors/hasl"),      ImageProcessor::getCustomHaslColor(),          nullptr});
    rows.append(ColorRow{QStringLiteral("裸露基材"),    QStringLiteral("colors/bare"),      ImageProcessor::getCustomBareSubstrateColor(), nullptr});
    rows.append(ColorRow{QStringLiteral("蓝色阻焊"),    QStringLiteral("colors/maskBlue"),  ImageProcessor::getCustomMaskBlueColor(),      nullptr});
    rows.append(ColorRow{QStringLiteral("黑色阻焊"),    QStringLiteral("colors/maskBlack"), ImageProcessor::getCustomMaskBlackColor(),     nullptr});
    rows.append(ColorRow{QStringLiteral("红色阻焊"),    QStringLiteral("colors/maskRed"),   ImageProcessor::getCustomMaskRedColor(),       nullptr});
    rows.append(ColorRow{QStringLiteral("绿色阻焊"),    QStringLiteral("colors/maskGreen"), ImageProcessor::getCustomMaskGreenColor(),     nullptr});
    rows.append(ColorRow{QStringLiteral("白色阻焊"),    QStringLiteral("colors/maskWhite"), ImageProcessor::getCustomMaskWhiteColor(),     nullptr});
    rows.append(ColorRow{QStringLiteral("黄色阻焊"),    QStringLiteral("colors/maskYellow"), ImageProcessor::getCustomMaskYellowColor(),    nullptr});
    rows.append(ColorRow{QStringLiteral("紫色阻焊"),    QStringLiteral("colors/maskPurple"), ImageProcessor::getCustomMaskPurpleColor(),    nullptr});

    // 打开对话框时的初始值：供「还原」按钮恢复（与出厂默认值不同）
    QVector<QColor> initialValues;
    for (const ColorRow& r : rows) initialValues.append(r.value);

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("颜色设置"));
    dlg.setModal(true);
    dlg.setMinimumWidth(520);

    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    lay->setSpacing(10);
    lay->addWidget(new QLabel(QStringLiteral(
        "自定义各工艺与阻焊层的显示色值（本地持久化，与 .pcblg 工程无关）：\n"
        "修改 OSP 色值后，OSP 金属匹配色相也会跟随新色值。"), &dlg));

    // 更新色块按钮（左侧小色块图标 + 右侧十六进制值）
    auto updateSwatch = [](ColorRow& row) {
        const QSize sz(24, 22); // 小色块，不覆盖文字
        QPixmap pm(sz);
        pm.fill(row.value);
        row.btn->setIcon(QIcon(pm));
        row.btn->setIconSize(sz);
        row.btn->setText(row.value.name().toUpper());
    };

    for (int i = 0; i < rows.size(); ++i) {
        QHBoxLayout *hl = new QHBoxLayout;
        QLabel *nameLbl = new QLabel(rows[i].name, &dlg);
        nameLbl->setMinimumWidth(90);
        hl->addWidget(nameLbl);

        rows[i].btn = new QPushButton(&dlg);
        rows[i].btn->setFixedSize(150, 30); // 小色块图标 + 十六进制文字并排
        rows[i].btn->setToolTip(QStringLiteral("点击选择颜色"));
        hl->addWidget(rows[i].btn);

        QPushButton *resetBtn = new QPushButton(QStringLiteral("重置"), &dlg);
        resetBtn->setFixedWidth(64);
        hl->addWidget(resetBtn);
        hl->addStretch();
        lay->addLayout(hl);

        const int idx = i;
        connect(rows[i].btn, &QPushButton::clicked, &dlg, [&rows, idx, &dlg, &updateSwatch]() {
            QColor c = QColorDialog::getColor(rows[idx].value, &dlg,
                                              QStringLiteral("选择颜色 - ") + rows[idx].name);
            if (c.isValid()) {
                rows[idx].value = c;
                updateSwatch(rows[idx]);
            }
        });
        connect(resetBtn, &QPushButton::clicked, &dlg, [&rows, idx, &defaults, &updateSwatch]() {
            rows[idx].value = defaults[idx];
            updateSwatch(rows[idx]);
        });
        updateSwatch(rows[i]);
    }

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QPushButton *revertBtn = box->addButton(QStringLiteral("还原"), QDialogButtonBox::ResetRole);
    QPushButton *resetAllBtn = box->addButton(QStringLiteral("全部恢复默认"), QDialogButtonBox::ResetRole);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    // 还原：恢复为打开对话框前已保存的色值
    connect(revertBtn, &QPushButton::clicked, &dlg, [&rows, &initialValues, &updateSwatch]() {
        for (int i = 0; i < rows.size(); ++i) {
            rows[i].value = initialValues[i];
            updateSwatch(rows[i]);
        }
    });
    // 全部恢复默认：恢复为出厂默认色值
    connect(resetAllBtn, &QPushButton::clicked, &dlg, [&rows, &defaults, &updateSwatch]() {
        for (int i = 0; i < rows.size(); ++i) {
            rows[i].value = defaults[i];
            updateSwatch(rows[i]);
        }
    });
    lay->addWidget(box);

    if (dlg.exec() == QDialog::Accepted) {
        QSettings s;
        ImageProcessor::setCustomEnigColor(rows[0].value);
        ImageProcessor::setCustomOspColor(rows[1].value);
        ImageProcessor::setCustomHaslColor(rows[2].value);
        ImageProcessor::setCustomBareSubstrateColor(rows[3].value);
        ImageProcessor::setCustomMaskBlueColor(rows[4].value);
        ImageProcessor::setCustomMaskBlackColor(rows[5].value);
        ImageProcessor::setCustomMaskRedColor(rows[6].value);
        ImageProcessor::setCustomMaskGreenColor(rows[7].value);
        ImageProcessor::setCustomMaskWhiteColor(rows[8].value);
        ImageProcessor::setCustomMaskYellowColor(rows[9].value);
        ImageProcessor::setCustomMaskPurpleColor(rows[10].value);
        s.setValue(QStringLiteral("colors/enig"), rows[0].value);
        s.setValue(QStringLiteral("colors/osp"),  rows[1].value);
        s.setValue(QStringLiteral("colors/hasl"), rows[2].value);
        s.setValue(QStringLiteral("colors/bare"), rows[3].value);
        s.setValue(QStringLiteral("colors/maskBlue"),  rows[4].value);
        s.setValue(QStringLiteral("colors/maskBlack"), rows[5].value);
        s.setValue(QStringLiteral("colors/maskRed"),   rows[6].value);
        s.setValue(QStringLiteral("colors/maskGreen"), rows[7].value);
        s.setValue(QStringLiteral("colors/maskWhite"), rows[8].value);
        s.setValue(QStringLiteral("colors/maskYellow"), rows[9].value);
        s.setValue(QStringLiteral("colors/maskPurple"), rows[10].value);
        if (!m_origin.isNull()) updateProcess(); // 有图时立即重新渲染应用新色值
    }
}

void MainWindow::resetAllSettings() {
    const QMessageBox::StandardButton ret = QMessageBox::question(
        this,
        QStringLiteral("重置所有设置"),
        QStringLiteral("将清除所有本地设置（界面缩放、自定义色值）并恢复默认值。\n\n确定继续吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    // 清空 QSettings 全部键（界面缩放 / 自定义色值）
    QSettings s;
    s.remove(QString());

    // 恢复默认色值
    ImageProcessor::setCustomEnigColor(QColor(240, 217, 140));
    ImageProcessor::setCustomOspColor(QColor(240, 170, 147));
    ImageProcessor::setCustomHaslColor(QColor(200, 200, 215));
    ImageProcessor::setCustomBareSubstrateColor(QColor(153, 187, 119));
    ImageProcessor::setCustomMaskBlueColor(QColor(0, 50, 150));
    ImageProcessor::setCustomMaskBlackColor(QColor(10, 10, 10));
    ImageProcessor::setCustomMaskRedColor(QColor(150, 0, 0));
    ImageProcessor::setCustomMaskGreenColor(QColor(0, 100, 50));
    ImageProcessor::setCustomMaskWhiteColor(QColor(240, 240, 240));
    ImageProcessor::setCustomMaskYellowColor(QColor(192, 167, 13));
    ImageProcessor::setCustomMaskPurpleColor(QColor(26, 0, 31));

    // 恢复默认字体（13px 微软雅黑），并同步缩放菜单勾选态
    QFont f;
    f.setFamily(QStringLiteral("Microsoft YaHei"));
    f.setPixelSize(13);
    f.setHintingPreference(QFont::PreferNoHinting);
    qApp->setFont(f);
    if (m_scaleMenu) {
        for (QAction *act : m_scaleMenu->actions()) {
            act->setChecked(act->text() == QStringLiteral("100%"));
        }
    }

    // 有图时立即按默认色值重新渲染
    if (!m_origin.isNull()) updateProcess();

    QMessageBox::information(this, QStringLiteral("重置完成"),
        QStringLiteral("所有本地设置已恢复默认。"));
}

QSlider* MainWindow::createSlider(QString title, int min, int max, int def, QVBoxLayout* layout) {
    QLabel *lbl = new QLabel(QString("%1: %2").arg(title).arg(def));
    lbl->setWordWrap(true);
    QSlider* s = new QSlider(Qt::Horizontal);
    s->setProperty("valueLabel", QVariant::fromValue(lbl));
    s->setRange(min, max);
    s->setValue(def);
    layout->addWidget(lbl);
    layout->addWidget(s);
    connect(s, &QSlider::valueChanged, [=](int v){
        lbl->setText(QString("%1: %2").arg(title).arg(v));
        if (m_isApplyingArgs) return;
        trackGlobalRegionSlider(s);
        if (m_origin.isNull()) {
            updateProcess();
        } else if (s->isSliderDown()) {
            // 鼠标拖动时只提交合并后的自适应预览；键盘等离散修改仍直接得到完整结果。
            m_progressiveRenderController.sliderValueChanged(m_origin.size());
        } else {
            updateProcess();
        }
    });
    connect(s, &QSlider::sliderPressed, this, [this, s]() {
        beginRegionSlider(s);
        if (!m_origin.isNull()) {
            m_progressiveRenderController.sliderPressed(m_origin.size());
        }
    });
    connect(s, &QSlider::sliderReleased, this, [this, s]() {
        endRegionSlider(s);
        if (!m_origin.isNull()) {
            // 松手后不等待固定延迟，按图片大小选择是否经过中间细化再恢复原图。
            m_progressiveRenderController.sliderReleased(m_origin.size());
        }
    });
    return s;
}

void MainWindow::updateProcess() {
    if (m_isApplyingArgs) return;
    refreshRegionUI();
    if (m_origin.isNull()) {
        if (!m_isApplyingArgs) syncArgsToJson();
        return;
    }

    // 非拖动修改直接建立新代次并生成可导出的全分辨率结果。
    processedOrigin = m_origin;
    const quint64 generation = m_progressiveRenderController.invalidate();
    renderAtSize(m_origin.size(), generation, true);
}

QImage MainWindow::renderOriginForSize(const QSize& targetSize) {
    // 目标尺寸与源图一致时直接返回原图，不参与缩放缓存（全分辨率阶段没有缩放开销）。
    if (targetSize == m_origin.size()) {
        // 原图已改变，旧的缩放缓存不再有效，主动丢弃以释放内存。
        m_cachedRenderOrigin = QImage();
        return m_origin;
    }

    // 缓存键 = 源图标识 + 目标尺寸：拖动期间目标尺寸几乎不变，
    // 复用同一张缩放图即可让 EdgeSharpener/ImageProcessor 的缓存命中。
    const quint64 sourceKey = m_origin.cacheKey();
    if (!m_cachedRenderOrigin.isNull()
        && m_cachedRenderOriginSourceKey == sourceKey
        && m_cachedRenderOrigin.size() == targetSize) {
        return m_cachedRenderOrigin;
    }

    // 源图或目标尺寸变化时重建缓存，并同步记录源图标识用于失效判断。
    m_cachedRenderOriginSourceKey = sourceKey;
    m_cachedRenderOrigin = m_origin.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return m_cachedRenderOrigin;
}

void MainWindow::renderAtSize(const QSize& targetSize, quint64 generation, bool authoritative) {
    if (m_origin.isNull() || !targetSize.isValid() || targetSize.isEmpty()) {
        return;
    }

    // 预览只缩放临时输入和灯条副本，原图坐标与工程数据始终保持不变。
    const QImage renderOrigin = renderOriginForSize(targetSize);
    const QVector<LEDStrip> renderLedStrips = ProgressiveRendering::scaleLedStrips(
        m_ledStrips,
        m_origin.size(),
        renderOrigin.size());
    const qreal renderScale = qMin(
        static_cast<qreal>(renderOrigin.width()) / m_origin.width(),
        static_cast<qreal>(renderOrigin.height()) / m_origin.height());

    // 获取参数
    QString maskColorName = combo_maskColor->currentText();
    QString finishType = combo_surfaceFinish->currentText();
    bool isWhiteMask = (maskColorName == "白色");
    bool enableBareSubstrate = (check_bareSubstrateEnable && check_bareSubstrateEnable->isChecked());
    bool bareSubstrateUseGrayBinding = (radio_bareSubstrateGray && radio_bareSubstrateGray->isChecked());
    int bareSubstrateGrayMinPct = s_bareSubstrateGrayA ? s_bareSubstrateGrayA->value() : 0;
    int bareSubstrateGrayMaxPct = s_bareSubstrateGrayB ? s_bareSubstrateGrayB->value() : 0;
    int bareSubstrateColorSimilarityPct = s_bareSubstrateColorSimilarity ? s_bareSubstrateColorSimilarity->value() : 0;

    // 在执行边缘操作前，准备金属勾线参数
    bool useMetalEdge = (check_useMetalEdge && check_useMetalEdge->isChecked());
    QColor metalEdgeColor = m_imageProcessor.getMetalRenderColor(finishType);

    // 应用边缘操作（边缘增强 / 描边Canny）——
    // 若使用金属勾线（useMetalEdge == true），不要在源图上直接绘制（以免被后续的像素分类覆盖），
    // 改为在生成的合成图像上再绘制。否则保持原有行为（在源图上绘制，影响各层）。
    EdgeSharpener::OperationMode edgeMode =
        (radio_edgeStroke && radio_edgeStroke->isChecked())
            ? EdgeSharpener::OperationMode::StrokeCanny
            : EdgeSharpener::OperationMode::EdgeEnhance;

    // 当不使用金属勾线时，不应把边缘直接绘制回本阶段输入图，
    // 否则会被 ImageProcessor 的像素分类放大/改变导致线变粗或误判。
    // edgeMask 基于当前阶段输入生成，用于在分类后覆盖为丝印。

    int goldThresh = s_gold->value();
    int silkThresh = s_silk->value();
    int transThresh = s_trans->value();
    const int radVal = qMax(1, qRound(s_ledRad->value() * renderScale));
    // 灯光开关只影响预览合成图，不改变生产层内容。
    const bool showOverlay = check_lightEnable
        && check_lightEnable->isChecked()
        && check_showLEDOverlay
        && check_showLEDOverlay->isChecked();

    // 使用 ImageProcessor 处理图像
    QImage imgCopper, imgMask, imgSilk, imgBottom, imgComp;

    Regions::RenderContext regionContext;
    const Regions::Parameters globals = globalRegionParameters();
    const bool regional = m_regions.hasOffsets();
    regionContext.parameters.insert(0, globals);
    if (regional) {
        regionContext.owners = m_regions.owners(renderOrigin.size());
        regionContext.revision = m_regions.revision;
        for (const Regions::Region& region : m_regions.items)
            regionContext.parameters.insert(region.id, Regions::effective(globals, region));
    }

    m_imageProcessor.processImage(
        renderOrigin,
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
        renderLedStrips,
        false,
        regional ? &regionContext : nullptr
    );

    if (check_edgeEnable && check_edgeEnable->isChecked()) {
        QMap<QPair<int, int>, QSet<quint32>> groups;
        for (auto it = regionContext.parameters.constBegin(); it != regionContext.parameters.constEnd(); ++it)
            groups[qMakePair(it.value()[Regions::EdgeMin], it.value()[Regions::EdgeMax])].insert(it.key());
        // 完整图像上下文计算边缘，仅限制写入归属，避免区域边界产生假轮廓。
        for (auto group = groups.constBegin(); group != groups.constEnd(); ++group) {
            const QImage edgeMask = m_edgeSharpener.buildEdgeMaskForImage(renderOrigin, edgeMode,
                group.key().first, group.key().second, m_edgePrefilterEnabled, m_edgePrefilterKernelSize, m_edgePrefilterSigma);
            auto belongs = [&](int x, int y) {
                return !regional || group.value().contains(regionContext.owners[y * renderOrigin.width() + x]);
            };
            int w = qMin(edgeMask.width(), imgCopper.width());
            int h = qMin(edgeMask.height(), imgCopper.height());
            const QColor silkColor = m_imageProcessor.getSilkColor(maskColorName);

            if (useMetalEdge) {
                // 将边缘反映或作为预览遮罩颜色覆盖（若未选择裸露铜则用浅色阻焊预览）
                QRgb metalRgb = metalEdgeColor.rgb();
                QColor maskColor = m_imageProcessor.getSolderMaskColor(maskColorName);
                QRgb previewMaskLight = maskColor.lighter(135).rgb();
                auto blendRgb = [](QRgb base, QRgb top, int topWeight255) -> QRgb {
                    int baseWeight255 = 255 - topWeight255;
                    return qRgb(
                        (qRed(base) * baseWeight255 + qRed(top) * topWeight255) / 255,
                        (qGreen(base) * baseWeight255 + qGreen(top) * topWeight255) / 255,
                        (qBlue(base) * baseWeight255 + qBlue(top) * topWeight255) / 255);
                };
                for (int y = 0; y < h; ++y) {
                    const uchar* em = (const uchar*)edgeMask.constScanLine(y);
                    QRgb *lineCopper = (QRgb*)imgCopper.scanLine(y);
                    QRgb *lineMask = (QRgb*)imgMask.scanLine(y);
                    QRgb *lineSilk = (QRgb*)imgSilk.scanLine(y);
                    QRgb *lineComp = (QRgb*)imgComp.scanLine(y);
                    for (int x = 0; x < w; ++x) {
                        if (em[x] > 0 && belongs(x, y)) {
                            // 金属勾线始终要反映到铜层
                            lineCopper[x] = 0xFFFFFFFF;

                            // 仅在“裸露金属勾线”被选中时改变阻焊/丝印
                            if (check_exposeMetalEdge && check_exposeMetalEdge->isChecked()) {
                                lineSilk[x] = 0xFF000000; // 移除该位置的丝印
                                lineMask[x] = 0xFFFFFFFF; // 阻焊开窗以露出铜
                                // 露铜预览使用金属颜色
                                lineComp[x] = metalRgb;
                            } else {
                                // 未露铜：沿用“敷铜层较深”相同的浅色阻焊混合方式
                                lineComp[x] = blendRgb(lineComp[x], previewMaskLight, 170);
                            }
                        }
                    }
                }
            } else {
                // 将边缘作为丝印覆盖到 imgSilk 和 imgComp（避免被判为金属）
                int w2 = qMin(edgeMask.width(), imgSilk.width());
                int h2 = qMin(edgeMask.height(), imgSilk.height());
                for (int y = 0; y < h2; ++y) {
                    const uchar* em = (const uchar*)edgeMask.constScanLine(y);
                    QRgb *lineSilk = (QRgb*)imgSilk.scanLine(y);
                    QRgb *lineComp = (QRgb*)imgComp.scanLine(y);
                    for (int x = 0; x < w2; ++x) {
                        if (em[x] > 0 && belongs(x, y)) {
                            lineSilk[x] = 0xFFFFFFFF;
                            lineComp[x] = silkColor.rgb();
                        }
                    }
                }
            }
        }
    }

    // 所有阶段先写入预览层；只有原图阶段才会复制到可导出的生产层。
    QMap<QString, QImage> renderedLayers;
    m_layerGenerator.generateLayers(imgCopper, imgMask, imgSilk, imgBottom, renderedLayers);

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
            renderLedStrips,
            radVal,
            imgCopper,
            imgBottom,
            showOverlay,
            s_ledIntensity->value(),
            showOverlay
        );
    }
    m_previewComposite = imgComp;
    m_previewLayers = renderedLayers;
    updateCompositePreview(m_previewComposite);
    updateLayerPreview(l_copper, m_previewLayers["Top_Copper"], m_layerPreviewStates[l_copper]);
    updateLayerPreview(l_mask, m_previewLayers["Top_Mask"], m_layerPreviewStates[l_mask]);
    updateLayerPreview(l_silk, m_previewLayers["Top_Silk"], m_layerPreviewStates[l_silk]);
    updateLayerPreview(l_bottom, m_previewLayers["Bottom_Mask"], m_layerPreviewStates[l_bottom]);

    if (authoritative && renderOrigin.size() == m_origin.size()) {
        // 只在完整阶段更新导出层和参数文件，避免保存到过渡帧或频繁写磁盘。
        m_layers = renderedLayers;
        m_fullResolutionGeneration = generation;
        if (!m_isApplyingArgs) syncArgsToJson();
    }
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
    paintRegionOverlay(painter, targetRect);
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
    paintRegionOverlay(painter, targetRect);
    painter.end();

    l_composite->setPixmap(canvas);
}

bool MainWindow::mapLabelToImage(const QPoint& labelPos, QPoint& imgPos) const {
    if (m_previewComposite.isNull() || processedOrigin.isNull()
        || !l_composite || l_composite->size().isEmpty()) return false;

    const QRectF drawRect = calcPreviewRect(l_composite->size(), m_previewComposite.size(), m_previewZoom, m_previewPan);
    if (!drawRect.contains(QPointF(labelPos))) return false;

    const double nx = (labelPos.x() - drawRect.left()) / drawRect.width();
    const double ny = (labelPos.y() - drawRect.top()) / drawRect.height();
    // 低分辨率预览中的鼠标位置仍要映射回原图坐标，供 LED 工程数据使用。
    const int x = qBound(
        0,
        static_cast<int>(std::floor(nx * processedOrigin.width())),
        processedOrigin.width() - 1);
    const int y = qBound(
        0,
        static_cast<int>(std::floor(ny * processedOrigin.height())),
        processedOrigin.height() - 1);
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

        // Qt 6 使用浮点坐标 position()；保留 Qt 5 分支以兼容原项目工具链。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint cursorPos = we->position().toPoint();
#else
        const QPoint cursorPos = we->pos();
#endif
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
    if (m_controlScroll && (obj == m_controlScroll || obj == m_controlScroll->widget()) &&
        (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::LayoutRequest) && !m_controlWidthQueued) {
        m_controlWidthQueued = true;
        QTimer::singleShot(0, this, [this]() { m_controlWidthQueued = false; updateControlWidth(); });
    }
    if (obj == this && event->type() == QEvent::WindowDeactivate) cancelRegionGesture();
    if (QLabel* label = qobject_cast<QLabel*>(obj)) {
        if ((label == l_composite || m_layerPreviewKeys.contains(label)) && handleRegionEvent(label, event)) return true;
    }
    // 可折叠分组框：点击标题区展开/收起（带动画）
    if (event->type() == QEvent::MouseButtonPress) {
        QGroupBox *gb = qobject_cast<QGroupBox*>(obj);
        if (gb && m_collapsibleGroups.contains(gb)) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                QStyleOptionGroupBox opt;
                opt.initFrom(gb);
                opt.text = gb->title();
                QRect labelRect = gb->style()->subControlRect(QStyle::CC_GroupBox, &opt, QStyle::SC_GroupBoxLabel, gb);
                // 标题带内点击才触发，避免误触内容区
                if (me->pos().y() <= qMax(labelRect.isValid() ? labelRect.bottom() : 0, 30)) {
                    QCheckBox *cb = m_groupToggleCheckbox.value(gb, nullptr);
                    if (cb) {
                        cb->toggle();
                    } else {
                        QWidget *content = m_collapsibleGroups.value(gb, nullptr);
                        if (content) toggleContent(content, content->isHidden());
                    }
                    return true;
                }
            }
        }
    }
    if (obj == l_composite && !processedOrigin.isNull()) {
        if (event->type() == QEvent::Wheel) {
            QWheelEvent *we = static_cast<QWheelEvent*>(event);
            const int delta = we->angleDelta().y();
            if (delta == 0 || m_previewComposite.isNull()) return true;

            const double factor = std::pow(1.12, delta / 120.0);
            const double oldZoom = m_previewZoom;
            const double newZoom = qBound(0.2, oldZoom * factor, 8.0);
            if (std::abs(newZoom - oldZoom) < 1e-6) return true;

            // 主预览与图层预览采用相同的 Qt 5/Qt 6 鼠标坐标兼容逻辑。
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint cursorPos = we->position().toPoint();
#else
            const QPoint cursorPos = we->pos();
#endif
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
    if (label && m_layerPreviewKeys.contains(label) && !m_previewLayers.isEmpty()) {
        const QString key = m_layerPreviewKeys.value(label);
        if (m_previewLayers.contains(key)) {
            // 缩放和平移始终作用于当前可见阶段，不读取可能仍是旧代次的生产层。
            return handleLayerPreviewEvent(label, event, m_previewLayers.value(key), m_layerPreviewStates[label]);
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

    cancelRegionGesture(); m_sliderBefore.reset(); m_historySlider.clear();
    const bool preserveRegions = alreadyInTemp && loaded.size() == m_origin.size();
    const bool changedRegionSize = alreadyInTemp && !preserveRegions && !m_regions.items.isEmpty();
    if (preserveRegions) { m_regions.clearHistory(); ++m_regions.revision; }
    else m_regions.clear(loaded.size());
    m_origin = loaded.convertToFormat(QImage::Format_RGB32);
    if (changedRegionSize) QMessageBox::information(this, QStringLiteral("区域已清空"), QStringLiteral("图片尺寸改变，已清空区域与局部调整。"));
    // 源图更换后立即作废旧缩放缓存，避免残留上一张图的预览输入导致串图。
    m_cachedRenderOrigin = QImage();
    m_cachedRenderOriginSourceKey = 0;
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
    // 改动：临时工作目录从固定的 temp/ 改为按进程隔离的 temp/<PID>/。
    // 原因：原实现所有实例共用同一个 temp 目录与固定的 source.*/args.json 文件名，
    //       多实例同时运行时会发生互相删图、互相覆盖参数、自动重载串图等问题；
    // 目的：每个实例使用独立的临时目录，互不干扰，彻底隔离导入/编辑/保存的副作用。
    if (m_tempDirPath.isEmpty()) {
        m_tempDirPath = QDir(QCoreApplication::applicationDirPath())
                            .filePath(QString("temp/%1").arg(QCoreApplication::applicationPid()));
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

QJsonObject MainWindow::projectArgs() const {
    QJsonObject root;
    root["schemaVersion"] = 2;

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
    controls["useMetalEdge"] = check_useMetalEdge ? check_useMetalEdge->isChecked() : false;
    controls["exposeMetalEdge"] = check_exposeMetalEdge ? check_exposeMetalEdge->isChecked() : true;

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

    if (!m_regions.items.isEmpty()) root["regions"] = m_regions.toJson();
    return root;
}

void MainWindow::syncArgsToJson() {
    initTempWorkspace();
    QSaveFile file(m_tempArgsPath);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(projectArgs()).toJson(QJsonDocument::Indented));
    file.commit();
}

bool MainWindow::loadArgsFromJson(const QString& argsPath) {
    QFile file(argsPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    QVector<Regions::Region> regions;
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        !validateProjectArgs(document.object(), m_origin.size(), regions)) return false;
    applyProjectArgs(document.object(), regions);
    return true;
}

void MainWindow::applyProjectArgs(const QJsonObject& root, const QVector<Regions::Region>& regions) {
    cancelRegionGesture();
    m_sliderBefore.reset(); m_historySlider.clear();
    const QJsonObject controls = root.value("controls").toObject();
    const QJsonObject experimental = root.value("experimental").toObject();

    auto setSlider = [](QSlider* s, int value) {
        if (!s) return;
        const int clamped = qBound(s->minimum(), value, s->maximum());
        QSignalBlocker blocker(s);
        s->setValue(clamped);
        if (QLabel* label = s->property("valueLabel").value<QLabel*>())
            label->setText(label->text().section(':', 0, 0) + QString(": %1").arg(clamped));
    };
    // setCheck intentionally emits the toggled() signal so UI detail panels update when loading projects
    auto setCheck = [](QCheckBox* c, bool checked) {
        if (!c) return;
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
    const QString edgeMode = controls.value("edgeMode").toString(QStringLiteral("stroke"));
    setRadio(radio_edgeStroke, edgeMode == "stroke");
    setRadio(radio_edgeEnhance, edgeMode != "stroke");
    setSlider(s_edgeThresh, controls.value("edgeThreshMin").toInt(s_edgeThresh ? s_edgeThresh->value() : 0));
    setSlider(s_edgeThreshMax, controls.value("edgeThreshMax").toInt(s_edgeThreshMax ? s_edgeThreshMax->value() : 0));
    setSlider(s_autoInvert, controls.value("autoInvert").toInt(s_autoInvert ? s_autoInvert->value() : 0));
    setCheck(check_useMetalEdge, controls.value("useMetalEdge").toBool(check_useMetalEdge ? check_useMetalEdge->isChecked() : false));
    setCheck(check_exposeMetalEdge, controls.value("exposeMetalEdge").toBool(check_exposeMetalEdge ? check_exposeMetalEdge->isChecked() : true));

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

    m_regions.clear(m_origin.size()); m_regions.items = regions;
    m_knownGlobals = globalRegionParameters();
    m_isApplyingArgs = false;
    refreshRegionUI(); updateProcess();
}

bool MainWindow::saveProjectToBlg(const QString& blgPath) {
    initTempWorkspace();
    syncArgsToJson();

    if (m_origin.isNull()) return false;
    QTemporaryDir stageDir;
    if (!stageDir.isValid()) return false;
    const QString stageImagePath = QDir(stageDir.path()).filePath("source.png");
    const QString stageArgsPath = QDir(stageDir.path()).filePath("args.json");
    if (!m_origin.save(stageImagePath, "PNG")) return false;
    QJsonObject args = projectArgs(); args["imageFileName"] = "source.png";
    QSaveFile file(stageArgsPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(QJsonDocument(args).toJson()) < 0 || !file.commit()) return false;

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

    QFile zip(zipTempPath);
    if (!zip.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = zip.readAll();
    QSaveFile destination(blgPath);
    if (!destination.open(QIODevice::WriteOnly) || destination.write(bytes) != bytes.size()) return false;
    return destination.commit();
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

    QStringList extractedImages;
    QString extractedArgsPath;
    QDirIterator it(extractRoot, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        const QFileInfo fi(p);
        const QString nameLower = fi.fileName().toLower();
        if (nameLower == "args.json") {
            if (!extractedArgsPath.isEmpty()) return false;
            extractedArgsPath = p;
            continue;
        }
        if (isSupportedImageExtension(fi.suffix().toLower())) extractedImages.append(p);
    }

    if (extractedImages.isEmpty() || extractedArgsPath.isEmpty()) return false;
    QFile argsFile(extractedArgsPath);
    if (!argsFile.open(QIODevice::ReadOnly)) return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(argsFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject root = document.object();
    QString extractedImagePath;
    if (root.contains("imageFileName")) {
        if (!root["imageFileName"].isString()) return false;
        const QString imageName = root["imageFileName"].toString();
        if (imageName.isEmpty() || imageName.contains('/') || imageName.contains('\\')) return false;
        for (const QString& path : extractedImages) {
            if (QFileInfo(path).fileName().compare(imageName, Qt::CaseInsensitive) != 0) continue;
            if (!extractedImagePath.isEmpty()) return false;
            extractedImagePath = path;
        }
    } else if (extractedImages.size() == 1) {
        extractedImagePath = extractedImages.first();
    }
    if (extractedImagePath.isEmpty()) return false;
    QImageReader reader(extractedImagePath); reader.setAutoTransform(true);
    const QSize rawSize = reader.size();
    if (qint64(rawSize.width()) * rawSize.height() >= getMaxImportPixels()) {
        if (root.contains("regions")) {
            QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("含区域的工程超过像素上限，不能自动缩放。"));
            return false;
        }
        reader.setScaledSize(scaleDownToPixelLimit(rawSize, getMaxImportPixels()));
    }
    QImage loaded = reader.read().convertToFormat(QImage::Format_RGB32);
    QVector<Regions::Region> regions;
    if (loaded.isNull() || !validateProjectArgs(root, loaded.size(), regions)) return false;

    // 解码和校验完成后才准备替换，失败不删除当前临时源图。
    const QString target = QDir(m_tempDirPath).filePath("source_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
    if (!loaded.save(target, "PNG")) return false;
    QJsonObject applied = root; applied["imageFileName"] = QFileInfo(target).fileName();
    QSaveFile file(m_tempArgsPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(applied).toJson()) < 0 || !file.commit()) {
        QFile::remove(target); return false;
    }
    m_origin = loaded; m_cachedRenderOrigin = QImage(); m_cachedRenderOriginSourceKey = 0;
    m_tempImagePath = target; m_previewZoom = 1; m_previewPan = QPointF(); m_isPlacing = false; m_isPanningPreview = false;
    btn_export->setEnabled(true); if (action_exportLayers) action_exportLayers->setEnabled(true);
    applyProjectArgs(applied, regions);
    cleanupTempImages(target);
    m_tempImageMTimeMs = fileStampMs(QFileInfo(target)); m_tempImageSize = QFileInfo(target).size();
    return true;
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

    if (m_fullResolutionGeneration != m_progressiveRenderController.currentGeneration()) {
        // 用户在渐进阶段点击导出时，先同步补齐当前参数对应的原图生产层。
        updateProcess();
    }

    QString d = QFileDialog::getExistingDirectory(this, "选择导出目录");
    if (d.isEmpty()) return;

    const QString finishType = combo_surfaceFinish->currentText();
    QString finishName = finishType.contains("沉金") ? "ENIG"
                       : finishType.contains("OSP") ? "OSP"
                       : "HASL";

    bool success = m_layerGenerator.exportLayersToFiles(m_layers, d, finishName, m_ledStrips);

    if (success) {
        QMessageBox::information(this, "导出成功", "图纸已导出，灯条参考图为透明底色。");

        // 求 star 弹窗：导出成功后引导用户去 GitHub 支持
        QMessageBox starBox(this);
        starBox.setIcon(QMessageBox::Question);
        starBox.setWindowTitle(QStringLiteral("导出成功 🎉"));
        starBox.setText(QStringLiteral(
            "图纸已成功导出！\n\n"
            "如果「PCB_lightgraph」帮到了你，\n"
            "欢迎到 GitHub 点一个 Star 支持作者 ⭐\n\n"
            "https://github.com/tomatorigid/PCB_lightgraph"));
        QPushButton *starBtn = starBox.addButton(QStringLiteral("去 GitHub 点个 Star ⭐"), QMessageBox::AcceptRole);
        starBox.addButton(QStringLiteral("下次再说"), QMessageBox::RejectRole);
        starBox.exec();
        if (starBox.clickedButton() == starBtn) {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/tomatorigid/PCB_lightgraph")));
        }
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

    // 窗口尺寸变化只重绘当前可见阶段，不触发新的图像处理。
    if (l_copper && m_previewLayers.contains("Top_Copper")) {
        updateLayerPreview(
            l_copper,
            m_previewLayers["Top_Copper"],
            m_layerPreviewStates[l_copper]);
    }
    if (l_mask && m_previewLayers.contains("Top_Mask")) {
        updateLayerPreview(
            l_mask,
            m_previewLayers["Top_Mask"],
            m_layerPreviewStates[l_mask]);
    }
    if (l_silk && m_previewLayers.contains("Top_Silk")) {
        updateLayerPreview(
            l_silk,
            m_previewLayers["Top_Silk"],
            m_layerPreviewStates[l_silk]);
    }
    if (l_bottom && m_previewLayers.contains("Bottom_Mask")) {
        updateLayerPreview(
            l_bottom,
            m_previewLayers["Bottom_Mask"],
            m_layerPreviewStates[l_bottom]);
    }
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
