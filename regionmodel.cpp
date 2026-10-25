#include "regionmodel.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Regions {
int maximum(int p) { return p == Gold ? 359 : (p >= BareMin && p <= BareSimilarity ? 100 : 255); }
QString key(int p) {
    static const char* keys[] = {"goldThresh", "silkThresh", "transThresh", "copperDepth", "bareGrayMin", "bareGrayMax", "bareSim", "edgeMin", "edgeMax"};
    return QString::fromLatin1(keys[p]);
}
QString title(int p) {
    static const char* names[] = {"金色/银色判定", "丝印阈值", "基材透光阈值", "敷铜层较深阈值", "灰度下限A (%)", "灰度上限B (%)", "颜色相似度C (%)", "强边缘阈值/边缘下限阈值", "弱边缘阈值/边缘上限阈值"};
    return QString::fromUtf8(names[p]);
}
Parameters effective(const Parameters& globals, const Region& r) {
    Parameters result;
    for (int p = 0; p < ParameterCount; ++p) result[p] = qBound(0, globals[p] + r.offsets[p], maximum(p));
    return result;
}
static bool spanLess(const Span& a, const Span& b) { return a.y != b.y ? a.y < b.y : a.begin < b.begin; }
QVector<Span> unite(const QVector<Span>& a, const QVector<Span>& b) {
    QVector<Span> ordered;
    ordered.reserve(a.size() + b.size());
    std::merge(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(ordered), spanLess);
    QVector<Span> result;
    for (const Span& span : ordered) {
        if (!result.isEmpty() && result.last().y == span.y && result.last().end >= span.begin)
            result.last().end = qMax(result.last().end, span.end);
        else result.append(span);
    }
    return result;
}
QVector<Span> subtract(const QVector<Span>& a, const QVector<Span>& b) {
    QVector<Span> result;
    int j = 0;
    for (const Span& s : a) {
        int x = s.begin;
        while (j < b.size() && (b[j].y < s.y || (b[j].y == s.y && b[j].end <= x))) ++j;
        for (int k = j; k < b.size() && b[k].y == s.y && b[k].begin < s.end; ++k) {
            if (b[k].begin > x) result.append(Span(s.y, x, qMin(b[k].begin, s.end)));
            x = qMax(x, b[k].end);
            if (x >= s.end) break;
        }
        if (x < s.end) result.append(Span(s.y, x, s.end));
    }
    return result;
}
const Region* Model::current() const {
    for (const Region& r : items) if (r.id == selected) return &r;
    return nullptr;
}
Region* Model::current() {
    // 可写访问必须触发 QVector 分离，不能通过 const_cast 改坏历史快照。
    for (Region& r : items) if (r.id == selected) return &r;
    return nullptr;
}
quint32 Model::nextId() const {
    QSet<quint32> used;
    for (const Region& r : items) used.insert(r.id);
    for (quint32 id = 1; id != 0; ++id) if (!used.contains(id)) return id;
    return 0;
}
Snapshot Model::snapshot(const Parameters& globals) const { Snapshot s; s.items = items; s.selected = selected; s.globals = globals; return s; }
bool Model::record(const Snapshot& before, const Parameters& globals, bool geometry) {
    if (before == snapshot(globals)) return false;
    m_undo.append(before);
    if (m_undo.size() > 50) m_undo.removeFirst();
    m_redo.clear();
    ++revision;
    if (geometry) invalidateMasks();
    return true;
}
bool Model::restoreHistory(bool redo, Parameters& globals) {
    QVector<Snapshot>& source = redo ? m_redo : m_undo;
    QVector<Snapshot>& destination = redo ? m_undo : m_redo;
    if (source.isEmpty()) return false;
    destination.append(snapshot(globals));
    const Snapshot old = source.takeLast();
    items = old.items; selected = old.selected; globals = old.globals;
    ++revision; invalidateMasks();
    return true;
}
void Model::clearHistory() { m_undo.clear(); m_redo.clear(); }
void Model::clear(const QSize& newSize) { size = newSize; items.clear(); selected = 0; clearHistory(); ++revision; invalidateMasks(); }
void Model::invalidateMasks() { ++maskRevision; m_originalOwners.clear(); m_previewOwners.clear(); m_previewSize = QSize(); }
const QVector<quint32>& Model::owners(const QSize& target) {
    if (target == size && !m_originalOwners.isEmpty()) return m_originalOwners;
    if (target == m_previewSize && !m_previewOwners.isEmpty()) return m_previewOwners;
    if (m_originalOwners.isEmpty() && size.isValid() && !size.isEmpty()) {
        m_originalOwners.fill(0, size.width() * size.height());
        for (const Region& r : items) for (const Span& s : r.spans)
            std::fill(m_originalOwners.begin() + s.y * size.width() + s.begin, m_originalOwners.begin() + s.y * size.width() + s.end, r.id);
    }
    if (target == size) { m_previewOwners.clear(); m_previewSize = QSize(); return m_originalOwners; }
    m_previewSize = target;
    m_previewOwners.fill(0, target.width() * target.height());
    if (size.isEmpty() || target.isEmpty()) return m_previewOwners;
    for (int y = 0; y < target.height(); ++y) {
        const int sy = qMin(size.height() - 1, int((y + 0.5) * size.height() / target.height()));
        for (int x = 0; x < target.width(); ++x) {
            const int sx = qMin(size.width() - 1, int((x + 0.5) * size.width() / target.width()));
            m_previewOwners[y * target.width() + x] = m_originalOwners[sy * size.width() + sx];
        }
    }
    return m_previewOwners;
}
bool Model::hasOffsets() const {
    for (const Region& r : items) for (int p : r.offsets.values) if (p != 0) return true;
    return false;
}
QJsonObject Model::toJson() const {
    QJsonArray array;
    for (const Region& r : items) {
        QJsonArray spans; QJsonObject offsets;
        for (const Span& s : r.spans) spans.append(QJsonArray{s.y, s.begin, s.end});
        for (int p = 0; p < ParameterCount; ++p) if (r.offsets[p]) offsets[key(p)] = r.offsets[p];
        QJsonObject item; item["id"] = double(r.id); item["name"] = r.name; item["spans"] = spans; item["offsets"] = offsets;
        array.append(item);
    }
    QJsonObject root; root["schemaVersion"] = 1; root["width"] = size.width(); root["height"] = size.height(); root["items"] = array;
    return root;
}
static bool integer(const QJsonValue& value, double lo, double hi) {
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    return value.isDouble() && std::isfinite(number) && number >= lo && number <= hi && std::floor(number) == number;
}
bool Model::fromJson(const QJsonValue& value, const QSize& size, QVector<Region>& result, QString* error) {
    auto fail = [error]() { if (error) *error = QStringLiteral("工程区域数据损坏或与图片尺寸不符。"); return false; };
    QVector<Region> parsed;
    if (value.isUndefined()) { result.clear(); return true; }
    if (!value.isObject() || size.isEmpty() || qint64(size.width()) * size.height() >= 16000000) return fail();
    const QJsonObject root = value.toObject();
    if (!integer(root["schemaVersion"], 1, 1) || !integer(root["width"], size.width(), size.width()) ||
        !integer(root["height"], size.height(), size.height()) || !root["items"].isArray()) return fail();
    QSet<quint32> ids;
    QMap<int, QVector<Span>> rows;
    for (const QJsonValue& v : root["items"].toArray()) {
        if (!v.isObject()) return fail();
        const QJsonObject item = v.toObject();
        if (!integer(item["id"], 1, 4294967295.0) || !item["name"].isString() || item["name"].toString().trimmed().isEmpty() ||
            item["name"].toString().size() > 100 || !item["spans"].isArray() || item["spans"].toArray().isEmpty() || !item["offsets"].isObject()) return fail();
        Region r; r.id = quint32(item["id"].toDouble()); r.name = item["name"].toString();
        if (ids.contains(r.id)) return fail();
        ids.insert(r.id);
        const QJsonObject offsets = item["offsets"].toObject();
        for (auto it = offsets.begin(); it != offsets.end(); ++it) {
            int p = 0; while (p < ParameterCount && key(p) != it.key()) ++p;
            if (p == ParameterCount || !integer(it.value(), -maximum(p), maximum(p))) return fail();
            r.offsets[p] = it.value().toInt();
        }
        Span previous(-1, 0, 0);
        for (const QJsonValue& spanValue : item["spans"].toArray()) {
            if (!spanValue.isArray()) return fail();
            const QJsonArray a = spanValue.toArray();
            if (a.size() != 3 || !integer(a[0], 0, size.height() - 1) || !integer(a[1], 0, size.width() - 1) || !integer(a[2], 1, size.width())) return fail();
            const Span s(a[0].toInt(), a[1].toInt(), a[2].toInt());
            if (s.begin >= s.end || s.y < previous.y || (s.y == previous.y && s.begin < previous.end)) return fail();
            r.spans.append(s); rows[s.y].append(s); previous = s;
        }
        parsed.append(r);
    }
    for (auto it = rows.begin(); it != rows.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(), spanLess);
        int end = 0;
        for (const Span& s : it.value()) { if (s.begin < end) return fail(); end = s.end; }
    }
    result = parsed;
    return true;
}
}
