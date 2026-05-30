#include "gaussian_blur.h"
#include <QVector>
#include <cmath>

static int clampOddKernelSize(int kernelSize) {
    if (kernelSize < 3) kernelSize = 3;
    if (kernelSize > 7) kernelSize = 7;
    if ((kernelSize % 2) == 0) --kernelSize;
    if (kernelSize < 3) kernelSize = 3;
    return kernelSize;
}

static QVector<double> buildGaussian1DKernel(int kernelSize, double sigma) {
    kernelSize = clampOddKernelSize(kernelSize);
    int radius = kernelSize / 2;
    QVector<double> w(kernelSize);
    const double twoSigma2 = 2.0 * sigma * sigma;
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        double v = std::exp(-(i * i) / twoSigma2);
        w[i + radius] = v;
        sum += v;
    }
    for (int i = 0; i < kernelSize; ++i) w[i] /= sum;
    return w;
}

QImage applyGaussianBlur(const QImage& srcImage, int kernelSize, double sigma) {
    kernelSize = clampOddKernelSize(kernelSize);
    if (srcImage.isNull() || kernelSize <= 1) return srcImage;

    const QVector<double> weights = buildGaussian1DKernel(kernelSize, sigma);
    const int radius = kernelSize / 2;

    QImage src = srcImage.convertToFormat(QImage::Format_ARGB32);
    QImage temp(src.size(), QImage::Format_ARGB32);
    QImage dst(src.size(), QImage::Format_ARGB32);

    const int w = src.width();
    const int h = src.height();

    auto clampIndex = [](int v, int lo, int hi) { return qBound(lo, v, hi); };

    for (int y = 0; y < h; ++y) {
        QRgb* out = reinterpret_cast<QRgb*>(temp.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double a = 0, r = 0, g = 0, b = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = clampIndex(x + k, 0, w - 1);
                const QRgb px = src.pixel(sx, y);
                const double wt = weights[k + radius];
                a += qAlpha(px) * wt;
                r += qRed(px) * wt;
                g += qGreen(px) * wt;
                b += qBlue(px) * wt;
            }
            out[x] = qRgba(static_cast<int>(r + 0.5), static_cast<int>(g + 0.5), static_cast<int>(b + 0.5), static_cast<int>(a + 0.5));
        }
    }

    for (int y = 0; y < h; ++y) {
        QRgb* out = reinterpret_cast<QRgb*>(dst.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double a = 0, r = 0, g = 0, b = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = clampIndex(y + k, 0, h - 1);
                const QRgb px = temp.pixel(x, sy);
                const double wt = weights[k + radius];
                a += qAlpha(px) * wt;
                r += qRed(px) * wt;
                g += qGreen(px) * wt;
                b += qBlue(px) * wt;
            }
            out[x] = qRgba(static_cast<int>(r + 0.5), static_cast<int>(g + 0.5), static_cast<int>(b + 0.5), static_cast<int>(a + 0.5));
        }
    }

    return dst;
}

