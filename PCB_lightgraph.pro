#-------------------------------------------------
#
# Project created by QtCreator 2026-02-10T12:11:55
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = PCB_lightgraph
TEMPLATE = app
# 渐进渲染调度器使用 lambda 和成员默认值，统一启用 C++11。
# Qt 6 要求 C++17；Qt 5 维持 C++11 即可，按主版本自动切换。
greaterThan(QT_MAJOR_VERSION, 5): CONFIG += c++17
else: CONFIG += c++11

# FluentUI3Style（WinUI3/Fluent 风格）：仅 Qt 6 下启用。
# 该库需要 C++17（Qt 5.9 的 MinGW 5.3 工具链不支持），且其代码按 Qt5/Qt6 双兼容编写。
greaterThan(QT_MAJOR_VERSION, 5) {
    QT += svg
    INCLUDEPATH += $$PWD/third_party/FluentUI3Style/fluentui3style
    include($$PWD/third_party/FluentUI3Style/fluentui3style/fluentui3style.pri)
    # 内嵌样式库在 Qt 6.9+ 漏引入外观声明，由工程补齐而不改第三方源码。
    greaterThan(QT_MINOR_VERSION, 8) {
        SOURCES -= $$PWD/third_party/FluentUI3Style/fluentui3style/fluentui3style.cpp
        SOURCES += $$PWD/fluentstylecompat.cpp
    }
}

# MSVC：源码含中文/UTF-8 注释与字符串，必须按 UTF-8 解析，否则乱码或报错
win32-msvc {
    QMAKE_CXXFLAGS += /utf-8
}

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0


SOURCES += \
        main.cpp \
        mainwindow.cpp \
        imageprocessor.cpp \
        edgesharpener.cpp \
        gaussian_blur.cpp \
        dp_simplify.cpp \
        ledlayoutengine.cpp \
        layergenerator.cpp \
        progressiverendercontroller.cpp \
        progressiverenderutils.cpp \
        regionmodel.cpp \
        regionselection.cpp \
        regionslider.cpp \
        mainwindowregions.cpp

HEADERS += \
        mainwindow.h \
        imageprocessor.h \
        edgesharpener.h \
        gaussian_blur.h \
        dp_simplify.h \
        ledlayoutengine.h \
        layergenerator.h \
        ledstrip.h \
        progressiverendercontroller.h \
        progressiverenderutils.h \
        regionmodel.h \
        regionselection.h \
        regionslider.h

FORMS += \
        mainwindow.ui

RESOURCES += \
        icons.qrc

RC_FILE += \
        app.rc
