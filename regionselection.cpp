#include "regionselection.h"
#include <QElapsedTimer>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace Regions {
SelectionJob::SelectionJob(const Snapshot& before, const QSize& size, Operation op, QObject* parent)
    : QObject(parent), m_before(before), m_size(size), m_operation(op) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() { step(); });
}
void SelectionJob::startGeometry(const QPainterPath& path) {
    m_mask = QImage(m_size, QImage::Format_Mono);
    if (m_mask.isNull()) { if (failed) failed(QStringLiteral("无法分配选区内存。")); deleteLater(); return; }
    m_mask.setColorTable({qRgb(0, 0, 0), qRgb(255, 255, 255)});
    m_mask.fill(0);
    QPainter painter(&m_mask);
    painter.fillPath(path, Qt::white);
    painter.end();
    m_timer.start(0);
}
void SelectionJob::startWand(const QImage& image, const QPoint& seed, int tolerance, bool global) {
    m_wand = true; m_global = global; m_image = image.convertToFormat(QImage::Format_ARGB32);
    m_seed = m_image.pixel(seed); m_limit = 3.0 * 255 * 255 * std::pow(qBound(0, tolerance, 100) / 100.0, 2);
    m_flags = QByteArray(m_size.width() * m_size.height(), 0);
    m_stage = Search;
    if (!global) enqueue(seed.y() * m_size.width() + seed.x());
    m_timer.start(0);
}
void SelectionJob::cancel() { m_timer.stop(); completed = nullptr; failed = nullptr; deleteLater(); }
bool SelectionJob::matches(int pixel) const {
    const QRgb color = reinterpret_cast<const QRgb*>(m_image.constScanLine(pixel / m_size.width()))[pixel % m_size.width()];
    if (!qAlpha(m_seed)) return !qAlpha(color);
    if (!qAlpha(color)) return false;
    const int r = qRed(color) - qRed(m_seed), g = qGreen(color) - qGreen(m_seed), b = qBlue(color) - qBlue(m_seed);
    return double(r * r + g * g + b * b) <= m_limit;
}
void SelectionJob::enqueue(int pixel) {
    if (m_flags.at(pixel)) return;
    if (matches(pixel)) { m_flags[pixel] = 1; m_pending.push_back(pixel); }
    else m_flags[pixel] = 2;
}
void SelectionJob::step() {
    QElapsedTimer clock; clock.start();
    const int width = m_size.width(), count = width * m_size.height();
    int work = 0;
    // 每批最多占用约 8ms，扫描和归属合并都允许界面取消。
    while (clock.elapsed() < 8) {
        if (m_stage == Search) {
            if (m_global) {
                if (m_cursor == count) { m_stage = Scan; m_cursor = 0; continue; }
                m_flags[m_cursor] = matches(m_cursor) ? 1 : 2; ++m_cursor;
            } else {
                if (m_pending.empty()) { m_stage = Scan; m_cursor = 0; continue; }
                const int p = m_pending.front(); m_pending.pop_front();
                const int x = p % width, y = p / width;
                if (x) enqueue(p - 1);
                if (x + 1 < width) enqueue(p + 1);
                if (y) enqueue(p - width);
                if (y + 1 < m_size.height()) enqueue(p + width);
            }
        } else if (m_stage == Scan) {
            if (m_cursor == count) { m_stage = Prepare; m_mask = QImage(); m_flags.clear(); m_image = QImage(); continue; }
            const int x = m_cursor % width, y = m_cursor / width;
            const bool active = m_wand ? m_flags.at(m_cursor) == 1 : m_mask.pixelIndex(x, y) != 0;
            if (active && m_run < 0) m_run = x;
            if ((!active || x == width - 1) && m_run >= 0) {
                m_candidate[y].append(Span(y, m_run, active ? x + 1 : x)); m_run = -1;
            }
            ++m_cursor;
        } else if (m_stage == Prepare) {
            if (m_region == m_before.items.size()) { m_stage = Merge; continue; }
            const Region& region = m_before.items.at(m_region);
            if (m_span == region.spans.size()) { ++m_region; m_span = 0; continue; }
            const Span span = region.spans.at(m_span++);
            if (m_operation != Operation::New && region.id == m_before.selected) m_target[span.y].append(span);
            else m_occupied[span.y].append(span);
        } else {
            if (m_row == m_size.height()) {
                m_timer.stop();
                if (completed) completed(m_result);
                deleteLater(); return;
            }
            QVector<Span> other = m_occupied.take(m_row);
            std::sort(other.begin(), other.end(), [](const Span& a, const Span& b) { return a.begin < b.begin; });
            const QVector<Span> candidate = m_candidate.take(m_row), target = m_target.take(m_row);
            const QVector<Span> row = m_operation == Operation::Subtract ? subtract(target, candidate) :
                (m_operation == Operation::Add ? unite(target, subtract(candidate, other)) : subtract(candidate, other));
            m_result += row; ++m_row;
        }
        // 高频像素循环按小批次读时钟，避免逐像素计时开销。
        if (++work < 256 && (m_stage == Search || m_stage == Scan)) continue;
        work = 0;
    }
    m_timer.start(0);
}
}
