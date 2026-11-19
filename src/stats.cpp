#include "stats.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <optional>

#include "facebookapi.h"
#include "instagramapi.h"
#include "youtubeapi.h"

namespace {

enum { Instagram, Facebook, YouTube };
const char *const kPlatforms[] = {"instagram", "facebook", "youtube"};

const int kMaxReels = 500;     // per platform
const int kMaxPages = 40;      // list pages per platform, whatever they hold
const int kSlots = 4;          // per-reel insight calls at once, per platform
const int kMaxSnapshots = 400; // history kept per reel

// What Instagram and Facebook are asked for per reel, and the metric key
// each lands on (src/stats.h has the table).
const QStringList kInstagramMetrics{"views", "reach", "likes", "comments", "shares", "saved", "total_interactions",
                                    "ig_reels_avg_watch_time", "ig_reels_video_view_total_time"};
const QStringList kFacebookMetrics{"blue_reels_play_count", "fb_reels_total_plays", "fb_reels_replay_count",
                                   "post_impressions_unique", "post_video_avg_time_watched", "post_video_view_time",
                                   "post_video_followers", "post_video_social_actions"};

// A number however the API wrote it: YouTube sends counts as strings.
std::optional<double> number(const QJsonValue &v) {
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok && std::isfinite(d))
            return d;
    }
    return std::nullopt;
}

// Meta's times look like "2026-11-01T07:00:00+0000", which ISO parsing
// doesn't take without the colon in the offset.
qint64 parseTime(const QString &text) {
    QDateTime t = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!t.isValid()) {
        static const QRegularExpression offset("([+-]\\d\\d)(\\d\\d)$");
        QString fixed = text;
        fixed.replace(offset, "\\1:\\2");
        t = QDateTime::fromString(fixed, Qt::ISODateWithMs);
    }
    if (!t.isValid()) {
        const QDate day = QDate::fromString(text, Qt::ISODate);
        if (day.isValid())
            t = day.startOfDay(QTimeZone::UTC);
    }
    return t.isValid() ? t.toMSecsSinceEpoch() : 0;
}

// "PT1M5S" -> 65. 0 when it isn't one.
double isoDuration(const QString &text) {
    static const QRegularExpression pattern(
        "^P(?:(\\d+)D)?(?:T(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+(?:\\.\\d+)?)S)?)?$");
    const QRegularExpressionMatch m = pattern.match(text);
    if (!m.hasMatch())
        return 0;
    return m.captured(1).toDouble() * 86400 + m.captured(2).toDouble() * 3600 + m.captured(3).toDouble() * 60
           + m.captured(4).toDouble();
}

// Meta insights: {data: [{name, values: [{value}]}]}, or with total_value
// {value} instead. The value is a number, or a map for some metrics.
QHash<QString, QJsonValue> insightValues(const QJsonObject &json) {
    QHash<QString, QJsonValue> out;
    for (const QJsonValue &entry : json.value("data").toArray()) {
        const QJsonObject o = entry.toObject();
        const QString name = o.value("name").toString();
        if (name.isEmpty())
            continue;
        QJsonValue value;
        const QJsonObject total = o.value("total_value").toObject();
        const QJsonArray values = o.value("values").toArray();
        if (total.contains("value"))
            value = total.value("value");
        else if (!values.isEmpty())
            value = values.last().toObject().value("value");
        else if (o.contains("value"))
            value = o.value("value");
        if (!value.isUndefined() && !value.isNull())
            out.insert(name, value);
    }
    return out;
}

// Meta insights over days: {data: [{name, values: [{value, end_time}]}]}.
QHash<QString, QVector<QPair<qint64, double>>> insightSeries(const QJsonObject &json) {
    QHash<QString, QVector<QPair<qint64, double>>> out;
    for (const QJsonValue &entry : json.value("data").toArray()) {
        const QJsonObject o = entry.toObject();
        const QString name = o.value("name").toString();
        for (const QJsonValue &v : o.value("values").toArray()) {
            const QJsonObject point = v.toObject();
            const qint64 t = parseTime(point.value("end_time").toString());
            const auto value = number(point.value("value"));
            if (!name.isEmpty() && t > 0 && value)
                out[name] << qMakePair(t, *value);
        }
    }
    return out;
}

// Counts per action type, from a map {"like": 3, ...} or a list
// [{"action_type": "like", "value": 3}], keys lowercased.
QHash<QString, double> actionCounts(const QJsonValue &value) {
    QHash<QString, double> counts;
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            if (const auto n = number(it.value()))
                counts[it.key().toLower()] += *n;
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            const QJsonObject o = item.toObject();
            const QString type = o.value("action_type").toString(o.value("type").toString(o.value("name").toString()));
            const auto n = number(o.contains("value") ? o.value("value") : o.value("count"));
            if (!type.isEmpty() && n)
                counts[type.toLower()] += *n;
        }
    }
    return counts;
}

// Puts what Instagram or Facebook said about a reel under its metric keys.
void applyMeta(QVariantMap &metrics, const QString &name, const QJsonValue &value) {
    static const QHash<QString, QString> keys{
        {"views", "views"}, {"reach", "reach"}, {"likes", "likes"}, {"comments", "comments"}, {"shares", "shares"},
        {"saved", "saves"}, {"total_interactions", "interactions"},
        {"fb_reels_total_plays", "views"}, {"post_impressions_unique", "reach"},
        {"fb_reels_replay_count", "replays"}, {"blue_reels_play_count", "initialPlays"},
        {"post_video_followers", "followsGained"}};
    // Watch times come in milliseconds; the page shows seconds.
    static const QHash<QString, QString> milliseconds{
        {"ig_reels_avg_watch_time", "avgWatch"}, {"ig_reels_video_view_total_time", "totalWatch"},
        {"post_video_avg_time_watched", "avgWatch"}, {"post_video_view_time", "totalWatch"}};
    if (name == "post_video_social_actions") {
        const QHash<QString, double> counts = actionCounts(value);
        auto first = [&](const QStringList &names) -> std::optional<double> {
            for (const QString &n : names)
                if (counts.contains(n))
                    return counts.value(n);
            return std::nullopt;
        };
        std::optional<double> likes = first({"like", "likes"});
        if (!likes) {  // reactions instead of likes
            double reactions = 0;
            bool any = false;
            for (auto it = counts.begin(); it != counts.end(); ++it)
                if (it.key().contains("reaction")) {
                    reactions += it.value();
                    any = true;
                }
            if (any)
                likes = reactions;
        }
        if (likes)
            metrics["likes"] = *likes;
        if (const auto comments = first({"comment", "comments"}))
            metrics["comments"] = *comments;
        if (const auto shares = first({"share", "shares"}))
            metrics["shares"] = *shares;
        return;
    }
    const auto n = number(value);
    if (!n)
        return;
    if (keys.contains(name))
        metrics[keys.value(name)] = *n;
    else if (milliseconds.contains(name))
        metrics[milliseconds.value(name)] = *n / 1000.0;
}

