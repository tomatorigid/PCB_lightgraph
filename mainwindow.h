#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QMap>
#include <QVector>
#include <QPoint>
#include <QComboBox>
#include <QPainter>

struct LEDStrip {
    QPoint start;
    QPoint end;
    int radius;
    QColor color; // 存储检测到的智能颜色
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    bool eventFilter(QObject *obj, QEvent *event) override;
protected:
    void resizeEvent(QResizeEvent *event) override;
private slots:
    void loadAndProcess();
    void updateProcess();
    void exportLayers();
    void autoSuggestLEDs();

private:
    void setupUI();
    QSlider* createSlider(QString title, int min, int max, int def, class QVBoxLayout* layout);
    float distanceToSegment(QPoint p, QPoint v, QPoint w);

    QImage m_origin;
    QMap<QString, QImage> m_layers;
    QVector<LEDStrip> m_ledStrips;

    QPoint m_pendingStart;
    bool m_isPlacing = false;
    QSlider *s_autoSense;
    void updatePreviewOnly(); // 新增：仅刷新预览，不重算布灯

    // UI 组件
    QLabel *l_copper, *l_mask, *l_silk, *l_bottom, *l_composite, *l_ledLayer;
    QSlider *s_silk, *s_gold, *s_trans, *s_ledRad;
    QComboBox *combo_maskColor; // 阻焊颜色选择
    QPushButton *btn_export;
    QComboBox *combo_surfaceFinish; // 表面处理：沉金 / 喷锡
    //QSlider *s_autoSense;

    // 物理参数
    QColor getSolderMaskColor();
    QColor getSilkColor();
};

#endif
