#include "mainwindow.h"
#include "regionslider.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPathStroker>
#include <QSignalBlocker>
#include <QShortcut>
#include <QTimer>
#include <QApplication>
#include <QFontMetrics>
#include <QScrollArea>
#include <QStyle>
#include <QStyleHints>
#include <cmath>
#include <QJsonArray>
#include <limits>

using namespace Regions;

void MainWindow::updateControlWidth() {
    if (!m_controlScroll || !m_controlScroll->widget()) return;
    // 控制台不显示横向滚动条，字体缩放后必须给内容留足宽度。
    const int width = m_controlScroll->widget()->minimumSizeHint().width() + m_controlScroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 4;
    m_controlScroll->setMinimumWidth(width);
}

bool MainWindow::validateProjectArgs(const QJsonObject& root, const QSize& size, QVector<Region>& regions) const {
    if (root.contains("schemaVersion") && !root["schemaVersion"].isDouble()) return false;
    const double version = root.value("schemaVersion").toDouble(1);
    if ((version != 1 && version != 2) || !root.value("controls").isObject()) return false;
    const QJsonObject controls = root.value("controls").toObject();
    auto validInteger = [](const QJsonValue& value) {
        const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
        return value.isDouble() && std::isfinite(number) && std::floor(number) == number && number >= -2147483648.0 && number <= 2147483647.0;
    };
    const QStringList numeric{"surfaceFinishIndex", "maskColorIndex", "goldThresh", "silkThresh", "transThresh", "copperDepth", "autoSense",
        "ledRadius", "ledIntensity", "bareSubstrateGrayA", "bareSubstrateGrayB", "bareSubstrateColorSimilarity", "edgeThreshMin", "edgeThreshMax", "autoInvert"};
    for (const QString& name : numeric) if (controls.contains(name) && !validInteger(controls[name])) return false;
    const QStringList checks{"lightEnable", "showLEDOverlay", "bareSubstrateEnable", "bareSubstrateGrayMode", "edgeEnable", "useMetalEdge", "exposeMetalEdge"};
    for (const QString& name : checks) if (controls.contains(name) && !controls[name].isBool()) return false;
    if (controls.contains("edgeMode") && controls["edgeMode"].toString() != "stroke" && controls["edgeMode"].toString() != "enhance") return false;
    if (root.contains("experimental") && !root["experimental"].isObject()) return false;
    const QJsonObject experimental = root["experimental"].toObject();
    for (auto it = experimental.begin(); it != experimental.end(); ++it) {
        if (it.key() == "edgePrefilterEnabled" || it.key() == "dpEnabled") { if (!it.value().isBool()) return false; }
        else if (!it.value().isDouble() || !std::isfinite(it.value().toDouble()) || it.value().toDouble() < 0 || it.value().toDouble() > 1000) return false;
    }
    if (root.contains("ledStrips") && !root["ledStrips"].isArray()) return false;
    for (const QJsonValue& value : root["ledStrips"].toArray()) {
        if (!value.isObject()) return false;
        const QJsonObject strip = value.toObject();
        for (const QString& name : {QString("startX"), QString("startY"), QString("endX"), QString("endY"), QString("radius"), QString("r"), QString("g"), QString("b"), QString("a")})
            if (strip.contains(name) && !validInteger(strip[name])) return false;
    }
    return Model::fromJson(root.value("regions"), size, regions);
}

