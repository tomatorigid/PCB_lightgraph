#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
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
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUI();
    setWindowTitle("PCB  透光画拆分工具 https://github.com/tomatorigid/PCB_lightgraph");
}

MainWindow::~MainWindow() {}

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

    leftLayout->addWidget(new QLabel("<b>【预览区】支持拖动缩放，点击画面布灯</b>"));
    leftLayout->addWidget(l_composite, 5); // 权重分配

    // --- 中间：控制台 ---
    QVBoxLayout *ctrl = new QVBoxLayout;
    ctrl->setContentsMargins(10, 10, 10, 10);
    ctrl->setSpacing(10);

    ctrl->addWidget(new QLabel("<b>核心参数控制</b>"));

    combo_surfaceFinish = new QComboBox();
    combo_surfaceFinish->addItems({"沉金 (亮金)", "喷锡 (银色)"});
    connect(combo_surfaceFinish, SIGNAL(currentIndexChanged(int)), this, SLOT(updateProcess()));
    ctrl->addWidget(new QLabel("表面处理工艺:"));
    ctrl->addWidget(combo_surfaceFinish);

    combo_maskColor = new QComboBox();
    combo_maskColor->addItems({"蓝色", "黑色", "红色", "绿色", "白色"});
    connect(combo_maskColor, SIGNAL(currentIndexChanged(int)), this, SLOT(updateProcess()));
    ctrl->addWidget(new QLabel("阻焊颜色:"));
    ctrl->addWidget(combo_maskColor);

    s_gold   = createSlider("金色/银色判定", 0, 359, 45, ctrl);
    s_silk   = createSlider("丝印阈值", 0, 255, 180, ctrl);
    s_trans  = createSlider("基材透光阈值", 0, 255, 120, ctrl);
    s_copperDepth = createSlider("敷铜层较深阈值", 0, 255, 150, ctrl);
    s_ledRad = createSlider("灯光散射半径", 20, 500, 150, ctrl);
    s_ledIntensity = createSlider("灯光中心不透明度", 0, 255, 200, ctrl);
    check_showLEDOverlay = new QCheckBox("在预览中叠加显示灯光与范围");
    connect(check_showLEDOverlay, &QCheckBox::toggled, [=](bool){ updateProcess(); });
    ctrl->addWidget(check_showLEDOverlay);

    s_autoSense = createSlider("自动布灯敏感度(0关)", 0, 10, 1, ctrl);
    check_edge = new QCheckBox("启用描边 (边缘增强)");
    ctrl->addWidget(check_edge);

    s_edgeThresh = createSlider("边缘下限阈值", 0, 255, 50, ctrl);
    s_edgeThreshMax = createSlider("边缘上限阈值", 0, 255, 200, ctrl); // 新增上限，默认值设高一些
    s_autoInvert = createSlider("自动反色范围", -1, 50, 10, ctrl);

    // 初始化状态控制：勾选才启用滑动条
    s_edgeThresh->setEnabled(false);
    s_autoInvert->setEnabled(false);
    s_edgeThreshMax->setEnabled(false);
    connect(check_edge, &QCheckBox::toggled, [=](bool checked){
        s_edgeThresh->setEnabled(checked);
        s_autoInvert->setEnabled(checked);
        s_edgeThreshMax->setEnabled(checked);
        updateProcess();
    });

    QPushButton *btn_load = new QPushButton("1. 导入图片");
    btn_load->setMinimumHeight(40);
    connect(btn_load, &QPushButton::clicked, this, &MainWindow::loadAndProcess);
    ctrl->addWidget(btn_load);

    QPushButton *btn_auto = new QPushButton("2. 自动重心布灯");
    connect(btn_auto, &QPushButton::clicked, this, &MainWindow::autoSuggestLEDs);
    ctrl->addWidget(btn_auto);

    btn_export = new QPushButton("3. 导出各层图纸");
    btn_export->setEnabled(false);
    btn_export->setMinimumHeight(50);
    btn_export->setStyleSheet("background-color: #2d5a2d; color: white; font-weight: bold;");
    connect(btn_export, &QPushButton::clicked, this, &MainWindow::exportLayers);
    ctrl->addWidget(btn_export);
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

    mainLayout->addLayout(leftLayout, 4);
    mainLayout->addLayout(ctrl, 1);
    mainLayout->addLayout(rightGrid, 3);

    setCentralWidget(central);
    resize(1200, 800); // 初始窗口大小
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
    if (m_origin.isNull()) return;

    processedOrigin = m_origin;

    // 应用边缘锐化
    if (check_edge && check_edge->isChecked()) {
        processedOrigin = m_edgeSharpener.processEdgeSharpening(
            processedOrigin,
            s_edgeThresh->value(),
            s_edgeThreshMax->value(),
            s_autoInvert->value()
        );
    }

    // 获取参数
    QString maskColorName = combo_maskColor->currentText();
    QString finishType = combo_surfaceFinish->currentText();
    bool isWhiteMask = (maskColorName == "白色");

    int goldThresh = s_gold->value();
    int silkThresh = s_silk->value();
    int transThresh = s_trans->value();
    int radVal = s_ledRad->value();
    // Overlay switch is only for preview composition.
    bool showOverlay = (check_showLEDOverlay && check_showLEDOverlay->isChecked());

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

    // 更新 UI 显示
    auto setScaled = [this](QLabel* label, const QImage& img) {
        if (img.isNull() || label->size().isEmpty()) return;
        label->setPixmap(QPixmap::fromImage(img).scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };

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
    setScaled(l_copper, m_layers["Top_Copper"]);
    setScaled(l_mask, m_layers["Top_Mask"]);
    setScaled(l_silk, m_layers["Top_Silk"]);
    setScaled(l_bottom, m_layers["Bottom_Mask"]);

    // 独立的灯条预览widget已移除，灯光在主预览上叠加显示
}

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
    return QMainWindow::eventFilter(obj, event);
}

// --- 加载与导出 ---
void MainWindow::loadAndProcess() {
    QString f = QFileDialog::getOpenFileName(this, "选择图片", "", "Images (*.png *.jpg)");
    if (f.isEmpty()) return;
    m_origin = QImage(f).convertToFormat(QImage::Format_RGB32);
    m_previewZoom = 1.0;
    m_previewPan = QPointF(0, 0);
    m_isPanningPreview = false;
    btn_export->setEnabled(true);
    m_ledStrips.clear();
    updateProcess();
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
}
