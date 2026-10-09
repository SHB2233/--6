TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = tcp_basic_client
SOURCES += tcp_basic_client.cpp
HEADERS += tcp_basic_common.h
win32:LIBS += -lws2_32