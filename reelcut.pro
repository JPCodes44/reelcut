QT += core gui qml quick quickcontrols2 multimedia network concurrent dbus

CONFIG += c++17 release
TARGET = reelcut
TEMPLATE = app

HEADERS += \
    src/waveformprovider.h \
    src/backend.h \
    src/bodyeffects.h \
    src/captions.h \
    src/downloads.h \
    src/effects.h \
    src/exportgraph.h \
    src/filepicker.h \
    src/filters.h \
    src/jobs.h \
    src/mcpserver.h \
    src/media.h \
    src/portalfilepicker.h \
    src/project.h \
    src/segment.h \
    src/fisheyetext.h \
    src/colorgrade.h \
    src/posting.h \
    src/publisher.h src/instagramapi.h src/facebookapi.h src/youtubeapi.h src/postscheduler.h src/higgsfield.h src/stats.h \
    src/silence.h \
    src/theme.h \
    src/thumbprovider.h \
    src/transcript.h \
    src/transitions.h \
    src/translator.h

SOURCES += \
    src/waveformprovider.cpp \
    src/main.cpp \
    src/backend.cpp \
    src/bodyeffects.cpp \
    src/captions.cpp \
    src/downloads.cpp \
    src/effects.cpp \
    src/exportgraph.cpp \
    src/filters.cpp \
    src/jobs.cpp \
    src/mcpserver.cpp \
    src/media.cpp \
    src/portalfilepicker.cpp \
    src/project.cpp \
    src/segment.cpp \
    src/fisheyetext.cpp \
    src/colorgrade.cpp \
    src/posting.cpp \
    src/publisher.cpp src/instagramapi.cpp src/facebookapi.cpp src/youtubeapi.cpp src/postscheduler.cpp src/higgsfield.cpp src/stats.cpp \
    src/silence.cpp \
    src/theme.cpp \
    src/thumbprovider.cpp \
    src/transcript.cpp \
    src/transitions.cpp \
    src/translator.cpp

# Person segmentation for body effects (OpenCV's DNN module).
INCLUDEPATH += /usr/include/opencv5
LIBS += -lopencv_core -lopencv_dnn

RESOURCES += src/resources.qrc

HEADERS += src/accounts.h
SOURCES += src/accounts.cpp

HEADERS += src/automationregistry.h src/appbridge.h
SOURCES += src/automationregistry.cpp src/appbridge.cpp

HEADERS += src/songrequest.h src/modalusage.h src/songgen.h
SOURCES += src/songrequest.cpp src/modalusage.cpp src/songgen.cpp
RESOURCES += src/songs.qrc