// Google's reasons for an error: error.errors[].reason, error.details[].reason
// and error.status.
QStringList googleReasons(const QJsonObject &json) {
    const QJsonObject error = json.value("error").toObject();
    QStringList reasons{error.value("status").toString()};
    for (const char *list : {"errors", "details"})
        for (const QJsonValue &v : error.value(list).toArray())
            reasons << v.toObject().value("reason").toString();
    reasons.removeAll(QString());
    return reasons;
}

}  // namespace

Stats::Stats(InstagramApi *instagram, FacebookApi *facebook, YouTubeApi *youtube, QObject *parent)
    : QObject(parent), m_instagram(instagram), m_facebook(facebook), m_youtube(youtube),
      m_net(new QNetworkAccessManager(this)), m_touch(new QTimer(this)) {
    // A stalled request (no bytes for 2 min) fails instead of hanging forever.
    m_net->setTransferTimeout(120000);
    for (int i = 0; i < 3; ++i) {
        m_sides[i].platform = kPlatforms[i];
        m_accounts << emptyAccount(i);
    }
    m_touch->setSingleShot(true);
    m_touch->setInterval(250);
    connect(m_touch, &QTimer::timeout, this, &Stats::changed);
    for (Publisher *p : {static_cast<Publisher *>(instagram), static_cast<Publisher *>(facebook),
                         static_cast<Publisher *>(youtube)})
        if (p)
            connect(p, &Publisher::accountChanged, this, &Stats::publishersChanged);
    // After the constructor, so tests can set stateDir first.
    QTimer::singleShot(0, this, [this] {
        const bool fresh = !m_loaded;
        ensureLoaded();
        publishersChanged();
        if (fresh)
            emit changed();
    });
}

Publisher *Stats::publisher(int side) const {
    switch (side) {
    case Instagram: return m_instagram;
    case Facebook: return m_facebook;
    default: return m_youtube;
    }
}

QString Stats::statsFile() const { return Net::stateFile(stateDir, "stats.json"); }

QVariantList Stats::metricInfo() const {
    struct Info { const char *key, *unit; QString label; QStringList platforms; };
    const QStringList meta{"instagram", "facebook"}, all{"instagram", "facebook", "youtube"};
    const QVector<Info> infos{
        {"views", "count", tr("Views"), all},
        {"reach", "count", tr("Accounts reached"), meta},
        {"likes", "count", tr("Likes"), all},
        {"comments", "count", tr("Comments"), all},
        {"shares", "count", tr("Shares"), all},
        {"saves", "count", tr("Saves"), {"instagram"}},
        {"interactions", "count", tr("Total interactions"), {"instagram"}},
        {"replays", "count", tr("Replays"), {"facebook"}},
        {"initialPlays", "count", tr("Initial plays"), {"facebook"}},
        {"avgWatch", "seconds", tr("Avg watch time"), all},
        {"totalWatch", "seconds", tr("Total watch time"), all},
        {"avgViewPct", "percent", tr("Avg % viewed"), {"youtube"}},
        {"followsGained", "count", tr("Followers gained"), {"facebook", "youtube"}},
        {"subsLost", "count", tr("Subscribers lost"), {"youtube"}},
    };
    QVariantList out;
    for (const Info &i : infos)
        out << QVariantMap{{"key", i.key}, {"label", i.label}, {"unit", i.unit}, {"platforms", i.platforms}};
    return out;
}

QString Stats::status() const {
    if (!m_busy)
        return {};
    static const char *const names[] = {"Instagram", "Facebook", "YouTube"};
    QStringList parts;
    for (int i = 0; i < 3; ++i) {
        const Side &s = m_sides[i];
        if (!s.active)
            continue;
        if (!s.listed)
            parts << tr("Loading %1 reels (%2)…").arg(names[i]).arg(s.reels.size());
        else
            parts << tr("Loading %1 reels (%2/%3)…").arg(names[i]).arg(qMin(s.insightsDone, int(s.reels.size()))).arg(s.reels.size());
    }
    return parts.join(" · ");
}

void Stats::touch() {
    if (!m_touch->isActive())
        m_touch->start();
}

// ---- Storage ----

