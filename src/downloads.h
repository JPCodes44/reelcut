#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QUrl>
#include <QVariantList>

class QProcess;

// Saves reels from YouTube, Instagram, TikTok, Facebook (anything yt-dlp
// reads) into one folder, and lists that folder as a library to drag clips
// from. Handed to QML as `downloads`. Each download is its own yt-dlp run,
// so several can go at once while editing carries on.
class Downloads : public QObject {
    Q_OBJECT
    // Saved videos, newest first: path, url (file://), title, thumb (file:// or empty), size.
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    // Running downloads: id, link, title, progress (0..1, or -1 while unknown).
    Q_PROPERTY(QVariantList active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)
    // False when yt-dlp isn't installed.
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    // folder defaults to ~/Videos/Reelcut Downloads; tests pass a temp dir.
    explicit Downloads(QObject *parent = nullptr, const QString &folder = {});
    ~Downloads() override;

    QVariantList items() const { return m_items; }
    QVariantList active() const;
    QString folder() const { return m_folder; }
    bool available() const;

    // Starts downloading `link`; false (and failed()) when it isn't a web link.
    Q_INVOKABLE bool download(const QString &link);
    Q_INVOKABLE void cancel(int id);
    // Deletes a saved video and its thumbnail from the library folder.
    Q_INVOKABLE void remove(const QString &path);
    Q_INVOKABLE void showFolder();
    // Drags a saved video out of the app as a real file: drop it on Claude,
    // ChatGPT, Codex, Antigravity, a browser upload box, a terminal (pastes the
    // path) or the timeline. Blocks until the drop; returns true if it landed.
    Q_INVOKABLE bool startDrag(const QString &path, const QUrl &thumb = {});
    // The file on the clipboard, to paste into a chat or upload box with Ctrl+V.
    Q_INVOKABLE void copyFile(const QString &path);
    // The file's path as text, for terminal tools (claude, codex).
    Q_INVOKABLE void copyPath(const QString &path);
    // What a drag or copy of `path` offers: file URL, plain path, GNOME/KDE file list.
    static class QMimeData *mimeFor(const QString &path);
    Q_INVOKABLE void rescan();

    // The yt-dlp arguments for one download into `folder`.
    static QStringList ytDlpArgs(const QString &link, const QString &folder);
    // A saved file's display name: "Title [id].mp4" reads as "Title".
    static QString titleFor(const QString &fileName);

signals:
    void itemsChanged();
    void activeChanged();
    void finished(const QString &path, const QString &title);
    void failed(const QString &message);

private:
    struct Running {
        int id = 0;
        QString link;
        QString title;
        double progress = -1.0;
        QString file;
        QByteArray errors;
        QByteArray pending;
        bool cancelled = false;
        QProcess *process = nullptr;
    };
    void readOutput(Running &run);
    Running *find(int id);

    QString m_folder;
    QVariantList m_items;
    QList<Running *> m_running;
    int m_nextId = 1;
    // While a drag runs, the library list holds still (its tiles own the drag).
    bool m_dragging = false;
    bool m_rescanPending = false;
    QFileSystemWatcher m_watcher;
};
