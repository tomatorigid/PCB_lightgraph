#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>
#include <QDebug>
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUI();
    setWindowTitle("PCB 透光画拆分工具 ");
    //resize(1200, 800);
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

    l_ledLayer = new QLabel("灯条布线参考");
    l_ledLayer->setFixedHeight(150); // 灯条建议区固定高度，宽度随动
    l_ledLayer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    l_ledLayer->setStyleSheet("border: 1px solid #00FF00; background: #000;");
    l_ledLayer->setAlignment(Qt::AlignCenter);

    leftLayout->addWidget(new QLabel("<b>【预览区】支持拖动缩放，点击画面布灯</b>"));
    leftLayout->addWidget(l_composite, 5); // 权重分配
    leftLayout->addWidget(l_ledLayer, 1);

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
    combo_maskColor->addItems({"蓝色", "黑色", "红色", "白色"});
    connect(combo_maskColor, SIGNAL(currentIndexChanged(int)), this, SLOT(updateProcess()));
    ctrl->addWidget(new QLabel("阻焊颜色:"));
    ctrl->addWidget(combo_maskColor);

    s_gold   = createSlider("金色/银色判定", 0, 359, 45, ctrl);
    s_silk   = createSlider("丝印阈值", 0, 255, 180, ctrl);
    s_trans  = createSlider("基材透光阈值", 0, 255, 120, ctrl);
    s_ledRad = createSlider("灯光散射半径", 20, 500, 150, ctrl);
    s_autoSense = createSlider("自动布灯敏感度(0关)", 0, 10, 1, ctrl);

    QPushButton *btn_load = new QPushButton("1. 导入图片");
    btn_load->setMinimumHeight(40);
    connect(btn_load, &QPushButton::clicked, this, &MainWindow::loadAndProcess);
    ctrl->addWidget(btn_load);

    QPushButton *btn_auto = new QPushButton("2. 自动重心布灯");
    connect(btn_auto, &QPushButton::clicked, this, &MainWindow::autoSuggestLEDs);
    ctrl->addWidget(btn_auto);

    btn_export = new QPushButton("3. 导出生产图纸");
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
QColor MainWindow::getSolderMaskColor() {
    QString color = combo_maskColor->currentText();
    if (color == "蓝色") return QColor(0, 50, 150, 200);
    if (color == "红色") return QColor(150, 0, 0, 200);
    if (color == "黑色") return QColor(10, 10, 10, 245);
    return QColor(240, 240, 240, 220); // 白色
}
QColor MainWindow::getSilkColor() {
    return (combo_maskColor->currentText() == "白色") ? Qt::black : Qt::white;
}
void MainWindow::updateProcess() {
    if (m_origin.isNull()) return;

    int w = m_origin.width();
    int h = m_origin.height();

    // 1. 获取 UI 参数（缓存参数，避免在百万次循环中重复查询 UI 组件）
    QString maskColorName = combo_maskColor->currentText();
    QColor maskColor = getSolderMaskColor();
    QColor silkColor = getSilkColor();
    bool isWhiteMask = (maskColorName == "白色");
    bool isHASL = combo_surfaceFinish->currentText().contains("喷锡");
    QColor metalRenderColor = isHASL ? QColor(200, 200, 215) : QColor(218, 165, 32);

    int goldThresh = s_gold->value();
    int silkThresh = s_silk->value();
    int transThresh = s_trans->value();
    int radVal = s_ledRad->value();

    // 2. 初始化图像
    QImage imgCopper(w, h, QImage::Format_RGB32);
    QImage imgMask(w, h, QImage::Format_RGB32);
    QImage imgSilk(w, h, QImage::Format_RGB32);
    QImage imgBottom(w, h, QImage::Format_RGB32);
    QImage imgComp(w, h, QImage::Format_RGB32);

    // 3. 高性能像素处理循环
    for (int y = 0; y < h; ++y) {
        // 使用行指针大幅提升像素写入速度
        QRgb *lineCopper = (QRgb *)imgCopper.scanLine(y);
        QRgb *lineMask   = (QRgb *)imgMask.scanLine(y);
        QRgb *lineSilk   = (QRgb *)imgSilk.scanLine(y);
        QRgb *lineBottom = (QRgb *)imgBottom.scanLine(y);
        QRgb *lineComp   = (QRgb *)imgComp.scanLine(y);
        const QRgb *lineOri = (const QRgb *)m_origin.constScanLine(y);

        for (int x = 0; x < w; ++x) {
            QColor col(lineOri[x]);
            int gray = qGray(lineOri[x]);

            // --- 物理层判定 ---
            bool isMetal = false;
            if (!isHASL) {
                isMetal = (std::abs(col.hue() - goldThresh) < 25 && col.saturation() > 50 && col.value() > 70);
            } else {
                bool isSilverHue = (col.saturation() < 40 || (col.hue() > 160 && col.hue() < 260));
                isMetal = isSilverHue && (col.value() > goldThresh);
            }

            bool silk = (gray > silkThresh) && !isMetal;
            bool maskOpen = isMetal || (silk && !isWhiteMask);
            bool bottomOpen = (gray > transThresh);

            // 填充生产数据 (黑底白图)
            lineCopper[x] = isMetal ? 0xFFFFFFFF : 0xFF000000;
            lineMask[x]   = maskOpen ? 0xFFFFFFFF : 0xFF000000;
            lineSilk[x]   = silk ? 0xFFFFFFFF : 0xFF000000;
            lineBottom[x] = bottomOpen ? 0xFFFFFFFF : 0xFF000000;

            // --- 效果预览渲染逻辑 ---
            QColor pixelRes(40, 35, 25); // 基材色

            if (bottomOpen) {
                int rAcc = 0, gAcc = 0, bAcc = 0;
                for (const auto& s : m_ledStrips) {
                    // 性能优化：快速过滤掉距离过远的像素
                    if (std::abs(x - s.end.x()) > radVal && std::abs(x - s.start.x()) > radVal) continue;

                    float d = distanceToSegment(QPoint(x, y), s.start, s.end);
                    if (d < radVal) {
                        float factor = std::pow(1.0f - (d / radVal), 1.8f);
                        rAcc += s.color.red() * factor;
                        gAcc += s.color.green() * factor;
                        bAcc += s.color.blue() * factor;
                    }
                }
                if (rAcc + gAcc + bAcc > 10) {
                    pixelRes = QColor(qMin(255, rAcc), qMin(255, gAcc), qMin(255, bAcc));
                }
            }

            // 叠加油墨效果
            if (!maskOpen) {
                pixelRes.setRed((pixelRes.red() * 50 + maskColor.red() * 205) / 255);
                pixelRes.setGreen((pixelRes.green() * 50 + maskColor.green() * 205) / 255);
                pixelRes.setBlue((pixelRes.blue() * 50 + maskColor.blue() * 205) / 255);
            }

            // 叠加金属与丝印
            if (isMetal) pixelRes = metalRenderColor;
            else if (silk) pixelRes = silkColor;

            lineComp[x] = pixelRes.rgb();
        }
    }

    // 4. 在最终效果图上绘制虚线辅助圆 (仅供 UI 查看，不影响导出)
    QPainter pComp(&imgComp);
    pComp.setRenderHint(QPainter::Antialiasing);
    QPen dashPen(QColor(255, 255, 255, 120), w / 300.0, Qt::DashLine);
    for (const auto& s : m_ledStrips) {
        pComp.setPen(dashPen);
        pComp.drawEllipse(s.end, radVal, radVal);
    }
    pComp.end();

    // 5. 绘制灯条预览图 (左下角方框)
    QImage imgLED_Preview(w, h, QImage::Format_RGB32);
    imgLED_Preview.fill(Qt::black);
    QPainter pLED(&imgLED_Preview);
    pLED.setRenderHint(QPainter::Antialiasing);
    for (const auto& s : m_ledStrips) {
        pLED.setPen(QPen(s.color, w / 60, Qt::SolidLine, Qt::RoundCap));
        pLED.drawLine(s.start, s.end);

        QPen ledDash(QColor(s.color.red(), s.color.green(), s.color.blue(), 180), w / 150.0, Qt::DashLine);
        pLED.setPen(ledDash);
        pLED.drawEllipse(s.end, radVal, radVal);
    }
    pLED.end();

    // 6. 生成反色生产图并存入缓存 (解决导出为空的问题)
    auto getInverted = [](const QImage& img) {
        QImage inv = img.copy();
        inv.invertPixels();
        return inv;
    };
    m_layers["Top_Copper"]  = getInverted(imgCopper);
    m_layers["Top_Mask"]    = getInverted(imgMask);
    m_layers["Top_Silk"]    = getInverted(imgSilk);
    m_layers["Bottom_Mask"] = getInverted(imgBottom);

    // 7. 刷新 UI 界面显示
    auto setScaled = [this](QLabel* label, const QImage& img) {
        if (img.isNull() || label->size().isEmpty()) return;
        label->setPixmap(QPixmap::fromImage(img).scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };

    setScaled(l_composite, imgComp);
    setScaled(l_copper,    m_layers["Top_Copper"]);
    setScaled(l_mask,      m_layers["Top_Mask"]);
    setScaled(l_silk,      m_layers["Top_Silk"]);
    setScaled(l_bottom,    m_layers["Bottom_Mask"]);
    setScaled(l_ledLayer,  imgLED_Preview);
}
void MainWindow::autoSuggestLEDs() {
    if (m_origin.isNull()) return;
    m_ledStrips.clear();

    // 修复：必须先获取图片的宽高
    int w = m_origin.width();
    int h = m_origin.height();

    int targetCount = s_autoSense->value();
    if (targetCount <= 0) { updateProcess(); return; }

    // 定义色彩桶结构
    struct ColorBucket {
        QVector<QPoint> points;
        long long totalBrightness = 0;
    };
    QMap<int, ColorBucket> buckets;

    // 1. 采样：寻找彩色区域
    for(int y = 0; y < h; y += 15) {
        for(int x = 0; x < w; x += 15) {
            QColor c = m_origin.pixelColor(x, y);
            if (c.value() > 80) {
                // 饱和度低判定为灰色区 (-1)，否则按色相分桶
                int bucketIdx = (c.saturation() < 30) ? -1 : (c.hue() / 30);
                buckets[bucketIdx].points.append(QPoint(x, y));
                buckets[bucketIdx].totalBrightness += c.value();
            }
        }
    }

    // 2. 桶排序逻辑：修复迭代器访问
    struct RankedBucket { int id; long long weight; };
    QList<RankedBucket> ranked;
    QMapIterator<int, ColorBucket> it(buckets);
    while (it.hasNext()) {
        it.next();
        RankedBucket rb;
        rb.id = it.key();
        rb.weight = it.value().totalBrightness; // 修正点：使用 it.value()
        ranked.append(rb);
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedBucket& a, const RankedBucket& b){
        return a.weight > b.weight;
    });

    // 3. 按颜色重要性布灯
    int added = 0;
    for(const auto& rb : ranked) {
        if (added >= targetCount) break;

        QPoint bestPos;
        int maxV = -1;
        QColor bestCol = Qt::white;

        for(const QPoint& p : buckets[rb.id].points) {
            QColor c = m_origin.pixelColor(p.x(), p.y());
            if (c.value() > maxV) {
                // 简单的间距检查
                bool tooClose = false;
                for(const auto& s : m_ledStrips) {
                    if (QPoint(p - s.end).manhattanLength() < 60) { tooClose = true; break; }
                }
                if (!tooClose) {
                    maxV = c.value();
                    bestPos = p;
                    bestCol = c;
                }
            }
        }

        if (maxV != -1) {
            LEDStrip s;
            s.start = QPoint(bestPos.x(), bestPos.y() - 25);
            s.end = QPoint(bestPos.x(), bestPos.y() + 25);
            s.radius = s_ledRad->value();
            s.color = bestCol; // 核心：这里确保了颜色是彩色
            m_ledStrips.append(s);
            added++;
        }
    }
    updateProcess();
}
bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == l_composite && !m_origin.isNull()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {

                // --- 计算坐标映射 (Label像素 -> 原始图片像素) ---
                // 1. 获取图片在Label中缩放后的实际尺寸和偏移
                QSize labelSize = l_composite->size();
                QSize pixmapSize = l_composite->pixmap()->size();

                int offsetX = (labelSize.width() - pixmapSize.width()) / 2;
                int offsetY = (labelSize.height() - pixmapSize.height()) / 2;

                // 2. 检查点击是否在有效图片区域内
                int clickX = me->x() - offsetX;
                int clickY = me->y() - offsetY;

                if (clickX >= 0 && clickX < pixmapSize.width() &&
                    clickY >= 0 && clickY < pixmapSize.height()) {

                    // 3. 映射到原始图片比例 (0.0 ~ 1.0)
                    float rx = (float)clickX / pixmapSize.width();
                    float ry = (float)clickY / pixmapSize.height();

                    // 4. 存储为原始图片坐标（用于算法计算）
                    QPoint imgPos(rx * m_origin.width(), ry * m_origin.height());

                    if (!m_isPlacing) {
                        m_pendingStart = imgPos;
                        m_isPlacing = true;
                    } else {
                        LEDStrip s;
                        s.start = m_pendingStart;
                        s.end = imgPos;
                        s.radius = s_ledRad->value();
                        // 智能颜色提取：取终点像素色作为灯条色
                        s.color = m_origin.pixelColor(imgPos.x(), imgPos.y());
                        if (s.color.value() < 50) s.color = Qt::white; // 太暗则默认白光

                        m_ledStrips.append(s);
                        m_isPlacing = false;
                        updateProcess();
                    }
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}
// --- 辅助函数：创建滑动条 ---
QSlider* MainWindow::createSlider(QString title, int min, int max, int def, QVBoxLayout* layout) {
    QLabel *lbl = new QLabel(QString("%1: %2").arg(title).arg(def));
    QSlider* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max); s->setValue(def);
    layout->addWidget(lbl); layout->addWidget(s);
    connect(s, &QSlider::valueChanged, [=](int v){
        lbl->setText(QString("%1: %2").arg(title).arg(v));
        updateProcess();
    });
    return s;
}

