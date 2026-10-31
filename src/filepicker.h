#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

// File dialogs behind an interface, so tests can inject a fake picker.
// The picker shape follows Omacut (MIT, David Heinemeier Hansson).
class FilePicker : public QObject {
    Q_OBJECT

public:
    enum Kind {
        Media,        // video clips to add, several at once
        Music,        // one audio file for the music track
        Lut,          // one .cube LUT
        OpenProject,  // a .reel project
        SaveProject,  // where to save a .reel project
        Export        // where to write the finished .mp4
    };
    Q_ENUM(Kind)

    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void open(Kind kind) = 0;
    virtual void save(Kind kind, const QUrl &suggestedUrl) = 0;

signals:
    void selected(int kind, const QList<QUrl> &urls);
    void failed(const QString &message);
};