void Stats::ensureLoaded() const {
    if (m_loaded)
        return;
    m_loaded = true;
    // A corrupt or odd file is read as far as it makes sense, or not at all.
    const QJsonObject json = Net::load(statsFile());
    const qint64 refreshed = qint64(json.value("refreshed").toDouble());
    if (refreshed > 0)
        m_refreshed = QDateTime::fromMSecsSinceEpoch(refreshed);
    const QStringList platforms{kPlatforms[0], kPlatforms[1], kPlatforms[2]};
    auto numbersOnly = [](const QJsonObject &o) {
        QVariantMap out;
        for (auto it = o.begin(); it != o.end(); ++it)
            if (it.value().isDouble())
                out.insert(it.key(), it.value().toDouble());
        return out;
    };
    for (const QJsonValue &v : json.value("reels").toArray()) {
        const QJsonObject o = v.toObject();
        if (!platforms.contains(o.value("platform").toString()) || o.value("id").toString().isEmpty())
            continue;
        Reel reel;
        reel.id = o.value("id").toString();
        reel.caption = o.value("caption").toString();
        reel.title = o.value("title").toString();
        reel.thumbnail = o.value("thumbnail").toString();
        reel.permalink = o.value("permalink").toString();
        reel.privacyStatus = o.value("privacyStatus").toString();
        reel.published = qint64(o.value("published").toDouble());
        reel.duration = o.value("duration").toDouble();
        reel.metrics = numbersOnly(o.value("metrics").toObject());
        m_reels << reelMap(o.value("platform").toString(), reel);
    }
    for (const QJsonValue &v : json.value("accounts").toArray()) {
        const QJsonObject o = v.toObject();
        const int side = platforms.indexOf(o.value("platform").toString());
        if (side < 0)
            continue;
        QVariantMap account = emptyAccount(side);
        account["connected"] = o.value("connected").toBool();
        account["account"] = o.value("account").toString();
        if (o.value("followers").isDouble())
            account["followers"] = o.value("followers").toDouble();
        account["reelCount"] = o.value("reelCount").toInt();
        account["error"] = o.value("error").toString();
        account["needsPermission"] = o.value("needsPermission").toString();
        for (const char *series : {"followerSeries", "viewSeries"}) {
            QVariantList points;
            for (const QJsonValue &p : o.value(series).toArray()) {
                const QJsonObject point = p.toObject();
                if (point.value("t").isDouble() && point.value("v").isDouble())
                    points << QVariantMap{{"t", qint64(point.value("t").toDouble())}, {"v", point.value("v").toDouble()}};
            }
            account[series] = points;
        }
        m_accounts[side] = account;
    }
    const QJsonObject history = json.value("history").toObject();
    for (auto it = history.begin(); it != history.end(); ++it) {
        QVariantList snapshots;
        for (const QJsonValue &s : it.value().toArray()) {
            const QJsonObject o = s.toObject();
            if (o.value("t").isDouble() && o.value("metrics").isObject())
                snapshots << QVariantMap{{"t", qint64(o.value("t").toDouble())},
                                         {"metrics", numbersOnly(o.value("metrics").toObject())}};
        }
        if (!snapshots.isEmpty())
            m_history.insert(it.key(), snapshots.mid(qMax(0, int(snapshots.size()) - kMaxSnapshots)));
    }
}

void Stats::save() {
    QJsonObject history;
    for (auto it = m_history.begin(); it != m_history.end(); ++it)
        history.insert(it.key(), QJsonArray::fromVariantList(it.value()));
    Net::savePrivate(statsFile(), {{"version", 1},
                                   {"refreshed", double(m_refreshed.isValid() ? m_refreshed.toMSecsSinceEpoch() : 0)},
                                   {"reels", QJsonArray::fromVariantList(m_reels)},
                                   {"accounts", QJsonArray::fromVariantList(m_accounts)},
                                   {"history", history}});
}

QVariantList Stats::history(const QString &platform, const QString &id) const {
    ensureLoaded();
    return m_history.value(platform + ":" + id);
}

// A publisher connecting or disconnecting shows at once: a disconnected
// account's reels go, a connected one waits for the next refresh.
void Stats::publishersChanged() {
    if (!m_loaded)
        return;
    bool any = false;
    for (int i = 0; i < 3; ++i) {
        Publisher *p = publisher(i);
        const bool connected = p && p->connected();
        QVariantMap account = m_accounts[i].toMap();
        if (account.value("connected").toBool() == connected
            && (!connected || account.value("account").toString() == p->account()))
            continue;
        any = true;
        if (!connected) {
            m_accounts[i] = emptyAccount(i);
            QVariantList kept;
            for (const QVariant &r : std::as_const(m_reels))
                if (r.toMap().value("platform").toString() != kPlatforms[i])
                    kept << r;
            m_reels = kept;
        } else {
            account["connected"] = true;
            account["account"] = p->account();
            m_accounts[i] = account;
        }
    }
    if (any)
        emit changed();
}

QVariantMap Stats::emptyAccount(int side) const {
    static const char *const labels[] = {"Instagram", "Facebook", "YouTube"};
    return {{"platform", kPlatforms[side]}, {"label", labels[side]}, {"connected", false}, {"account", QString()},
            {"reelCount", 0}, {"error", QString()}, {"needsPermission", QString()},
            {"followerSeries", QVariantList()}, {"viewSeries", QVariantList()}};
}

QVariantMap Stats::reelMap(const QString &platform, const Reel &reel) {
    QVariantMap m{{"platform", platform}, {"id", reel.id}, {"caption", reel.caption}, {"title", reel.title},
                  {"thumbnail", reel.thumbnail}, {"permalink", reel.permalink}, {"published", reel.published},
                  {"duration", reel.duration}, {"metrics", reel.metrics}, {"privacyStatus", reel.privacyStatus}};
    const double views = reel.metrics.value("views").toDouble();
    if (views > 0) {
        double engaged = 0;
        for (const char *key : {"likes", "comments", "shares", "saves"})
            engaged += reel.metrics.value(key).toDouble();
        m["engagementRate"] = qBound(0.0, engaged / views, 1.0);
    }
    return m;
}

// ---- Refreshing ----

void Stats::refresh() {
    ensureLoaded();
    if (m_busy)
        return;
    m_busy = true;
    ++m_run;
    for (int i = 0; i < 3; ++i) {
        Side fresh;
        fresh.platform = kPlatforms[i];
        fresh.active = true;
        m_sides[i] = fresh;
    }
    emit changed();
    startInstagram();
    startFacebook();
    startYouTube();
}

void Stats::call(int side, QNetworkReply *reply, const std::function<void(const Net::Response &)> &then) {
    if (!reply)
        return;
    ++m_sides[side].pending;
    const int run = m_run;
    Net::whenDone(reply, this, [this, side, run, then](const Net::Response &r) {
        if (run != m_run)
            return;
        then(r);
        --m_sides[side].pending;
        touch();
        settle(side);
    });
}

