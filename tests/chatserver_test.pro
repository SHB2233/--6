QT += network testlib
QT -= gui
CONFIG += c++17 testcase console no_moc_predefs
TEMPLATE = app
TARGET = chatserver_test
SOURCES += chatserver_test.cpp ../chatserver.cpp
HEADERS += ../chatserver.h ../wire.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
