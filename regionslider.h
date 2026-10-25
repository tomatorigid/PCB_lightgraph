#ifndef REGIONSLIDER_H
#define REGIONSLIDER_H
#include <QSlider>
class RegionSlider : public QSlider {
public:
    explicit RegionSlider(QWidget* parent = nullptr) : QSlider(Qt::Horizontal, parent) {}
    void setBaseline(int value) { m_baseline = value; update(); }
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    int m_baseline = 0;
};
#endif
