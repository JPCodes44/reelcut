#include "publisher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace Net {

void whenDone(QNetworkReply *reply, const std::function<void(const Response &)> &then) {
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, then] {
        Response r;
        r.body = reply->readAll();
        r.json = QJsonDocument::fromJson(r.body).object();
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.location = reply->rawHeader("Location");
        const QJsonValue error = r.json.value("error");
        if (error.isObject())
            r.error = error.toObject().value("message").toString();
        else if (error.isString())
            r.error = r.json.value("error_description").toString(error.toString());
        if (r.error.isEmpty() && reply->error() != QNetworkReply::NoError)
            r.error = reply->errorString();
        reply->deleteLater();
        then(r);
    });
}

QByteArray formBody(const QList<QPair<QString, QString>> &form) {
    QByteArray body;
    for (const auto &item : form) {
        if (!body.isEmpty())
            body += '&';
        body += QUrl::toPercentEncoding(item.first) + '=' + QUrl::toPercentEncoding(item.second);
    }
    return body;
}

bool savePrivate(const QString &path, const QJsonObject &json) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    // Only you may read it, from the moment it exists.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(json).toJson());
    if (!file.commit())
        return false;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QJsonObject load(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString stateFile(const QString &stateDir, const QString &name) {
    const QString dir = !stateDir.isEmpty()
                            ? stateDir
                            : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/reelcut";
    return dir + "/" + name;
}

}  // namespace Net
