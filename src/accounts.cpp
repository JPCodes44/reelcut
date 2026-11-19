#include "accounts.h"
#include "instagramapi.h"
#include "facebookapi.h"
#include "youtubeapi.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace {
qint64 expiry(const QJsonObject &info, const QString &field) {
    const auto v = info.value(field);
    return v.isDouble() && v.toDouble() >= 0 && v.toDouble() <= 253402300799.0 ? qint64(v.toDouble()) : -1;
}
QString status(const QJsonObject &info, bool connected) {
    if (!connected) return QObject::tr("Not connected");
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (info.value("invalid").toBool() || (expiry(info, "expiresAt") > 0 && expiry(info, "expiresAt") <= now)
        || (expiry(info, "dataAccessExpiresAt") > 0 && expiry(info, "dataAccessExpiresAt") <= now))
        return QObject::tr("Expired — reconnect");
    return info.value("checkedAt").toDouble() > 0 ? QObject::tr("Connected") : QObject::tr("Saved — test connection");
}
}
Accounts::Accounts(InstagramApi *ig, FacebookApi *fb, YouTubeApi *yt, const QString &stateDir, QObject *parent)
    : QObject(parent), m_instagram(ig), m_facebook(fb), m_youtube(yt), m_net(new QNetworkAccessManager(this)), m_stateDir(stateDir) {
    // Metadata is loaded before any publisher's queued load can emit accountChanged.
    m_info = Net::load(Net::stateFile(stateDir, "account-status.json"));
    for (Publisher *p : {static_cast<Publisher *>(ig), static_cast<Publisher *>(fb), static_cast<Publisher *>(yt)})
        connect(p, &Publisher::accountChanged, this, &Accounts::sync);
    connect(ig, &InstagramApi::connectFinished, this, [this](bool ok, const QString &message) { connectionFinished("instagram", ok, message); });
    connect(fb, &FacebookApi::connectFinished, this, [this](bool ok, const QString &message) { connectionFinished("facebook", ok, message); });
    connect(yt, &YouTubeApi::connectFinished, this, [this](bool ok, const QString &message) { connectionFinished("youtube", ok, message); });
    connect(fb, &FacebookApi::pagesFound, this, [this](const QVariantList &pages) { m_pages = pages; m_busy = false; emit changed(); });
    connect(yt, &YouTubeApi::tokenTimesChanged, this, &Accounts::changed);
    QTimer::singleShot(0, this, &Accounts::sync);
}
QString Accounts::token(const QString &platform) const {
    return platform == "instagram" ? m_instagram->token() : platform == "facebook" ? m_facebook->pageToken() : QString();
}
QString Accounts::key(const QString &platform) const {
    const auto value = platform == "youtube" ? m_youtube->channelId() : token(platform);
    return value.isEmpty() ? QString() : QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString Accounts::base(const QString &platform) const { return platform == "instagram" ? m_instagram->apiBase() : m_facebook->apiBase(); }
void Accounts::save() {
    if (!Net::savePrivate(Net::stateFile(m_stateDir, "account-status.json"), m_info)) {
        for (const auto &id : {QString("instagram"), QString("facebook"), QString("youtube")}) {
            auto info = m_info.value(id).toObject(); info["message"] = tr("Connection status could not be saved. Check folder permissions."); m_info[id] = info;
        }
    }
    emit changed();
}
void Accounts::sync() {
    // A publisher may not have loaded yet. Don't erase another platform's saved metadata.
    for (const auto &id : {QString("instagram"), QString("facebook"), QString("youtube")}) {
        const QString fingerprint = key(id);
        auto info = m_info.value(id).toObject();
        if (!fingerprint.isEmpty() && info.value("key").toString() != fingerprint)
            m_info[id] = QJsonObject{{"key", fingerprint}};
    }
    emit changed();
}
QVariantList Accounts::entries() const {
    QVariantList result;
    const struct { const char *id, *label, *url; } platforms[] = {
        {"instagram", "Instagram", "https://www.instagram.com/"}, {"facebook", "Facebook", "https://www.facebook.com/"},
        {"youtube", "YouTube", "https://studio.youtube.com/"}, {"tiktok", "TikTok", "https://www.tiktok.com/tiktokstudio/upload"}
    };
    for (const auto &p : platforms) {
        const QString id = p.id;
        const bool connected = id == "instagram" ? m_instagram->connected() : id == "facebook" ? m_facebook->connected() : id == "youtube" && m_youtube->connected();
        const auto info = m_info.value(id).toObject();
        qint64 expires = expiry(info, "expiresAt");
        qint64 dataExpires = expiry(info, "dataAccessExpiresAt");
        if (id == "youtube") {
            expires = m_youtube->accessExpires().isValid() ? m_youtube->accessExpires().toSecsSinceEpoch() : -1;
            dataExpires = m_youtube->refreshExpires().isValid() ? m_youtube->refreshExpires().toSecsSinceEpoch() : -1;
        }
        // YouTube access tokens renew automatically; their hourly expiry isn't a reconnect requirement.
        auto connectionInfo = info;
        if (id == "youtube") connectionInfo.remove("expiresAt");
        result << QVariantMap{{"id", id}, {"label", p.label}, {"url", p.url}, {"api", id != "tiktok"}, {"connected", connected},
            {"name", id == "instagram" ? m_instagram->account() : id == "facebook" ? m_facebook->account() : id == "youtube" ? m_youtube->account() : QString()},
            {"status", id == "tiktok" ? tr("Browser sign-in") : status(connectionInfo, connected)},
            {"message", info.value("message").toString()}, {"expiresAt", expires}, {"dataAccessExpiresAt", dataExpires},
            {"checkedAt", info.value("checkedAt").toDouble()}};
    }
    return result;
}
void Accounts::request(const QString &api, const QString &path, const QString &access,
                       const QList<QPair<QString, QString>> &query, int generation,
                       const std::function<void(const QJsonObject &, const QString &, int)> &done) {
    QUrl url(api + path); QUrlQuery q;
    for (const auto &item : query) q.addQueryItem(item.first, item.second);
    url.setQuery(q);
    QNetworkRequest req(url); req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    req.setRawHeader("Authorization", "Bearer " + access.toUtf8());
    auto *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, done] {
        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        const int code = json.value("error").toObject().value("code").toInt();
        // Never forward a server URL/error string containing a credential into QML.
        const bool failed = json.contains("error") || reply->error() != QNetworkReply::NoError || json.isEmpty();
        reply->deleteLater();
        if (generation == m_generation) done(json, failed ? tr("Could not verify this connection. Check your network, permissions and token, then try again.") : QString(), code);
    });
}
void Accounts::finish(const QString &platform, const QString &message) {
    auto info = m_info.value(platform).toObject(); info["message"] = message; m_info[platform] = info;
    m_busy = false; m_active.clear(); m_inspector.clear(); save();
}
void Accounts::connectToken(const QString &platform, const QString &raw) {
    if (m_busy || !m_pages.isEmpty() || (platform != "instagram" && platform != "facebook")) return;
    const QString candidate = raw.trimmed();
    if (candidate.isEmpty()) return;
    m_active = platform; m_busy = true; const int generation = ++m_generation; emit changed();
    const QString api = platform == "facebook" ? m_facebook->apiBase() : !m_instagram->graphBase.isEmpty() ? m_instagram->graphBase
        : candidate.startsWith("EAA") ? QString("https://graph.facebook.com/v26.0") : QString("https://graph.instagram.com");
    request(api, "/debug_token", candidate, {{"input_token", candidate}}, generation,
        [this, platform, candidate](const QJsonObject &json, const QString &error, int) {
        const auto data = json.value("data").toObject();
        if (error.isEmpty() && data.contains("is_valid") && !data.value("is_valid").toBool()) {
            const QString message = tr("The new token is invalid. Your previous connection has been kept.");
            finish(platform, message); emit connectionRejected(platform, message); return;
        }
        m_pendingMeta = error.isEmpty() ? data : QJsonObject{};
        m_inspector = candidate;
        if (platform == "instagram") m_instagram->connectToken(candidate);
        else m_facebook->connectToken(candidate);
    });
}
void Accounts::connectYouTube(const QString &id, const QString &secret) {
    if (m_busy || !m_pages.isEmpty()) return;
    m_active = "youtube"; m_busy = true; ++m_generation; emit changed();
    m_youtube->connectClient(id, secret);
}
void Accounts::connectionFinished(const QString &platform, bool ok, const QString &) {
    if (!m_active.isEmpty() && m_active != platform) return;
    m_pages.clear();
    if (!ok) { finish(platform, tr("Connection failed. Your previous connection has been kept. Check the setup and try again.")); return; }
    if (platform == "youtube") {
        m_active = platform; m_busy = false; testConnection(platform); return;
    }
    const auto data = platform == "facebook" ? m_facebook->tokenMetadata() : m_pendingMeta;
    m_pendingMeta = {};
    QJsonObject info{{"key", key(platform)}, {"checkedAt", double(QDateTime::currentSecsSinceEpoch())}};
    if (data.contains("is_valid")) {
        info["invalid"] = !data.value("is_valid").toBool();
        info["expiresAt"] = double(expiry(data, "expires_at"));
        info["dataAccessExpiresAt"] = double(expiry(data, "data_access_expires_at"));
    }
    m_info[platform] = info;
    finish(platform, data.contains("is_valid") ? tr("Connection checked.") : tr("Account accepted. Expiry details unavailable; check Meta's debugger."));
}
void Accounts::inspect(const QString &platform, int generation, const QString &inspector) {
    const QString current = token(platform), fingerprint = key(platform);
    request(base(platform), "/debug_token", inspector.isEmpty() ? current : inspector, {{"input_token", current}}, generation,
        [this, platform, fingerprint](const QJsonObject &json, const QString &error, int) {
        if (fingerprint != key(platform)) { m_busy = false; emit changed(); return; }
        auto info = m_info.value(platform).toObject();
        info["key"] = fingerprint; info["checkedAt"] = double(QDateTime::currentSecsSinceEpoch());
        const auto data = json.value("data").toObject();
        if (!error.isEmpty() || !data.contains("is_valid")) {
            m_info[platform] = info;
            finish(platform, tr("Account accepted. Expiry details unavailable; check Meta's debugger. Previously reported dates are retained.")); return;
        }
        info["invalid"] = !data.value("is_valid").toBool();
        info["expiresAt"] = double(expiry(data, "expires_at"));
        info["dataAccessExpiresAt"] = double(expiry(data, "data_access_expires_at"));
        m_info[platform] = info;
        finish(platform, info.value("invalid").toBool() ? tr("The token is invalid. Reconnect this account.") : tr("Connection checked."));
    });
}
void Accounts::testConnection(const QString &platform) {
    if (m_busy && m_active != platform) return;
    if (platform == "tiktok") return;
    const QString fingerprint = key(platform);
    if (fingerprint.isEmpty()) return;
    m_active = platform; m_busy = true; const int generation = ++m_generation; emit changed();
    if (platform == "youtube") {
        m_youtube->freshAccess(this, [this, generation, fingerprint](const QString &access, const QString &error) {
            if (generation != m_generation) return;
            if (fingerprint != key("youtube")) { finish("youtube", tr("The YouTube connection changed. Sign in again if access was revoked.")); return; }
            if (!error.isEmpty()) { finish("youtube", tr("YouTube access could not be renewed. Sign in again if access was revoked.")); return; }
            request(m_youtube->apiBase, "/channels", access, {{"part", "snippet"}, {"mine", "true"}}, generation,
                [this](const QJsonObject &json, const QString &error, int code) {
                auto info = m_info.value("youtube").toObject();
                if (!error.isEmpty()) {
                    if (code == 401) info["invalid"] = true;
                    m_info["youtube"] = info;
                    finish("youtube", tr("YouTube could not be checked. Your saved connection is unchanged; try again when the service is available.")); return;
                }
                const auto channels = json.value("items").toArray();
                const bool valid = channels.size() == 1 && channels.first().toObject().value("id").toString() == m_youtube->channelId();
                info["invalid"] = !valid;
                if (valid) info["checkedAt"] = double(QDateTime::currentSecsSinceEpoch());
                m_info["youtube"] = info;
                finish("youtube", valid ? tr("Channel verified. Access tokens renew automatically.") : tr("Could not verify the saved channel. Reconnect and choose the correct channel."));
            });
        });
        return;
    }
    request(base(platform), platform == "instagram" ? "/" + m_instagram->userId() : "/me", token(platform),
        {{"fields", platform == "instagram" ? "id,username" : "id,name"}}, generation,
        [this, platform, generation, fingerprint](const QJsonObject &json, const QString &error, int code) {
        if (fingerprint != key(platform)) { m_busy = false; emit changed(); return; }
        auto info = m_info.value(platform).toObject();
        if (!error.isEmpty() || json.value("id").toString().isEmpty()) {
            if (code == 190) info["invalid"] = true;
            m_info[platform] = info; finish(platform, error.isEmpty() ? tr("The saved account could not be verified. Reconnect or try again.") : error); return;
        }
        info["invalid"] = false; m_info[platform] = info;
        inspect(platform, generation);
    });
}
void Accounts::selectFacebookPage(const QString &id) {
    if (m_busy) return;
    m_pages.clear(); m_busy = true; m_active = "facebook"; emit changed(); m_facebook->choosePage(id);
}
void Accounts::cancelSelection() {
    if (m_busy || m_pages.isEmpty()) return;
    m_facebook->cancelConnect(); m_pages.clear(); m_inspector.clear(); m_active.clear(); emit changed();
}
void Accounts::disconnectAccount(const QString &platform) {
    ++m_generation; m_busy = false; m_active.clear(); m_inspector.clear(); m_pages.clear();
    m_info.remove(platform);
    if (platform == "instagram") m_instagram->disconnect();
    else if (platform == "facebook") m_facebook->disconnect();
    else if (platform == "youtube") m_youtube->disconnect();
    save();
}
