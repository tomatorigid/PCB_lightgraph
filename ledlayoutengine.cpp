#include "ledlayoutengine.h"
#include <cmath>
#include <algorithm>
#include <QPainter>
#include <QDebug>
#include <QMap>

namespace {
static float cross2d(const QPointF& a, const QPointF& b, const QPointF& c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}

static bool onSegment(const QPointF& a, const QPointF& b, const QPointF& p) {
    return p.x() >= qMin(a.x(), b.x()) && p.x() <= qMax(a.x(), b.x())
        && p.y() >= qMin(a.y(), b.y()) && p.y() <= qMax(a.y(), b.y());
}

static bool segmentsIntersect(const QPointF& a1, const QPointF& a2, const QPointF& b1, const QPointF& b2) {
    float d1 = cross2d(a1, a2, b1);
    float d2 = cross2d(a1, a2, b2);
    float d3 = cross2d(b1, b2, a1);
    float d4 = cross2d(b1, b2, a2);

    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
        ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
        return true;
    }

    const float eps = 1e-5f;
    if (std::abs(d1) <= eps && onSegment(a1, a2, b1)) return true;
    if (std::abs(d2) <= eps && onSegment(a1, a2, b2)) return true;
    if (std::abs(d3) <= eps && onSegment(b1, b2, a1)) return true;
    if (std::abs(d4) <= eps && onSegment(b1, b2, a2)) return true;
    return false;
}

static float segmentToSegmentDistance(const QPoint& a1, const QPoint& a2, const QPoint& b1, const QPoint& b2) {
    QPointF af1(a1), af2(a2), bf1(b1), bf2(b2);
    if (segmentsIntersect(af1, af2, bf1, bf2)) {
        return 0.0f;
    }

    float d1 = LEDLayoutEngine::distanceToSegment(a1, b1, b2);
    float d2 = LEDLayoutEngine::distanceToSegment(a2, b1, b2);
    float d3 = LEDLayoutEngine::distanceToSegment(b1, a1, a2);
    float d4 = LEDLayoutEngine::distanceToSegment(b2, a1, a2);
    return std::min(std::min(d1, d2), std::min(d3, d4));
}
}

LEDLayoutEngine::LEDLayoutEngine() {
}

LEDLayoutEngine::~LEDLayoutEngine() {
}


float LEDLayoutEngine::distanceToSegment(QPoint p, QPoint v, QPoint w) {
    float l2 = std::pow(v.x() - w.x(), 2) + std::pow(v.y() - w.y(), 2);
    if (l2 == 0.0) {
        return std::sqrt(std::pow(p.x() - v.x(), 2) + std::pow(p.y() - v.y(), 2));
    }
    float t = std::max(0.0f, std::min(1.0f,
        (float)((p.x() - v.x()) * (w.x() - v.x()) +
               (p.y() - v.y()) * (w.y() - v.y())) / l2));
    QPoint proj(v.x() + t * (w.x() - v.x()), v.y() + t * (w.y() - v.y()));
    return std::sqrt(std::pow(p.x() - proj.x(), 2) + std::pow(p.y() - proj.y(), 2));
}

