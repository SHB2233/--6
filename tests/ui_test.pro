QT += widgets network testlib
CONFIG += c++17 testcase console no_moc_predefs
TEMPLATE = app
TARGET = ui_test
SOURCES += ui_test.cpp ../mainwindow.cpp ../chatserver.cpp ../directcall.cpp
HEADERS += ../mainwindow.h ../chatserver.h ../directcall.h ../wire.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
