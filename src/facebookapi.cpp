#include "facebookapi.h"

#include <QFile>
#include <QJsonArray>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

FacebookApi::FacebookApi(QObject *parent) : Publisher(parent), m_net(new QNetworkAccessManager(this)) {
    // After the constructor, so tests can set stateDir first.
    QTimer::singleShot(0, this, [this] { load(); });
}

QString FacebookApi::accountFile() const { return Net::stateFile(stateDir, "facebook.json"); }

QString FacebookApi::base() const {
    return graphBase.isEmpty() ? QStringLiteral("https://graph.facebook.com/v26.0") : graphBase;
}

void FacebookApi::load() {
    const QJsonObject json = Net::load(accountFile());
    m_pageId = json.value("pageId").toString();
    m_pageName = json.value("pageName").toString();
    m_pageToken = json.value("pageToken").toString();
    emit accountChanged();
}

void FacebookApi::keep(const Page &page) {
    m_pageId = page.id;
    m_pageName = page.name;
    m_pageToken = page.token;
    m_candidates.clear();
    Net::savePrivate(accountFile(), {{"pageId", page.id}, {"pageName", page.name}, {"pageToken", page.token}});
    emit accountChanged();
    emit connectFinished(true, tr("Connected to the Facebook Page %1.").arg(page.name));
}

QNetworkReply *FacebookApi::get(const QString &path, const QList<QPair<QString, QString>> &query, const QString &token) {
    QUrl url(base() + path);
    QUrlQuery q;
    for (const auto &item : query)
        q.addQueryItem(item.first, item.second);
    q.addQueryItem("access_token", token);
    url.setQuery(q);
    return m_net->get(QNetworkRequest(url));
}

