#include "instagramapi.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

#include <functional>

namespace {

// The JSON a reply carried, and Instagram's error message if it's one.
QJsonObject readJson(QNetworkReply *reply, QString *error) {
    const QByteArray body = reply->readAll();
    const QJsonObject json = QJsonDocument::fromJson(body).object();
    const QString apiError = json.value("error").toObject().value("message").toString();
    if (!apiError.isEmpty())
        *error = apiError;
    else if (reply->error() != QNetworkReply::NoError)
        *error = reply->errorString();
    return json;
}

// Runs `then` once the reply is in, with the raw body too, and lets the reply go.
void whenDoneRaw(QNetworkReply *reply,
                 const std::function<void(const QJsonObject &, const QString &, const QByteArray &)> &then) {
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, then] {
        const QByteArray body = reply->readAll();
        const QJsonObject json = QJsonDocument::fromJson(body).object();
        QString error = json.value("error").toObject().value("message").toString();
        if (error.isEmpty() && reply->error() != QNetworkReply::NoError)
            error = reply->errorString();
        reply->deleteLater();
        then(json, error, body);
    });
}

// Runs `then` once the reply is in, and lets the reply go.
void whenDone(QNetworkReply *reply, const std::function<void(const QJsonObject &, const QString &)> &then) {
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, then] {
        QString error;
        const QJsonObject json = readJson(reply, &error);
        reply->deleteLater();
        then(json, error);
    });
}

}  // namespace

