TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tcp_basic_server
SOURCES += tcp_basic_server.cpp
HEADERS += tcp_basic_common.h
win32:LIBS += -lws2_32