#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class ModalUsage;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QHttpMultiPart;

// Makes songs with ACE-Step on Modal: sends the Songs page's request, waits
// for the takes, saves them with the request in their own folder.
class SongGenerator : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY configChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY configChanged)
    Q_PROPERTY(QString tokenId READ tokenId NOTIFY configChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QVariantList songs READ songs NOTIFY songsChanged)
    Q_PROPERTY(QString songsDir READ songsDir CONSTANT)
public:
    SongGenerator(ModalUsage *usage, const QString &stateDir = {}, const QString &songsDir = {}, QObject *parent = nullptr);

    bool configured() const { return !m_endpoint.isEmpty() && !m_tokenId.isEmpty() && !m_tokenSecret.isEmpty(); }
    QString endpoint() const { return m_endpoint; }
    QString tokenId() const { return m_tokenId; }
    bool busy() const { return m_busy; }
    QString stage() const { return m_stage; }
    QString lastError() const { return m_lastError; }
    QVariantList songs() const { return m_songs; }
    QString songsDir() const { return m_songsDir; }

    Q_INVOKABLE void configure(const QString &endpoint, const QString &tokenId, const QString &tokenSecret);
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE bool generate(const QVariantMap &request);
    Q_INVOKABLE void writeLyrics(const QVariantMap &request);
    Q_INVOKABLE void describeImages(const QStringList &paths);
    Q_INVOKABLE void reload();
    Q_INVOKABLE QString readTextFile(const QString &path) const;   // a lyrics .txt for the page

    QString claudeCommand;      // empty: found on PATH or in the usual places
    int pollMs = 2000;
    int timeoutMs = 600000;

signals:
    void configChanged();
    void stateChanged();
    void songsChanged();
    void connectionTested(bool ok, const QString &message);
    void finished(const QString &folder);
    void failed(const QString &message);
    void lyricsWritten(const QString &lyrics, const QString &error);
    void imagesDescribed(const QString &text, const QString &error);

private:
    QNetworkRequest request(const QString &path) const;
    static QString explain(QNetworkReply *reply);
    void setStage(const QString &stage);
    void later(const std::function<void()> &f);
    void poll();
    void send(QHttpMultiPart *multi);
    void download(const QStringList &files, int index);
    void finish();
    void fail(const QString &message);
    QString audioFor(const QString &path);   // ffmpeg video → mp3 in the song folder

    QPointer<ModalUsage> m_usage;
    QNetworkAccessManager *m_net;
    QString m_stateDir, m_songsDir;
    QString m_endpoint, m_tokenId, m_tokenSecret;
    bool m_busy = false;
    QString m_stage, m_lastError;
    QVariantList m_songs;
    // The request in flight.
    QVariantMap m_request;
    QJsonObject m_sent;
    QString m_folder, m_taskId;
    QStringList m_saved;
    QElapsedTimer m_clock;
    QTimer m_poll;
    int m_pollErrors = 0;
};