void Stats::limited(int side, const std::function<QNetworkReply *()> &make,
                    const std::function<void(const Net::Response &)> &then) {
    m_sides[side].queue << [this, side, make, then] {
        if (m_sides[side].stopInsights)
            return;
        ++m_sides[side].running;
        call(side, make(), [this, side, then](const Net::Response &r) {
            --m_sides[side].running;
            if (!m_sides[side].stopInsights)
                then(r);
        });
    };
}

void Stats::stopInsights(int side) {
    m_sides[side].stopInsights = true;
    m_sides[side].queue.clear();
}

void Stats::settle(int side) {
    Side &s = m_sides[side];
    while (s.running < kSlots && !s.queue.isEmpty())
        s.queue.takeFirst()();
    if (s.active && s.pending == 0 && s.queue.isEmpty())
        finishSide(side);
}

QNetworkReply *Stats::graphGet(int side, const QString &pathOrUrl, const QList<QPair<QString, QString>> &query) {
    QString base, token;
    if (side == Instagram && m_instagram) {
        base = m_instagram->apiBase();
        token = m_instagram->token();
    } else if (side == Facebook && m_facebook) {
        base = m_facebook->apiBase();
        token = m_facebook->pageToken();
    }
    // A paging.next address is used as given; it carries its own token.
    QUrl url(pathOrUrl.startsWith("http") ? pathOrUrl : base + pathOrUrl);
    QUrlQuery q(url);
    for (const auto &item : query)
        q.addQueryItem(item.first, item.second);
    if (!q.hasQueryItem("access_token"))
        q.addQueryItem("access_token", token);
    url.setQuery(q);
    return m_net->get(QNetworkRequest(url));
}

QNetworkReply *Stats::googleGet(int side, const QUrl &url) {
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + m_sides[side].youtubeToken.toUtf8());
    return m_net->get(request);
}

bool Stats::metaStops(int side, const Net::Response &r) {
    Side &s = m_sides[side];
    const QJsonObject error = r.json.value("error").toObject();
    const int code = error.value("code").toInt();
    const bool permission = (code >= 200 && code < 300) || r.error.contains("permission", Qt::CaseInsensitive);
    if (permission) {
        if (side == Instagram)
            // Tokens from Facebook Login name it differently from Instagram Login ones.
            s.needsPermission = m_instagram && m_instagram->token().startsWith("EAA") ? "instagram_manage_insights"
                                                                                      : "instagram_business_manage_insights";
        else
            s.needsPermission = "read_insights";
        stopInsights(side);
        return true;
    }
    if (code == 190) {
        s.error = tr("The token stopped working (%1). Reconnect the account in Settings → Connected accounts.").arg(r.error);
        stopInsights(side);
        return true;
    }
    if (code == 4 || code == 17 || code == 32 || code == 613) {
        s.error = tr("Meta is limiting requests right now, so some numbers are missing. Try again in an hour.");
        stopInsights(side);
        return true;
    }
    return false;
}

void Stats::finishSide(int side) {
    Side &s = m_sides[side];
    s.active = false;
    // Instagram gives followers gained per day; counted back from today's
    // total, they become the follower count on each day.
    if (s.followerSeries.isEmpty() && !s.followerGains.isEmpty() && s.followers >= 0) {
        std::sort(s.followerGains.begin(), s.followerGains.end(), [](const Point &a, const Point &b) { return a.t < b.t; });
        double total = s.followers;
        QVector<Point> series(s.followerGains.size());
        for (int i = s.followerGains.size() - 1; i >= 0; --i) {
            series[i] = {s.followerGains[i].t, total};
            total -= s.followerGains[i].v;
        }
        s.followerSeries = series;
    }
    touch();
    for (const Side &other : m_sides)
        if (other.active)
            return;
    finishAll();
}

QVariantMap Stats::accountFor(int side) const {
    const Side &s = m_sides[side];
    Publisher *p = publisher(side);
    QVariantMap account = emptyAccount(side);
    if (!p || !p->connected())
        return account;
    account["connected"] = true;
    account["account"] = p->account();
    if (s.followers >= 0)
        account["followers"] = s.followers;
    account["reelCount"] = int(s.reels.size());
    account["error"] = s.error;
    account["needsPermission"] = s.needsPermission;
    auto list = [](QVector<Point> points) {
        std::sort(points.begin(), points.end(), [](const Point &a, const Point &b) { return a.t < b.t; });
        QVariantList out;
        for (const Point &point : points)
            out << QVariantMap{{"t", point.t}, {"v", point.v}};
        return out;
    };
    account["followerSeries"] = list(s.followerSeries);
    account["viewSeries"] = list(s.viewSeries);
    return account;
}

void Stats::finishAll() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVariantList reels;
    QVariantList accounts;
    for (int i = 0; i < 3; ++i) {
        const Side &s = m_sides[i];
        accounts << accountFor(i);
        Publisher *p = publisher(i);
        if (!p || !p->connected())
            continue;
        // The list itself didn't come back: keep showing the last numbers.
        if (!s.listed) {
            for (const QVariant &r : std::as_const(m_reels))
                if (r.toMap().value("platform").toString() == s.platform)
                    reels << r;
            continue;
        }
        for (const Reel &reel : s.reels) {
            reels << reelMap(s.platform, reel);
            if (reel.metrics.isEmpty())
                continue;
            // A snapshot only when something moved, so the history is growth.
            QVariantList &snapshots = m_history[s.platform + ":" + reel.id];
            if (!snapshots.isEmpty() && snapshots.last().toMap().value("metrics").toMap() == reel.metrics)
                continue;
            snapshots << QVariantMap{{"t", now}, {"metrics", reel.metrics}};
            while (snapshots.size() > kMaxSnapshots)
                snapshots.removeFirst();
        }
    }
    std::stable_sort(reels.begin(), reels.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("published").toLongLong() > b.toMap().value("published").toLongLong();
    });
    m_reels = reels;
    m_accounts = accounts;
    m_refreshed = QDateTime::fromMSecsSinceEpoch(now);
    m_busy = false;
    m_touch->stop();
    save();
    emit changed();
}

