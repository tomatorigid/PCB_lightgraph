#ifndef REGIONSELECTION_H
#define REGIONSELECTION_H
#include "regionmodel.h"
#include <QObject>
#include <QTimer>
#include <QPainterPath>
#include <deque>
#include <functional>

namespace Regions {
class SelectionJob : public QObject {
public:
    SelectionJob(const Snapshot& before, const QSize& size, Operation operation, QObject* parent = nullptr);
    void startGeometry(const QPainterPath& path);
    void startWand(const QImage& image, const QPoint& seed, int tolerance, bool global);
    void cancel();
    std::function<void(const QVector<Span>&)> completed;
    std::function<void(const QString&)> failed;
private:
    enum Stage { Search, Scan, Prepare, Merge };
    void step();
    bool matches(int pixel) const;
    void enqueue(int pixel);
    QTimer m_timer;
    Snapshot m_before;
    QSize m_size;
    Operation m_operation;
    Stage m_stage = Scan;
    bool m_global = false, m_wand = false;
    QImage m_image, m_mask;
    QRgb m_seed = 0;
    double m_limit = 0;
    QByteArray m_flags;
    std::deque<int> m_pending;
    int m_cursor = 0, m_run = -1, m_region = 0, m_span = 0, m_row = 0;
    QMap<int, QVector<Span>> m_candidate, m_target, m_occupied;
    QVector<Span> m_result;
};
}
#endif
