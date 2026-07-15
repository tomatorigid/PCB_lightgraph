// Clean, modular implementation of EdgeSharpener that delegates Gaussian blur and DP simplify
#include "edgesharpener.h"
#include "gaussian_blur.h"
#include "dp_simplify.h"
#include <cmath>
#include <QDebug>
#include <QVector>
#include <algorithm>
#include <numeric>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPoint>

EdgeSharpener::EdgeSharpener() {}
EdgeSharpener::~EdgeSharpener() {}

void EdgeSharpener::buildCacheIfNeeded(const QImage& srcImage) {
    if (srcImage.isNull()) return;
    quint64 key = srcImage.cacheKey();
    if (m_cachedImageKey == key && m_cachedW == srcImage.width() && m_cachedH == srcImage.height()) return;
    m_cachedImageKey = key;
    m_cachedW = srcImage.width();
    m_cachedH = srcImage.height();
    int w = m_cachedW; int h = m_cachedH;
    m_gray = QVector<int>(w*h, 0);
    m_edgeStrength = QVector<int>(w*h, 0);
    m_integral = QVector<quint64>((w+1)*(h+1), 0ULL);
    m_sobelMagnitude.clear();
    m_sobelDirection.clear();
    m_cannyNms.clear();
    m_cannyCacheReady = false;
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) m_gray[y*w + x] = qGray(srcImage.pixel(x,y));
    for (int y=1;y<h-1;++y) for (int x=1;x<w-1;++x) {
        int center = m_gray[y*w + x];
        int val = std::abs(4*center - m_gray[y*w + (x-1)] - m_gray[y*w + (x+1)] - m_gray[(y-1)*w + x] - m_gray[(y+1)*w + x]);
        m_edgeStrength[y*w + x] = val;
    }
    int iw = w+1;
    for (int j=0;j<=h;++j) for (int i=0;i<=w;++i) {
        if (i==0 || j==0) m_integral[j*iw + i] = 0;
        else {
            quint64 above = m_integral[(j-1)*iw + i];
            quint64 left = m_integral[j*iw + (i-1)];
            quint64 diag = m_integral[(j-1)*iw + (i-1)];
            m_integral[j*iw + i] = above + left - diag + (quint64)m_gray[(j-1)*w + (i-1)];
        }
    }
}

void EdgeSharpener::buildCannyCacheIfNeeded() {
    if (m_cachedW <= 2 || m_cachedH <= 2) return;
    if (m_cannyCacheReady) return;

    const int w = m_cachedW;
    const int h = m_cachedH;
    m_sobelMagnitude = QVector<int>(w * h, 0);
    m_sobelDirection = QVector<uchar>(w * h, 0);
    m_cannyNms = QVector<int>(w * h, 0);

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const int idx = y * w + x;
            const int a = m_gray[(y - 1) * w + (x - 1)];
            const int b = m_gray[(y - 1) * w + x];
            const int c = m_gray[(y - 1) * w + (x + 1)];
            const int d = m_gray[y * w + (x - 1)];
            const int f = m_gray[y * w + (x + 1)];
            const int g = m_gray[(y + 1) * w + (x - 1)];
            const int hVal = m_gray[(y + 1) * w + x];
            const int i = m_gray[(y + 1) * w + (x + 1)];

            const int gx = -a + c - 2 * d + 2 * f - g + i;
            const int gy = a + 2 * b + c - g - 2 * hVal - i;
            const int mag = static_cast<int>(std::sqrt(static_cast<double>(gx * gx + gy * gy)));
            m_sobelMagnitude[idx] = mag;

            const double kRadToDeg = 57.29577951308232;
            double angle = std::atan2(static_cast<double>(gy), static_cast<double>(gx)) * kRadToDeg;
            if (angle < 0.0) angle += 180.0;
            uchar dir = 0;
            if ((angle >= 0.0 && angle < 22.5) || angle >= 157.5) dir = 0;
            else if (angle < 67.5) dir = 45;
            else if (angle < 112.5) dir = 90;
            else dir = 135;
            m_sobelDirection[idx] = dir;
        }
    }

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const int idx = y * w + x;
            const int mag = m_sobelMagnitude[idx];
            int m1 = 0;
            int m2 = 0;
            switch (m_sobelDirection[idx]) {
            case 0:
                m1 = m_sobelMagnitude[idx - 1];
                m2 = m_sobelMagnitude[idx + 1];
                break;
            case 45:
                m1 = m_sobelMagnitude[(y - 1) * w + (x + 1)];
                m2 = m_sobelMagnitude[(y + 1) * w + (x - 1)];
                break;
            case 90:
                m1 = m_sobelMagnitude[(y - 1) * w + x];
                m2 = m_sobelMagnitude[(y + 1) * w + x];
                break;
            default:
                m1 = m_sobelMagnitude[(y - 1) * w + (x - 1)];
                m2 = m_sobelMagnitude[(y + 1) * w + (x + 1)];
                break;
            }
            m_cannyNms[idx] = (mag >= m1 && mag >= m2) ? mag : 0;
        }
    }

    m_cannyCacheReady = true;
}

