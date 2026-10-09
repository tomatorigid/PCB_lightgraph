QT += core gui widgets testlib svg
CONFIG += console testcase c++17
TEMPLATE = app
TARGET = regions_tests
INCLUDEPATH += .. $$OUT_PWD
SOURCES += tst_regions.cpp \
    ../mainwindow.cpp ../mainwindowregions.cpp \
    ../regionmodel.cpp ../regionselection.cpp ../regionslider.cpp \
    ../imageprocessor.cpp ../edgesharpener.cpp ../gaussian_blur.cpp ../dp_simplify.cpp \
    ../ledlayoutengine.cpp ../layergenerator.cpp \
    ../progressiverendercontroller.cpp ../progressiverenderutils.cpp \
    $$OUT_PWD/baseline_imageprocessor.cpp
HEADERS += ../mainwindow.h ../regionmodel.h ../regionselection.h ../regionslider.h \
    ../imageprocessor.h ../edgesharpener.h ../ledlayoutengine.h ../layergenerator.h \
    ../progressiverendercontroller.h ../progressiverenderutils.h
RESOURCES += ../icons.qrc
greaterThan(QT_MAJOR_VERSION, 5) {
    INCLUDEPATH += $$PWD/../third_party/FluentUI3Style/fluentui3style
    include($$PWD/../third_party/FluentUI3Style/fluentui3style/fluentui3style.pri)
    greaterThan(QT_MINOR_VERSION, 8) {
        SOURCES -= $$absolute_path(../third_party/FluentUI3Style/fluentui3style/fluentui3style.cpp, $$PWD)
        SOURCES += ../fluentstylecompat.cpp
    }
}
