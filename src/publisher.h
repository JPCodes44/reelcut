#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkReply;

// One reel to put on a platform: the exported video, the text (caption and
// hashtags as Posting::text gives them), a title for platforms that show one
// (YouTube), and when: invalid for now, otherwise the time the platform
// should publish it.
struct PostRequest {
    QString file;
    QString text;
    QString title;
    QDateTime when;
};

// A platform Reelcut publishes to through its official API: Instagram
// (src/instagramapi.h), Facebook Pages (src/facebookapi.h) and YouTube
// (src/youtubeapi.h). Each keeps its account in a file under
// ~/.config/reelcut that only you can read.
class Publisher : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual QString platform() const = 0;  // the Posting::Target id
    virtual bool connected() const = 0;
    virtual QString account() const = 0;   // the name shown for the account
    virtual bool busy() const = 0;
    // Uploads and publishes (or schedules) the post. Signals progress, then published.
    virtual void publish(const PostRequest &post) = 0;

signals:
    void accountChanged();
    // What's happening; fraction -1 when unknown.
    void progress(const QString &text, double fraction);
    // ok, what happened, and where the post can be seen.
    void published(bool ok, const QString &message, const QUrl &link);
};

namespace Net {

// A finished reply: its JSON body, the service's error message if any, and
// the parts some APIs answer with.
struct Response {
    QJsonObject json;
    QString error;   // empty when it worked
    QByteArray body;
    int status = 0;  // HTTP status
    QByteArray location;  // the Location header
};

// Runs `then` once the reply is in, and lets the reply go. Reads the error
// the way Meta ({"error": {"message"}}) and Google ({"error": {"message"}}
// or {"error": "...", "error_description"}) send it.
void whenDone(QNetworkReply *reply, const std::function<void(const Response &)> &then);

// Writes `json` to `path` readable only by you (it holds tokens).
bool savePrivate(const QString &path, const QJsonObject &json);
QJsonObject load(const QString &path);
// ~/.config/reelcut/<name>, or <stateDir>/<name> when stateDir is set (tests).
QString stateFile(const QString &stateDir, const QString &name);

}  // namespace Net