int EdgeSharpener::averageGrayInBox(int x0, int y0, int x1, int y1) const {
    if (m_cachedW <= 0 || m_cachedH <=0) return 128;
    x0 = std::max(0, std::min(x0, m_cachedW-1));
    x1 = std::max(0, std::min(x1, m_cachedW-1));
    y0 = std::max(0, std::min(y0, m_cachedH-1));
    y1 = std::max(0, std::min(y1, m_cachedH-1));
    if (x1 < x0 || y1 < y0) return 128;
    int w = m_cachedW; int iw = w+1;
    quint64 A = m_integral[y0*iw + x0];
    quint64 B = m_integral[y0*iw + (x1+1)];
    quint64 C = m_integral[(y1+1)*iw + x0];
    quint64 D = m_integral[(y1+1)*iw + (x1+1)];
    quint64 sum = D + A - B - C;
    quint64 cnt = (quint64)(x1 - x0 + 1) * (quint64)(y1 - y0 + 1);
    if (cnt == 0) return 128;
    return static_cast<int>(sum / cnt);
}

int EdgeSharpener::calculateEdgeStrength(const QImage& img, int x, int y) {
    int w = img.width();
    int h = img.height();
    if (x <= 0 || x >= w - 1 || y <= 0 || y >= h - 1) return 0;
    int center = qGray(img.pixel(x, y));
    int edgeVal = std::abs(
        4 * center - qGray(img.pixel(x - 1, y))
                   - qGray(img.pixel(x + 1, y))
                   - qGray(img.pixel(x, y - 1))
                   - qGray(img.pixel(x, y + 1))
    );
    return edgeVal;
}

QImage EdgeSharpener::buildLaplacianMask(int edgeThreshMin, int edgeThreshMax) const {
    const int w = m_cachedW;
    const int h = m_cachedH;
    QImage edgeMask(w, h, QImage::Format_Grayscale8);
    edgeMask.fill(0);
    for (int y = 1; y < h - 1; ++y) {
        uchar* line = edgeMask.scanLine(y);
        for (int x = 1; x < w - 1; ++x) {
            const int ev = m_edgeStrength[y * w + x];
            if (ev > edgeThreshMin && ev < edgeThreshMax) line[x] = 255;
        }
    }
    return edgeMask;
}

QImage EdgeSharpener::buildCannyMask(int edgeThreshMin, int edgeThreshMax) {
    buildCannyCacheIfNeeded();
    const int w = m_cachedW;
    const int h = m_cachedH;

    int low = qBound(0, edgeThreshMin, 4096);
    int high = qBound(0, edgeThreshMax, 4096);
    if (low > high) std::swap(low, high);

    QImage edgeMask(w, h, QImage::Format_Grayscale8);
    edgeMask.fill(0);
    QVector<char> visited(w * h, 0);
    QVector<QPoint> stack;

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const int idx = y * w + x;
            if (visited[idx] || m_cannyNms[idx] < high) continue;

            stack.clear();
            stack.append(QPoint(x, y));
            visited[idx] = 1;

            while (!stack.isEmpty()) {
                const QPoint p = stack.takeLast();
                edgeMask.scanLine(p.y())[p.x()] = 255;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int nx = p.x() + dx;
                        const int ny = p.y() + dy;
                        if (nx <= 0 || nx >= w - 1 || ny <= 0 || ny >= h - 1) continue;
                        const int nidx = ny * w + nx;
                        if (visited[nidx]) continue;
                        if (m_cannyNms[nidx] >= low) {
                            visited[nidx] = 1;
                            stack.append(QPoint(nx, ny));
                        }
                    }
                }
            }
        }
    }
    return edgeMask;
}

QColor EdgeSharpener::determineEdgeColor(const QImage& srcImage, int x, int y, int autoInvertRange) {
    int w = srcImage.width();
    int h = srcImage.height();
    if (autoInvertRange == -1) return QColor(0,0,0);
    if (autoInvertRange == 0) return QColor(255,255,255);
    long long sumGray = 0; int count = 0;
    for (int dy = -autoInvertRange; dy <= autoInvertRange; ++dy) {
        for (int dx = -autoInvertRange; dx <= autoInvertRange; ++dx) {
            int nx = x + dx; int ny = y + dy;
            if (nx >=0 && nx < w && ny >=0 && ny < h) { sumGray += qGray(srcImage.pixel(nx,ny)); ++count; }
        }
    }
    int avg = (count>0) ? (int)(sumGray / count) : 128;
    return (avg > 128) ? QColor(0,0,0) : QColor(255,255,255);
}

