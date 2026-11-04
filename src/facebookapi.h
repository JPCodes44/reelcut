#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVector>

#include "publisher.h"

class QNetworkAccessManager;
class QNetworkReply;

// Publishing reels to a Facebook Page with Meta's Reels Publishing API. The
// API only posts to Pages, not personal profiles. Connecting takes a Meta
// user token with pages_show_list, pages_read_engagement and
// pages_manage_posts (the Instagram token works when the same Meta app has
// them); Reelcut keeps only the Page's own token, which doesn't expire.
//   1. POST /<page>/video_reels  upload_phase=start   -> video_id, upload_url
//   2. POST <upload_url>         the bytes, Authorization: OAuth <page token>
//   3. POST /<page>/video_reels  upload_phase=finish, video_state=PUBLISHED,
//                                or SCHEDULED with scheduled_publish_time
//   4. GET  /<video>?fields=status  until published or scheduled
//   5. GET  /<video>?fields=permalink_url
// Facebook schedules natively, 10 minutes to 29 days ahead.
class FacebookApi : public Publisher {
    Q_OBJECT
public:
    explicit FacebookApi(QObject *parent = nullptr);

    QString platform() const override { return "facebook"; }
    bool connected() const override { return !m_pageToken.isEmpty(); }
    QString account() const override { return m_pageName; }
    bool busy() const override { return m_busy; }
    void publish(const PostRequest &post) override;

    QString pageId() const { return m_pageId; }
    QString accountFile() const;

    // Finds the Pages the user token manages: one is connected at once,
    // several are offered through pagesFound for choosePage.
    void connectToken(const QString &userToken);
    void choosePage(const QString &id);
    void disconnect();

    // Tests point these at a stand-in server.
    QString graphBase;  // empty: https://graph.facebook.com/v26.0
    QString stateDir;   // empty: ~/.config/reelcut
    int pollIntervalMs = 3000;

signals:
    void connectFinished(bool ok, const QString &message);
    // id and name of each Page, when there's more than one to pick from.
    void pagesFound(const QVariantList &pages);

private:
    struct Page { QString id, name, token; };
    QString base() const;
    QNetworkReply *get(const QString &path, const QList<QPair<QString, QString>> &query, const QString &token);
    QNetworkReply *postForm(const QString &path, const QList<QPair<QString, QString>> &form);
    void load();
    void keep(const Page &page);
    void upload(const QString &video, const QUrl &uri, const PostRequest &post);
    void finishUpload(const QString &video, const PostRequest &post);
    void waitForPublishing(const QString &video, const PostRequest &post, int tries);
    void finish(bool ok, const QString &message, const QUrl &link = {});

    QNetworkAccessManager *m_net;
    QString m_pageId, m_pageName, m_pageToken;
    QVector<Page> m_candidates;  // Pages offered, waiting for choosePage
    bool m_busy = false;
};
