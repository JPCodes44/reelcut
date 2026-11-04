#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

#include "publisher.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

// Publishing Shorts with the YouTube Data API. Google only lets an app post
// through an OAuth client of your own: once, in Google Cloud, enable the
// YouTube Data API v3 and make an OAuth client of type "Desktop app", then
// give Reelcut its id and secret. Signing in:
//   1. Reelcut listens on 127.0.0.1 and opens Google's sign-in page in the
//      browser, with a random state and a PKCE (S256) challenge.
//   2. Google sends the browser back to 127.0.0.1 with a code.
//   3. POST oauth2.googleapis.com/token: the code and verifier -> access and
//      refresh tokens. The refresh token is kept; access tokens last an hour
//      and are renewed with it when needed.
//   4. GET channels?mine=true -> the channel's name.
// Publishing is a resumable upload: POST the video's details (title,
// description, privacy, publishAt to schedule) -> an upload address; PUT the
// bytes there -> the video's id. YouTube schedules natively: a private video
// with publishAt goes public then.
class YouTubeApi : public Publisher {
    Q_OBJECT
public:
    explicit YouTubeApi(QObject *parent = nullptr);
    ~YouTubeApi() override;

    QString platform() const override { return "youtube"; }
    bool connected() const override { return !m_refreshToken.isEmpty(); }
    QString account() const override { return m_channelTitle; }
    bool busy() const override { return m_busy; }
    void publish(const PostRequest &post) override;

    QString accountFile() const;
    // Opens Google's sign-in in the browser; connectFinished says how it went.
    void connectClient(const QString &clientId, const QString &clientSecret);
    void disconnect();

    // How the browser is opened; tests catch it instead.
    std::function<void(const QUrl &)> openUrl;
    // Tests point these at a stand-in server.
    QString authBase = "https://accounts.google.com/o/oauth2/v2/auth";
    QString tokenUrl = "https://oauth2.googleapis.com/token";
    QString apiBase = "https://www.googleapis.com/youtube/v3";
    QString uploadBase = "https://www.googleapis.com/upload/youtube/v3";
    QString stateDir;  // empty: ~/.config/reelcut

signals:
    void connectFinished(bool ok, const QString &message);

private:
    void load();
    void stopListening();
    void signedIn(const QString &code);
    void failConnect(const QString &message);
    // Calls `then` with a fresh access token, or an error.
    void withAccess(const std::function<void(const QString &error)> &then);
    QNetworkReply *postToken(const QList<QPair<QString, QString>> &form);
    void startUpload(const PostRequest &post);
    void sendBytes(const QUrl &session, const PostRequest &post);
    void finish(bool ok, const QString &message, const QUrl &link = {});

    QNetworkAccessManager *m_net;
    QString m_clientId, m_clientSecret, m_refreshToken, m_channelId, m_channelTitle;
    QString m_accessToken;
    QDateTime m_accessExpires;
    bool m_busy = false;

    // While signing in.
    QTcpServer *m_listener = nullptr;
    QString m_pendingId, m_pendingSecret, m_state, m_verifier, m_redirect;
};
