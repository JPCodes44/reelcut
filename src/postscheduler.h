#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

class QNetworkAccessManager;
class QJsonValue;

// Scheduled Instagram posts, through Reelcut's own small Convex backend
// (scheduler/ in this repository). Instagram's API can only publish right
// away, so the backend keeps the video and, at the time picked, has
// Instagram fetch it and publish it. Nothing here has to be running then.
//
// The backend's address and key live in ~/.config/reelcut/scheduler.json:
//   {"url": "https://<deployment>.convex.cloud", "key": "<REELCUT_SECRET>"}
// readable only by you. Scheduling:
//   1. posts:uploadUrl            -> where to send the video
//   2. POST <that address>        the video bytes -> its storage id
//   3. posts:schedule             the post, the time, the account to post as
class PostScheduler : public QObject {
    Q_OBJECT
public:
    explicit PostScheduler(QObject *parent = nullptr);

    QString configFile() const;
    bool configured() const { return !m_url.isEmpty() && !m_key.isEmpty(); }
    QString url() const { return m_url; }
    // Saves the address and key, then checks them with a refresh.
    void configure(const QString &url, const QString &key);

    struct Account { QString userId, username, token, apiBase; };
    void schedule(const QString &file, const QString &title, const QString &caption, const QDateTime &when,
                  const Account &account);
    bool busy() const { return m_busy; }

    void refresh();
    void cancel(const QString &id);
    void reschedule(const QString &id, const QDateTime &when);
    // Again now, with the account's current token in case the old one ran out.
    void retry(const QString &id, const QString &token);
    void remove(const QString &id);
    // Newest first: id, title, caption, publishAt (ms), status, username, error, permalink.
    QVariantList posts() const { return m_posts; }
    QString lastError() const { return m_lastError; }

    QString stateDir;  // empty: ~/.config/reelcut

signals:
    void configChanged();
    void postsChanged();
    void progress(const QString &text, double fraction);
    void scheduled(bool ok, const QString &message);
    // A change to a post (cancel, retry...) went through or didn't.
    void changed(bool ok, const QString &message);

private:
    using Then = std::function<void(bool ok, const QJsonValue &value, const QString &error)>;
    void call(const QString &kind, const QString &path, QJsonObject args, const Then &then);
    void change(const QString &path, QJsonObject args, const QString &done);
    void load();

    QNetworkAccessManager *m_net;
    QString m_url, m_key;
    QVariantList m_posts;
    QString m_lastError;
    bool m_busy = false;
};
