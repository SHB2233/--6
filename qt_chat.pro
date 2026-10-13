QT += core gui widgets network
CONFIG += c++17 no_moc_predefs
TEMPLATE = app
TARGET = QtTcpChat
SOURCES += main.cpp mainwindow.cpp chatserver.cpp directcall.cpp
HEADERS += mainwindow.h chatserver.h directcall.h wire.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