// ---- Instagram ----

void Stats::startInstagram() {
    if (!m_instagram || !m_instagram->connected() || m_instagram->userId().isEmpty())
        return finishSide(Instagram);
    const QString user = "/" + m_instagram->userId();
    call(Instagram, graphGet(Instagram, user, {{"fields", "followers_count,media_count"}}), [this](const Net::Response &r) {
        if (const auto followers = number(r.json.value("followers_count")))
            m_sides[Instagram].followers = *followers;
    });
    const QDateTime now = QDateTime::currentDateTimeUtc();
    call(Instagram,
         graphGet(Instagram, user + "/insights",
                  {{"metric", "follower_count,reach"}, {"period", "day"},
                   {"since", QString::number(now.addDays(-30).toSecsSinceEpoch())},
                   {"until", QString::number(now.toSecsSinceEpoch())}}),
         [this](const Net::Response &r) {
             Side &s = m_sides[Instagram];
             if (!r.error.isEmpty()) {
                 const int code = r.json.value("error").toObject().value("code").toInt();
                 if ((code >= 200 && code < 300) || r.error.contains("permission", Qt::CaseInsensitive))
                     s.needsPermission = m_instagram && m_instagram->token().startsWith("EAA")
                                             ? "instagram_manage_insights" : "instagram_business_manage_insights";
                 return;
             }
             const auto series = insightSeries(r.json);
             for (const auto &p : series.value("follower_count"))
                 s.followerGains << Point{p.first, p.second};
             for (const auto &p : series.value("reach"))
                 s.viewSeries << Point{p.first, p.second};
         });
    listInstagram(QUrl(m_instagram->apiBase() + user + "/media"));
}

void Stats::listInstagram(const QUrl &url) {
    Side &side = m_sides[Instagram];
    ++side.pages;
    const bool first = side.pages == 1;
    QList<QPair<QString, QString>> query;
    if (first)
        query = {{"fields", "id,caption,media_type,media_product_type,permalink,thumbnail_url,media_url,timestamp,"
                            "like_count,comments_count"},
                 {"limit", "50"}};
    call(Instagram, graphGet(Instagram, url.toString(), query), [this](const Net::Response &r) {
        Side &s = m_sides[Instagram];
        if (!r.error.isEmpty()) {
            if (s.pages == 1) {
                s.error = tr("Couldn't list your Instagram reels: %1").arg(r.error);
                return;
            }
            // A later page failing: show what came back.
        } else {
            for (const QJsonValue &v : r.json.value("data").toArray()) {
                const QJsonObject o = v.toObject();
                const QString id = o.value("id").toString();
                if (o.value("media_product_type").toString() != "REELS" || id.isEmpty() || s.index.contains(id)
                    || s.reels.size() >= kMaxReels)
                    continue;
                Reel reel;
                reel.id = id;
                reel.caption = o.value("caption").toString();
                reel.thumbnail = o.value("thumbnail_url").toString();
                reel.permalink = o.value("permalink").toString();
                reel.published = parseTime(o.value("timestamp").toString());
                // What the list says, kept even if insights can't be read.
                if (const auto likes = number(o.value("like_count")))
                    reel.metrics["likes"] = *likes;
                if (const auto comments = number(o.value("comments_count")))
                    reel.metrics["comments"] = *comments;
                s.index.insert(id, s.reels.size());
                s.reels << reel;
            }
            const QUrl next(r.json.value("paging").toObject().value("next").toString());
            if (r.error.isEmpty() && next.isValid() && next.scheme().startsWith("http") && s.reels.size() < kMaxReels
                && s.pages < kMaxPages)
                return listInstagram(next);
        }
        s.listed = true;
        for (const Reel &reel : std::as_const(s.reels))
            instagramInsights(reel.id, kInstagramMetrics, false);
    });
}

void Stats::instagramInsights(const QString &reel, const QStringList &wanted, bool single) {
    limited(
        Instagram,
        [this, reel, wanted] {
            QStringList metrics;
            for (const QString &m : wanted)
                if (!m_sides[Instagram].failing.contains(m))
                    metrics << m;
            // Every metric is known not to work: nothing to ask.
            if (metrics.isEmpty())
                metrics = wanted;
            return graphGet(Instagram, "/" + reel + "/insights", {{"metric", metrics.join(',')}});
        },
        [this, reel, wanted, single](const Net::Response &r) {
            Side &s = m_sides[Instagram];
            if (!single)
                ++s.insightsDone;
            const int at = s.index.value(reel, -1);
            if (at < 0)
                return;
            if (r.error.isEmpty()) {
                const auto values = insightValues(r.json);
                for (auto it = values.begin(); it != values.end(); ++it)
                    applyMeta(s.reels[at].metrics, it.key(), it.value());
                return;
            }
            if (metaStops(Instagram, r))
                return;
            const int code = r.json.value("error").toObject().value("code").toInt();
            if (single) {
                s.failing.insert(wanted.value(0));
                return;
            }
            // (#10) without "permission": this reel has no insights (posted
            // before the account turned professional, or too few viewers).
            if (code == 10)
                return;
            // One metric Meta won't give spoils the whole call: ask one at a time.
            for (const QString &m : wanted)
                if (!s.failing.contains(m))
                    instagramInsights(reel, {m}, true);
        });
}

// ---- Facebook ----

void Stats::startFacebook() {
    if (!m_facebook || !m_facebook->connected() || m_facebook->pageId().isEmpty())
        return finishSide(Facebook);
    const QString page = "/" + m_facebook->pageId();
    call(Facebook, graphGet(Facebook, page, {{"fields", "followers_count,fan_count"}}), [this](const Net::Response &r) {
        const auto followers = number(r.json.value("followers_count"));
        const auto fans = number(r.json.value("fan_count"));
        if (followers || fans)
            m_sides[Facebook].followers = followers ? *followers : *fans;
    });
    facebookSeries({"page_follows", "page_video_views"});
    listFacebook(QUrl(m_facebook->apiBase() + page + "/video_reels"));
}

