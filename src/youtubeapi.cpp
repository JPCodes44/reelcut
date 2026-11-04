#include "youtubeapi.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace {

// Characters PKCE allows in a verifier, and that are safe in a state.
QString randomToken(int length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString out;
    for (int i = 0; i < length; ++i)
        out += QChar(alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))]);
    return out;
}

// What the browser shows once Google sends it back.
QByteArray page(const QString &message) {
    const QByteArray html = "<!doctype html><meta charset=utf-8><title>Reelcut</title>"
                            "<body style=\"font:16px sans-serif;margin:3em\"><h2>Reelcut</h2><p>"
                            + message.toHtmlEscaped().toUtf8() + "</p></body>";
    return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
           + QByteArray::number(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
}

}  // namespace

YouTubeApi::YouTubeApi(QObject *parent) : Publisher(parent), m_net(new QNetworkAccessManager(this)) {
    QTimer::singleShot(0, this, [this] { load(); });
}

YouTubeApi::~YouTubeApi() { stopListening(); }

QString YouTubeApi::accountFile() const { return Net::stateFile(stateDir, "youtube.json"); }

void YouTubeApi::load() {
    const QJsonObject json = Net::load(accountFile());
    m_clientId = json.value("clientId").toString();
    m_clientSecret = json.value("clientSecret").toString();
    m_refreshToken = json.value("refreshToken").toString();
    m_channelId = json.value("channelId").toString();
    m_channelTitle = json.value("channelTitle").toString();
    emit accountChanged();
}

void YouTubeApi::connectClient(const QString &clientId, const QString &clientSecret) {
    stopListening();
    m_pendingId = clientId.trimmed();
    m_pendingSecret = clientSecret.trimmed();
    if (m_pendingId.isEmpty() || m_pendingSecret.isEmpty())
        return failConnect(tr("Paste both the client ID and the client secret."));
    m_listener = new QTcpServer(this);
    if (!m_listener->listen(QHostAddress::LocalHost))
        return failConnect(tr("Couldn't listen for Google's answer: %1").arg(m_listener->errorString()));
    m_redirect = QString("http://127.0.0.1:%1").arg(m_listener->serverPort());
    m_state = randomToken(32);
    m_verifier = randomToken(64);
    const QByteArray challenge = QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256)
                                     .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    connect(m_listener, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_listener ? m_listener->nextPendingConnection() : nullptr) {
            // Outlives the listener, which goes as soon as the code is in,
            // so the page still reaches the browser.
            socket->setParent(this);
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                if (!socket->canReadLine())
                    return;
                // "GET /?state=…&code=… HTTP/1.1"
                const QList<QByteArray> request = socket->readLine().trimmed().split(' ');
                const QUrlQuery query(QUrl("http://x" + QString::fromUtf8(request.value(1))));
                const QString code = query.queryItemValue("code", QUrl::FullyDecoded);
                const QString error = query.queryItemValue("error", QUrl::FullyDecoded);
                if (code.isEmpty() && error.isEmpty()) {  // the browser asking for a favicon, say
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    return;
                }
                const bool ours = query.queryItemValue("state") == m_state;
                socket->write(page(ours && error.isEmpty() ? tr("Signed in. You can close this tab and go back to Reelcut.")
                                                           : tr("Signing in didn't work. Go back to Reelcut to try again.")));
                socket->disconnectFromHost();
                if (!ours)
                    return failConnect(tr("Google's answer didn't match this sign-in, so it was ignored. Try again."));
                if (!error.isEmpty())
                    return failConnect(tr("Google said: %1").arg(error));
                signedIn(code);
            });
        }
    });
    // Nobody finished signing in: stop waiting.
    QTimer::singleShot(10 * 60 * 1000, m_listener, [this] { failConnect(tr("Signing in to Google timed out.")); });

    QUrl url(authBase);
    QUrlQuery q;
    q.addQueryItem("client_id", m_pendingId);
    q.addQueryItem("redirect_uri", QString::fromLatin1(QUrl::toPercentEncoding(m_redirect)));
    q.addQueryItem("response_type", "code");
    q.addQueryItem("scope", QString::fromLatin1(QUrl::toPercentEncoding(
                                "https://www.googleapis.com/auth/youtube.upload https://www.googleapis.com/auth/youtube.readonly")));
    q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "consent");
    q.addQueryItem("state", m_state);
    q.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    q.addQueryItem("code_challenge_method", "S256");
    url.setQuery(q);
    if (openUrl)
        openUrl(url);
    else
        QDesktopServices::openUrl(url);
}