// --- 加载与导出 ---
void MainWindow::loadAndProcess() {
    QString f = QFileDialog::getOpenFileName(this, "选择图片", "", "Images (*.png *.jpg)");
    if (f.isEmpty()) return;
    m_origin = QImage(f).convertToFormat(QImage::Format_RGB32);
    btn_export->setEnabled(true);
    updateProcess();
}

void MainWindow::exportLayers() {
    if (m_origin.isNull()) return;
    QString d = QFileDialog::getExistingDirectory(this, "选择导出目录");
    if (d.isEmpty()) return;

    int w = m_origin.width();
    int h = m_origin.height();
    QString finishName = (combo_surfaceFinish->currentText().contains("沉金")) ? "ENIG" : "HASL";

    // 1. 导出四个生产层 (反色 1-bit)
    QMapIterator<QString, QImage> i(m_layers);
    while (i.hasNext()) {
        i.next();
        // 转换为单色位图
        QImage exp = i.value().convertToFormat(QImage::Format_Mono);

        // 傻逼EDA锚点补丁
        exp.setPixel(0, 0, 1);
        exp.setPixel(0, 1, 0);
        exp.setPixel(1, 0, 0);

        QString path = QString("%1/%2_%3_Inverted.png").arg(d).arg(i.key()).arg(finishName);
        exp.save(path);
    }

    // 2. 导出灯条参考层 (透明底色 RGBA)
    QImage imgLED_Export(w, h, QImage::Format_ARGB32);
    imgLED_Export.fill(Qt::transparent); // 设置底色透明

    QPainter painter(&imgLED_Export);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto& s : m_ledStrips) {
        painter.setPen(QPen(s.color, w / 60, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(s.start, s.end);
    }
    painter.end();

    imgLED_Export.save(d + "/LED_Placement_Reference.png");

    QMessageBox::information(this, "导出成功", "图纸已导出，灯条参考图为透明底色。");
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
    if (!m_origin.isNull()) {
        updateProcess();
    }
}