InstagramApi::InstagramApi(QObject *parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {
    QTimer::singleShot(0, this, [this] {
        load();
        refreshIfOld();
    });
}

QString InstagramApi::accountFile() const {
    const QString dir = !stateDir.isEmpty()
                            ? stateDir
                            : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/reelcut";
    return dir + "/instagram.json";
}

void InstagramApi::load() {
    QFile file(accountFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    m_token = json.value("token").toString();
    m_username = json.value("username").toString();
    m_userId = json.value("userId").toString();
    m_issued = QDateTime::fromString(json.value("issued").toString(), Qt::ISODate);
    emit accountChanged();
}

void InstagramApi::save(const QString &token, const QString &userId, const QString &username, const QDateTime &issued) {
    m_token = token;
    m_userId = userId;
    m_username = username;
    m_issued = issued;
    QDir().mkpath(QFileInfo(accountFile()).absolutePath());
    QSaveFile file(accountFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{"token", token}, {"userId", userId}, {"username", username},
                                             {"issued", issued.toString(Qt::ISODate)}})
                       .toJson());
        file.commit();
        // The token posts as you: only you may read it.
        QFile::setPermissions(accountFile(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    emit accountChanged();
}

bool InstagramApi::usesUnifiedLogin() const {
    // New Meta apps issue Facebook Graph tokens even when signing in with Instagram.
    return m_token.startsWith("EAA");
}

QString InstagramApi::apiBase() const {
    if (!graphBase.isEmpty())
        return graphBase;
    return usesUnifiedLogin() ? "https://graph.facebook.com/v26.0" : "https://graph.instagram.com";
}

QNetworkReply *InstagramApi::get(const QString &path, const QList<QPair<QString, QString>> &query) {
    QUrl url(apiBase() + path);
    QUrlQuery q;
    for (const auto &item : query)
        q.addQueryItem(item.first, item.second);
    if (!q.hasQueryItem("access_token"))
        q.addQueryItem("access_token", m_token);
    url.setQuery(q);
    return m_net->get(QNetworkRequest(url));
}

QNetworkReply *InstagramApi::postForm(const QString &path, const QList<QPair<QString, QString>> &form) {
    QUrlQuery q;
    for (const auto &item : form)
        q.addQueryItem(item.first, item.second);
    q.addQueryItem("access_token", m_token);
    QNetworkRequest request(QUrl(apiBase() + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    return m_net->post(request, q.query(QUrl::FullyEncoded).toUtf8());
}

void InstagramApi::connectToken(const QString &token) {
    const QString previous = m_token;
    m_token = token.trimmed();
    if (usesUnifiedLogin()) {
        whenDone(get("/me/assigned_instagram_accounts", {{"fields", "id,username"}}),
                 [this, previous](const QJsonObject &json, const QString &error) {
            const QJsonArray accounts = json.value("data").toArray();
            QString reason = error;
            if (reason.isEmpty() && accounts.isEmpty())
                reason = tr("This token has no Instagram account assigned. In Meta, share your Instagram account and allow basic access.");
            if (reason.isEmpty() && (accounts.size() != 1 || !json.value("paging").toObject().value("next").toString().isEmpty()))
                reason = tr("This token covers more than one Instagram account. Generate a token sharing only the account you want to use in Reelcut.");
            const QJsonObject account = accounts.isEmpty() ? QJsonObject{} : accounts.first().toObject();
            const QString id = account.value("id").toString();
            const QString username = account.value("username").toString();
            static const QRegularExpression accountId("^[0-9]+$");
            if (reason.isEmpty() && (!accountId.match(id).hasMatch() || username.isEmpty()))
                reason = tr("Meta didn't return a valid Instagram account ID and username.");
            if (!reason.isEmpty()) {
                m_token = previous;
                emit connectFinished(false, tr("Instagram didn't accept that token: %1").arg(reason));
                return;
            }
            save(m_token, id, username, QDateTime::currentDateTimeUtc());
            emit connectFinished(true, tr("Connected as @%1.").arg(m_username));
        });
        return;
    }
    whenDoneRaw(get("/me", {{"fields", "user_id,username"}}),
                [this, previous](const QJsonObject &json, const QString &error, const QByteArray &body) {
        if (!error.isEmpty() || json.value("username").toString().isEmpty()) {
            m_token = previous;
            emit connectFinished(false, tr("Instagram didn't accept that token: %1")
                                            .arg(error.isEmpty() ? tr("no account came back") : error));
            return;
        }
        // user_id is the Instagram id the publish calls take. It's 17 digits, more
        // than a JSON number keeps exactly, so it's read from the text itself.
        static const QRegularExpression idPattern("\"user_id\"\\s*:\\s*\"?(\\d+)");
        const QString userId = idPattern.match(QString::fromUtf8(body)).captured(1);
        if (userId.isEmpty()) {
            m_token = previous;
            emit connectFinished(false, tr("Instagram didn't say which account the token is for."));
            return;
        }
        save(m_token, userId, json.value("username").toString(), QDateTime::currentDateTimeUtc());
        emit connectFinished(true, tr("Connected as @%1.").arg(m_username));
    });
}

void InstagramApi::disconnect() {
    QFile::remove(accountFile());
    m_token.clear();
    m_userId.clear();
    m_username.clear();
    emit accountChanged();
}

void InstagramApi::refreshIfOld() {
    // A long-lived token can be renewed once it's a day old; doing it weekly
    // keeps it from running out (60 days) as long as Reelcut gets opened.
    if (usesUnifiedLogin() || m_token.isEmpty() || !m_issued.isValid() || m_issued.daysTo(QDateTime::currentDateTimeUtc()) < 7)
        return;
    whenDone(get("/refresh_access_token", {{"grant_type", "ig_refresh_token"}, {"access_token", m_token}}),
             [this](const QJsonObject &json, const QString &error) {
                 if (error.isEmpty() && !json.value("access_token").toString().isEmpty())
                     save(json.value("access_token").toString(), m_userId, m_username, QDateTime::currentDateTimeUtc());
             });
}

void InstagramApi::publish(const QString &file, const QString &caption) {
    if (m_busy)
        return;
    if (!connected())
        return finish(false, tr("Connect your Instagram account first."));
    m_busy = true;
    createContainer(file, caption);
}

void InstagramApi::createContainer(const QString &file, const QString &caption) {
    emit progress(tr("Starting the reel on Instagram…"), 0.0);
    whenDone(postForm("/" + m_userId + "/media", {{"media_type", "REELS"}, {"upload_type", "resumable"}, {"caption", caption}}),
             [this, file](const QJsonObject &json, const QString &error) {
                 const QString container = json.value("id").toString();
                 const QUrl uri(json.value("uri").toString());
                 if (!error.isEmpty() || container.isEmpty() || !uri.isValid())
                     return finish(false, tr("Instagram wouldn't start the reel: %1")
                                              .arg(error.isEmpty() ? tr("no upload address came back") : error));
                 upload(container, uri, file);
             });
}

void InstagramApi::upload(const QString &container, const QUrl &uri, const QString &file) {
    auto *source = new QFile(file, this);
    if (!source->open(QIODevice::ReadOnly)) {
        delete source;
        return finish(false, tr("Couldn't read %1.").arg(file));
    }
    QNetworkRequest request(uri);
    request.setRawHeader("Authorization", "OAuth " + m_token.toUtf8());
    request.setRawHeader("offset", "0");
    request.setRawHeader("file_size", QByteArray::number(source->size()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    QNetworkReply *reply = m_net->post(request, source);
    source->setParent(reply);
    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0)
            emit progress(tr("Uploading the video…"), double(sent) / total);
    });
    whenDone(reply, [this, container](const QJsonObject &json, const QString &error) {
        if (!error.isEmpty() || !json.value("success").toBool(true))
            return finish(false, tr("The upload didn't go through: %1").arg(error.isEmpty() ? tr("refused") : error));
        waitForProcessing(container, 0);
    });
}

void InstagramApi::waitForProcessing(const QString &container, int tries) {
    emit progress(tr("Instagram is processing the video…"), -1);
    whenDone(get("/" + container, {{"fields", "status_code,status"}}),
             [this, container, tries](const QJsonObject &json, const QString &error) {
                 const QString status = json.value("status_code").toString();
                 if (!error.isEmpty())
                     return finish(false, tr("Couldn't check on the upload: %1").arg(error));
                 if (status == "FINISHED")
                     return publishContainer(container);
                 if (status == "ERROR" || status == "EXPIRED")
                     return finish(false, tr("Instagram couldn't use the video: %1")
                                              .arg(json.value("status").toString(status)));
                 if (tries >= 200)  // ten minutes at the usual pace
                     return finish(false, tr("Instagram is taking too long to process the video."));
                 QTimer::singleShot(pollIntervalMs, this, [this, container, tries] { waitForProcessing(container, tries + 1); });
             });
}

void InstagramApi::publishContainer(const QString &container) {
    emit progress(tr("Publishing…"), 1.0);
    whenDone(postForm("/" + m_userId + "/media_publish", {{"creation_id", container}}), [this](const QJsonObject &json, const QString &error) {
        const QString media = json.value("id").toString();
        if (!error.isEmpty() || media.isEmpty())
            return finish(false, tr("Instagram didn't publish it: %1").arg(error.isEmpty() ? tr("no post came back") : error));
        whenDone(get("/" + media, {{"fields", "permalink"}}), [this](const QJsonObject &post, const QString &) {
            finish(true, tr("Posted to Instagram."), QUrl(post.value("permalink").toString()));
        });
    });
}

void InstagramApi::finish(bool ok, const QString &message, const QUrl &permalink) {
    m_busy = false;
    emit published(ok, message, permalink);
}