QNetworkReply *FacebookApi::postForm(const QString &path, const QList<QPair<QString, QString>> &form) {
    QNetworkRequest request(QUrl(base() + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    return m_net->post(request, Net::formBody(form + QList<QPair<QString, QString>>{{"access_token", m_pageToken}}));
}

void FacebookApi::connectToken(const QString &userToken) {
    Net::whenDone(get("/me/accounts", {{"fields", "id,name,access_token"}, {"limit", "100"}}, userToken.trimmed()),
                  [this](const Net::Response &r) {
        static const QRegularExpression pageId("^[0-9]+$");
        QVector<Page> pages;
        for (const QJsonValue &v : r.json.value("data").toArray()) {
            const QJsonObject o = v.toObject();
            const Page page{o.value("id").toString(), o.value("name").toString(), o.value("access_token").toString()};
            if (pageId.match(page.id).hasMatch() && !page.token.isEmpty())
                pages << page;
        }
        QString reason = r.error;
        if (reason.isEmpty() && pages.isEmpty())
            reason = tr("no Page came back. In your Meta app, add the pages_show_list, pages_read_engagement and "
                        "pages_manage_posts permissions, and pick your Page when you generate the token.");
        if (!reason.isEmpty()) {
            emit connectFinished(false, tr("Facebook didn't accept that token: %1").arg(reason));
            return;
        }
        if (pages.size() == 1)
            return keep(pages.first());
        m_candidates = pages;
        QVariantList offered;
        for (const Page &p : pages)
            offered << QVariantMap{{"id", p.id}, {"name", p.name}};
        emit pagesFound(offered);
    });
}

void FacebookApi::choosePage(const QString &id) {
    for (const Page &p : m_candidates)
        if (p.id == id)
            return keep(p);
    emit connectFinished(false, tr("That Page isn't one the token manages."));
}

void FacebookApi::disconnect() {
    QFile::remove(accountFile());
    m_pageId.clear();
    m_pageName.clear();
    m_pageToken.clear();
    emit accountChanged();
}

void FacebookApi::publish(const PostRequest &post) {
    if (m_busy)
        return;
    if (!connected())
        return finish(false, tr("Connect a Facebook Page first."));
    m_busy = true;
    emit progress(tr("Starting the reel on Facebook…"), 0.0);
    Net::whenDone(postForm("/" + m_pageId + "/video_reels", {{"upload_phase", "start"}}), [this, post](const Net::Response &r) {
        const QString video = r.json.value("video_id").toString();
        QUrl uri(r.json.value("upload_url").toString());
        if (!uri.isValid() || uri.isEmpty())
            uri = QUrl("https://rupload.facebook.com/video-upload/v26.0/" + video);
        if (!r.error.isEmpty() || video.isEmpty())
            return finish(false, tr("Facebook wouldn't start the reel: %1")
                                     .arg(r.error.isEmpty() ? tr("no video id came back") : r.error));
        upload(video, uri, post);
    });
}

void FacebookApi::upload(const QString &video, const QUrl &uri, const PostRequest &post) {
    auto *source = new QFile(post.file, this);
    if (!source->open(QIODevice::ReadOnly)) {
        delete source;
        return finish(false, tr("Couldn't read %1.").arg(post.file));
    }
    QNetworkRequest request(uri);
    request.setRawHeader("Authorization", "OAuth " + m_pageToken.toUtf8());
    request.setRawHeader("offset", "0");
    request.setRawHeader("file_size", QByteArray::number(source->size()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    QNetworkReply *reply = m_net->post(request, source);
    source->setParent(reply);
    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0)
            emit progress(tr("Uploading the video to Facebook…"), double(sent) / total);
    });
    Net::whenDone(reply, [this, video, post](const Net::Response &r) {
        if (!r.error.isEmpty() || !r.json.value("success").toBool())
            return finish(false, tr("The upload to Facebook didn't go through: %1")
                                     .arg(r.error.isEmpty() ? tr("refused") : r.error));
        finishUpload(video, post);
    });
}

void FacebookApi::finishUpload(const QString &video, const PostRequest &post) {
    emit progress(post.when.isValid() ? tr("Scheduling on Facebook…") : tr("Publishing on Facebook…"), 1.0);
    QList<QPair<QString, QString>> form{{"upload_phase", "finish"}, {"video_id", video}, {"description", post.text}};
    if (post.when.isValid()) {
        form << qMakePair(QString("video_state"), QString("SCHEDULED"))
             << qMakePair(QString("scheduled_publish_time"), QString::number(post.when.toSecsSinceEpoch()));
    } else {
        form << qMakePair(QString("video_state"), QString("PUBLISHED"));
    }
    Net::whenDone(postForm("/" + m_pageId + "/video_reels", form), [this, video, post](const Net::Response &r) {
        if (!r.error.isEmpty() || !r.json.value("success").toBool())
            return finish(false, tr("Facebook didn't publish it: %1").arg(r.error.isEmpty() ? tr("refused") : r.error));
        waitForPublishing(video, post, 0);
    });
}

void FacebookApi::waitForPublishing(const QString &video, const PostRequest &post, int tries) {
    emit progress(tr("Facebook is processing the video…"), -1);
    Net::whenDone(get("/" + video, {{"fields", "status"}}, m_pageToken), [this, video, post, tries](const Net::Response &r) {
        if (!r.error.isEmpty())
            return finish(false, tr("Couldn't check on the Facebook upload: %1").arg(r.error));
        const QJsonObject status = r.json.value("status").toObject();
        const QString videoStatus = status.value("video_status").toString();
        const QJsonObject processing = status.value("processing_phase").toObject();
        const QJsonObject publishing = status.value("publishing_phase").toObject();
        const QString published = publishing.value("publish_status").toString();
        if (videoStatus == "error" || videoStatus == "upload_failed" || videoStatus == "expired"
            || processing.value("status") == "error" || publishing.value("status") == "error") {
            const QString why = processing.value("error").toObject().value("message").toString(
                publishing.value("error").toObject().value("message").toString(videoStatus));
            return finish(false, tr("Facebook couldn't use the video: %1").arg(why));
        }
        if (published == "published" || published == "scheduled") {
            Net::whenDone(get("/" + video, {{"fields", "permalink_url"}}, m_pageToken), [this, video, post](const Net::Response &p) {
                QString link = p.json.value("permalink_url").toString();
                if (link.startsWith('/'))
                    link = "https://www.facebook.com" + link;
                if (link.isEmpty())
                    link = "https://www.facebook.com/reel/" + video;
                finish(true,
                       post.when.isValid()
                           ? tr("Scheduled on Facebook for %1.").arg(QLocale().toString(post.when.toLocalTime(), "ddd MMM d, h:mm AP"))
                           : tr("Posted to Facebook."),
                       QUrl(link));
            });
            return;
        }
        if (tries >= 200)
            return finish(false, tr("Facebook is taking too long to process the video."));
        QTimer::singleShot(pollIntervalMs, this, [this, video, post, tries] { waitForPublishing(video, post, tries + 1); });
    });
}

void FacebookApi::finish(bool ok, const QString &message, const QUrl &link) {
    m_busy = false;
    emit published(ok, message, link);
}
