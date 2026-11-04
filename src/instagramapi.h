#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

#include "publisher.h"

class QNetworkAccessManager;
class QNetworkReply;

// Publishing reels with Instagram's official API (the Instagram API with
// Instagram Login or Unified Login). It needs an Instagram Creator or
// Business account and an access token for it, made once in the Meta app
// dashboard and pasted into Reelcut. Then:
//   1. POST /<id>/media        media_type=REELS, upload_type=resumable, caption
//                              -> a container id and an upload address
//   2. POST <upload address>   the video bytes (Meta's resumable upload)
//   3. GET  /<container>       status_code, until FINISHED (or ERROR)
//   4. POST /<id>/media_publish creation_id=<container> -> the post's id
//   5. GET  /<post>            permalink, to open it
// Legacy Instagram tokens are renewed weekly; Unified Login tokens must be
// renewed through Meta when their configured lifetime ends.
class InstagramApi : public Publisher {
    Q_OBJECT
public:
    explicit InstagramApi(QObject *parent = nullptr);

    QString platform() const override { return "instagram"; }
    QString account() const override { return m_username; }
    // Publishes now; scheduling Instagram is the scheduler's job (src/postscheduler.h).
    void publish(const PostRequest &post) override { publish(post.file, post.text); }

    // Where the token is kept, readable only by you.
    QString accountFile() const;
    bool connected() const override { return !m_token.isEmpty(); }
    QString username() const { return m_username; }
    // What a scheduled post needs to publish as this account later.
    QString token() const { return m_token; }
    QString userId() const { return m_userId; }
    QString apiBase() const;

    // Checks the token with /me and keeps it if it works.
    void connectToken(const QString &token);
    void disconnect();
    // Publishes `file` as a reel with `caption`. Signals progress, then published.
    void publish(const QString &file, const QString &caption);
    bool busy() const override { return m_busy; }

    // Tests point these at a stand-in server.
    QString graphBase;  // optional API override; otherwise selected for the token
    QString stateDir;          // empty: ~/.config/reelcut
    int pollIntervalMs = 3000;

signals:
    void connectFinished(bool ok, const QString &message);

private:
    bool usesUnifiedLogin() const;
    QNetworkReply *get(const QString &path, const QList<QPair<QString, QString>> &query);
    QNetworkReply *postForm(const QString &path, const QList<QPair<QString, QString>> &form);
    void load();
    void save(const QString &token, const QString &userId, const QString &username, const QDateTime &issued);
    void refreshIfOld();
    void createContainer(const QString &file, const QString &caption);
    void upload(const QString &container, const QUrl &uri, const QString &file);
    void waitForProcessing(const QString &container, int tries);
    void publishContainer(const QString &container);
    void finish(bool ok, const QString &message, const QUrl &permalink = {});

    QNetworkAccessManager *m_net;
    QString m_token;
    QString m_username;
    QString m_userId;  // the account's Instagram id, used in the publish calls
    QDateTime m_issued;
    bool m_busy = false;
};