void Stats::facebookSeries(const QStringList &metrics) {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    call(Facebook,
         graphGet(Facebook, "/" + (m_facebook ? m_facebook->pageId() : QString()) + "/insights",
                  {{"metric", metrics.join(',')}, {"period", "day"},
                   {"since", QString::number(now.addDays(-30).toSecsSinceEpoch())},
                   {"until", QString::number(now.toSecsSinceEpoch())}}),
         [this, metrics](const Net::Response &r) {
             Side &s = m_sides[Facebook];
             if (!r.error.isEmpty()) {
                 const int code = r.json.value("error").toObject().value("code").toInt();
                 if ((code >= 200 && code < 300) || r.error.contains("permission", Qt::CaseInsensitive))
                     s.needsPermission = "read_insights";
                 // Meta retires Page metrics now and then: whichever still work.
                 else if (metrics.size() > 1)
                     for (const QString &m : metrics)
                         facebookSeries({m});
                 return;
             }
             const auto series = insightSeries(r.json);
             // page_follows is the total on each day.
             for (const auto &p : series.value("page_follows"))
                 s.followerSeries << Point{p.first, p.second};
             for (const auto &p : series.value("page_video_views"))
                 s.viewSeries << Point{p.first, p.second};
         });
}

void Stats::listFacebook(const QUrl &url) {
    Side &side = m_sides[Facebook];
    ++side.pages;
    QList<QPair<QString, QString>> query;
    if (side.pages == 1)
        query = {{"fields", "id,description,title,created_time,permalink_url,length,picture"}, {"limit", "50"}};
    call(Facebook, graphGet(Facebook, url.toString(), query), [this](const Net::Response &r) {
        Side &s = m_sides[Facebook];
        if (!r.error.isEmpty()) {
            if (s.pages == 1) {
                s.error = tr("Couldn't list your Facebook reels: %1").arg(r.error);
                return;
            }
        } else {
            for (const QJsonValue &v : r.json.value("data").toArray()) {
                const QJsonObject o = v.toObject();
                const QString id = o.value("id").toString();
                if (id.isEmpty() || s.index.contains(id) || s.reels.size() >= kMaxReels)
                    continue;
                Reel reel;
                reel.id = id;
                reel.caption = o.value("description").toString();
                reel.title = o.value("title").toString();
                reel.thumbnail = o.value("picture").toString();
                reel.permalink = o.value("permalink_url").toString();
                if (reel.permalink.startsWith('/'))
                    reel.permalink = "https://www.facebook.com" + reel.permalink;
                reel.published = parseTime(o.value("created_time").toString());
                reel.duration = number(o.value("length")).value_or(0);
                s.index.insert(id, s.reels.size());
                s.reels << reel;
            }
            const QUrl next(r.json.value("paging").toObject().value("next").toString());
            if (next.isValid() && next.scheme().startsWith("http") && s.reels.size() < kMaxReels && s.pages < kMaxPages)
                return listFacebook(next);
        }
        s.listed = true;
        for (const Reel &reel : std::as_const(s.reels))
            facebookInsights(reel.id, kFacebookMetrics, false);
    });
}

void Stats::facebookInsights(const QString &reel, const QStringList &wanted, bool single) {
    limited(
        Facebook,
        [this, reel, wanted] {
            QStringList metrics;
            for (const QString &m : wanted)
                if (!m_sides[Facebook].failing.contains(m))
                    metrics << m;
            if (metrics.isEmpty())
                metrics = wanted;
            return graphGet(Facebook, "/" + reel + "/video_insights", {{"metric", metrics.join(',')}});
        },
        [this, reel, wanted, single](const Net::Response &r) {
            Side &s = m_sides[Facebook];
            if (!single)
                ++s.insightsDone;
            const int at = s.index.value(reel, -1);
            if (at < 0)
                return;
            if (r.error.isEmpty()) {
                const auto values = insightValues(r.json);
                for (auto it = values.begin(); it != values.end(); ++it)
                    applyMeta(s.reels[at].metrics, it.key(), it.value());
                return;
            }
            if (metaStops(Facebook, r))
                return;
            if (single) {
                s.failing.insert(wanted.value(0));
                return;
            }
            for (const QString &m : wanted)
                if (!s.failing.contains(m))
                    facebookInsights(reel, {m}, true);
        });
}

// ---- YouTube ----

void Stats::startYouTube() {
    if (!m_youtube || !m_youtube->connected())
        return finishSide(YouTube);
    // Held open while the token is renewed, which isn't a call of ours.
    ++m_sides[YouTube].pending;
    const int run = m_run;
    m_youtube->freshAccess(this, [this, run](const QString &token, const QString &error) {
        if (run != m_run)
            return;
        Side &s = m_sides[YouTube];
        if (!error.isEmpty() || !m_youtube) {
            s.error = error;
        } else {
            s.youtubeToken = token;
            call(YouTube, googleGet(YouTube, QUrl(m_youtube->apiBase + "/channels?part=snippet,contentDetails,statistics&mine=true")),
                 [this](const Net::Response &r) {
                     Side &s = m_sides[YouTube];
                     const QJsonObject channel = r.json.value("items").toArray().at(0).toObject();
                     s.uploads = channel.value("contentDetails").toObject().value("relatedPlaylists").toObject()
                                     .value("uploads").toString();
                     const QJsonObject statistics = channel.value("statistics").toObject();
                     if (!statistics.value("hiddenSubscriberCount").toBool())
                         if (const auto subscribers = number(statistics.value("subscriberCount")))
                             s.followers = *subscribers;
                     if (!r.error.isEmpty() || s.uploads.isEmpty()) {
                         s.error = tr("Couldn't read your YouTube channel: %1")
                                       .arg(r.error.isEmpty() ? tr("it has no uploads list") : r.error);
                         return;
                     }
                     youtubeChannelSeries();
                     listYouTube({});
                 });
        }
        --s.pending;
        settle(YouTube);
    });
}

