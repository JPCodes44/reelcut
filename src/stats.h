#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <array>
#include <functional>

#include "publisher.h"

class FacebookApi;
class InstagramApi;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class YouTubeApi;

// Reel stats: every reel on each connected account (Instagram, the Facebook
// Page, YouTube Shorts) with what its platform's API says about it, for the
// Stats page. Handed to QML as `stats`. A refresh reads all three at once in
// the background; one failing never stops the others. The last refresh and a
// growth history per reel are kept in ~/.config/reelcut/stats.json, so the
// page opens with numbers at once.
class Stats : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // What's loading, e.g. "Loading Instagram reels (12/40)…"; "" when idle.
    Q_PROPERTY(QString status READ status NOTIFY changed)
    // When the last refresh finished; invalid until one has.
    Q_PROPERTY(QDateTime refreshed READ refreshed NOTIFY changed)
    // One map per reel, newest first: platform, id, caption, title, thumbnail,
    // permalink, published (ms), duration (s), metrics {key: number},
    // engagementRate (0..1, missing without views).
    Q_PROPERTY(QVariantList reels READ reels NOTIFY changed)
    // One map per platform, always all three: platform, label, connected,
    // account, followers, reelCount, error, needsPermission, followerSeries,
    // viewSeries ([{t, v}], daily, oldest first).
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY changed)
    // {key, label, unit ("count", "seconds", "percent"), platforms} per metric.
    Q_PROPERTY(QVariantList metricInfo READ metricInfo CONSTANT)

public:
    Stats(InstagramApi *instagram, FacebookApi *facebook, YouTubeApi *youtube, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString status() const;
    QDateTime refreshed() const { return m_refreshed; }
    QVariantList reels() const { return m_reels; }
    QVariantList accounts() const { return m_accounts; }
    QVariantList metricInfo() const;

    // Reads every connected account again. Does nothing while one is running.
    Q_INVOKABLE void refresh();
    // The snapshots kept of a reel, oldest first: [{t, metrics}].
    Q_INVOKABLE QVariantList history(const QString &platform, const QString &id) const;
    // A reel's views per day: from YouTube Analytics for YouTube, otherwise
    // (or when Analytics can't say) the views in its history. Answers with dailyLoaded.
    Q_INVOKABLE void loadDaily(const QString &platform, const QString &id);

    QString statsFile() const;

    // Tests point these at a stand-in server. Instagram and Facebook use
    // their publisher's graphBase, YouTube's Data API its apiBase.
    QString youtubeAnalyticsBase = "https://youtubeanalytics.googleapis.com/v2";
    QString stateDir;  // empty: ~/.config/reelcut

signals:
    void changed();
    void dailyLoaded(const QString &platform, const QString &id, const QVariantList &points);

private:
    struct Reel {
        QString id, caption, title, thumbnail, permalink, privacyStatus;
        qint64 published = 0;
        double duration = 0;
        QVariantMap metrics;
    };
    struct Point {
        qint64 t;
        double v;
    };
    // One platform during a refresh.
    struct Side {
        QString platform;
        bool active = false;
        int pending = 0;   // requests out
        int running = 0;   // per-reel insight calls out
        QList<std::function<void()>> queue;  // per-reel insight calls waiting
        QVector<Reel> reels;
        QHash<QString, int> index;  // reel id -> position in reels
        QSet<QString> failing;      // metrics this account can't give, this refresh
        bool stopInsights = false;
        bool listed = false;        // the reel list came back
        int insightsDone = 0;
        QString error, needsPermission;
        double followers = -1;      // unknown
        QVector<Point> followerSeries, followerGains, viewSeries;
        QString youtubeToken, uploads;
        int pages = 0;
    };

    void ensureLoaded() const;
    void save();
    void startInstagram();
    void listInstagram(const QUrl &url);
    void instagramInsights(const QString &reel, const QStringList &metrics, bool single);
    void startFacebook();
    void listFacebook(const QUrl &url);
    void facebookInsights(const QString &reel, const QStringList &metrics, bool single);
    void facebookSeries(const QStringList &metrics);
    void startYouTube();
    void listYouTube(const QString &pageToken);
    void youtubeAnalytics();
    void youtubeChannelSeries();
    // Handles a Meta insights error: true when it was the whole platform's
    // problem (permission, token, rate limit) and insights stop.
    bool metaStops(int side, const Net::Response &r);

    QNetworkReply *graphGet(int side, const QString &path, const QList<QPair<QString, QString>> &query);
    QNetworkReply *googleGet(int side, const QUrl &url);
    // Sends a request for side `side`, counting it, and runs `then` unless
    // the refresh it belongs to is over.
    void call(int side, QNetworkReply *reply, const std::function<void(const Net::Response &)> &then);
    // The same, waiting for one of the four per-reel slots.
    void limited(int side, const std::function<QNetworkReply *()> &make,
                 const std::function<void(const Net::Response &)> &then);
    void stopInsights(int side);
    void settle(int side);
    void finishSide(int side);
    void finishAll();
    void publishersChanged();
    void touch();  // the status changed: tell QML, at most a few times a second

    QVariantMap accountFor(int side) const;
    QVariantMap emptyAccount(int side) const;
    static QVariantMap reelMap(const QString &platform, const Reel &reel);
    Publisher *publisher(int side) const;

    QPointer<InstagramApi> m_instagram;
    QPointer<FacebookApi> m_facebook;
    QPointer<YouTubeApi> m_youtube;
    QNetworkAccessManager *m_net;
    QTimer *m_touch;
    std::array<Side, 3> m_sides;
    int m_run = 0;
    bool m_busy = false;
    // Read from stats.json on first need (after tests set stateDir).
    mutable bool m_loaded = false;
    mutable QDateTime m_refreshed;
    mutable QVariantList m_reels, m_accounts;
    mutable QHash<QString, QVariantList> m_history;  // "platform:id" -> [{t, metrics}]
};
