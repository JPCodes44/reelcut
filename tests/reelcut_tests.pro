QT += core gui quick quickcontrols2 multimedia network concurrent testlib dbus
CONFIG += c++17 testcase
TARGET = reelcut_tests
TEMPLATE = app

INCLUDEPATH += ../src
HEADERS += ../src/waveformprovider.h
SOURCES += ../src/waveformprovider.cpp

HEADERS += ../src/theme.h ../src/translator.h ../src/project.h ../src/silence.h ../src/captions.h \
    ../src/transcript.h ../src/exportgraph.h ../src/bodyeffects.h ../src/effects.h ../src/segment.h ../src/fisheyetext.h ../src/colorgrade.h ../src/posting.h ../src/instagramapi.h ../src/postscheduler.h ../src/filters.h ../src/transitions.h ../src/media.h \
    ../src/backend.h ../src/mcpserver.h ../src/jobs.h ../src/filepicker.h ../src/thumbprovider.h
SOURCES += reelcut_tests.cpp ../src/theme.cpp ../src/translator.cpp ../src/project.cpp ../src/silence.cpp \
    ../src/captions.cpp ../src/transcript.cpp ../src/exportgraph.cpp ../src/bodyeffects.cpp ../src/effects.cpp ../src/segment.cpp ../src/fisheyetext.cpp ../src/colorgrade.cpp ../src/posting.cpp ../src/instagramapi.cpp ../src/postscheduler.cpp ../src/filters.cpp ../src/transitions.cpp ../src/media.cpp \
    ../src/backend.cpp ../src/mcpserver.cpp ../src/jobs.cpp ../src/thumbprovider.cpp

INCLUDEPATH += /usr/include/opencv5
LIBS += -lopencv_core -lopencv_dnn

RESOURCES += ../src/resources.qrc