void Stats::listYouTube(const QString &pageToken) {
    if (!m_youtube)
        return;
    ++m_sides[YouTube].pages;
    QUrl url(m_youtube->apiBase + "/playlistItems");
    QUrlQuery q{{"part", "contentDetails"}, {"playlistId", m_sides[YouTube].uploads}, {"maxResults", "50"}};
    if (!pageToken.isEmpty())
        q.addQueryItem("pageToken", pageToken);
    url.setQuery(q);
    call(YouTube, googleGet(YouTube, url), [this](const Net::Response &r) {
        Side &s = m_sides[YouTube];
        // A channel that never uploaded has no uploads playlist yet.
        if (!r.error.isEmpty() && r.status != 404) {
            if (s.pages == 1)
                s.error = tr("Couldn't list your YouTube videos: %1").arg(r.error);
            else
                s.listed = true;
            if (s.listed)
                youtubeAnalytics();
            return;
        }
        QStringList ids;
        for (const QJsonValue &v : r.json.value("items").toArray()) {
            const QString id = v.toObject().value("contentDetails").toObject().value("videoId").toString();
            if (!id.isEmpty() && !ids.contains(id))
                ids << id;
        }
        const QString next = r.json.value("nextPageToken").toString();
        auto more = [this, next] {
            Side &s = m_sides[YouTube];
            if (!next.isEmpty() && s.reels.size() < kMaxReels && s.pages < kMaxPages)
                return listYouTube(next);
            s.listed = true;
            youtubeAnalytics();
        };
        if (ids.isEmpty() || !m_youtube)
            return more();
        QUrl videos(m_youtube->apiBase + "/videos");
        videos.setQuery(QUrlQuery{{"part", "snippet,statistics,contentDetails,status"}, {"id", ids.join(',')}, {"maxResults", "50"}});
        call(YouTube, googleGet(YouTube, videos), [this, more](const Net::Response &v) {
            Side &s = m_sides[YouTube];
            for (const QJsonValue &item : v.json.value("items").toArray()) {
                const QJsonObject o = item.toObject();
                const QString id = o.value("id").toString();
                const double duration = isoDuration(o.value("contentDetails").toObject().value("duration").toString());
                // Shorts are up to 3 minutes; the API has no other way to tell.
                if (id.isEmpty() || s.index.contains(id) || duration <= 0 || duration > 180 || s.reels.size() >= kMaxReels)
                    continue;
                const QJsonObject snippet = o.value("snippet").toObject();
                const QJsonObject thumbs = snippet.value("thumbnails").toObject();
                Reel reel;
                reel.id = id;
                reel.title = snippet.value("title").toString();
                reel.caption = snippet.value("description").toString();
                for (const char *size : {"high", "medium", "default"})
                    if (reel.thumbnail.isEmpty())
                        reel.thumbnail = thumbs.value(size).toObject().value("url").toString();
                reel.permalink = "https://youtube.com/shorts/" + id;
                reel.privacyStatus = o.value("status").toObject().value("privacyStatus").toString();
                reel.published = parseTime(snippet.value("publishedAt").toString());
                reel.duration = duration;
                const QJsonObject statistics = o.value("statistics").toObject();
                const QList<QPair<const char *, const char *>> counts{
                    {"viewCount", "views"}, {"likeCount", "likes"}, {"commentCount", "comments"}};
                for (const auto &c : counts)
                    if (const auto n = number(statistics.value(c.first)))
                        reel.metrics[c.second] = *n;
                s.index.insert(id, s.reels.size());
                s.reels << reel;
            }
            more();
        });
    });
}

namespace {
// What a YouTube Analytics error means for Reelcut: the scope missing
// (connected before Reelcut asked for it), the API not turned on in the
// Google Cloud project, or something else.
enum class GoogleProblem { None, Scope, Disabled, Other };
GoogleProblem googleProblem(const Net::Response &r) {
    if (r.error.isEmpty())
        return GoogleProblem::None;
    const QStringList reasons = googleReasons(r.json);
    if (reasons.contains("insufficientPermissions") || reasons.contains("ACCESS_TOKEN_SCOPE_INSUFFICIENT")
        || r.error.contains("insufficient authentication scopes", Qt::CaseInsensitive))
        return GoogleProblem::Scope;
    if (reasons.contains("accessNotConfigured") || reasons.contains("SERVICE_DISABLED"))
        return GoogleProblem::Disabled;
    return GoogleProblem::Other;
}
}  // namespace

void Stats::youtubeAnalytics() {
    Side &side = m_sides[YouTube];
    side.insightsDone = 0;
    QStringList ids;
    for (const Reel &reel : std::as_const(side.reels))
        ids << reel.id;
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    for (int from = 0; from < ids.size(); from += 200) {
        const QStringList chunk = ids.mid(from, 200);
        QUrl url(youtubeAnalyticsBase + "/reports");
        url.setQuery(QUrlQuery{{"ids", "channel==MINE"}, {"startDate", "2005-01-01"}, {"endDate", today},
                               {"metrics", "views,estimatedMinutesWatched,averageViewDuration,averageViewPercentage,"
                                           "likes,comments,shares,subscribersGained,subscribersLost"},
                               {"dimensions", "video"}, {"filters", "video==" + chunk.join(',')},
                               {"maxResults", "200"}, {"sort", "-views"}});
        const int count = chunk.size();
        call(YouTube, googleGet(YouTube, url), [this, count](const Net::Response &r) {
            Side &s = m_sides[YouTube];
            s.insightsDone += count;
            switch (googleProblem(r)) {
            case GoogleProblem::None: break;
            case GoogleProblem::Scope: s.needsPermission = "yt-analytics.readonly"; return;
            case GoogleProblem::Disabled:
                s.error = tr("Turn on the YouTube Analytics API in your Google Cloud project to see watch time and shares.");
                return;
            case GoogleProblem::Other:
                s.error = tr("Couldn't read YouTube Analytics: %1").arg(r.error);
                return;
            }
            QStringList columns;
            for (const QJsonValue &h : r.json.value("columnHeaders").toArray())
                columns << h.toObject().value("name").toString();
            const int videoColumn = columns.indexOf("video");
            if (videoColumn < 0)
                return;
            for (const QJsonValue &row : r.json.value("rows").toArray()) {
                const QJsonArray cells = row.toArray();
                const int at = s.index.value(cells.at(videoColumn).toString(), -1);
                if (at < 0)
                    continue;
                QVariantMap &metrics = s.reels[at].metrics;
                for (int c = 0; c < columns.size() && c < cells.size(); ++c) {
                    const auto n = number(cells.at(c));
                    if (!n)
                        continue;
                    const QString &name = columns[c];
                    // The Data API's counts are live; Analytics' lag a day or two.
                    if ((name == "views" || name == "likes" || name == "comments") && !metrics.contains(name))
                        metrics[name] = *n;
                    else if (name == "estimatedMinutesWatched")
                        metrics["totalWatch"] = *n * 60;
                    else if (name == "averageViewDuration")
                        metrics["avgWatch"] = *n;
                    else if (name == "averageViewPercentage")
                        metrics["avgViewPct"] = *n;
                    else if (name == "shares")
                        metrics["shares"] = *n;
                    else if (name == "subscribersGained")
                        metrics["followsGained"] = *n;
                    else if (name == "subscribersLost")
                        metrics["subsLost"] = *n;
                }
            }
        });
    }
}

