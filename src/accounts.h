#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include <functional>
class InstagramApi;
class FacebookApi;
class YouTubeApi;
class QNetworkAccessManager;

// Non-secret account status over the same publishers used by Post, Stats and Schedule.
class Accounts : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QVariantList facebookPages READ facebookPages NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    Accounts(InstagramApi *, FacebookApi *, YouTubeApi *, const QString &stateDir, QObject *parent = nullptr);
    QVariantList entries() const;
    QVariantList facebookPages() const { return m_pages; }
    bool busy() const { return m_busy; }
    Q_INVOKABLE void connectToken(const QString &platform, const QString &token);
    Q_INVOKABLE void connectYouTube(const QString &clientId, const QString &secret);
    Q_INVOKABLE void selectFacebookPage(const QString &id);
    Q_INVOKABLE void cancelSelection();
    Q_INVOKABLE void testConnection(const QString &platform);
    Q_INVOKABLE void disconnectAccount(const QString &platform);
signals:
    void changed();
    void connectionRejected(const QString &platform, const QString &message);
private:
    void request(const QString &base, const QString &path, const QString &token,
                 const QList<QPair<QString, QString>> &query, int generation,
                 const std::function<void(const QJsonObject &, const QString &, int)> &done);
    void inspect(const QString &platform, int generation, const QString &inspector = {});
    void connectionFinished(const QString &platform, bool ok, const QString &message);
    void sync();
    void save();
    void finish(const QString &platform, const QString &message);
    QString token(const QString &platform) const;
    QString key(const QString &platform) const;
    QString base(const QString &platform) const;
    InstagramApi *m_instagram;
    FacebookApi *m_facebook;
    YouTubeApi *m_youtube;
    QNetworkAccessManager *m_net;
    QString m_stateDir;
    QJsonObject m_info, m_pendingMeta;
    QVariantList m_pages;
    QString m_active, m_inspector;
    bool m_busy = false;
    int m_generation = 0;
};
