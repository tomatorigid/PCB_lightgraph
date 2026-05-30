#include "dp_simplify.h"
#include <QVector>
#include <QPoint>
#include <QImage>
#include <functional>

QVector<QVector<QPoint>> extractConnectedComponents(const QImage& mask) {
    QVector<QVector<QPoint>> components;
    if (mask.isNull()) return components;
    int w = mask.width();
    int h = mask.height();
    QVector<char> visited(w * h, 0);
    auto idx = [w](int x, int y){ return y * w + x; };
    const int dx[8] = {1,1,0,-1,-1,-1,0,1};
    const int dy[8] = {0,1,1,1,0,-1,-1,-1};

    for (int y = 1; y < h-1; ++y) {
        const uchar* line = mask.constScanLine(y);
        for (int x = 1; x < w-1; ++x) {
            if (!line[x]) continue;
            int id = idx(x,y);
            if (visited[id]) continue;
            QVector<QPoint> stack;
            QVector<QPoint> comp;
            stack.append(QPoint(x,y)); visited[id] = 1;
            while (!stack.isEmpty()) {
                QPoint p = stack.takeLast();
                comp.append(p);
                for (int k = 0; k < 8; ++k) {
                    int nx = p.x() + dx[k];
                    int ny = p.y() + dy[k];
                    if (nx <= 0 || nx >= w-1 || ny <= 0 || ny >= h-1) continue;
                    int nid = idx(nx,ny);
                    if (!visited[nid] && mask.constScanLine(ny)[nx]) {
                        visited[nid] = 1;
                        stack.append(QPoint(nx,ny));
                    }
                }
            }
            if (comp.size() >= 2) components.append(comp);
        }
    }
    return components;
}

static double perpendicularDistance(const QPoint& a, const QPoint& b, const QPoint& p) {
    double dx = b.x() - a.x();
    double dy = b.y() - a.y();
    double num = std::abs(dy * p.x() - dx * p.y() + b.x()*a.y() - b.y()*a.x());
    double den = std::sqrt(dx*dx + dy*dy);
    return den == 0.0 ? 0.0 : num / den;
}

void douglasPeuckerSimplify(const QVector<QPoint>& pts, double eps, QVector<QPoint>& out) {
    if (pts.size() < 3) { out = pts; return; }
    int n = pts.size();
    int idxMax = 0; double distMax = 0.0;
    for (int i = 1; i < n-1; ++i) {
        double d = perpendicularDistance(pts.first(), pts.last(), pts[i]);
        if (d > distMax) { distMax = d; idxMax = i; }
    }
    if (distMax > eps) {
        QVector<QPoint> left = pts.mid(0, idxMax+1);
        QVector<QPoint> right = pts.mid(idxMax, pts.size()-idxMax);
        QVector<QPoint> outL, outR;
        douglasPeuckerSimplify(left, eps, outL);
        douglasPeuckerSimplify(right, eps, outR);
        out = outL;
        for (int i = 1; i < outR.size(); ++i) out.append(outR[i]);
    } else {
        out.clear();
        out.append(pts.first());
        out.append(pts.last());
    }
}

