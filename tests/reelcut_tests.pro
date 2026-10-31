QT += core gui quick quickcontrols2 multimedia network concurrent testlib dbus
CONFIG += c++17 testcase
TARGET = reelcut_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/theme.h ../src/translator.h ../src/project.h ../src/silence.h ../src/captions.h \
    ../src/transcript.h ../src/exportgraph.h ../src/media.h \
    ../src/backend.h ../src/jobs.h ../src/filepicker.h ../src/thumbprovider.h
SOURCES += reelcut_tests.cpp ../src/theme.cpp ../src/translator.cpp ../src/project.cpp ../src/silence.cpp \
    ../src/captions.cpp ../src/transcript.cpp ../src/exportgraph.cpp ../src/media.cpp \
    ../src/backend.cpp ../src/jobs.cpp ../src/thumbprovider.cpp