Parameters MainWindow::globalRegionParameters() const {
    Parameters values;
    for (int p = 0; p < ParameterCount; ++p) if (m_globalRegionSliders[p]) values[p] = m_globalRegionSliders[p]->value();
    return values;
}
void MainWindow::setGlobalRegionParameters(const Parameters& values) {
    for (int p = 0; p < ParameterCount; ++p) {
        QSignalBlocker blocker(m_globalRegionSliders[p]);
        m_globalRegionSliders[p]->setValue(values[p]);
        if (QLabel* label = m_globalRegionSliders[p]->property("valueLabel").value<QLabel*>())
            label->setText(QStringLiteral("%1: %2").arg(title(p)).arg(values[p]));
    }
    m_knownGlobals = values;
}
void MainWindow::setupRegionUI(QVBoxLayout* layout) {
    m_globalRegionSliders = {{s_gold, s_silk, s_trans, s_copperDepth, s_bareSubstrateGrayA, s_bareSubstrateGrayB,
        s_bareSubstrateColorSimilarity, s_edgeThresh, s_edgeThreshMax}};
    for (int p = 0; p < ParameterCount; ++p) m_globalRegionSliders[p]->setObjectName("global_" + key(p));
    auto group = new QGroupBox(QStringLiteral("区域选择/编辑"));
    group->setObjectName("groupRegions");
    auto outer = new QVBoxLayout(group);
    m_regionContent = new QWidget(group);
    auto inner = new QVBoxLayout(m_regionContent);
    inner->setContentsMargins(0, 0, 0, 0);
    m_regionEnabled = new QCheckBox(group); m_regionEnabled->hide();
    m_regionList = new QListWidget;
    m_regionList->setObjectName("regionList"); m_regionList->setMaximumHeight(110);
    m_regionList->setMinimumHeight(70); m_regionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inner->addWidget(m_regionList);
    connect(m_regionList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item) selectRegion(item->data(Qt::UserRole).toUInt());
    });
    const QStringList names{QStringLiteral("选择"), QStringLiteral("矩形"), QStringLiteral("套索"), QStringLiteral("画笔"), QStringLiteral("魔棒")};
    m_regionToolCombo = new QComboBox;
    m_regionToolCombo->setObjectName("regionTool"); m_regionToolCombo->addItems(names);
    connect(m_regionToolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) setRegionTool(Tool(index));
    });
    inner->addWidget(m_regionToolCombo);
    m_regionBrushControls = new QWidget;
    auto brushLayout = new QVBoxLayout(m_regionBrushControls); brushLayout->setContentsMargins(0, 0, 0, 0);
    auto brushLabel = new QLabel;
    m_regionBrushSize = new QSlider(Qt::Horizontal); m_regionBrushSize->setRange(1, 160); m_regionBrushSize->setValue(24);
    auto updateBrush = [this, brushLabel]() { brushLabel->setText(QStringLiteral("画笔粗细: %1").arg(m_regionBrushSize->value())); };
    connect(m_regionBrushSize, &QSlider::valueChanged, this, updateBrush); updateBrush();
    brushLayout->addWidget(brushLabel); brushLayout->addWidget(m_regionBrushSize); inner->addWidget(m_regionBrushControls);
    m_regionWandControls = new QWidget;
    auto wandLayout = new QVBoxLayout(m_regionWandControls); wandLayout->setContentsMargins(0, 0, 0, 0);
    auto toleranceLabel = new QLabel;
    m_wandTolerance = new QSlider(Qt::Horizontal); m_wandTolerance->setRange(0, 100); m_wandTolerance->setValue(15);
    auto updateTolerance = [this, toleranceLabel]() { cancelRegionGesture(); toleranceLabel->setText(QStringLiteral("容差: %1").arg(m_wandTolerance->value())); };
    connect(m_wandTolerance, &QSlider::valueChanged, this, updateTolerance); updateTolerance();
    m_wandGlobal = new QCheckBox(QStringLiteral("全图同色"));
    connect(m_wandGlobal, &QCheckBox::toggled, this, [this]() { cancelRegionGesture(); });
    wandLayout->addWidget(toleranceLabel); wandLayout->addWidget(m_wandTolerance); wandLayout->addWidget(m_wandGlobal);
    inner->addWidget(m_regionWandControls);
    m_regionOperation = new QComboBox;
    m_regionOperation->addItems({QStringLiteral("新增区域"), QStringLiteral("增选"), QStringLiteral("减选")});
    m_regionOperation->setObjectName("regionOperation");
    connect(m_regionOperation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { cancelRegionGesture(); });
    inner->addWidget(m_regionOperation);
    m_regionHighlight = new QCheckBox(QStringLiteral("显示选区高光")); m_regionHighlight->setChecked(true);
    connect(m_regionHighlight, &QCheckBox::toggled, this, [this]() { refreshRegionPreviews(); }); inner->addWidget(m_regionHighlight);
    m_regionBusy = new QLabel(QStringLiteral("正在计算选区…")); inner->addWidget(m_regionBusy);
    auto actions = new QGridLayout;
    auto action = [actions](const QString& text, int row, int col) {
        auto button = new QPushButton(text); actions->addWidget(button, row, col); return button;
    };
    m_regionRename = action(QStringLiteral("重命名"), 0, 0); m_regionDelete = action(QStringLiteral("删除区域"), 0, 1);
    m_regionReset = action(QStringLiteral("重置局部调整"), 1, 0); m_regionCopy = action(QStringLiteral("复制局部调整"), 1, 1);
    m_regionUndo = action(QStringLiteral("撤销"), 2, 0); m_regionRedo = action(QStringLiteral("重做"), 2, 1);
    inner->addLayout(actions); outer->addWidget(m_regionContent);
    m_collapsibleGroups.insert(group, m_regionContent); m_groupToggleCheckbox.insert(group, m_regionEnabled);
    group->installEventFilter(this);
    m_regionContent->setMaximumHeight(0); m_regionContent->hide();
    layout->insertWidget(1, group);
    connect(m_regionEnabled, &QCheckBox::toggled, this, &MainWindow::setRegionEditing);
    connect(m_regionRename, &QPushButton::clicked, this, [this]() {
        cancelRegionGesture(); const Region* item = m_regions.current(); if (!item) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("重命名此选区"), QStringLiteral("区域名称"), QLineEdit::Normal, item->name, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (name.size() > 100) { QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("区域名称最多 100 个字符。")); return; }
        const Snapshot before = m_regions.snapshot(globalRegionParameters()); m_regions.current()->name = name;
        m_regions.record(before, globalRegionParameters()); refreshRegionUI(); syncArgsToJson();
    });
    connect(m_regionDelete, &QPushButton::clicked, this, [this]() {
        cancelRegionGesture(); if (!m_regions.current()) return;
        if (QMessageBox::question(this, QStringLiteral("删除区域"), QStringLiteral("是否删除区域选择并将参数重置为全局？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        const Snapshot before = m_regions.snapshot(globalRegionParameters());
        for (int i = 0; i < m_regions.items.size(); ++i) if (m_regions.items[i].id == m_regions.selected) { m_regions.items.removeAt(i); break; }
        m_regions.selected = 0; m_regions.record(before, globalRegionParameters(), true); refreshRegionUI(); updateProcess();
    });
    connect(m_regionReset, &QPushButton::clicked, this, [this]() {
        cancelRegionGesture(); if (!m_regions.current()) return;
        const Snapshot before = m_regions.snapshot(globalRegionParameters()); m_regions.current()->offsets.values.fill(0);
        if (m_regions.record(before, globalRegionParameters())) { refreshRegionUI(); updateProcess(); }
    });
    connect(m_regionCopy, &QPushButton::clicked, this, [this]() {
        cancelRegionGesture(); if (!m_regions.current()) return;
        QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("复制局部调整至")); dialog.resize(300, 320);
        auto box = new QVBoxLayout(&dialog); auto targets = new QListWidget; targets->setSelectionMode(QAbstractItemView::MultiSelection);
        for (const Region& r : m_regions.items) if (r.id != m_regions.selected) {
            auto row = new QListWidgetItem(r.name, targets); row->setData(Qt::UserRole, r.id);
        }
        box->addWidget(targets); auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        box->addWidget(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        const Snapshot before = m_regions.snapshot(globalRegionParameters()); const Parameters offsets = m_regions.current()->offsets;
        for (QListWidgetItem* row : targets->selectedItems()) for (Region& r : m_regions.items)
            if (r.id == row->data(Qt::UserRole).toUInt()) r.offsets = offsets;
        if (m_regions.record(before, globalRegionParameters())) { refreshRegionUI(); updateProcess(); }
    });
    connect(m_regionUndo, &QPushButton::clicked, this, [this]() { undoRegionChange(false); });
    connect(m_regionRedo, &QPushButton::clicked, this, [this]() { undoRegionChange(true); });
    m_localRegionGroup = new QGroupBox(QStringLiteral("区域参数")); m_localRegionGroup->setObjectName("regionalControls");
    auto localOuter = new QVBoxLayout(m_localRegionGroup);
    auto localContent = new QWidget(m_localRegionGroup);
    auto localLayout = new QVBoxLayout(localContent);
    localLayout->setContentsMargins(0, 0, 0, 0); localOuter->addWidget(localContent);
    m_collapsibleGroups.insert(qobject_cast<QGroupBox*>(m_localRegionGroup), localContent);
    m_localRegionGroup->installEventFilter(this);
    for (int p = 0; p < ParameterCount; ++p) {
        auto container = new QWidget; auto rowLayout = new QVBoxLayout(container); rowLayout->setContentsMargins(0, 0, 0, 0);
        auto label = new QLabel; label->setWordWrap(true); auto slider = new RegionSlider; slider->setRange(0, maximum(p));
        slider->setObjectName("local_" + key(p)); rowLayout->addWidget(label); rowLayout->addWidget(slider);
        slider->setProperty("valueLabel", QVariant::fromValue(label));
        m_localRegionRows[p] = container; m_localRegionSliders[p] = slider; localLayout->addWidget(container);
        connect(slider, &QSlider::sliderPressed, this, [this, slider]() { beginRegionSlider(slider); });
        connect(slider, &QSlider::sliderReleased, this, [this, slider]() { endRegionSlider(slider); });
        connect(slider, &QSlider::valueChanged, this, [this, p, slider](int value) {
            if (!m_regions.current() || m_isApplyingArgs) return;
            cancelRegionGesture(); const Snapshot before = m_regions.snapshot(globalRegionParameters());
            m_regions.current()->offsets[p] = value - m_globalRegionSliders[p]->value();
            ++m_regions.revision;
            if (!slider->isSliderDown()) m_regions.record(before, globalRegionParameters());
            refreshRegionUI();
            if (slider->isSliderDown()) m_progressiveRenderController.sliderValueChanged(m_origin.size()); else updateProcess();
        });
    }
    layout->insertWidget(3, m_localRegionGroup);
    auto undo = new QShortcut(QKeySequence::Undo, this); connect(undo, &QShortcut::activated, this, [this]() { undoRegionChange(false); });
    auto redo = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Y")), this); connect(redo, &QShortcut::activated, this, [this]() { undoRegionChange(true); });
    auto redoShift = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")), this); connect(redoShift, &QShortcut::activated, this, [this]() { undoRegionChange(true); });
    auto escape = new QShortcut(QKeySequence(Qt::Key_Escape), this); connect(escape, &QShortcut::activated, this, [this]() { setRegionTool(Tool::Select); });
    installEventFilter(this); m_regionUIReady = true; m_knownGlobals = globalRegionParameters(); refreshRegionUI();
}
void MainWindow::refreshRegionUI() {
    if (!m_regionUIReady) return;
    QSignalBlocker listBlocker(m_regionList); m_regionList->clear();
    auto add = [this](quint32 id, const QString& name) {
        auto item = new QListWidgetItem(name, m_regionList); item->setToolTip(name); item->setData(Qt::UserRole, id);
        if (id == m_regions.selected) m_regionList->setCurrentItem(item);
    };
    add(0, QStringLiteral("全局")); for (const Region& r : m_regions.items) add(r.id, r.name);
    const Region* selected = m_regions.current(); const bool image = !m_origin.isNull();
    m_localRegionGroup->setVisible(selected != nullptr);
    if (selected) {
        const QString fullTitle = QStringLiteral("区域参数：%1").arg(selected->name);
        qobject_cast<QGroupBox*>(m_localRegionGroup)->setTitle(m_localRegionGroup->fontMetrics().elidedText(fullTitle, Qt::ElideRight, 220));
        m_localRegionGroup->setToolTip(fullTitle);
    }
    const Parameters globals = globalRegionParameters();
    for (int p = 0; p < ParameterCount; ++p) {
        if (selected) {
            QSignalBlocker sliderBlocker(m_localRegionSliders[p]); const int value = effective(globals, *selected)[p];
            m_localRegionSliders[p]->setValue(value); m_localRegionSliders[p]->setBaseline(globals[p]);
            m_localRegionSliders[p]->property("valueLabel").value<QLabel*>()->setText(QStringLiteral("%1: %2").arg(title(p)).arg(value));
        }
        bool show = true;
        if (p >= BareMin && p <= BareSimilarity) show = check_bareSubstrateEnable->isChecked() &&
            (p == BareSimilarity ? radio_bareSubstrateColor->isChecked() : radio_bareSubstrateGray->isChecked());
        if (p >= EdgeMin) show = check_edgeEnable->isChecked();
        m_localRegionRows[p]->setVisible(show);
    }
    QSignalBlocker toolBlocker(m_regionToolCombo);
    m_regionToolCombo->setEnabled(image); m_regionToolCombo->setCurrentIndex(int(m_regionTool));
    m_regionList->setEnabled(image); m_regionOperation->setEnabled(image);
    m_regionBrushControls->setVisible(m_regionTool == Tool::Brush); m_regionWandControls->setVisible(m_regionTool == Tool::Wand);
    m_regionBusy->setVisible(!m_regionJob.isNull());
    m_regionRename->setEnabled(selected); m_regionDelete->setEnabled(selected); m_regionReset->setEnabled(selected);
    m_regionCopy->setEnabled(selected && m_regions.items.size() > 1);
    m_regionUndo->setEnabled(m_regions.undoCount()); m_regionRedo->setEnabled(m_regions.redoCount());
    if (m_regionEditing) l_composite->setCursor(m_regionTool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    else l_composite->unsetCursor();
}
void MainWindow::beginRegionSlider(QSlider* slider) {
    if (!m_regionUIReady) return;
    bool regional = false;
    for (int p = 0; p < ParameterCount; ++p) if (slider == m_globalRegionSliders[p] || slider == m_localRegionSliders[p]) regional = true;
    if (!regional) return;
    cancelRegionGesture(); m_sliderBefore.reset(new Snapshot(m_regions.snapshot(globalRegionParameters()))); m_historySlider = slider;
    if (!m_origin.isNull()) m_progressiveRenderController.sliderPressed(m_origin.size());
}
void MainWindow::endRegionSlider(QSlider* slider) {
    if (m_historySlider != slider || !m_sliderBefore) return;
    m_regions.record(*m_sliderBefore, globalRegionParameters()); m_sliderBefore.reset(); m_historySlider.clear();
    m_knownGlobals = globalRegionParameters(); refreshRegionUI();
    if (!m_origin.isNull()) m_progressiveRenderController.sliderReleased(m_origin.size());
}
void MainWindow::trackGlobalRegionSlider(QSlider* slider) {
    if (!m_regionUIReady || m_isApplyingArgs) return;
    bool tracked = false; for (QSlider* s : m_globalRegionSliders) if (s == slider) tracked = true;
    if (!tracked) return;
    cancelRegionGesture();
    if (!slider->isSliderDown()) {
        Snapshot before = m_regions.snapshot(m_knownGlobals); m_regions.record(before, globalRegionParameters());
    }
    m_knownGlobals = globalRegionParameters(); refreshRegionUI();
}
void MainWindow::undoRegionChange(bool redo) {
    if (!m_regionUIReady || m_origin.isNull()) return;
    cancelRegionGesture(); if (m_historySlider) endRegionSlider(m_historySlider);
    Parameters globals = globalRegionParameters();
    if (!m_regions.restoreHistory(redo, globals)) return;
    setGlobalRegionParameters(globals); refreshRegionUI(); updateProcess();
}
void MainWindow::setRegionEditing(bool enabled) {
    cancelRegionGesture(); m_regionEditing = enabled; m_isPlacing = false; m_isPanningPreview = false;
    m_regionTool = Tool::Select; toggleContent(m_regionContent, enabled); refreshRegionUI(); refreshRegionPreviews();
}
void MainWindow::setRegionTool(Tool tool) { cancelRegionGesture(); m_regionTool = tool; refreshRegionUI(); refreshRegionPreviews(); }
void MainWindow::selectRegion(quint32 id) {
    cancelRegionGesture(); if (m_historySlider) endRegionSlider(m_historySlider);
    m_regions.selected = id; refreshRegionUI(); refreshRegionPreviews();
}
void MainWindow::cancelRegionGesture() {
    if (m_regionJob) { m_regionJob->cancel(); m_regionJob.clear(); }
    m_gestureActive = false; m_regionSelectPanning = false; m_regionStartButton = Qt::NoButton;
    m_gesturePoints.clear(); m_gesturePath = QPainterPath();
    if (m_regionPressLabel && QWidget::mouseGrabber() == m_regionPressLabel) m_regionPressLabel->releaseMouse();
    m_regionPressLabel.clear();
    if (m_regionBusy) m_regionBusy->hide();
    scheduleRegionOverlay();
}
void MainWindow::startRegionSelection(const QPoint& point, Operation operation, const QPainterPath* geometry) {
    if (m_origin.isNull() || m_regionJob || (operation != Operation::New && !m_regions.current())) return;
    const Snapshot before = m_regions.snapshot(globalRegionParameters()); const quint64 revision = m_regions.revision;
    auto job = new SelectionJob(before, m_origin.size(), operation, this); m_regionJob = job;
    job->completed = [this, job, before, revision, operation](const QVector<Span>& spans) {
        if (m_regionJob != job || m_regions.revision != revision) return;
        m_regionJob.clear();
        if (operation == Operation::New && !spans.isEmpty()) {
            Region item; item.id = m_regions.nextId(); item.name = QStringLiteral("区域 %1").arg(item.id); item.spans = spans;
            m_regions.items.append(item); m_regions.selected = item.id;
        } else if (operation != Operation::New && m_regions.current()) {
            if (spans.isEmpty()) {
                for (int i = 0; i < m_regions.items.size(); ++i) if (m_regions.items[i].id == m_regions.selected) { m_regions.items.removeAt(i); break; }
                m_regions.selected = 0;
            } else m_regions.current()->spans = spans;
        }
        const bool changed = m_regions.record(before, globalRegionParameters(), true);
        refreshRegionUI(); if (changed) updateProcess(); else { syncArgsToJson(); refreshRegionPreviews(); }
    };
    job->failed = [this, job](const QString& error) {
        if (m_regionJob != job) return;
        m_regionJob.clear(); refreshRegionUI(); QMessageBox::warning(this, QStringLiteral("选区失败"), error);
    };
    refreshRegionUI();
    if (geometry) job->startGeometry(*geometry); else job->startWand(m_origin, point, m_wandTolerance->value(), m_wandGlobal->isChecked());
}
bool MainWindow::regionPoint(QLabel* label, const QPointF& pos, QPointF& result, bool outside) const {
    if (m_origin.isNull() || !label || label->size().isEmpty()) return false;
    double zoom = m_previewZoom; QPointF pan = m_previewPan;
    if (label != l_composite) { const PreviewState state = m_layerPreviewStates.value(label); zoom = state.zoom; pan = state.pan; }
    const double scale = qMin(double(label->width()) / m_origin.width(), double(label->height()) / m_origin.height()) * zoom;
    const QSizeF extent(m_origin.width() * scale, m_origin.height() * scale);
    const QPointF top((label->width() - extent.width()) / 2 + pan.x(), (label->height() - extent.height()) / 2 + pan.y());
    const QRectF draw(top, extent);
    if (!outside && !draw.contains(pos)) return false;
    result = QPointF(qBound(0.0, (pos.x() - top.x()) / scale, m_origin.width() - 0.001),
        qBound(0.0, (pos.y() - top.y()) / scale, m_origin.height() - 0.001));
    return true;
}
void MainWindow::updateGesturePath() {
    if (m_gesturePoints.isEmpty()) return;
    QPainterPath path;
    if (m_regionTool == Tool::Rectangle) {
        const QPointF first(std::floor(m_gesturePoints.first().x()), std::floor(m_gesturePoints.first().y()));
        const QPointF last(std::floor(m_gesturePoints.last().x()), std::floor(m_gesturePoints.last().y()));
        const QRectF rect(first, last); const QRectF normalized = rect.normalized();
        path.addRect(QRectF(normalized.topLeft(), normalized.size() + QSizeF(1, 1)));
    } else {
        path.moveTo(m_gesturePoints.first()); for (int i = 1; i < m_gesturePoints.size(); ++i) path.lineTo(m_gesturePoints[i]);
        if (m_regionTool == Tool::Brush) {
            QPainterPathStroker stroke; stroke.setWidth(m_regionBrushSize->value()); stroke.setCapStyle(Qt::RoundCap); stroke.setJoinStyle(Qt::RoundJoin);
            path = stroke.createStroke(path);
            path.addEllipse(m_gesturePoints.first(), m_regionBrushSize->value() / 2.0, m_regionBrushSize->value() / 2.0);
        } else path.closeSubpath();
    }
    m_gesturePath = path;
}
bool MainWindow::handleRegionEvent(QLabel* label, QEvent* event) {
    if (!m_regionEditing || m_origin.isNull()) return false;
    if (event->type() == QEvent::UngrabMouse && (m_gestureActive || m_regionSelectPanning)) { cancelRegionGesture(); return false; }
    if (event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) return false;
        if (m_gestureActive || m_regionSelectPanning || m_regionJob) return true;
        QPointF point; if (!regionPoint(label, mouse->pos(), point)) return mouse->button() == Qt::LeftButton;
        if (m_regionTool == Tool::Select || label != l_composite) {
            if (mouse->button() != Qt::LeftButton) return false;
            m_regionPressPos = mouse->pos(); m_regionPressLabel = label; m_regionSelectPanning = true;
            m_regionSelectDragged = false; m_regionStartButton = Qt::LeftButton; label->grabMouse(); return true;
        }
        Operation op = m_regions.current() ? Operation(m_regionOperation->currentIndex()) : Operation::New;
        if (mouse->button() == Qt::RightButton) {
            if (op == Operation::New || !m_regions.current()) return false;
            op = op == Operation::Add ? Operation::Subtract : Operation::Add;
        } else if (mouse->button() != Qt::LeftButton) return false;
        if (op != Operation::New && !m_regions.current()) return true;
        m_regionStartButton = mouse->button(); m_gestureOperation = op; m_regionPressLabel = label;
        if (m_regionTool == Tool::Wand) { startRegionSelection(QPoint(int(point.x()), int(point.y())), op); return true; }
        m_gestureActive = true; m_gesturePoints = {point}; updateGesturePath(); label->grabMouse(); scheduleRegionOverlay(); return true;
    }
    if (event->type() == QEvent::MouseMove) {
        auto mouse = static_cast<QMouseEvent*>(event);
        if (m_regionSelectPanning && m_regionPressLabel == label) {
            const QPoint delta = mouse->pos() - m_regionPressPos;
            if (m_regionSelectDragged || delta.manhattanLength() >= QApplication::startDragDistance()) {
                m_regionSelectDragged = true; m_regionPressPos = mouse->pos();
                if (label == l_composite) m_previewPan += delta; else m_layerPreviewStates[label].pan += delta;
                label->setCursor(Qt::ClosedHandCursor); refreshRegionPreviews();
            }
            return true;
        }
        if (m_gestureActive && m_regionPressLabel == label) {
            QPointF point; regionPoint(label, mouse->pos(), point, true);
            if (m_regionTool == Tool::Rectangle) { if (m_gesturePoints.size() == 1) m_gesturePoints.append(point); else m_gesturePoints[1] = point; }
            else m_gesturePoints.append(point);
            updateGesturePath(); scheduleRegionOverlay(); return true;
        }
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        auto mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != m_regionStartButton) return m_gestureActive || m_regionSelectPanning;
        if (m_regionSelectPanning) {
            const bool dragged = m_regionSelectDragged; m_regionSelectPanning = false;
            if (!dragged) {
                QPointF point;
                if (regionPoint(label, mouse->pos(), point)) {
                    const auto& owners = m_regions.owners(m_origin.size());
                    selectRegion(owners[int(point.y()) * m_origin.width() + int(point.x())]);
                }
            }
            cancelRegionGesture(); refreshRegionUI(); refreshRegionPreviews(); return true;
        }
        if (m_gestureActive) {
            const QPainterPath path = m_gesturePath; const Operation op = m_gestureOperation;
            cancelRegionGesture(); startRegionSelection(QPoint(), op, &path); return true;
        }
        if (m_regionJob && m_regionTool == Tool::Wand) { m_regionStartButton = Qt::NoButton; return true; }
    }
    return false;
}
void MainWindow::scheduleRegionOverlay() {
    if (!m_regionUIReady || m_overlayQueued) return;
    m_overlayQueued = true; QTimer::singleShot(0, this, [this]() { m_overlayQueued = false; refreshRegionPreviews(); });
}
void MainWindow::refreshRegionPreviews() {
    if (!m_regionUIReady || m_origin.isNull()) return;
    if (l_composite->isVisible()) updateCompositePreview(m_previewComposite);
    for (auto it = m_layerPreviewKeys.begin(); it != m_layerPreviewKeys.end(); ++it)
        if (it.key()->isVisible()) updateLayerPreview(it.key(), m_previewLayers.value(it.value()), m_layerPreviewStates[it.key()]);
}
void MainWindow::paintRegionOverlay(QPainter& painter, const QRectF& target) {
    if (!m_regionUIReady || m_origin.isNull() || target.isEmpty()) return;
    const bool selected = m_regionHighlight->isChecked() && m_regions.current();
    if (!selected && !m_gestureActive) return;
    const QRect visible = target.toAlignedRect().intersected(painter.viewport());
    if (visible.isEmpty()) return;
    QImage live;
    if (m_gestureActive) {
        live = QImage(visible.size(), QImage::Format_ARGB32_Premultiplied); live.fill(Qt::transparent);
        QPainter raster(&live); raster.translate(target.topLeft() - visible.topLeft());
        raster.scale(target.width() / m_origin.width(), target.height() / m_origin.height());
        raster.fillPath(m_gesturePath, Qt::white);
    }
    QImage overlay(visible.size(), QImage::Format_ARGB32_Premultiplied); overlay.fill(Qt::transparent);
    const QVector<quint32>* owners = m_regions.items.isEmpty() ? nullptr : &m_regions.owners(m_origin.size());
    const int width = m_origin.width(), height = m_origin.height();
    for (int y = 0; y < visible.height(); ++y) {
        auto row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        const QRgb* liveRow = live.isNull() ? nullptr : reinterpret_cast<const QRgb*>(live.constScanLine(y));
        const int sy = qBound(0, int((visible.y() + y + 0.5 - target.y()) * height / target.height()), height - 1);
        for (int x = 0; x < visible.width(); ++x) {
            const int sx = qBound(0, int((visible.x() + x + 0.5 - target.x()) * width / target.width()), width - 1);
            const int p = sy * width + sx; const quint32 owner = owners ? owners->at(p) : 0;
            if (selected && owner == m_regions.selected) {
                const bool boundary = sx == 0 || sy == 0 || sx + 1 == width || sy + 1 == height ||
                    owners->at(p - (sx > 0 ? 1 : 0)) != owner || owners->at(p + (sx + 1 < width ? 1 : 0)) != owner ||
                    owners->at(p - (sy > 0 ? width : 0)) != owner || owners->at(p + (sy + 1 < height ? width : 0)) != owner;
                row[x] = boundary ? qPremultiply(qRgba(100, 200, 255, 220)) : qPremultiply(qRgba(70, 155, 255, 65));
            }
            if (liveRow && qAlpha(liveRow[x])) {
                const bool allowed = m_gestureOperation == Operation::Subtract ? owner == m_regions.selected && owner != 0 :
                    (!owner || (m_gestureOperation == Operation::Add && owner == m_regions.selected));
                // 实时轮廓也遵守互斥归属，不把其他区域误显示为待新增像素。
                if (allowed) row[x] = m_gestureOperation == Operation::Subtract ? qPremultiply(qRgba(255, 90, 100, 100)) : qPremultiply(qRgba(70, 230, 150, 100));
            }
        }
    }
    painter.drawImage(visible.topLeft(), overlay);
}
