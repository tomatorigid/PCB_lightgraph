#include <QtTest>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QScrollArea>
#include <QProcess>
#include <QElapsedTimer>
#include <QPainter>
#include <QTimer>
#include <QSet>
#include <QDir>
#include <QImageReader>
#include <deque>
#include <cmath>
#include "mainwindow.h"
#include "regionslider.h"
#include "baseline_imageprocessor.h"
#include "progressiverenderutils.h"
#include "fluentuiappearance.h"

using namespace Regions;
class RegionTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_workspace;
    MainWindow* m_window = nullptr;
    QString m_source;
    QString artifacts() const { return qEnvironmentVariable("PCBLG_TEST_ARTIFACTS", QDir::tempPath()); }
    static QImage fixture(int w = 96, int h = 72) {
        QImage image(w, h, QImage::Format_RGB32);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
            image.setPixel(x, y, qRgb((x * 17 + y * 11) % 256, (x * 3 + y * 23) % 256, (x * 13 + y * 7) % 256));
        return image;
    }
    static Region rectangle(quint32 id, const QRect& rect) {
        Region r; r.id = id; r.name = QStringLiteral("区域 %1").arg(id);
        for (int y = rect.top(); y <= rect.bottom(); ++y) r.spans.append(Span(y, rect.left(), rect.right() + 1));
        return r;
    }
    bool waitJob(SelectionJob* job, const std::function<void()>& start, QVector<Span>& result) {
        bool done = false;
        job->completed = [&](const QVector<Span>& spans) { result = spans; done = true; };
        start(); QElapsedTimer timeout; timeout.start();
        while (!done && timeout.elapsed() < 15000) QTest::qWait(1);
        return done;
    }
    QPoint screenPoint(QLabel* label, QPointF p) {
        double zoom = m_window->m_previewZoom; QPointF pan = m_window->m_previewPan;
        if (label != m_window->l_composite) { zoom = m_window->m_layerPreviewStates[label].zoom; pan = m_window->m_layerPreviewStates[label].pan; }
        const QSize size = m_window->m_origin.size();
        const double scale = qMin(double(label->width()) / size.width(), double(label->height()) / size.height()) * zoom;
        return QPoint(int((label->width() - size.width() * scale) / 2 + pan.x() + p.x() * scale),
            int((label->height() - size.height() * scale) / 2 + pan.y() + p.y() * scale));
    }
    void move(QLabel* label, const QPoint& point, Qt::MouseButtons buttons) {
        QMouseEvent event(QEvent::MouseMove, QPointF(point), QPointF(label->mapToGlobal(point)), Qt::NoButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(label, &event);
    }
    void drag(QLabel* label, QPointF from, QPointF to, Qt::MouseButton button = Qt::LeftButton) {
        QTest::mousePress(label, button, Qt::NoModifier, screenPoint(label, from));
        move(label, screenPoint(label, to), button);
        QTest::mouseRelease(label, button, Qt::NoModifier, screenPoint(label, to));
    }
    void setRegions(const QVector<Region>& regions) {
        m_window->m_regions.items = regions; m_window->m_regions.selected = regions.isEmpty() ? 0 : regions.first().id;
        m_window->m_regions.invalidateMasks(); ++m_window->m_regions.revision;
        m_window->refreshRegionUI(); m_window->updateProcess();
    }
    void setControls(int mask, int finish, bool bare, bool gray, bool edge, bool stroke, bool metal, bool expose) {
        m_window->m_isApplyingArgs = true;
        m_window->combo_maskColor->setCurrentIndex(mask); m_window->combo_surfaceFinish->setCurrentIndex(finish);
        m_window->check_bareSubstrateEnable->setChecked(bare);
        m_window->radio_bareSubstrateGray->setChecked(gray); m_window->radio_bareSubstrateColor->setChecked(!gray);
        m_window->check_edgeEnable->setChecked(edge); m_window->radio_edgeStroke->setChecked(stroke); m_window->radio_edgeEnhance->setChecked(!stroke);
        m_window->check_useMetalEdge->setChecked(metal); m_window->check_exposeMetalEdge->setChecked(expose);
        m_window->m_isApplyingArgs = false; m_window->updateProcess();
    }
private slots:
    void initTestCase() {
        QVERIFY(m_workspace.isValid());
        QCoreApplication::setOrganizationName("PCB_lightgraphTests"); QCoreApplication::setApplicationName("RegionRegression");
        fluentUIAppearance.initialize(); fluentUIAppearance.setTheme(Theme::Dark);
        QFont font(QStringLiteral("Microsoft YaHei")); font.setPixelSize(13); qApp->setFont(font);
        m_source = m_workspace.filePath("source.png"); QVERIFY(fixture().save(m_source));
    }
    void init() {
        m_window = new MainWindow; m_window->m_tempReloadTimer->stop();
        m_window->resize(1200, 820); m_window->show(); m_window->hide(); m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window)); QVERIFY(m_window->loadImageFromPath(m_source)); QTest::qWait(5);
    }
    void cleanup() { delete m_window; m_window = nullptr; QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void geometryIntervalsMatchIndependentPixels() {
        for (int trial = 0; trial < 80; ++trial) {
            const int w = 31, h = 17;
            QVector<Span> a, b; QVector<int> pa(w * h), pb(w * h);
            for (int y = 0; y < h; ++y) {
                int startA = -1, startB = -1;
                for (int x = 0; x < w; ++x) {
                    const bool onA = ((x * 13 + y * 7 + trial * 3) % 11) < 5;
                    const bool onB = ((x * 5 + y * 17 + trial) % 13) < 6;
                    pa[y * w + x] = onA; pb[y * w + x] = onB;
                    if (onA && startA < 0) startA = x; if (onB && startB < 0) startB = x;
                    if ((!onA || x + 1 == w) && startA >= 0) { a.append(Span(y, startA, onA ? x + 1 : x)); startA = -1; }
                    if ((!onB || x + 1 == w) && startB >= 0) { b.append(Span(y, startB, onB ? x + 1 : x)); startB = -1; }
                }
            }
            for (int operation = 0; operation < 2; ++operation) {
                const QVector<Span> output = operation ? subtract(a, b) : unite(a, b);
                QVector<int> actual(w * h); for (const Span& s : output) for (int x = s.begin; x < s.end; ++x) actual[s.y * w + x] = 1;
                for (int p = 0; p < w * h; ++p) QCOMPARE(actual[p], int(operation ? pa[p] && !pb[p] : pa[p] || pb[p]));
            }
        }
    }
    void fixedSeedWandMatchesIndependentSearch() {
        QImage image(25, 19, QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgba((x * 29 + y * 11) % 256, (x * 17 + y * 23) % 256, (x * 7 + y * 13) % 256, (x + y) % 9 ? 255 : 0));
        for (int tolerance : {0, 15, 40, 100}) for (bool global : {false, true}) for (const QPoint& seed : {QPoint(5, 4), QPoint(6, 4)}) {
            Model model; model.clear(image.size());
            auto job = new SelectionJob(model.snapshot(Parameters()), image.size(), Operation::New);
            QVector<Span> spans; QVERIFY(waitJob(job, [&]() { job->startWand(image, seed, tolerance, global); }, spans));
            const QRgb origin = image.pixel(seed); const double limit = 3.0 * 255 * 255 * std::pow(tolerance / 100.0, 2);
            auto matches = [&](int p) {
                const QRgb c = image.pixel(p % image.width(), p / image.width());
                if (!qAlpha(origin)) return !qAlpha(c); if (!qAlpha(c)) return false;
                const int r = qRed(c) - qRed(origin), g = qGreen(c) - qGreen(origin), b = qBlue(c) - qBlue(origin);
                return r * r + g * g + b * b <= limit;
            };
            QSet<int> expected; std::deque<int> pending; pending.push_back(seed.y() * image.width() + seed.x()); QSet<int> visited;
            if (global) { for (int p = 0; p < image.width() * image.height(); ++p) if (matches(p)) expected.insert(p); }
            else while (!pending.empty()) {
                int p = pending.front(); pending.pop_front(); if (visited.contains(p)) continue; visited.insert(p);
                if (!matches(p)) continue; expected.insert(p); const int x = p % image.width(), y = p / image.width();
                if (x) pending.push_back(p - 1); if (x + 1 < image.width()) pending.push_back(p + 1);
                if (y) pending.push_back(p - image.width()); if (y + 1 < image.height()) pending.push_back(p + image.width());
            }
            QSet<int> actual; for (const Span& s : spans) for (int x = s.begin; x < s.end; ++x) actual.insert(s.y * image.width() + x);
            QCOMPARE(actual, expected);
        }
    }
    void exclusiveSelectionsAndHistory() {
        Model model; model.clear(QSize(60, 40)); model.items = {rectangle(1, QRect(10, 10, 10, 10)), rectangle(2, QRect(30, 10, 10, 10))}; model.selected = 1;
        for (Operation op : {Operation::New, Operation::Add, Operation::Subtract}) {
            QPainterPath path; path.addRect(QRectF(5, 5, 40, 20));
            auto job = new SelectionJob(model.snapshot(Parameters()), model.size, op);
            QVector<Span> spans; QVERIFY(waitJob(job, [&]() { job->startGeometry(path); }, spans));
            for (const Span& s : spans) for (int x = s.begin; x < s.end; ++x) {
                const quint32 owner = model.owners(model.size)[s.y * 60 + x];
                if (op == Operation::New) QCOMPARE(owner, quint32(0));
                if (op == Operation::Add) QVERIFY(owner == 0 || owner == 1);
                if (op == Operation::Subtract) QVERIFY(owner == 1 && !(x >= 5 && x < 45 && s.y >= 5 && s.y < 25));
            }
        }
        Parameters globals; const Snapshot before = model.snapshot(globals); model.items[0].offsets[Silk] = 23;
        QVERIFY(model.record(before, globals)); QVERIFY(model.restoreHistory(false, globals)); QCOMPARE(model.items[0].offsets[Silk], 0);
        QVERIFY(model.restoreHistory(true, globals)); QCOMPARE(model.items[0].offsets[Silk], 23);
        for (int i = 0; i < 75; ++i) { Snapshot s = model.snapshot(globals); model.items[0].name = QString::number(i); model.record(s, globals); }
        QCOMPARE(model.undoCount(), 50);
    }
    void offsetsClampWithoutLosingDifference() {
        Region item = rectangle(1, QRect(1, 1, 2, 2)); item.offsets[Silk] = 50; item.offsets[Trans] = -60;
        Parameters globals; globals[Silk] = 240; globals[Trans] = 20;
        QCOMPARE(effective(globals, item)[Silk], 255); QCOMPARE(effective(globals, item)[Trans], 0);
        globals[Silk] = 100; globals[Trans] = 160;
        QCOMPARE(effective(globals, item)[Silk], 150); QCOMPARE(effective(globals, item)[Trans], 100);
        QCOMPARE(item.offsets[Silk], 50); QCOMPARE(item.offsets[Trans], -60);
        setRegions({item}); m_window->m_localRegionSliders[Silk]->setValue(230);
        QCOMPARE(m_window->m_regions.current()->offsets[Silk], 50);
        m_window->s_silk->setValue(250); QCOMPARE(m_window->m_localRegionSliders[Silk]->value(), 255);
        m_window->s_silk->setValue(180); QCOMPARE(m_window->m_localRegionSliders[Silk]->value(), 230);
        m_window->undoRegionChange(false); QCOMPARE(m_window->s_silk->value(), 250);
        m_window->undoRegionChange(true); QCOMPARE(m_window->s_silk->value(), 180);
    }
    void originalV151ClassificationIsPixelIdentical() {
        QImage src = fixture(); ImageProcessor current; BaselineImageProcessor baseline;
        const QStringList masks{QStringLiteral("蓝色"), QStringLiteral("黑色"), QStringLiteral("红色"), QStringLiteral("绿色"), QStringLiteral("白色"), QStringLiteral("黄色"), QStringLiteral("紫色")};
        for (const QString& mask : masks) for (const QString& finish : {QStringLiteral("沉金"), QStringLiteral("喷锡"), QStringLiteral("OSP")}) for (bool gray : {false, true}) {
            QImage expected[5], actual[5];
            baseline.processImage(src, 45, 180, 120, 150, 150, mask, finish, mask == QStringLiteral("白色"), true, gray, 20, 65, 80,
                expected[0], expected[1], expected[2], expected[3], expected[4], {}, false);
            current.processImage(src, 45, 180, 120, 150, 150, mask, finish, mask == QStringLiteral("白色"), true, gray, 20, 65, 80,
                actual[0], actual[1], actual[2], actual[3], actual[4], {}, false);
            for (int layer = 0; layer < 5; ++layer) QCOMPARE(actual[layer], expected[layer]);
        }
    }
    void zeroOffsetsAndRegionalEdgesStayIsolated() {
        for (bool stroke : {false, true}) for (bool metal : {false, true}) for (bool expose : {false, true}) {
            setRegions({}); setControls(4, 0, true, true, true, stroke, metal, expose);
            const auto baseline = m_window->m_layers; const QImage composite = m_window->m_previewComposite;
            Region r = rectangle(1, QRect(20, 15, 35, 30)); setRegions({r});
            QCOMPARE(m_window->m_layers, baseline); QCOMPARE(m_window->m_previewComposite, composite);
            for (int p = 0; p < ParameterCount; ++p) r.offsets[p] = p % 2 ? 35 : -25;
            setRegions({r}); const auto& owners = m_window->m_regions.owners(m_window->m_origin.size());
            bool changed = false;
            for (auto it = baseline.begin(); it != baseline.end(); ++it) for (int y = 0; y < it.value().height(); ++y) for (int x = 0; x < it.value().width(); ++x) {
                const QRgb old = it.value().pixel(x, y), now = m_window->m_layers[it.key()].pixel(x, y);
                if (!owners[y * it.value().width() + x]) QCOMPARE(now, old); else if (old != now) changed = true;
            }
            QVERIFY(changed);
        }
    }
    void everyLocalParameterMatchesFullImageContext() {
        const Parameters globals = m_window->globalRegionParameters();
        for (bool stroke : {false, true}) for (int p = 0; p < ParameterCount; ++p) {
            m_window->m_regions.clear(m_window->m_origin.size()); m_window->setGlobalRegionParameters(globals);
            setControls(p % 2 ? 4 : 0, 0, true, p != BareSimilarity, true, stroke, p % 2, true);
            const auto baseline = m_window->m_layers;
            Region region = rectangle(1, QRect(20, 15, 35, 30)); region.offsets[p] = p % 2 ? -40 : 40;
            m_window->setGlobalRegionParameters(effective(globals, region)); m_window->updateProcess();
            const auto uniform = m_window->m_layers;
            m_window->setGlobalRegionParameters(globals); setRegions({region});
            const auto& owners = m_window->m_regions.owners(m_window->m_origin.size());
            for (auto it = baseline.begin(); it != baseline.end(); ++it) for (int y = 0; y < it.value().height(); ++y) for (int x = 0; x < it.value().width(); ++x)
                QCOMPARE(m_window->m_layers[it.key()].pixel(x, y), owners[y * it.value().width() + x] ? uniform[it.key()].pixel(x, y) : it.value().pixel(x, y));
        }
    }
    void geometryQtMouseEventsAndReverseOperation() {
        m_window->m_regionEnabled->setChecked(true); QTest::qWait(200);
        for (Tool tool : {Tool::Rectangle, Tool::Lasso, Tool::Brush}) {
            m_window->m_regionOperation->setCurrentIndex(0); m_window->setRegionTool(tool);
            if (tool == Tool::Lasso) {
                auto label = m_window->l_composite;
                QTest::mousePress(label, Qt::LeftButton, Qt::NoModifier, screenPoint(label, QPointF(65, 10)));
                move(label, screenPoint(label, QPointF(85, 10)), Qt::LeftButton); move(label, screenPoint(label, QPointF(80, 30)), Qt::LeftButton);
                QTest::mouseRelease(label, Qt::LeftButton, Qt::NoModifier, screenPoint(label, QPointF(80, 30)));
            } else drag(m_window->l_composite, tool == Tool::Brush ? QPointF(65, 55) : QPointF(10, 10), tool == Tool::Brush ? QPointF(80, 60) : QPointF(35, 35));
            QTRY_VERIFY_WITH_TIMEOUT(m_window->m_regionJob.isNull(), 5000);
        }
        QCOMPARE(m_window->m_regions.items.size(), 3);
        m_window->selectRegion(1); m_window->setRegionTool(Tool::Rectangle); m_window->m_regionOperation->setCurrentIndex(1);
        const QVector<Span> original = m_window->m_regions.current()->spans;
        drag(m_window->l_composite, QPointF(15, 15), QPointF(25, 25), Qt::RightButton);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->m_regionJob.isNull(), 5000);
        QVERIFY(m_window->m_regions.current()->spans != original); QCOMPARE(m_window->m_regionOperation->currentIndex(), 1);
        m_window->undoRegionChange(false); QCOMPARE(m_window->m_regions.current()->spans, original);
        const int history = m_window->m_regions.undoCount();
        QTest::mousePress(m_window->l_composite, Qt::LeftButton, Qt::NoModifier, screenPoint(m_window->l_composite, QPointF(2, 2)));
        m_window->setRegionTool(Tool::Select); QCOMPARE(m_window->m_regions.undoCount(), history); QVERIFY(!m_window->m_gestureActive);
    }
    void selectionZoomPanLightingAndOtherPreviews() {
        setRegions({rectangle(1, QRect(15, 15, 30, 25))}); m_window->m_regionEnabled->setChecked(true); QTest::qWait(200);
        auto label = m_window->l_composite; m_window->m_previewZoom = 2.5; m_window->m_previewPan = QPointF(20, -15); m_window->refreshRegionPreviews();
        QTest::mouseClick(label, Qt::LeftButton, Qt::NoModifier, screenPoint(label, QPointF(25, 25)));
        QCOMPARE(m_window->m_regions.selected, quint32(1)); QCOMPARE(m_window->m_ledStrips.size(), 0);
        const QPointF oldPan = m_window->m_previewPan; drag(label, QPointF(25, 25), QPointF(30, 30)); QVERIFY(m_window->m_previewPan != oldPan);
        m_window->check_expandPreviews->setChecked(true); QTest::qWait(240);
        for (QLabel* preview : {m_window->l_copper, m_window->l_mask, m_window->l_silk, m_window->l_bottom}) {
            m_window->m_regions.selected = 0; QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, screenPoint(preview, QPointF(25, 25)));
            QCOMPARE(m_window->m_regions.selected, quint32(1));
        }
        m_window->m_regionEnabled->setChecked(false); QTest::qWait(200);
        QTest::mouseClick(label, Qt::LeftButton, Qt::NoModifier, screenPoint(label, QPointF(25, 25)));
        QTest::mouseClick(label, Qt::LeftButton, Qt::NoModifier, screenPoint(label, QPointF(30, 30)));
        QCOMPARE(m_window->m_ledStrips.size(), 1);
    }
    void wandMouseReverseCancellationAndEmptySelections() {
        setRegions({rectangle(1, QRect(10, 10, 30, 30))}); m_window->m_regionEnabled->setChecked(true); QTest::qWait(200);
        m_window->setRegionTool(Tool::Wand); m_window->m_wandTolerance->setValue(0); m_window->m_regionOperation->setCurrentIndex(1);
        const QVector<Span> before = m_window->m_regions.current()->spans;
        QTest::mouseClick(m_window->l_composite, Qt::RightButton, Qt::NoModifier, screenPoint(m_window->l_composite, QPointF(20.5, 20.5)));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->m_regionJob.isNull(), 5000);
        QVERIFY(m_window->m_regions.current()->spans != before); QCOMPARE(m_window->m_regionOperation->currentIndex(), 1);
        m_window->undoRegionChange(false); QCOMPARE(m_window->m_regions.current()->spans, before);
        const Snapshot snapshot = m_window->m_regions.snapshot(m_window->globalRegionParameters());
        m_window->startRegionSelection(QPoint(20, 20), Operation::New); m_window->setRegionTool(Tool::Rectangle); QTest::qWait(30);
        QVERIFY(m_window->m_regions.snapshot(m_window->globalRegionParameters()) == snapshot);
        m_window->m_regionOperation->setCurrentIndex(0); drag(m_window->l_composite, QPointF(15, 15), QPointF(25, 25));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->m_regionJob.isNull(), 5000); QCOMPARE(m_window->m_regions.items.size(), 1);
        const int steps = m_window->m_regions.undoCount();
        m_window->m_regionOperation->setCurrentIndex(2); drag(m_window->l_composite, QPointF(5, 5), QPointF(50, 50));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->m_regionJob.isNull(), 5000); QVERIFY(m_window->m_regions.items.isEmpty());
        QCOMPARE(m_window->m_regions.undoCount(), steps + 1); m_window->undoRegionChange(false); QCOMPARE(m_window->m_regions.current()->spans, before);
    }
    void liveOverlayExcludesOtherRegionPixels() {
        setRegions({rectangle(1, QRect(10, 10, 25, 25)), rectangle(2, QRect(55, 10, 25, 25))});
        m_window->m_regionEnabled->setChecked(true); m_window->setRegionTool(Tool::Rectangle); m_window->m_regionHighlight->setChecked(false); QTest::qWait(200);
        m_window->m_regionOperation->setCurrentIndex(1); m_window->refreshRegionPreviews();
        const QImage before = m_window->l_composite->grab().toImage();
        QTest::mousePress(m_window->l_composite, Qt::LeftButton, Qt::NoModifier, screenPoint(m_window->l_composite, QPointF(5, 5)));
        move(m_window->l_composite, screenPoint(m_window->l_composite, QPointF(85, 40)), Qt::LeftButton); m_window->refreshRegionPreviews();
        const QImage live = m_window->l_composite->grab().toImage();
        const QPoint occupied = screenPoint(m_window->l_composite, QPointF(60, 20)); const QPoint available = screenPoint(m_window->l_composite, QPointF(45, 20));
        QCOMPARE(live.pixel(occupied), before.pixel(occupied)); QVERIFY(live.pixel(available) != before.pixel(available));
        m_window->cancelRegionGesture();
    }
    void localDragIsOneHistoryStepAndBaselineRenders() {
        setRegions({rectangle(1, QRect(10, 10, 30, 30))}); m_window->m_regionEnabled->setChecked(true); QTest::qWait(200);
        const int oldHistory = m_window->m_regions.undoCount(); auto slider = m_window->m_localRegionSliders[Silk];
        const int initial = slider->value();
        slider->setSliderDown(true); slider->setValue(120); slider->setValue(110); slider->setValue(100); slider->setSliderDown(false);
        QTRY_COMPARE(m_window->m_fullResolutionGeneration, m_window->m_progressiveRenderController.currentGeneration());
        QCOMPARE(m_window->m_regions.undoCount(), oldHistory + 1); m_window->undoRegionChange(false); QCOMPARE(slider->value(), initial);
        m_window->undoRegionChange(true); QCOMPARE(slider->value(), 100);
        for (int fontSize : {10, 13, 20}) {
            QFont font = qApp->font(); font.setPixelSize(fontSize); qApp->setFont(font); QTest::qWait(5);
            const QImage preview = slider->grab().toImage(); QVERIFY(!preview.isNull());
        }
        QFont font = qApp->font(); font.setPixelSize(13); qApp->setFont(font);
    }
    void highlightCannotPolluteLayersOrExports() {
        Region r = rectangle(1, QRect(10, 10, 40, 35)); r.offsets[Silk] = -40; setRegions({r});
        const auto layers = m_window->m_layers; const auto composite = m_window->m_previewComposite;
        m_window->m_regionHighlight->setChecked(true); m_window->refreshRegionPreviews(); const QImage lit = m_window->l_composite->grab().toImage();
        m_window->m_regionHighlight->setChecked(false); m_window->refreshRegionPreviews(); const QImage plain = m_window->l_composite->grab().toImage();
        QVERIFY(lit != plain); QCOMPARE(m_window->m_layers, layers); QCOMPARE(m_window->m_previewComposite, composite);
        const QString directory = m_workspace.filePath("export"); QDir().mkpath(directory);
        QVERIFY(m_window->m_layerGenerator.exportLayersToFiles(layers, directory, "ENIG", m_window->m_ledStrips));
        QStringList files = QDir(directory).entryList({"*.png"}, QDir::Files); QVERIFY(files.size() >= 4);
        for (auto it = layers.begin(); it != layers.end(); ++it) {
            bool found = false;
            for (const QString& filename : files) if (filename.contains(it.key())) {
                const QImage png(QDir(directory).filePath(filename)); const QImage production = it.value().convertToFormat(QImage::Format_Mono);
                for (int y = 0; y < png.height(); ++y) for (int x = 0; x < png.width(); ++x) {
                    // 原版导出保留角落的六个 EDA 定位像素，其余必须对应生产层。
                    const bool anchor = (x == 0 && y <= 1) || (x == 1 && y == 0) ||
                        (x == png.width() - 1 && y >= png.height() - 2) || (x == png.width() - 2 && y == png.height() - 1);
                    if (!anchor) QCOMPARE(png.pixel(x, y), production.pixel(x, y));
                }
                found = true;
            }
            QVERIFY(found);
        }
    }
    void jsonValidationAndOldProject() {
        setRegions({rectangle(1, QRect(10, 10, 30, 30)), rectangle(2, QRect(60, 10, 20, 20))});
        const QJsonObject valid = m_window->projectArgs(); QVector<Region> parsed;
        QVERIFY(m_window->validateProjectArgs(valid, m_window->m_origin.size(), parsed)); QCOMPARE(parsed, m_window->m_regions.items);
        for (int kind = 0; kind < 7; ++kind) {
            QJsonObject root = valid, regions = root["regions"].toObject(); QJsonArray items = regions["items"].toArray(); QJsonObject first = items[0].toObject();
            if (kind == 0) regions["width"] = 99;
            if (kind == 1) first["spans"] = QJsonArray{QJsonArray{-1, 0, 2}};
            if (kind == 2) first["spans"] = QJsonArray{QJsonArray{1, 3, 2}};
            if (kind == 3) first["offsets"] = QJsonObject{{"silkThresh", 300}};
            if (kind == 4) first["offsets"] = QJsonObject{{"silkThresh", "broken"}};
            if (kind == 5) { QJsonObject second = items[1].toObject(); second["id"] = 1; items[1] = second; }
            if (kind == 6) { QJsonObject second = items[1].toObject(); second["spans"] = first["spans"]; items[1] = second; }
            items[0] = first; regions["items"] = items; root["regions"] = regions;
            QVERIFY(!m_window->validateProjectArgs(root, m_window->m_origin.size(), parsed));
        }
        QJsonObject old = valid; old["schemaVersion"] = 1; old.remove("regions");
        QVERIFY(m_window->validateProjectArgs(old, m_window->m_origin.size(), parsed)); QVERIFY(parsed.isEmpty());
        QJsonObject badVersion = valid; badVersion["schemaVersion"] = "2";
        QVERIFY(!m_window->validateProjectArgs(badVersion, m_window->m_origin.size(), parsed));
        m_window->applyProjectArgs(old, parsed); QVERIFY(m_window->m_regions.items.isEmpty());
    }
    void pcblgRoundTripAndFailedImportPreservesProject() {
        Region r = rectangle(1, QRect(10, 10, 30, 30)); r.offsets[Silk] = -60; setRegions({r});
        const auto layers = m_window->m_layers; QJsonObject args = m_window->projectArgs(); args["imageFileName"] = "source.png";
        const QString project = m_workspace.filePath("regions.pcblg"); QVERIFY(m_window->saveProjectToBlg(project));
        m_window->m_regions.clear(m_window->m_origin.size()); m_window->updateProcess();
        QVERIFY(m_window->importProjectFromBlg(project)); QCOMPARE(m_window->m_regions.items, QVector<Region>{r}); QCOMPARE(m_window->m_layers, layers);
        const QString badDir = m_workspace.filePath("bad"); QDir().mkpath(badDir); QVERIFY(fixture(50, 50).save(QDir(badDir).filePath("source.png")));
        QFile file(QDir(badDir).filePath("args.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(args).toJson()); file.close();
        const QString zip = m_workspace.filePath("bad.zip");
        QProcess process; process.start("powershell", {"-NoProfile", "-Command", QString("Compress-Archive -LiteralPath '%1','%2' -DestinationPath '%3' -Force").arg(QDir(badDir).filePath("source.png"), QDir(badDir).filePath("args.json"), zip)});
        QVERIFY(process.waitForFinished(30000)); QCOMPARE(process.exitCode(), 0);
        const auto before = m_window->m_regions.snapshot(m_window->globalRegionParameters()); const auto image = m_window->m_origin;
        const QString source = m_window->m_tempImagePath; QFile sourceFile(source); QVERIFY(sourceFile.open(QIODevice::ReadOnly)); const QByteArray bytes = sourceFile.readAll(); sourceFile.close();
        QVERIFY(!m_window->importProjectFromBlg(zip)); QCOMPARE(m_window->m_origin, image); QVERIFY(m_window->m_regions.snapshot(m_window->globalRegionParameters()) == before);
        QCOMPARE(m_window->m_tempImagePath, source); QVERIFY(sourceFile.open(QIODevice::ReadOnly)); QCOMPARE(sourceFile.readAll(), bytes);
    }
    void archiveUsesDeclaredImageAndRejectsMissingSource() {
        setRegions({rectangle(1, QRect(10, 10, 20, 20))});
        const QString directory = m_workspace.filePath("images"); QDir().mkpath(directory);
        QVERIFY(fixture().save(QDir(directory).filePath("source.png")));
        QVERIFY(fixture(20, 20).save(QDir(directory).filePath("decoy.png")));
        QJsonObject args = m_window->projectArgs(); args["imageFileName"] = "source.png";
        auto pack = [&](const QString& name) {
            QFile file(QDir(directory).filePath("args.json")); if (!file.open(QIODevice::WriteOnly)) return false;
            file.write(QJsonDocument(args).toJson()); file.close();
            QProcess process; process.start("powershell", {"-NoProfile", "-Command", QString("Compress-Archive -Path '%1/*' -DestinationPath '%2' -Force").arg(directory, m_workspace.filePath(name))});
            return process.waitForFinished(30000) && process.exitCode() == 0;
        };
        QVERIFY(pack("declared.zip")); QVERIFY(m_window->importProjectFromBlg(m_workspace.filePath("declared.zip")));
        QCOMPARE(m_window->m_origin, fixture()); const QJsonObject before = m_window->projectArgs();
        args["imageFileName"] = "missing.png"; QVERIFY(pack("missing.zip"));
        QVERIFY(!m_window->importProjectFromBlg(m_workspace.filePath("missing.zip"))); QCOMPARE(m_window->projectArgs(), before);
    }
    void oversizedRegionProjectIsRejectedWithoutScaling() {
        setRegions({rectangle(1, QRect(10, 10, 20, 20))});
        const QJsonObject before = m_window->projectArgs(); const QImage origin = m_window->m_origin;
        const QString directory = m_workspace.filePath("oversized"); QDir().mkpath(directory);
        QImage image(4000, 4000, QImage::Format_RGB32); image.fill(Qt::gray);
        QVERIFY(image.save(QDir(directory).filePath("source.png"))); image = QImage();
        QJsonObject args = before; args["imageFileName"] = "source.png";
        QFile file(QDir(directory).filePath("args.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(args).toJson()); file.close();
        const QString zip = m_workspace.filePath("oversized.zip");
        QProcess process; process.start("powershell", {"-NoProfile", "-Command", QString("Compress-Archive -Path '%1/*' -DestinationPath '%2' -Force").arg(directory, zip)});
        QVERIFY(process.waitForFinished(30000)); QCOMPARE(process.exitCode(), 0);
        bool warned = false;
        QTimer::singleShot(20, [&]() {
            auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(box);
            warned = box->text().contains(QStringLiteral("不能自动缩放")); box->button(QMessageBox::Ok)->click();
        });
        QVERIFY(!m_window->importProjectFromBlg(zip)); QVERIFY(warned);
        QCOMPARE(m_window->m_origin, origin); QCOMPARE(m_window->projectArgs(), before);
    }
    void paintReloadKeepsMasksAndNewImageClears() {
        Region r = rectangle(1, QRect(10, 10, 30, 30)); r.offsets[Trans] = 15; setRegions({r});
        const QString path = m_window->m_tempImagePath; QImage image = fixture(); image.fill(QColor(150, 100, 90)); QVERIFY(image.save(path));
        QVERIFY(m_window->loadImageFromPath(path, true)); QCOMPARE(m_window->m_regions.items, QVector<Region>{r}); QCOMPARE(m_window->m_regions.undoCount(), 0);
        QVERIFY(fixture(50, 50).save(path)); bool warned = false;
        QTimer::singleShot(20, [&]() {
            auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(box);
            warned = box->text().contains(QStringLiteral("图片尺寸改变")); box->button(QMessageBox::Ok)->click();
        });
        QVERIFY(m_window->loadImageFromPath(path, true)); QVERIFY(warned); QVERIFY(m_window->m_regions.items.isEmpty());
        QVERIFY(m_window->loadImageFromPath(m_source)); setRegions({r});
        QVERIFY(m_window->loadImageFromPath(m_source)); QVERIFY(m_window->m_regions.items.isEmpty());
    }
    void renameDeleteCopyAndResetAreUndoable() {
        Region first = rectangle(1, QRect(10, 10, 20, 20)), second = rectangle(2, QRect(50, 10, 20, 20));
        first.offsets[Silk] = -40; setRegions({first, second});
        QTimer::singleShot(20, []() {
            auto dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->setTextValue(QStringLiteral("左侧角色")); dialog->accept();
        });
        m_window->m_regionRename->click(); QCOMPARE(m_window->m_regions.current()->name, QStringLiteral("左侧角色"));
        m_window->undoRegionChange(false); QCOMPARE(m_window->m_regions.current()->name, first.name); m_window->undoRegionChange(true);
        QTimer::singleShot(20, []() {
            auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
            auto list = dialog->findChild<QListWidget*>(); QVERIFY(list); list->item(0)->setSelected(true); dialog->accept();
        });
        m_window->m_regionCopy->click(); QCOMPARE(m_window->m_regions.items[1].offsets[Silk], -40);
        const auto shape = m_window->m_regions.current()->spans; m_window->m_regionReset->click(); QCOMPARE(m_window->m_regions.current()->offsets[Silk], 0);
        QCOMPARE(m_window->m_regions.current()->spans, shape); m_window->undoRegionChange(false); QCOMPARE(m_window->m_regions.current()->offsets[Silk], -40);
        QTimer::singleShot(20, []() { auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::No)->click(); });
        m_window->m_regionDelete->click(); QCOMPARE(m_window->m_regions.items.size(), 2);
        QTimer::singleShot(20, []() { auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Yes)->click(); });
        m_window->m_regionDelete->click(); QCOMPARE(m_window->m_regions.items.size(), 1); m_window->undoRegionChange(false); QCOMPARE(m_window->m_regions.items.size(), 2);
    }
    void nearLimitRenderingAndCancellation() {
        QImage image(4000, 3999, QImage::Format_RGB32); image.fill(QColor(160, 150, 140));
        m_window->m_origin = image; m_window->m_regions.clear(image.size());
        QVector<Region> regions; for (int id = 1; id <= 3; ++id) { Region r = rectangle(id, QRect((id - 1) * 1000, 500, 900, 2400)); r.offsets[Silk] = -20 * id; regions.append(r); }
        m_window->m_regions.items = regions; ++m_window->m_regions.revision;
        QElapsedTimer clock; clock.start();
        const QSize preview = ProgressiveRendering::interactivePreviewSize(image.size());
        m_window->renderAtSize(preview, m_window->m_progressiveRenderController.currentGeneration(), false);
        const qint64 previewMs = clock.elapsed(); clock.restart();
        m_window->updateProcess(); const qint64 fullMs = clock.elapsed();
        QCOMPARE(m_window->m_layers["Top_Silk"].size(), image.size());
        QCOMPARE(m_window->m_regions.owners(image.size()).size(), 15996000);
        m_window->m_regionEnabled->setChecked(true); m_window->setRegionTool(Tool::Wand); m_window->m_wandGlobal->setChecked(true);
        const Snapshot before = m_window->m_regions.snapshot(m_window->globalRegionParameters());
        bool responsive = false; QTimer::singleShot(20, [&]() { responsive = true; m_window->cancelRegionGesture(); });
        clock.restart(); m_window->startRegionSelection(QPoint(5, 5), Operation::New);
        QTRY_VERIFY_WITH_TIMEOUT(responsive, 1000); QVERIFY(m_window->m_regionJob.isNull());
        QVERIFY(m_window->m_regions.snapshot(m_window->globalRegionParameters()) == before);
        qInfo("near-limit: preview=%lldms full=%lldms cancellation=%lldms", previewMs, fullMs, clock.elapsed());
    }
    void uiArtifactsAndScale() {
        setRegions({rectangle(1, QRect(15, 12, 45, 42)), rectangle(2, QRect(65, 12, 20, 20))});
        m_window->m_regions.current()->offsets[Silk] = -50; ++m_window->m_regions.revision;
        m_window->m_regionEnabled->setChecked(true); m_window->setRegionTool(Tool::Wand); m_window->updateProcess(); QTest::qWait(250);
        m_window->grab().save(QDir(artifacts()).filePath("regions-ui.png"));
        for (QScrollArea* scroll : m_window->findChildren<QScrollArea*>()) if (scroll->widget()->isAncestorOf(m_window->m_localRegionGroup)) scroll->ensureWidgetVisible(m_window->m_localRegionGroup);
        QTest::qWait(50); m_window->grab().save(QDir(artifacts()).filePath("regions-local-parameters.png"));
        m_window->m_regions.current()->name = QString(100, QChar(0x957f)); m_window->refreshRegionUI();
        for (int pixelSize : {10, 20}) {
            QFont font = qApp->font(); font.setPixelSize(pixelSize); qApp->setFont(font); m_window->resize(1366, 900); QTest::qWait(100);
            QVERIFY(m_window->width() <= 1366);
            for (QScrollArea* scroll : m_window->findChildren<QScrollArea*>()) QVERIFY(scroll->widget()->width() <= scroll->viewport()->width());
            QVERIFY(m_window->m_regionToolCombo->isVisible()); QCOMPARE(m_window->m_regionToolCombo->count(), 5);
            for (int tool = 0; tool < 5; ++tool) {
                m_window->m_regionToolCombo->setCurrentIndex(tool); QCOMPARE(int(m_window->m_regionTool), tool);
                QCOMPARE(m_window->m_regionBrushControls->isVisible(), tool == int(Tool::Brush));
                QCOMPARE(m_window->m_regionWandControls->isVisible(), tool == int(Tool::Wand));
            }
            m_window->grab().save(QDir(artifacts()).filePath(QString("regions-ui-%1.png").arg(pixelSize)));
        }
        QFont font = qApp->font(); font.setPixelSize(13); qApp->setFont(font);
    }
};

int main(int argc, char** argv) { QApplication application(argc, argv); RegionTests tests; return QTest::qExec(&tests, argc, argv); }
#include "tst_regions.moc"