void YouTubeApi::stopListening() {
    if (!m_listener)
        return;
    m_listener->close();
    m_listener->deleteLater();
    m_listener = nullptr;
}

void YouTubeApi::failConnect(const QString &message) {
    stopListening();
    m_pendingSecret.clear();
    m_verifier.clear();
    emit connectFinished(false, message);
}

QNetworkReply *YouTubeApi::postToken(const QList<QPair<QString, QString>> &form) {
    QNetworkRequest request{QUrl(tokenUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    return m_net->post(request, Net::formBody(form));
}

void YouTubeApi::signedIn(const QString &code) {
    stopListening();
    const QString id = m_pendingId, secret = m_pendingSecret;
    Net::whenDone(postToken({{"code", code}, {"client_id", id}, {"client_secret", secret}, {"redirect_uri", m_redirect},
                             {"grant_type", "authorization_code"}, {"code_verifier", m_verifier}}),
                  [this, id, secret](const Net::Response &r) {
        const QString access = r.json.value("access_token").toString();
        const QString refresh = r.json.value("refresh_token").toString();
        if (!r.error.isEmpty() || access.isEmpty() || refresh.isEmpty())
            return failConnect(tr("Google didn't sign you in: %1")
                                   .arg(r.error.isEmpty() ? tr("no refresh token came back") : r.error));
        QNetworkRequest request(QUrl(apiBase + "/channels?part=snippet&mine=true"));
        request.setRawHeader("Authorization", "Bearer " + access.toUtf8());
        const QDateTime expires = QDateTime::currentDateTimeUtc().addSecs(r.json.value("expires_in").toInt(3600));
        Net::whenDone(m_net->get(request), [this, id, secret, access, refresh, expires](const Net::Response &c) {
            const QJsonObject channel = c.json.value("items").toArray().first().toObject();
            const QString title = channel.value("snippet").toObject().value("title").toString();
            if (!c.error.isEmpty() || channel.value("id").toString().isEmpty())
                return failConnect(c.error.isEmpty() ? tr("This Google account has no YouTube channel yet. Make one on YouTube first.")
                                                     : tr("Couldn't find your YouTube channel: %1").arg(c.error));
            m_clientId = id;
            m_clientSecret = secret;
            m_refreshToken = refresh;
            m_accessToken = access;
            m_accessExpires = expires;
            m_channelId = channel.value("id").toString();
            m_channelTitle = title;
            m_pendingSecret.clear();
            m_verifier.clear();
            Net::savePrivate(accountFile(), {{"clientId", id}, {"clientSecret", secret}, {"refreshToken", refresh},
                                             {"channelId", m_channelId}, {"channelTitle", title}});
            emit accountChanged();
            emit connectFinished(true, tr("Connected to the YouTube channel %1.").arg(title));
        });
    });
}

void YouTubeApi::disconnect() {
    QFile::remove(accountFile());
    m_clientId.clear();
    m_clientSecret.clear();
    m_refreshToken.clear();
    m_channelId.clear();
    m_channelTitle.clear();
    m_accessToken.clear();
    emit accountChanged();
}

void YouTubeApi::withAccess(const std::function<void(const QString &error)> &then) {
    if (!m_accessToken.isEmpty() && QDateTime::currentDateTimeUtc().secsTo(m_accessExpires) > 60)
        return then({});
    Net::whenDone(postToken({{"client_id", m_clientId}, {"client_secret", m_clientSecret},
                             {"refresh_token", m_refreshToken}, {"grant_type", "refresh_token"}}),
                  [this, then](const Net::Response &r) {
        if (r.json.value("error").toString() == "invalid_grant") {
            // Revoked, or expired: Google ends refresh tokens of apps still in
            // "Testing" after 7 days. Only signing in again helps.
            disconnect();
            return then(tr("Google ended Reelcut's access to your channel (%1). Connect YouTube again.").arg(r.error));
        }
        const QString access = r.json.value("access_token").toString();
        if (!r.error.isEmpty() || access.isEmpty())
            return then(tr("Couldn't renew access to YouTube: %1").arg(r.error.isEmpty() ? tr("no token came back") : r.error));
        m_accessToken = access;
        m_accessExpires = QDateTime::currentDateTimeUtc().addSecs(r.json.value("expires_in").toInt(3600));
        then({});
    });
}

void YouTubeApi::publish(const PostRequest &post) {
    if (m_busy)
        return;
    if (!connected())
        return finish(false, tr("Connect your YouTube channel first."));
    m_busy = true;
    emit progress(tr("Signing in to YouTube…"), -1);
    withAccess([this, post](const QString &error) {
        if (!error.isEmpty())
            return finish(false, error);
        startUpload(post);
    });
}

void YouTubeApi::startUpload(const PostRequest &post) {
    QFileInfo info(post.file);
    if (!info.isReadable())
        return finish(false, tr("Couldn't read %1.").arg(post.file));
    // A Short is a vertical video of up to 3 minutes; the tag helps it be
    // found as one. Added once, only if it isn't there.
    QString description = post.text.trimmed();
    static const QRegularExpression shortsTag("(^|\\s)#shorts\\b", QRegularExpression::CaseInsensitiveOption);
    if (!shortsTag.match(description).hasMatch()) {
        static const QRegularExpression endsWithTag("#[\\p{L}\\p{N}_]+$");
        description = description.isEmpty() ? QString("#Shorts")
                      : endsWithTag.match(description).hasMatch() ? description + " #Shorts"
                                                                  : description + "\n\n#Shorts";
    }
    const QString title = (post.title.trimmed().isEmpty() ? info.completeBaseName() : post.title.trimmed()).left(100);
    QJsonObject status{{"selfDeclaredMadeForKids", false}};
    if (post.when.isValid()) {
        status["privacyStatus"] = "private";
        status["publishAt"] = post.when.toUTC().toString("yyyy-MM-ddTHH:mm:ss'Z'");
    } else {
        status["privacyStatus"] = "public";
    }
    const QJsonObject video{{"snippet", QJsonObject{{"title", title}, {"description", description}, {"categoryId", "22"}}},
                            {"status", status}};
    QNetworkRequest request(QUrl(uploadBase + "/videos?uploadType=resumable&part=snippet,status"));
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=UTF-8");
    request.setRawHeader("X-Upload-Content-Length", QByteArray::number(info.size()));
    request.setRawHeader("X-Upload-Content-Type", "video/mp4");
    emit progress(tr("Starting the upload to YouTube…"), 0.0);
    Net::whenDone(m_net->post(request, QJsonDocument(video).toJson(QJsonDocument::Compact)), [this, post](const Net::Response &r) {
        const QUrl session(QString::fromUtf8(r.location));
        if (!r.error.isEmpty() || !session.isValid() || session.isEmpty())
            return finish(false, tr("YouTube wouldn't start the upload: %1")
                                     .arg(r.error.isEmpty() ? tr("no upload address came back") : r.error));
        sendBytes(session, post);
    });
}

void YouTubeApi::sendBytes(const QUrl &session, const PostRequest &post) {
    auto *source = new QFile(post.file, this);
    if (!source->open(QIODevice::ReadOnly)) {
        delete source;
        return finish(false, tr("Couldn't read %1.").arg(post.file));
    }
    QNetworkRequest request(session);
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "video/mp4");
    QNetworkReply *reply = m_net->put(request, source);
    source->setParent(reply);
    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0)
            emit progress(tr("Uploading the video to YouTube…"), double(sent) / total);
    });
    Net::whenDone(reply, [this, post](const Net::Response &r) {
        const QString id = r.json.value("id").toString();
        if (!r.error.isEmpty() || id.isEmpty())
            return finish(false, tr("The upload to YouTube didn't go through: %1")
                                     .arg(r.error.isEmpty() ? tr("no video came back") : r.error));
        finish(true,
               post.when.isValid()
                   ? tr("Scheduled on YouTube for %1.").arg(QLocale().toString(post.when.toLocalTime(), "ddd MMM d, h:mm AP"))
                   : tr("Posted to YouTube. It may take a few minutes to finish processing."),
               QUrl("https://youtube.com/shorts/" + id));
    });
}

void YouTubeApi::finish(bool ok, const QString &message, const QUrl &link) {
    m_busy = false;
    emit published(ok, message, link);
}
