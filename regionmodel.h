#ifndef REGIONMODEL_H
#define REGIONMODEL_H

#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QSize>
#include <QString>
#include <QVector>
#include <array>

namespace Regions {
enum Parameter { Gold, Silk, Trans, Copper, BareMin, BareMax, BareSimilarity, EdgeMin, EdgeMax, ParameterCount };
enum class Tool { Select, Rectangle, Lasso, Brush, Wand };
enum class Operation { New, Add, Subtract };
struct Parameters {
    std::array<int, ParameterCount> values;
    Parameters() : values{{45, 180, 120, 150, 20, 65, 80, 50, 200}} {}
    int& operator[](int key) { return values.at(key); }
    int operator[](int key) const { return values.at(key); }
    bool operator==(const Parameters& other) const { return values == other.values; }
};
struct Span {
    int y = 0, begin = 0, end = 0;
    Span() = default;
    Span(int row, int first, int last) : y(row), begin(first), end(last) {}
    bool operator==(const Span& other) const { return y == other.y && begin == other.begin && end == other.end; }
};
struct Region {
    quint32 id = 0;
    QString name;
    QVector<Span> spans;
    Parameters offsets;
    Region() { offsets.values.fill(0); }
    bool operator==(const Region& other) const {
        return id == other.id && name == other.name && spans == other.spans && offsets == other.offsets;
    }
};
struct Snapshot {
    QVector<Region> items;
    quint32 selected = 0;
    Parameters globals;
    bool operator==(const Snapshot& other) const {
        return items == other.items && selected == other.selected && globals == other.globals;
    }
};
struct RenderContext {
    QVector<quint32> owners;
    QMap<quint32, Parameters> parameters;
    quint64 revision = 0;
};
int maximum(int parameter);
QString key(int parameter);
QString title(int parameter);
Parameters effective(const Parameters& globals, const Region& region);
QVector<Span> unite(const QVector<Span>& a, const QVector<Span>& b);
QVector<Span> subtract(const QVector<Span>& a, const QVector<Span>& b);

class Model {
public:
    QSize size;
    QVector<Region> items;
    quint32 selected = 0;
    quint64 revision = 1;
    quint64 maskRevision = 1;
    const Region* current() const;
    Region* current();
    quint32 nextId() const;
    Snapshot snapshot(const Parameters& globals) const;
    bool record(const Snapshot& before, const Parameters& globals, bool geometry = false);
    bool restoreHistory(bool redo, Parameters& globals);
    void clear(const QSize& newSize);
    void clearHistory();
    int undoCount() const { return m_undo.size(); }
    int redoCount() const { return m_redo.size(); }
    const QVector<quint32>& owners(const QSize& target);
    void invalidateMasks();
    bool hasOffsets() const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonValue& value, const QSize& size, QVector<Region>& result, QString* error = nullptr);

private:
    QVector<Snapshot> m_undo, m_redo;
    QVector<quint32> m_originalOwners, m_previewOwners;
    QSize m_previewSize;
};
}
#endif
