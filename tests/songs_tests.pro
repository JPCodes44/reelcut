QT += core network testlib
CONFIG += c++17 testcase
TARGET = songs_tests
TEMPLATE = app
INCLUDEPATH += ../src
HEADERS += ../src/songrequest.h
SOURCES += songs_tests.cpp ../src/songrequest.cpp