QImage EdgeSharpener::processEdgeSharpening(const QImage& srcImage,
    int edgeThreshMin,int edgeThreshMax,int autoInvertRange,
    bool useMetal,const QColor& metalColor,
    bool enablePreFilter,int gaussianKernelSize,double gaussianSigma,
    bool enableDouglasPeucker,double dpTolerance,int dpLineWidth) {

    return processEdgeOperation(
        srcImage,
        OperationMode::EdgeEnhance,
        edgeThreshMin,
        edgeThreshMax,
        autoInvertRange,
        useMetal,
        metalColor,
        enablePreFilter,
        gaussianKernelSize,
        gaussianSigma,
        enableDouglasPeucker,
        dpTolerance,
        dpLineWidth
    );
}

QImage EdgeSharpener::processEdgeOperation(const QImage& srcImage,
    OperationMode mode,
    int edgeThreshMin,int edgeThreshMax,int autoInvertRange,
    bool useMetal,const QColor& metalColor,
    bool enablePreFilter,int gaussianKernelSize,double gaussianSigma,
    bool enableDouglasPeucker,double dpTolerance,int dpLineWidth) {

    if (srcImage.isNull()) return QImage();
    QImage working = enablePreFilter ? applyGaussianBlur(srcImage, gaussianKernelSize, gaussianSigma) : srcImage;
    buildCacheIfNeeded(working);
    int w = m_cachedW; int h = m_cachedH;
    QImage result = srcImage.convertToFormat(QImage::Format_ARGB32);

    QImage edgeMask;
    if (mode == OperationMode::StrokeCanny) edgeMask = buildCannyMask(edgeThreshMin, edgeThreshMax);
    else edgeMask = buildLaplacianMask(edgeThreshMin, edgeThreshMax);

    if (enableDouglasPeucker) {
        auto components = extractConnectedComponents(edgeMask);
        QImage canvas = srcImage.convertToFormat(QImage::Format_ARGB32);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (const auto &comp : components) {
            QVector<QPoint> simplified; douglasPeuckerSimplify(comp, dpTolerance, simplified);
            if (simplified.size()>=2) {
                QColor edgeColor;
                if (useMetal) edgeColor = metalColor.isValid() ? metalColor : QColor(218,165,32);
                else edgeColor = determineEdgeColor(srcImage, comp.first().x(), comp.first().y(), autoInvertRange);
                QPen pen(edgeColor);
                pen.setWidth(qMax(1, dpLineWidth));
                painter.setPen(pen);
                QVector<QPointF> fp; fp.reserve(simplified.size()); for (auto &p: simplified) fp.append(QPointF(p));
                painter.drawPolyline(fp.constData(), fp.size());
            }
        }
        painter.end();
        return canvas;
    } else {
        // per-pixel coloring
        for (int y=1;y<h-1;++y) for (int x=1;x<w-1;++x) {
            if (!edgeMask.constScanLine(y)[x]) continue;
            QColor edgeColor;
            if (useMetal) {
                edgeColor = metalColor.isValid() ? metalColor : QColor(218,165,32);
            } else {
                if (autoInvertRange == -1) edgeColor = QColor(0,0,0);
                else if (autoInvertRange == 0) edgeColor = QColor(255,255,255);
                else {
                    int r = autoInvertRange; int avg = averageGrayInBox(x-r,y-r,x+r,y+r);
                    edgeColor = (avg>128)?QColor(0,0,0):QColor(255,255,255);
                }
            }
            result.setPixel(x,y, edgeColor.rgb());
        }
        return result;
    }
}

QImage EdgeSharpener::buildEdgeMaskForImage(const QImage& srcImage, OperationMode mode, int edgeThreshMin, int edgeThreshMax, bool enablePreFilter, int gaussianKernelSize, double gaussianSigma) {
    if (srcImage.isNull()) return QImage();
    QImage working = enablePreFilter ? applyGaussianBlur(srcImage, gaussianKernelSize, gaussianSigma) : srcImage;
    buildCacheIfNeeded(working);
    if (mode == OperationMode::StrokeCanny) return buildCannyMask(edgeThreshMin, edgeThreshMax);
    return buildLaplacianMask(edgeThreshMin, edgeThreshMax);
}