void Stats::youtubeChannelSeries() {
    const QDate today = QDate::currentDate();
    QUrl url(youtubeAnalyticsBase + "/reports");
    url.setQuery(QUrlQuery{{"ids", "channel==MINE"}, {"startDate", today.addDays(-90).toString(Qt::ISODate)},
                           {"endDate", today.toString(Qt::ISODate)},
                           {"metrics", "views,subscribersGained,subscribersLost"}, {"dimensions", "day"}, {"sort", "day"}});
    call(YouTube, googleGet(YouTube, url), [this](const Net::Response &r) {
        Side &s = m_sides[YouTube];
        if (googleProblem(r) == GoogleProblem::Scope)
            s.needsPermission = "yt-analytics.readonly";
        if (!r.error.isEmpty())
            return;  // the per-video call says what's wrong
        QStringList columns;
        for (const QJsonValue &h : r.json.value("columnHeaders").toArray())
            columns << h.toObject().value("name").toString();
        const int day = columns.indexOf("day"), views = columns.indexOf("views");
        const int gained = columns.indexOf("subscribersGained"), lost = columns.indexOf("subscribersLost");
        if (day < 0)
            return;
        QVector<Point> net;
        for (const QJsonValue &row : r.json.value("rows").toArray()) {
            const QJsonArray cells = row.toArray();
            const qint64 t = parseTime(cells.at(day).toString());
            if (t <= 0)
                continue;
            if (views >= 0)
                if (const auto n = number(cells.at(views)))
                    s.viewSeries << Point{t, *n};
            net << Point{t, number(cells.at(gained)).value_or(0) - number(cells.at(lost)).value_or(0)};
        }
        // Subscribers gained minus lost each day, counted back from today's total.
        if (s.followers >= 0 && !net.isEmpty()) {
            std::sort(net.begin(), net.end(), [](const Point &a, const Point &b) { return a.t < b.t; });
            double total = s.followers;
            QVector<Point> series(net.size());
            for (int i = net.size() - 1; i >= 0; --i) {
                series[i] = {net[i].t, total};
                total -= net[i].v;
            }
            s.followerSeries = series;
        }
    });
}

// ---- Per-day views of one reel ----

void Stats::loadDaily(const QString &platform, const QString &id) {
    ensureLoaded();
    // What Reelcut recorded: views at each refresh.
    auto recorded = [this, platform, id] {
        QVariantList points;
        for (const QVariant &s : m_history.value(platform + ":" + id)) {
            const QVariantMap snapshot = s.toMap();
            const QVariantMap metrics = snapshot.value("metrics").toMap();
            if (metrics.contains("views"))
                points << QVariantMap{{"t", snapshot.value("t")}, {"v", metrics.value("views").toDouble()}};
        }
        return points;
    };
    if (platform != "youtube" || !m_youtube || !m_youtube->connected()) {
        QTimer::singleShot(0, this, [this, platform, id, recorded] { emit dailyLoaded(platform, id, recorded()); });
        return;
    }
    QDate from = QDate::currentDate().addDays(-90);
    for (const QVariant &r : std::as_const(m_reels)) {
        const QVariantMap reel = r.toMap();
        if (reel.value("platform") == platform && reel.value("id") == id && reel.value("published").toLongLong() > 0)
            from = QDateTime::fromMSecsSinceEpoch(reel.value("published").toLongLong()).toUTC().date();
    }
    m_youtube->freshAccess(this, [this, platform, id, recorded, from](const QString &token, const QString &error) {
        if (!error.isEmpty() || !m_youtube)
            return emit dailyLoaded(platform, id, recorded());
        QUrl url(youtubeAnalyticsBase + "/reports");
        url.setQuery(QUrlQuery{{"ids", "channel==MINE"}, {"startDate", from.toString(Qt::ISODate)},
                               {"endDate", QDate::currentDate().toString(Qt::ISODate)}, {"metrics", "views"},
                               {"dimensions", "day"}, {"filters", "video==" + id}, {"sort", "day"}});
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        Net::whenDone(m_net->get(request), this, [this, platform, id, recorded](const Net::Response &r) {
            QStringList columns;
            for (const QJsonValue &h : r.json.value("columnHeaders").toArray())
                columns << h.toObject().value("name").toString();
            const int day = columns.indexOf("day"), views = columns.indexOf("views");
            if (!r.error.isEmpty() || day < 0 || views < 0)
                return emit dailyLoaded(platform, id, recorded());
            QVariantList points;
            for (const QJsonValue &row : r.json.value("rows").toArray()) {
                const QJsonArray cells = row.toArray();
                const qint64 t = parseTime(cells.at(day).toString());
                const auto v = number(cells.at(views));
                if (t > 0 && v)
                    points << QVariantMap{{"t", t}, {"v", *v}};
            }
            emit dailyLoaded(platform, id, points);
        });
    });
}
