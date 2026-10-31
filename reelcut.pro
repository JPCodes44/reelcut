QT += core gui qml quick quickcontrols2 multimedia network concurrent dbus

CONFIG += c++17 release
TARGET = reelcut
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/captions.h \
    src/exportgraph.h \
    src/filepicker.h \
    src/jobs.h \
    src/mcpserver.h \
    src/media.h \
    src/portalfilepicker.h \
    src/project.h \
    src/silence.h \
    src/theme.h \
    src/thumbprovider.h \
    src/transcript.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/captions.cpp \
    src/exportgraph.cpp \
    src/jobs.cpp \
    src/mcpserver.cpp \
    src/media.cpp \
    src/portalfilepicker.cpp \
    src/project.cpp \
    src/silence.cpp \
    src/theme.cpp \
    src/thumbprovider.cpp \
    src/transcript.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
