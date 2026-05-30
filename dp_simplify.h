#ifndef DP_SIMPLIFY_H
#define DP_SIMPLIFY_H

#include <QImage>
#include <QVector>
#include <QPoint>

// Extract connected components (8-neighbor) from an 8-bit grayscale mask image.
QVector<QVector<QPoint>> extractConnectedComponents(const QImage& mask);

// Simplify a polyline using Douglas-Peucker algorithm
void douglasPeuckerSimplify(const QVector<QPoint>& pts, double eps, QVector<QPoint>& out);

#endif // DP_SIMPLIFY_H

