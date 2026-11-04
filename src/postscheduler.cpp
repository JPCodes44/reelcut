#include "postscheduler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

PostScheduler::PostScheduler(QObject *parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {
    QTimer::singleShot(0, this, [this] {
        load();
        if (configured())
            refresh();
    });
}

QString PostScheduler::configFile() const {
    const QString dir = !stateDir.isEmpty()
                            ? stateDir
                            : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/reelcut";
    return dir + "/scheduler.json";
}

void PostScheduler::load() {
    QFile file(configFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    m_url = json.value("url").toString().trimmed();
    while (m_url.endsWith('/'))
        m_url.chop(1);
    m_key = json.value("key").toString().trimmed();
    emit configChanged();
}

void PostScheduler::configure(const QString &url, const QString &key) {
    QDir().mkpath(QFileInfo(configFile()).absolutePath());
    QSaveFile file(configFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{"url", url.trimmed()}, {"key", key.trimmed()}}).toJson());
        file.commit();
        // The key lets whoever has it post as you.
        QFile::setPermissions(configFile(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    load();
    refresh();
}

// One Convex function over its HTTP API: POST <url>/api/<query|mutation>.
void PostScheduler::call(const QString &kind, const QString &path, QJsonObject args, const Then &then) {
    if (!configured())
        return then(false, {}, tr("The scheduler isn't set up (%1).").arg(configFile()));
    args.insert("secret", m_key);
    QNetworkRequest request(QUrl(m_url + "/api/" + kind));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray body =
        QJsonDocument(QJsonObject{{"path", path}, {"args", args}, {"format", "json"}}).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_net->post(request, body);
    connect(reply, &QNetworkReply::finished, reply, [reply, then] {
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if (json.value("status").toString() == "success")
            return then(true, json.value("value"), {});
        // Our own errors (ConvexError) come as errorData; anything else as errorMessage.
        QString error = json.value("errorData").toString();
        if (error.isEmpty())
            error = json.value("errorMessage").toString();
        if (error.isEmpty())
            error = reply->error() != QNetworkReply::NoError ? reply->errorString() : tr("no answer from the scheduler");
        then(false, {}, error);
    });
}

void PostScheduler::schedule(const QString &file, const QString &title, const QString &caption, const QDateTime &when,
                             const Account &account) {
    if (m_busy)
        return;
    auto finish = [this](bool ok, const QString &message) {
        m_busy = false;
        emit scheduled(ok, message);
    };
    if (!configured())
        return finish(false, tr("The scheduler isn't set up yet."));
    if (account.userId.isEmpty() || account.token.isEmpty())
        return finish(false, tr("Connect your Instagram account first."));
    if (!when.isValid())
        return finish(false, tr("Pick a time to post."));
    m_busy = true;
    emit progress(tr("Getting ready to upload…"), -1);
    call("mutation", "posts:uploadUrl", {}, [=, this](bool ok, const QJsonValue &value, const QString &error) {
        if (!ok)
            return finish(false, tr("The scheduler didn't answer: %1").arg(error));
        auto *source = new QFile(file, this);
        if (!source->open(QIODevice::ReadOnly)) {
            delete source;
            return finish(false, tr("Couldn't read %1.").arg(file));
        }
        QNetworkRequest request(QUrl(value.toString()));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "video/mp4");
        QNetworkReply *upload = m_net->post(request, source);
        source->setParent(upload);
        connect(upload, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
            if (total > 0)
                emit progress(tr("Uploading the video…"), double(sent) / total);
        });
        connect(upload, &QNetworkReply::finished, this, [=, this] {
            const QJsonObject json = QJsonDocument::fromJson(upload->readAll()).object();
            const QString storageId = json.value("storageId").toString();
            const QString uploadError = upload->error() != QNetworkReply::NoError ? upload->errorString() : QString();
            upload->deleteLater();
            if (storageId.isEmpty())
                return finish(false, tr("The upload didn't go through: %1")
                                         .arg(uploadError.isEmpty() ? tr("no file id came back") : uploadError));
            emit progress(tr("Scheduling…"), 1.0);
            const QJsonObject args{{"storageId", storageId},
                                   {"title", title},
                                   {"caption", caption},
                                   {"publishAt", double(when.toMSecsSinceEpoch())},
                                   {"igUserId", account.userId},
                                   {"username", account.username},
                                   {"token", account.token},
                                   {"apiBase", account.apiBase}};
            call("mutation", "posts:schedule", args, [=, this](bool ok, const QJsonValue &, const QString &error) {
                if (!ok)
                    return finish(false, tr("Couldn't schedule it: %1").arg(error));
                finish(true, tr("Scheduled for %1. It posts even if Reelcut is closed.")
                                 .arg(QLocale().toString(when.toLocalTime(), "dddd, MMMM d 'at' h:mm AP")));
                refresh();
            });
        });
    });
}

void PostScheduler::refresh() {
    call("query", "posts:list", {}, [this](bool ok, const QJsonValue &value, const QString &error) {
        m_lastError = ok ? QString() : error;
        if (ok)
            m_posts = value.toArray().toVariantList();
        emit postsChanged();
    });
}

void PostScheduler::change(const QString &path, QJsonObject args, const QString &done) {
    call("mutation", path, args, [this, done](bool ok, const QJsonValue &, const QString &error) {
        emit changed(ok, ok ? done : error);
        refresh();
    });
}

void PostScheduler::cancel(const QString &id) { change("posts:cancel", {{"id", id}}, tr("Cancelled.")); }

void PostScheduler::reschedule(const QString &id, const QDateTime &when) {
    change("posts:reschedule", {{"id", id}, {"publishAt", double(when.toMSecsSinceEpoch())}}, tr("Moved."));
}

void PostScheduler::retry(const QString &id, const QString &token) {
    QJsonObject args{{"id", id}};
    if (!token.isEmpty())
        args.insert("token", token);
    change("posts:retry", args, tr("Trying again now."));
}

void PostScheduler::remove(const QString &id) { change("posts:remove", {{"id", id}}, tr("Removed.")); }
