#include "mainwindow.h"
#include <QApplication>
#include <QFont>
#include <QSettings>
#include <QScreen>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include "fluentuiappearance.h"
#endif

int main(int argc, char *argv[])
{
    // QSettings 需要组织/应用名（界面缩放等偏好持久化）
    QCoreApplication::setOrganizationName(QStringLiteral("PCB_lightgraph"));
    QCoreApplication::setApplicationName(QStringLiteral("PCB_lightgraph"));

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // 高分屏缩放进位策略：保证 Fluent 风格在缩放屏上渲染锐利
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    QApplication a(argc, argv);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // FluentUI3 风格（WinUI3 观感）：initialize 依据系统深浅色初始化，
    // setTheme 会应用调色板、安装 FluentUI3Style 并让 Windows 标题栏跟随深色。
    // 本程序为深色界面工具，强制深色主题以获得统一视觉。
    fluentUIAppearance.initialize();
    fluentUIAppearance.setTheme(Theme::Dark);

    // 中文界面字体：微软雅黑 + 13px，Fluent 观感更细腻；
    // 叠加用户设置的界面缩放比例（默认 100%）。
    {
        QSettings s;
        const double scale = s.value(QStringLiteral("ui/scale"), 1.0).toDouble();
        QFont f;
        f.setFamily(QStringLiteral("Microsoft YaHei"));
        f.setPixelSize(qMax(8, qRound(13.0 * scale)));
        f.setHintingPreference(QFont::PreferNoHinting);
        a.setFont(f);
    }
#endif

    MainWindow w;
    w.show();

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // 深色标题栏（DWM）：窗口显示后再绑定主窗口
    fluentUIAppearance.setMainWindow(&w);

    // 窗口尺寸不超过屏幕可用区域，防止小屏幕（如 1366x768）显示不全
    const QRect avail = QApplication::primaryScreen()->availableGeometry();
    if (w.width() > avail.width() || w.height() > avail.height())
        w.resize(qMin(w.width(), avail.width()), qMin(w.height(), avail.height()));
#endif

    return a.exec();
}