QVector<LEDStrip> LEDLayoutEngine::autoSuggestLEDs(
    const QImage& srcImage,
    int targetCount,
    int ledRadius) {

    QVector<LEDStrip> result;

    if (srcImage.isNull() || targetCount <= 0) {
        return result;
    }

    int w = srcImage.width();
    int h = srcImage.height();

    // 定义色彩桶结构
    QMap<int, ColorBucket> buckets;

    // 1. 采样：寻找彩色区域
    for (int y = 0; y < h; y += 15) {
        for (int x = 0; x < w; x += 15) {
            QColor c = srcImage.pixelColor(x, y);
            if (c.value() > 80) {
                // 饱和度低判定为灰色区 (-1)，否则按色相分桶
                int bucketIdx = (c.saturation() < 30) ? -1 : (c.hue() / 30);
                buckets[bucketIdx].points.append(QPoint(x, y));
                buckets[bucketIdx].totalBrightness += c.value();
            }
        }
    }

    // 2. 桶排序逻辑
    struct RankedBucket {
        int id;
        long long weight;
    };
    QList<RankedBucket> ranked;

    QMapIterator<int, ColorBucket> it(buckets);
    while (it.hasNext()) {
        it.next();
        RankedBucket rb;
        rb.id = it.key();
        rb.weight = it.value().totalBrightness;
        ranked.append(rb);
    }

    std::sort(ranked.begin(), ranked.end(),
        [](const RankedBucket& a, const RankedBucket& b) {
            return a.weight > b.weight;
        });

    // 3. 按颜色重要性布灯
    int added = 0;
    for (const auto& rb : ranked) {
        if (added >= targetCount) break;

        QPoint bestPos;
        int maxV = -1;
        QColor bestCol = Qt::white;

        for (const QPoint& p : buckets[rb.id].points) {
            QColor c = srcImage.pixelColor(p.x(), p.y());
            if (c.value() > maxV) {
                // 检查灯光覆盖范围（胶囊体）不重叠
                bool tooClose = false;
                QPoint candidateStart(p.x(), qMax(0, p.y() - 25));
                QPoint candidateEnd(p.x(), qMin(h - 1, p.y() + 25));
                for (const auto& s : result) {
                    float segDist = segmentToSegmentDistance(candidateStart, candidateEnd, s.start, s.end);
                    int existingRadius = (s.radius > 0) ? s.radius : ledRadius;
                    float minAllowed = static_cast<float>(qMax(1, ledRadius) + qMax(1, existingRadius));
                    if (segDist < minAllowed) {
                        tooClose = true;
                        break;
                    }
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
            s.start = QPoint(bestPos.x(), qMax(0, bestPos.y() - 25));
            s.end = QPoint(bestPos.x(), qMin(h - 1, bestPos.y() + 25));
            s.radius = ledRadius;
            s.color = bestCol;
            result.append(s);
            added++;
        }
    }

    return result;
}

QImage LEDLayoutEngine::generateLEDPreviewImage(
    int width,
    int height,
    const QVector<LEDStrip>& ledStrips) {

    QImage imgLED_Preview(width, height, QImage::Format_RGB32);
    imgLED_Preview.fill(Qt::black);

    QPainter pLED(&imgLED_Preview);
    pLED.setRenderHint(QPainter::Antialiasing);

    for (const auto& s : ledStrips) {
        qreal ledLineWidth = qBound<qreal>(1.0, width / 700.0, 3.0);
        pLED.setPen(QPen(s.color, ledLineWidth, Qt::SolidLine, Qt::RoundCap));
        pLED.drawLine(s.start, s.end);

        QPen ledDash(QColor(s.color.red(), s.color.green(), s.color.blue(), 180),
                     width / 150.0, Qt::DashLine);
        pLED.setPen(ledDash);
        pLED.drawEllipse(s.end, s.radius, s.radius);
    }
    pLED.end();

    return imgLED_Preview;
}

void LEDLayoutEngine::renderCompositeWithLEDs(
    QImage& composite,
    const QVector<LEDStrip>& ledStrips,
    int ledRadius,
    const QImage& copperMask,
    const QImage& bottomMask,
    bool showOverlay,
    int centerAlpha,
    bool forceShowOutlines) {

    if (composite.isNull()) return;

    // If overlay is disabled and outlines are not forced, do nothing
    if (!showOverlay && !forceShowOutlines) return;

    if (ledStrips.isEmpty() || ledRadius <= 0) return;

    int w = composite.width();
    int h = composite.height();

    // Accumulators
    QVector<int> aAcc(w * h);
    QVector<int> rAcc(w * h);
    QVector<int> gAcc(w * h);
    QVector<int> bAcc(w * h);

    // Accumulate per-strip light contribution
    for (const auto& s : ledStrips) {
        int minx = qMax(0, qMin(s.start.x(), s.end.x()) - ledRadius);
        int maxx = qMin(w - 1, qMax(s.start.x(), s.end.x()) + ledRadius);
        int miny = qMax(0, qMin(s.start.y(), s.end.y()) - ledRadius);
        int maxy = qMin(h - 1, qMax(s.start.y(), s.end.y()) + ledRadius);

        for (int y = miny; y <= maxy; ++y) {
            for (int x = minx; x <= maxx; ++x) {
                // distance to segment
                float l2 = std::pow(s.start.x() - s.end.x(), 2) + std::pow(s.start.y() - s.end.y(), 2);
                float d = 0.0f;
                if (l2 == 0.0f) {
                    d = std::sqrt(std::pow(x - s.end.x(), 2) + std::pow(y - s.end.y(), 2));
                } else {
                    float t = std::max(0.0f, std::min(1.0f,
                        (float)((x - s.start.x()) * (s.end.x() - s.start.x()) +
                               (y - s.start.y()) * (s.end.y() - s.start.y())) / l2));
                    QPoint proj(s.start.x() + t * (s.end.x() - s.start.x()), s.start.y() + t * (s.end.y() - s.start.y()));
                    d = std::sqrt(std::pow(x - proj.x(), 2) + std::pow(y - proj.y(), 2));
                }

                if (d >= ledRadius) continue;

                float factor = std::pow(1.0f - (d / (float)ledRadius), 1.8f);
                int addAlpha = qBound(0, (int)std::round(centerAlpha * factor), 255);
                if (addAlpha <= 0) continue;

                int idx = y * w + x;
                aAcc[idx] += addAlpha;
                rAcc[idx] += s.color.red() * addAlpha;
                gAcc[idx] += s.color.green() * addAlpha;
                bAcc[idx] += s.color.blue() * addAlpha;
            }
        }
    }

    // Blend accumulated light into composite respecting blocking masks
    for (int y = 0; y < h; ++y) {
        QRgb *lineComp = (QRgb *)composite.scanLine(y);
        const QRgb *lineCopper = copperMask.isNull() ? nullptr : (const QRgb *)copperMask.constScanLine(y);
        const QRgb *lineBottom = bottomMask.isNull() ? nullptr : (const QRgb *)bottomMask.constScanLine(y);

        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            if (aAcc[idx] <= 0) continue;

            int totalAlpha = qMin(255, aAcc[idx]);

            bool blocked = false;
            if (lineCopper) blocked = (qGray(lineCopper[x]) > 128);
            if (lineBottom) blocked = blocked || (qGray(lineBottom[x]) < 128);

            if (blocked) continue; // light is blocked at this pixel

            // compute average overlay color weighted by alpha
            float invA = 1.0f / (float)aAcc[idx];
            int ovR = (int)std::round(rAcc[idx] * invA);
            int ovG = (int)std::round(gAcc[idx] * invA);
            int ovB = (int)std::round(bAcc[idx] * invA);

            int baseR = qRed(lineComp[x]);
            int baseG = qGreen(lineComp[x]);
            int baseB = qBlue(lineComp[x]);

            float af = totalAlpha / 255.0f;
            int newR = qBound(0, (int)std::round(ovR * af + baseR * (1.0f - af)), 255);
            int newG = qBound(0, (int)std::round(ovG * af + baseG * (1.0f - af)), 255);
            int newB = qBound(0, (int)std::round(ovB * af + baseB * (1.0f - af)), 255);

            lineComp[x] = qRgb(newR, newG, newB);
        }
    }

    // Draw dashed capsule outlines and the LED lines (always drawn when overlay toggled)
    QPainter p(&composite);
    p.setRenderHint(QPainter::Antialiasing);
    // ensure same-type arguments for qMax (use double/qreal)
    QPen dashPen(QColor(255, 255, 255, 180), qMax(1.0, static_cast<double>(w) / 300.0), Qt::DashLine);

    for (const auto& s : ledStrips) {
        // capsule: draw rounded rect between start and end
        QPointF a = s.start;
        QPointF b = s.end;
        QRectF rect(QPointF(qMin(a.x(), b.x()), qMin(a.y(), b.y())), QPointF(qMax(a.x(), b.x()), qMax(a.y(), b.y())));
        rect.adjust(-ledRadius, -ledRadius, ledRadius, ledRadius);
        // angle to rotate capsule
        float dx = b.x() - a.x();
        float dy = b.y() - a.y();
        float length = std::sqrt(dx*dx + dy*dy);

        p.save();
        p.translate((a.x()+b.x())/2.0, (a.y()+b.y())/2.0);
        const double RAD2DEG = 57.29577951308232;
        float angle = std::atan2(dy, dx) * RAD2DEG;
        p.rotate(angle);

        // dashed outline
        p.setPen(dashPen);
        QRectF r(-length/2.0 - ledRadius, -ledRadius, length + ledRadius*2.0, ledRadius*2.0);
        p.drawRoundedRect(r, ledRadius, ledRadius);

        // LED main line: keep thin, independent from scattering radius
        qreal lineWidth = qBound<qreal>(1.0, w / 700.0, 3.0);
        QPen linePen(s.color, lineWidth, Qt::SolidLine, Qt::RoundCap);
        p.setPen(linePen);
        p.drawLine(QPointF(-length/2.0, 0), QPointF(length/2.0, 0));

        p.restore();
    }
}

