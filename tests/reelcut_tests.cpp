#include <QImage>
#include <QColor>
#include <QClipboard>
#include <QMimeData>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimeZone>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QLocalSocket>
#include <QUrlQuery>
#include <QtTest>

#include <functional>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QJSEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>

#include "backend.h"
#include "automationregistry.h"
#include "appbridge.h"
#include "downloads.h"
#include "waveformprovider.h"
#include "thumbprovider.h"
#include "captions.h"
#include "colorgrade.h"
#include "bodyeffects.h"
#include "effects.h"
#include "exportgraph.h"
#include "segment.h"
#include "filepicker.h"
#include "filters.h"
#include "fisheyetext.h"
#include "jobs.h"
#include "instagramapi.h"
#include "facebookapi.h"
#include "youtubeapi.h"
#include "higgsfield.h"
#include "postscheduler.h"
#include "mcpserver.h"
#include "posting.h"
#include "media.h"
#include "project.h"
#include "silence.h"
#include "stats.h"
#include "theme.h"
#include "transcript.h"
#include "transitions.h"
#include "translator.h"

namespace {
Clip clip(double in, double out, double speed = 1.0) {
    Clip c;
    c.path = "/videos/a.mp4";
    c.in = in;
    c.out = out;
    c.speed = speed;
    c.width = 1920;
    c.height = 1080;
    c.sourceDuration = out;
    return c;
}

// Answers every dialog with a preset file, the way the portal would.
class FakePicker : public FilePicker {
public:
    QString answer;
    void open(Kind kind) override { emit selected(kind, {QUrl::fromLocalFile(answer)}); }
    void save(Kind kind, const QUrl &) override { emit selected(kind, {QUrl::fromLocalFile(answer)}); }
};

bool runFfmpeg(const QStringList &args) {
    QProcess proc;
    proc.start("ffmpeg", args);
    return proc.waitForFinished(120000) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}
}  // namespace


// A stand-in for graph.instagram.com and Meta's upload host, on localhost:
// answers the calls InstagramApi makes and remembers them.

// An empty downloads library for windows under test.
static Downloads *testDownloads() {
    static Downloads *downloads = new Downloads(nullptr, (new QTemporaryDir)->path());
    return downloads;
}

class FakeInstagram : public QObject {
public:
    struct Call { QString method, path; QUrlQuery query; QByteArray body; QHash<QString, QString> headers; };
    QTcpServer server;
    QVector<Call> calls;
    QString containerStatus = "FINISHED";
    int inProgressFor = 1;  // status checks that say IN_PROGRESS first
    bool acceptToken = true;
    bool unified = false;
    int assignedAccounts = 1;

    FakeInstagram() {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                auto *buffer = new QByteArray;
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const int headerEnd = buffer->indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
                    Call call;
                    const QList<QByteArray> first = lines.first().trimmed().split(' ');
                    call.method = first.value(0);
                    const QUrl url("http://x" + QString::fromUtf8(first.value(1)));
                    call.path = url.path();
                    call.query = QUrlQuery(url);
                    for (int i = 1; i < lines.size(); ++i) {
                        const int colon = lines[i].indexOf(':');
                        if (colon > 0)
                            call.headers.insert(QString(lines[i].left(colon)).toLower(), lines[i].mid(colon + 1).trimmed());
                    }
                    const int length = call.headers.value("content-length").toInt();
                    if (buffer->size() < headerEnd + 4 + length)
                        return;
                    call.body = buffer->mid(headerEnd + 4, length);
                    if (call.headers.value("content-type").startsWith("application/x-www-form-urlencoded"))
                        call.query = QUrlQuery(QString::fromUtf8(call.body).replace('+', "%20"));  // "+" is a space in a form
                    calls << call;
                    const QJsonObject reply = answer(call);
                    QByteArray body = QJsonDocument(reply).toJson(QJsonDocument::Compact);
                    // The account id as Instagram sends it: a bare 17-digit number,
                    // which JSON doubles can't hold exactly.
                    if (!unified && call.path == "/me" && acceptToken && !reply.contains("error"))
                        body = R"({"user_id":17841400000000001,"username":"reelcut_test"})";
                    const QByteArray status = reply.contains("error") ? "400 Bad Request" : "200 OK";
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                    delete buffer;
                });
            }
        });
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(server.serverPort()); }
    QJsonObject answer(const Call &c) {
        const QString expectedToken = unified ? "EAA-test-token" : "good-token";
        if (c.query.queryItemValue("access_token") != expectedToken && !c.path.startsWith("/upload"))
            return {{"error", QJsonObject{{"message", "Invalid OAuth access token."}}}};
        if (unified && c.path == "/me/assigned_instagram_accounts") {
            if (!acceptToken)
                return {{"error", QJsonObject{{"message", "nope"}}}};
            QJsonArray accounts;
            for (int i = 0; i < assignedAccounts; ++i)
                accounts.append(QJsonObject{{"id", QString::number(17841400000000001LL + i)}, {"username", "unified_test"}});
            return {{"data", accounts}};
        }
        if (unified && c.path == "/me")
            return {{"error", QJsonObject{{"message", "me is a system user, not an Instagram account"}}}};
        if (c.path == "/me")
            return acceptToken ? QJsonObject{{"user_id", 17841400000000001.0}, {"username", "reelcut_test"}}
                               : QJsonObject{{"error", QJsonObject{{"message", "nope"}}}};
        if (c.method == "POST" && c.path.endsWith("/media"))
            return {{"id", "C1"}, {"uri", base() + "/upload/C1"}};
        if (c.path.startsWith("/upload/"))
            return {{"success", c.headers.value("authorization") == "OAuth " + expectedToken}};
        if (c.path == "/C1")
            return {{"status_code", inProgressFor-- > 0 ? "IN_PROGRESS" : containerStatus}, {"status", "Error: bad video"}};
        if (c.method == "POST" && c.path.endsWith("/media_publish"))
            return {{"id", "M1"}};
        if (c.path == "/M1")
            return {{"permalink", "https://www.instagram.com/reel/abc/"}};
        return {{"error", QJsonObject{{"message", "unexpected " + c.path}}}};
    }
};


// A stand-in for the scheduler's Convex deployment (scheduler/), on
// localhost: its HTTP function API and file upload, remembering the calls.
class FakeConvex : public QObject {
public:
    struct Call { QString path, function; QJsonObject args; QByteArray body; QString contentType; };
    QTcpServer server;
    QVector<Call> calls;
    QJsonArray posts;
    QString key = "0123456789abcdef-key";

    FakeConvex() {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                auto *buffer = new QByteArray;
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const int headerEnd = buffer->indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
                    QHash<QString, QString> headers;
                    for (int i = 1; i < lines.size(); ++i) {
                        const int colon = lines[i].indexOf(':');
                        if (colon > 0)
                            headers.insert(QString(lines[i].left(colon)).toLower(), lines[i].mid(colon + 1).trimmed());
                    }
                    const int length = headers.value("content-length").toInt();
                    if (buffer->size() < headerEnd + 4 + length)
                        return;
                    Call call;
                    call.path = QUrl("http://x" + QString::fromUtf8(lines.first().split(' ').value(1))).path();
                    call.body = buffer->mid(headerEnd + 4, length);
                    call.contentType = headers.value("content-type");
                    const QJsonObject request = QJsonDocument::fromJson(call.body).object();
                    call.function = request.value("path").toString();
                    call.args = request.value("args").toObject();
                    calls << call;
                    const QByteArray body = QJsonDocument(answer(call)).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                    delete buffer;
                });
            }
        });
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(server.serverPort()); }
    static QJsonObject ok(const QJsonValue &value) { return {{"status", "success"}, {"value", value}}; }
    static QJsonObject err(const QString &data) {
        return {{"status", "error"}, {"errorMessage", "Server Error"}, {"errorData", data}};
    }
    QJsonObject answer(const Call &c) {
        if (c.path == "/upload")
            return {{"storageId", "kg2storage"}};
        if (c.args.value("secret").toString() != key)
            return err("Wrong scheduler key.");
        if (c.function == "posts:uploadUrl")
            return ok(base() + "/upload");
        if (c.function == "posts:schedule") {
            QJsonObject post{{"id", "p" + QString::number(posts.size() + 1)}, {"title", c.args.value("title")},
                             {"caption", c.args.value("caption")}, {"publishAt", c.args.value("publishAt")},
                             {"status", "scheduled"}, {"username", c.args.value("username")}};
            posts.append(post);
            return ok(post);
        }
        if (c.function == "posts:list")
            return ok(posts);
        if (c.function == "posts:cancel") {
            for (int i = 0; i < posts.size(); ++i)
                if (posts[i].toObject().value("id") == c.args.value("id")) {
                    QJsonObject p = posts[i].toObject();
                    p["status"] = "cancelled";
                    posts[i] = p;
                    return ok(p);
                }
            return err("No such post.");
        }
        return err("unexpected " + c.function);
    }
};


// A local HTTP server for API stand-ins: parses each request, hands it to
// `answer`, and remembers it. Answers are JSON unless they say otherwise.
class FakeHttp : public QObject {
public:
    struct Call { QString method, path; QUrlQuery query; QByteArray body; QHash<QString, QString> headers; };
    struct Reply { int status = 200; QByteArray body; QList<QPair<QByteArray, QByteArray>> headers; };
    QTcpServer server;
    QVector<Call> calls;
    std::function<Reply(const Call &)> answer;

    FakeHttp() {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                auto *buffer = new QByteArray;
                connect(socket, &QTcpSocket::disconnected, socket, [socket, buffer] { delete buffer; socket->deleteLater(); });
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const int headerEnd = buffer->indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
                    Call call;
                    const QList<QByteArray> first = lines.first().trimmed().split(' ');
                    call.method = first.value(0);
                    const QUrl url("http://x" + QString::fromUtf8(first.value(1)));
                    call.path = url.path();
                    call.query = QUrlQuery(url);
                    for (int i = 1; i < lines.size(); ++i) {
                        const int colon = lines[i].indexOf(':');
                        if (colon > 0)
                            call.headers.insert(QString(lines[i].left(colon)).toLower(), lines[i].mid(colon + 1).trimmed());
                    }
                    const int length = call.headers.value("content-length").toInt();
                    if (buffer->size() < headerEnd + 4 + length)
                        return;
                    call.body = buffer->mid(headerEnd + 4, length);
                    buffer->clear();
                    // As a real server reads a form: "+" is a space.
                    if (call.headers.value("content-type").startsWith("application/x-www-form-urlencoded"))
                        for (const auto &item : QUrlQuery(QString::fromUtf8(call.body).replace('+', "%20")).queryItems(QUrl::FullyDecoded))
                            call.query.addQueryItem(item.first, item.second);
                    calls << call;
                    const Reply reply = answer(call);
                    QByteArray head = "HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\nContent-Length: "
                                      + QByteArray::number(reply.body.size()) + "\r\nConnection: close\r\n";
                    bool typed = false;
                    for (const auto &h : reply.headers) {
                        head += h.first + ": " + h.second + "\r\n";
                        typed = typed || h.first.toLower() == "content-type";
                    }
                    if (!typed)
                        head += "Content-Type: application/json\r\n";
                    socket->write(head + "\r\n" + reply.body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(server.serverPort()); }
    static Reply json(const QJsonObject &o, int status = 200) {
        return {status, QJsonDocument(o).toJson(QJsonDocument::Compact), {}};
    }
    static Reply error(const QString &message, int status = 400) {
        return json({{"error", QJsonObject{{"message", message}}}}, status);
    }
    QVector<Call> callsTo(const QString &path) const {
        QVector<Call> out;
        for (const Call &c : calls)
            if (c.path == path)
                out << c;
        return out;
    }
};

// Facebook's Graph API and its rupload host, for FacebookApi: Pages for a
// user token, then the three Reels phases and the status checks.
class FakeFacebook : public FakeHttp {
public:
    int pages = 1;
    int processingFor = 1;    // status checks that say processing first
    QString failWith;          // a processing error to report
    QString finishedState;     // video_state the finish call sent, lowercased

    FakeFacebook() {
        answer = [this](const Call &c) -> Reply {
            const QString token = c.query.queryItemValue("access_token");
            if (c.path == "/me/accounts") {
                if (token != "EAA-user-token")
                    return error("Invalid OAuth access token.");
                QJsonArray list;
                for (int i = 0; i < pages; ++i)
                    list.append(QJsonObject{{"id", QString::number(1000000000000001LL + i)},
                                            {"name", QString("Page %1").arg(i + 1)},
                                            {"access_token", QString("page-token-%1").arg(i + 1)}});
                return json({{"data", list}});
            }
            if (c.path.startsWith("/video-upload/")) {
                if (c.headers.value("authorization") != "OAuth page-token-1"
                    || c.headers.value("file_size").toLongLong() != c.body.size() || c.headers.value("offset") != "0")
                    return error("bad upload");
                return json({{"success", true}});
            }
            if (token != "page-token-1")
                return error("Invalid OAuth access token.");
            if (c.path == "/1000000000000001/video_reels" && c.query.queryItemValue("upload_phase") == "start")
                return json({{"video_id", "V1"}, {"upload_url", base() + "/video-upload/v26.0/V1"}});
            if (c.path == "/1000000000000001/video_reels" && c.query.queryItemValue("upload_phase") == "finish") {
                finishedState = c.query.queryItemValue("video_state").toLower();
                return json({{"success", c.query.queryItemValue("video_id") == "V1"}});
            }
            if (c.path == "/V1" && c.query.queryItemValue("fields") == "status") {
                const bool processing = processingFor-- > 0;
                QJsonObject processingPhase{{"status", !failWith.isEmpty() ? "error" : processing ? "in_progress" : "complete"}};
                if (!failWith.isEmpty())
                    processingPhase["error"] = QJsonObject{{"message", failWith}};
                return json({{"status", QJsonObject{
                    {"video_status", !failWith.isEmpty() ? "error" : processing ? "processing" : "ready"},
                    {"processing_phase", processingPhase},
                    {"publishing_phase", QJsonObject{{"status", processing ? "not_started" : "complete"},
                                                     {"publish_status", processing ? "draft" : finishedState}}}}}});
            }
            if (c.path == "/V1" && c.query.queryItemValue("fields") == "permalink_url")
                return json({{"permalink_url", "/reel/V1/"}});
            return error("unexpected " + c.path);
        };
    }
};


// Google's OAuth token endpoint and the YouTube Data API, for YouTubeApi.
class FakeGoogle : public FakeHttp {
public:
    QString lastVerifier;
    QByteArray uploaded;
    QJsonObject metadata;  // the video resource sent to start the upload
    QString channelId = "UC123";
    bool revoke = false;   // refreshing says invalid_grant

    FakeGoogle() {
        answer = [this](const Call &c) -> Reply {
            const QString auth = c.headers.value("authorization");
            if (c.path == "/token") {
                const QString grant = c.query.queryItemValue("grant_type");
                if (c.query.queryItemValue("client_id") != "cid.apps.googleusercontent.com"
                    || c.query.queryItemValue("client_secret") != "csecret")
                    return json({{"error", "invalid_client"}, {"error_description", "The OAuth client was not found."}}, 401);
                if (grant == "authorization_code" && c.query.queryItemValue("code") == "C0DE") {
                    lastVerifier = c.query.queryItemValue("code_verifier");
                    return json({{"access_token", "ya29-first"}, {"expires_in", 3599}, {"refresh_token", "1//refresh"},
                                 {"token_type", "Bearer"}});
                }
                if (grant == "refresh_token" && c.query.queryItemValue("refresh_token") == "1//refresh") {
                    if (revoke)
                        return json({{"error", "invalid_grant"}, {"error_description", "Token has been expired or revoked."}}, 400);
                    return json({{"access_token", "ya29-again"}, {"expires_in", 3599}, {"token_type", "Bearer"}});
                }
                return json({{"error", "invalid_grant"}, {"error_description", "Bad code."}}, 400);
            }
            if (!auth.startsWith("Bearer ya29-"))
                return error("Request had invalid authentication credentials.", 401);
            if (c.path == "/youtube/v3/channels" && c.query.queryItemValue("mine") == "true")
                return json({{"items", QJsonArray{QJsonObject{{"id", channelId}, {"snippet", QJsonObject{{"title", "JP Runs"}}}}}}});
            if (c.method == "POST" && c.path == "/upload/youtube/v3/videos" && c.query.queryItemValue("uploadType") == "resumable") {
                metadata = QJsonDocument::fromJson(c.body).object();
                if (c.headers.value("x-upload-content-type") != "video/mp4")
                    return error("Missing upload type");
                return {200, {}, {{"Location", (base() + "/upload/session/S1").toUtf8()}}};
            }
            if (c.method == "PUT" && c.path == "/upload/session/S1") {
                uploaded = c.body;
                return json({{"id", "VID1"}, {"status", QJsonObject{{"uploadStatus", "uploaded"}}}});
            }
            return error("unexpected " + c.path, 404);
        };
    }
};

// The three publishers and Stats, all reading their files from `state` and
// talking to one stand-in server at `base`.
struct StatsRig {
    InstagramApi instagram;
    FacebookApi facebook;
    YouTubeApi youtube;
    Stats stats{&instagram, &facebook, &youtube};

    StatsRig(const QString &state, const QString &base) {
        instagram.stateDir = facebook.stateDir = youtube.stateDir = stats.stateDir = state;
        instagram.graphBase = facebook.graphBase = base;
        youtube.tokenUrl = base + "/token";
        youtube.apiBase = base + "/youtube/v3";
        stats.youtubeAnalyticsBase = base + "/v2";
        // The publishers and Stats read their files once the loop runs.
        QTest::qWait(20);
    }
    bool refreshed() {
        stats.refresh();
        return QTest::qWaitFor([this] { return !stats.busy(); }, 15000);
    }
    QVariantMap reel(const QString &id) const {
        for (const QVariant &r : stats.reels())
            if (r.toMap().value("id").toString() == id)
                return r.toMap();
        return {};
    }
    QVariantMap account(const QString &platform) const {
        for (const QVariant &a : stats.accounts())
            if (a.toMap().value("platform").toString() == platform)
                return a.toMap();
        return {};
    }
};

// Meta's insights shapes: a lifetime value in values[], or total_value.
static QJsonObject insight(const QString &name, const QJsonValue &value, bool total = false) {
    if (total)
        return {{"name", name}, {"period", "lifetime"}, {"total_value", QJsonObject{{"value", value}}}};
    return {{"name", name}, {"period", "lifetime"}, {"values", QJsonArray{QJsonObject{{"value", value}}}}};
}

static QJsonObject igMedia(const QString &id, const QString &time, int likes, int comments,
                           const QString &product = "REELS") {
    return {{"id", id}, {"caption", "Reel " + id}, {"media_type", "VIDEO"}, {"media_product_type", product},
            {"permalink", "https://www.instagram.com/reel/" + id + "/"}, {"thumbnail_url", "https://cdn/" + id + ".jpg"},
            {"timestamp", time}, {"like_count", likes}, {"comments_count", comments}};
}

static void seedOne(const QString &state, const QString &platform) {
    if (platform == "instagram")
        Net::savePrivate(state + "/instagram.json",
                         {{"token", "good-token"}, {"userId", "17841400000000001"}, {"username", "reelcut_test"},
                          {"issued", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    else if (platform == "facebook")
        Net::savePrivate(state + "/facebook.json",
                         {{"pageId", "1000000000000001"}, {"pageName", "Page 1"}, {"pageToken", "page-token-1"}});
    else
        Net::savePrivate(state + "/youtube.json",
                         {{"clientId", "cid.apps.googleusercontent.com"}, {"clientSecret", "csecret"},
                          {"refreshToken", "1//refresh"}, {"channelId", "UC123"}, {"channelTitle", "JP Runs"}});
}

class ReelcutTests : public QObject {
    Q_OBJECT

private slots:
    // Saved downloads read by their title, newest first, ready to drag in.
    void downloadsLibrary()
    {
        QCOMPARE(Downloads::titleFor("Me at the zoo [jNQXAC9IVRw].mp4"), QString("Me at the zoo"));
        QCOMPARE(Downloads::titleFor("plain name.mp4"), QString("plain name"));
        QCOMPARE(Downloads::titleFor("[abc].mp4"), QString("[abc]"));

        const QStringList args = Downloads::ytDlpArgs("https://www.tiktok.com/@a/video/1", "/x");
        QCOMPARE(args.last(), QString("https://www.tiktok.com/@a/video/1"));
        QCOMPARE(args.at(args.size() - 2), QString("--"));  // a link can't pass as an option
        QVERIFY(args.contains("--no-playlist"));
        QCOMPARE(args.at(args.indexOf("-P") + 1), QString("/x"));

        QTemporaryDir dir;
        write(dir.filePath("Old one [a1].mp4"), "x");
        write(dir.filePath("Old one [a1].jpg"), "x");
        QFile old(dir.filePath("Old one [a1].mp4"));
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTime().addSecs(-60), QFileDevice::FileModificationTime));
        old.close();
        write(dir.filePath("New one [b2].webm"), "x");
        write(dir.filePath("Half done [c3].f137.mp4"), "x");
        write(dir.filePath("notes.txt"), "x");
        Downloads downloads(nullptr, dir.path());
        const QVariantList items = downloads.items();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].toMap()["title"].toString(), QString("New one"));
        QVERIFY(items[0].toMap()["thumb"].toUrl().isEmpty());
        QCOMPARE(items[1].toMap()["title"].toString(), QString("Old one"));
        QVERIFY(items[1].toMap()["thumb"].toUrl().isLocalFile());

        // Not links: refused before yt-dlp runs.
        QSignalSpy failed(&downloads, &Downloads::failed);
        QVERIFY(!downloads.download("hello"));
        QVERIFY(!downloads.download("file:///etc/passwd"));
        QCOMPARE(failed.size(), 2);
        QVERIFY(downloads.active().isEmpty());

        // Deleting takes the thumbnail too, and nothing outside the folder.
        QTemporaryDir other;
        write(other.filePath("keep.mp4"), "x");
        downloads.remove(other.filePath("keep.mp4"));
        QVERIFY(QFile::exists(other.filePath("keep.mp4")));
        downloads.remove(dir.filePath("Old one [a1].mp4"));
        QVERIFY(!QFile::exists(dir.filePath("Old one [a1].mp4")));
        QVERIFY(!QFile::exists(dir.filePath("Old one [a1].jpg")));
        QCOMPARE(downloads.items().size(), 1);

        // Dragged or copied out, a video is a file to other apps and a path to terminals.
        const QString kept = dir.filePath("New one [b2].webm");
        std::unique_ptr<QMimeData> mime(Downloads::mimeFor(kept));
        QCOMPARE(mime->urls(), QList<QUrl>{QUrl::fromLocalFile(kept)});
        QCOMPARE(mime->text(), kept);
        QVERIFY(mime->data("text/uri-list").startsWith("file:///"));
    }

    // A video dropped onto the timeline goes in where it was dropped.
    void addUrlsAtIndex()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x240",
                           "-frames:v", "1", dir.filePath("a.jpg")}));
        QVERIFY(QFile::copy(dir.filePath("a.jpg"), dir.filePath("b.jpg")));
        QVERIFY(QFile::copy(dir.filePath("a.jpg"), dir.filePath("c.jpg")));
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        backend.addUrls({QUrl::fromLocalFile(dir.filePath("a.jpg")), QUrl::fromLocalFile(dir.filePath("b.jpg"))});
        QTRY_COMPARE(backend.project().clips.size(), 2);
        backend.addUrls({QUrl::fromLocalFile(dir.filePath("c.jpg"))}, 1);
        QTRY_COMPARE(backend.project().clips.size(), 3);
        QVERIFY(backend.project().clips[1].path.endsWith("c.jpg"));
        QVERIFY(backend.project().clips[2].path.endsWith("b.jpg"));
    }

    void musicWaveformResizesByDraggingItsTopEdge()
    {
        QTemporaryDir dir, state;
        QImage photo(64, 64, QImage::Format_RGB32);
        photo.fill(Qt::white);
        QVERIFY(photo.save(dir.filePath("photo.png")));
        QVERIFY(runFfmpeg({"-v", "error", "-f", "lavfi", "-i", "sine=duration=2", dir.filePath("music.wav")}));
        Project project;
        project.clips = {media::clipFor(media::probe(dir.filePath("photo.png")), 2)};
        project.music.path = dir.filePath("music.wav");
        write(dir.filePath("resize.reel"), QJsonDocument(project.toJson(dir.path())).toJson());
        if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
        Theme theme(dir.path());
        auto *thumbs = new ThumbProvider();
        Backend backend(nullptr, thumbs, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("resize.reel")));
        const QJsonObject before = backend.project().toJson(dir.path());
        QQmlApplicationEngine engine;
        engine.addImageProvider("thumbs", thumbs);
        engine.addImageProvider("waveforms", new WaveformProvider());
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.loadData(R"(
            import QtQuick
            import QtQuick.Window
            Window {
                visible: true; width: 800; height: 700
                ReelTimeline {
                    objectName: "resizeTimeline"
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: implicitHeight
                }
            }
        )", QUrl("qrc:/ResizeWaveformTest.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *timeline = window->findChild<QQuickItem *>("resizeTimeline");
        auto *handle = window->findChild<QQuickItem *>("musicResizeHandle");
        QVERIFY2(handle, "The music lane needs a draggable resize edge");
        QVERIFY(timeline);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(100);
        auto drag = [&](int delta) {
            const QPointF from = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2));
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from.toPoint());
            for (int step = 1; step <= 8; ++step) {
                QTest::mouseMove(window, (from + QPointF(0, delta * step / 8.0)).toPoint());
                QTest::qWait(10);
            }
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, (from + QPointF(0, delta)).toPoint());
            QTest::qWait(30);
        };
        const double initialHeight = timeline->height();
        drag(-90);
        const double expanded = timeline->property("musicH").toDouble();
        QVERIFY(expanded > 120);
        QVERIFY(timeline->height() > initialHeight + 70);
        drag(60);
        QVERIFY(timeline->property("musicH").toDouble() < expanded - 40);
        drag(80);
        QCOMPARE(timeline->property("musicH").toDouble(), 52.0);
        drag(-400);
        QCOMPARE(timeline->property("musicH").toDouble(), 320.0);
        auto *wave = timeline->findChild<QQuickItem *>("musicWaveform");
        QVERIFY(wave);
        QCOMPARE(wave->height(), 300.0);
        QCOMPARE(backend.project().toJson(dir.path()), before);
    }

    void waveformPlacesNativeSamplesAtHighZoom()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("native.wav");
        QVERIFY(runFfmpeg({"-v", "error", "-f", "lavfi", "-i",
            "aevalsrc=if(eq(n\\,500)+eq(n\\,590)\\,0.9\\,0):s=48000:d=0.0125", "-c:a", "pcm_f32le", path}));
        const QImage image = waveform::render(path, 0, 0.0125, {512, 40});
        QCOMPARE(image.size(), QSize(512, 40));
        auto peak = [&](int x) {
            int count = 0;
            for (int y = 0; y < 40; ++y) count += qAlpha(image.pixel(x, y)) > 0;
            return count;
        };
        QVERIFY(peak(426) > 10);
        QVERIFY(peak(503) > 10);
        QCOMPARE(peak(500), 0);
        const QImage enlarged = waveform::render(path, 0.0103, 0.001, {512, 40});
        QCOMPARE(enlarged.size(), QSize(512, 40)); // more pixels than native samples
    }
    void timelineWaveformsFollowMusicAndClipEdits()
    {
        if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
        QTemporaryDir dir, state;
        const QString path = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-v", "error", "-f", "lavfi", "-i", "color=s=160x240:d=2",
            "-f", "lavfi", "-i", "sine=frequency=400:duration=2", "-shortest", path}));
        Project project;
        project.clips = {media::clipFor(media::probe(path))};
        project.clips[0].in = 0.2;
        project.clips[0].out = 1.8;
        project.clips[0].speed = 2;
        project.music.path = path;
        project.music.offset = 0.3;
        write(dir.filePath("waveforms.reel"), QJsonDocument(project.toJson(dir.path())).toJson());
        Theme theme(dir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("waveforms.reel")));
        QQmlApplicationEngine engine;
        engine.addImageProvider("waveforms", new WaveformProvider());
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.loadData("import QtQuick\nItem { width: 800; height: 278; ReelTimeline { anchors.fill: parent } }",
            QUrl("qrc:/TimelineWaveformTest.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *root = qobject_cast<QQuickItem *>(engine.rootObjects().first());
        std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name) return item;
            for (auto *child : item->childItems()) if (auto *hit = find(child, name)) return hit;
            return nullptr;
        };
        auto *music = find(root, "musicWaveform");
        auto *video = find(root, "clipWaveform0");
        QVERIFY(music && video);
        QCOMPARE(music->property("sourcePath").toString(), path);
        QCOMPARE(music->property("sourceStart").toDouble(), 0.3);
        QCOMPARE(video->property("sourceStart").toDouble(), 0.2);
        QCOMPARE(video->property("speed").toDouble(), 2.0);
        backend.setMusicValue("offset", 0.5);
        backend.setClipValue(0, "in", 0.4);
        backend.setClipValue(0, "speed", 0.5);
        QTRY_COMPARE(music->property("sourceStart").toDouble(), 0.5);
        QTRY_COMPARE(video->property("sourceStart").toDouble(), 0.4);
        QTRY_COMPARE(video->property("speed").toDouble(), 0.5);
    }

    void waveformTilesLoadAndFollowZoomAndOffset()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("audio # % é.wav");
        QVERIFY(runFfmpeg({"-v", "error", "-f", "lavfi", "-i", "sine=frequency=400:duration=2", path}));
        QQmlApplicationEngine engine;
        engine.addImageProvider("waveforms", new WaveformProvider());
        engine.rootContext()->setContextProperty("testAudioPath", path);
        engine.loadData(R"(
            import QtQuick
            import QtQuick.Window
            Window {
                visible: true; width: 640; height: 100
                Waveform {
                    objectName: "testWaveform"
                    width: 100000; height: 80
                    sourcePath: testAudioPath
                    sourceStart: 0.2
                    pixelsPerSecond: 1000
                    viewportWidth: 640
                }
            }
        )", QUrl("qrc:/WaveformTest.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *wave = engine.rootObjects().first()->findChild<QQuickItem *>("testWaveform");
        QVERIFY(wave);
        auto images = [&] {
            QList<QQuickItem *> result;
            for (auto *child : wave->childItems())
                if (child->property("source").isValid()) result.append(child);
            return result;
        };
        QTRY_COMPARE(images().size(), 3); // bounded to viewport plus one preload tile
        QQuickItem *image = images().first();
        QTRY_COMPARE_WITH_TIMEOUT(image->property("status").toInt(), 1, 10000);
        QUrlQuery query(image->property("source").toUrl());
        QCOMPARE(query.queryItemValue("path", QUrl::FullyDecoded), path);
        QCOMPARE(query.queryItemValue("start").toDouble(), 0.2);
        QCOMPARE(query.queryItemValue("duration").toDouble(), 0.512);
        QVERIFY(wave->setProperty("sourceStart", 0.4));
        QVERIFY(wave->setProperty("pixelsPerSecond", 2000));
        QTRY_COMPARE(QUrlQuery(image->property("source").toUrl()).queryItemValue("duration").toDouble(), 0.256);
        QCOMPARE(QUrlQuery(image->property("source").toUrl()).queryItemValue("start").toDouble(), 0.4);
        QTRY_COMPARE_WITH_TIMEOUT(image->property("status").toInt(), 1, 10000);
        QVERIFY(wave->setProperty("viewportStart", 20000));
        QTRY_VERIFY(images().size() <= 4);
    }

    void waveformPreservesPeaksAndStereo()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("stereo # % é.wav");
        // One-sample opposite-phase impulses: averaging would hide them.
        QVERIFY(runFfmpeg({"-v", "error", "-f", "lavfi", "-i",
            "aevalsrc=if(eq(n\\,12000)\\,0.9\\,0)|if(eq(n\\,12000)\\,-0.9\\,0):s=48000:d=1",
            "-c:a", "pcm_f32le", path}));
        const QImage image = waveform::render(path, 0, 1, {200, 80});
        QCOMPARE(image.size(), QSize(200, 80));
        QCOMPARE(waveform::render(path, 0, 1, {200, 800}).size(), QSize(200, 800));
        int top = 0, bottom = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 47; x <= 52; ++x)
                if (qAlpha(image.pixel(x, y)) > 0) (y < 40 ? top : bottom)++;
        QVERIFY2(top > 10 && bottom > 10, "Short peaks in both channels must survive aggregation");
        const QImage trimmed = waveform::render(path, 0.2, 0.1, {200, 80});
        int middle = 0;
        for (int y = 0; y < 80; ++y)
            for (int x = 95; x <= 105; ++x) middle += qAlpha(trimmed.pixel(x, y)) > 0;
        QVERIFY2(middle > 10, "Source offset and duration must move the impulse to the center");
    }

    void waveformSilenceTailAndCancellation()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("short.wav");
        QVERIFY(runFfmpeg({"-v", "error", "-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono", "-t", "0.1", path}));
        const QImage image = waveform::render(path, 0, 1, {400, 40});
        QCOMPARE(image.size(), QSize(400, 40));
        QCOMPARE(waveform::render(path, 0.2, 1, {400, 40}).size(), QSize(400, 40));
        QVERIFY(waveform::render(path, 0, 1, {400, 40}, "#ffffff", [] { return true; }).isNull());
        QVERIFY(waveform::render(dir.filePath("missing.wav"), 0, 1, {400, 40}).isNull());
    }
    // Theme and translator, from the Omarchy app template.

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    // Project

    void durationCountsSpeed()
    {
        Project p;
        p.clips = {clip(0, 4), clip(10, 14, 2.0)};
        QCOMPARE(p.duration(), 6.0);
        QCOMPARE(p.clipStart(1), 4.0);
    }

    void locateMapsTimelineToSource()
    {
        Project p;
        p.clips = {clip(0, 4), clip(10, 14, 2.0)};
        QCOMPARE(p.locate(1.0).index, 0);
        QCOMPARE(p.locate(1.0).source, 1.0);
        QCOMPARE(p.locate(5.0).index, 1);
        QCOMPARE(p.locate(5.0).source, 12.0);
        // The very end belongs to the last clip.
        QCOMPARE(p.locate(6.0).index, 1);
        QCOMPARE(p.locate(6.0).source, 14.0);
        QCOMPARE(Project().locate(0).index, -1);
    }

    void splitCutsTheClipUnderThePlayhead()
    {
        Project p;
        p.clips = {clip(0, 4), clip(10, 14, 2.0)};
        QVERIFY(p.split(5.0));
        QCOMPARE(p.clips.size(), 3);
        QCOMPARE(p.clips[1].out, 12.0);
        QCOMPARE(p.clips[2].in, 12.0);
        QCOMPARE(p.duration(), 6.0);
        // On an edge there is nothing to split.
        QVERIFY(!p.split(4.0));
        QVERIFY(!p.split(0.0));
    }

    void replaceKeepsFramingAndDropsSlivers()
    {
        Project p;
        p.clips = {clip(0, 10)};
        p.clips[0].zoom = 1.5;
        p.replaceClipWithRanges(0, {{0, 2}, {3, 3.01}, {5, 12}});
        QCOMPARE(p.clips.size(), 2);
        QCOMPARE(p.clips[1].in, 5.0);
        QCOMPARE(p.clips[1].out, 10.0);
        QCOMPARE(p.clips[1].zoom, 1.5);
    }

    // Captions sit on the platform's home spot and follow it until moved by
    // hand; moved captions stay put but are kept out of the new app's buttons.
    void captionsFollowThePlatform()
    {
        Project p;
        const QPointF instagram = Project::captionHome("instagram");
        const QPointF tiktok = Project::captionHome("tiktok");
        QVERIFY(instagram != tiktok);
        QVERIFY(tiktok.y() < instagram.y());   // TikTok's own text starts higher
        QVERIFY(tiktok.x() < 0.5 && instagram.x() < 0.5);  // clear of the right-hand buttons
        QCOMPARE(p.captionX, instagram.x());
        QCOMPARE(p.captionY, instagram.y());
        QVERIFY(p.captionAuto);

        p.setPlatform("tiktok");
        QCOMPARE(p.captionX, tiktok.x());
        QCOMPARE(p.captionY, tiktok.y());

        // Moved by hand: switching keeps the spot...
        QVERIFY(p.setCaptionValue("y", 0.3));
        QVERIFY(!p.captionAuto);
        p.setPlatform("instagram");
        QCOMPARE(p.captionY, 0.3);
        // ...unless it would sit under the app's text: then it's pulled back in.
        QVERIFY(p.setCaptionValue("y", 0.95));
        p.setPlatform("tiktok");
        QVERIFY(p.captionY <= (1920 - 480 - 90) / 1920.0 + 1e-9);

        // Saved and loaded, the choice sticks.
        const Project back = Project::fromJson(p.toJson());
        QVERIFY(!back.captionAuto);
        QCOMPARE(back.captionY, p.captionY);

        // A file from before this, with captions on the old fixed default,
        // counts as never moved and goes to the platform's spot.
        QJsonObject old = Project().toJson();
        old.remove("captionAuto");
        old["captionX"] = 0.5;
        old["captionY"] = 0.68;
        old["platform"] = "tiktok";
        const Project legacy = Project::fromJson(old);
        QVERIFY(legacy.captionAuto);
        QCOMPARE(legacy.captionY, tiktok.y());
    }

    // One table of platforms drives names, safe zones and caption spots.
    void platformTableCoversFourApps()
    {
        QCOMPARE(Posting::ids(), (QStringList{"instagram", "facebook", "youtube", "tiktok"}));
        QCOMPARE(Posting::normalize("youtube"), QString("youtube"));
        QCOMPARE(Posting::normalize("myspace"), QString("instagram"));
        const QHash<QString, QVector<double>> zones{{"instagram", {40, 220, 950, 1500}},
                                                    {"facebook", {65, 270, 960, 1250}},
                                                    {"youtube", {48, 290, 890, 1400}},
                                                    {"tiktok", {40, 160, 930, 1440}}};
        for (const QString &id : Posting::ids()) {
            const Posting::Target *t = Posting::find(id);
            QVERIFY(t);
            const QVector<double> z = zones.value(id);
            QCOMPARE(t->safe.left, z[0]);
            QCOMPARE(t->safe.top, z[1]);
            QCOMPARE(t->safe.right, z[2]);
            QCOMPARE(t->safe.bottom, z[3]);
            const QPointF home = Project::captionHome(id);
            QCOMPARE(home.x(), (z[0] + z[2]) / 2 / 1080.0);
            QCOMPARE(home.y(), (z[3] - 190) / 1920.0);
            Project p;
            p.setPlatform(id);
            QCOMPARE(p.platform, id);
            QCOMPARE(Project::fromJson(p.toJson()).platform, id);
            QCOMPARE(t->toMap().value("safeBottom").toDouble(), z[3]);
        }
        Project p;
        p.setPlatform("myspace");
        QCOMPARE(p.platform, QString("instagram"));
        QCOMPARE(Posting::find("facebook")->minSeconds, 3);
        QCOMPARE(Posting::find("facebook")->maxSeconds, 90);
        QCOMPARE(Posting::find("youtube")->maxSeconds, 180);
    }

    void jsonRoundTripsWithRelativePaths()
    {
        Project p;
        p.clips = {clip(1, 3, 1.5)};
        p.clips[0].path = "/home/me/reels/clips/a.mp4";
        p.clips[0].panX = -0.5;
        p.music.path = "/home/me/reels/song.mp3";
        p.overlays = {{"Wait for it", 0, 2, 0.5, 0.2, 100, "slide"}};
        p.words = {{"hello", 0.1, 0.4}};
        p.look.saturation = 0.3;
        p.captionStyle = "pop";

        const QJsonObject json = p.toJson("/home/me/reels");
        QCOMPARE(json.value("clips").toArray().first().toObject().value("path").toString(), QString("clips/a.mp4"));

        const Project back = Project::fromJson(json, "/home/me/reels");
        QCOMPARE(back.clips[0].path, QString("/home/me/reels/clips/a.mp4"));
        QCOMPARE(back.clips[0].speed, 1.5);
        QCOMPARE(back.clips[0].panX, -0.5);
        QCOMPARE(back.music.path, QString("/home/me/reels/song.mp3"));
        QCOMPARE(back.overlays[0].animation, QString("slide"));
        QCOMPARE(back.words[0].text, QString("hello"));
        QCOMPARE(back.look.saturation, 0.3);
        QCOMPARE(back.captionStyle, QString("pop"));
    }

    // Silence

    void parsesSilencedetectOutput()
    {
        const QByteArray log =
            "[silencedetect @ 0x5581] silence_start: 1.2\n"
            "[silencedetect @ 0x5581] silence_end: 2.5 | silence_duration: 1.3\n"
            "size=N/A time=00:00:05.00\n"
            "[silencedetect @ 0x5581] silence_start: 4.1\n";
        const auto silences = parseSilences(log);
        QCOMPARE(silences.size(), 2);
        QCOMPARE(silences[0], Range(1.2, 2.5));
        QCOMPARE(silences[1].first, 4.1);
        QVERIFY(std::isinf(silences[1].second));
    }

    void keepRangesPadsAndDropsShortBits()
    {
        const auto kept = keepRanges(0, 6, {{1.0, 2.0}, {2.1, 2.2}, {4.0, INFINITY}});
        // The 0.1 s silence vanishes into its padding; the trailing one cuts the end.
        QCOMPARE(kept.size(), 2);
        QCOMPARE(kept[0], Range(0.0, 1.1));
        QCOMPARE(kept[1], Range(1.9, 4.1));
    }

    // Captions

    void groupsWordsIntoShortLines()
    {
        const QVector<Word> words = {{"this", 0, .2}, {"is", .2, .3}, {"really", .3, .6},
                                     {"cool", .6, .9}, {"after", 2.0, 2.3}, {"pause", 2.3, 2.6}};
        const auto lines = groupLines(words);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].text(), QString("this is really"));
        QCOMPARE(lines[1].text(), QString("cool"));
        QCOMPARE(lines[2].text(), QString("after pause"));
        // A line holds until a pause, then ends with its last word.
        QCOMPARE(lines[1].end, 0.9);

        const auto byChars = groupLines({{"extraordinarily", 0, 1}, {"long", 1, 2}});
        QCOMPARE(byChars.size(), 2);
    }

    void formatsAssTime()
    {
        QCOMPARE(assTime(0), QString("0:00:00.00"));
        QCOMPARE(assTime(61.237), QString("0:01:01.24"));
        QCOMPARE(assTime(3725.5), QString("1:02:05.50"));
    }

    void highlightStyleLightsEachWord()
    {
        Project p;
        p.words = {{"hey", 0, .3}, {"you", .3, .6}};
        p.overlays = {{"Watch {this}", 0, 2, 0.5, 0.2, 96, "pop"}};
        const QString ass = assDocument(p, "#FF0000");
        QCOMPARE(ass.count("Style: Caption"), 1);
        QCOMPARE(ass.count(",Caption,"), 2);
        QVERIFY(ass.contains("{\\c&H0000FF&}HEY{\\c&HFFFFFF&} YOU"));
        QVERIFY(ass.contains("HEY {\\c&H0000FF&}YOU{\\c&HFFFFFF&}"));
        // User text can't open override tags.
        QVERIFY(ass.contains("}Watch (this)"));
        QVERIFY(ass.contains("PlayResY: 1920"));
    }

    // Transcript

    void parsesWhisperWords()
    {
        const QByteArray json = R"({"transcription":[
            {"offsets":{"from":0,"to":320},"text":" Hello"},
            {"offsets":{"from":320,"to":330},"text":","},
            {"offsets":{"from":400,"to":900},"text":" world"},
            {"offsets":{"from":900,"to":2000},"text":" [BLANK_AUDIO]"}]})";
        const auto words = parseWhisperJson(json);
        QCOMPARE(words.size(), 2);
        QCOMPARE(words[0].text, QString("Hello,"));
        QCOMPARE(words[0].end, 0.33);
        QCOMPARE(words[1].start, 0.4);
    }

    // Export graph

    void framingCoversTheFrame()
    {
        Clip landscape = clip(0, 1);
        QCOMPARE(framingFilter(landscape), QString("scale=3414:1920:flags=lanczos,crop=1080:1920:1167:0"));
        landscape.panX = -1.0;
        QVERIFY(framingFilter(landscape).endsWith("crop=1080:1920:0:0"));
        landscape.panX = 1.0;
        QVERIFY(framingFilter(landscape).endsWith("crop=1080:1920:2334:0"));

        Clip portrait = clip(0, 1);
        portrait.width = 1080;
        portrait.height = 1920;
        QCOMPARE(framingFilter(portrait), QString("scale=1080:1920:flags=lanczos,crop=1080:1920:0:0"));
        portrait.zoom = 2.0;
        QCOMPARE(framingFilter(portrait), QString("scale=2160:3840:flags=lanczos,crop=1080:1920:540:960"));
    }

    void atempoStaysInRange()
    {
        QCOMPARE(atempoChain(2.5), QString("atempo=2.5"));
        QCOMPARE(atempoChain(0.25), QString("atempo=0.5,atempo=0.5"));
        QCOMPARE(atempoChain(0.3), QString("atempo=0.5,atempo=0.6"));
    }

    void graphCoversMusicLookAndSilentClips()
    {
        Project p;
        p.clips = {clip(0, 2), clip(5, 7, 2.0)};
        p.clips[1].audio = false;
        p.music.path = "/music/song.mp3";
        p.look.lut = "/luts/film.cube";
        ExportOptions options{"/out/reel.mp4", "captions.ass", "look.cube", "libx264"};

        const QStringList args = exportArgs(p, options);
        const QString graph = args.at(args.indexOf("-filter_complex") + 1);
        QVERIFY(graph.contains("anullsrc=r=48000:cl=stereo,atrim=duration=1,"));
        QVERIFY(graph.contains("setpts=(PTS-STARTPTS)/2,"));
        QVERIFY(graph.contains("[2:a]atrim=start=0:duration=3,"));
        QVERIFY(graph.contains("sidechaincompress"));
        QVERIFY(graph.contains("[vcat]lut3d=file=look.cube,format=yuv444p,ass=filename=captions.ass,"
                               "scale=flags=lanczos+accurate_rnd+full_chroma_int,format=yuv420p[vout]"));
        QVERIFY(!graph.contains("eq="));
        QVERIFY(args.contains("libx264"));
        QCOMPARE(args.last(), QString("/out/reel.mp4"));

        p.music.duck = false;
        p.look.saturation = 0.5;
        const QStringList plain = exportArgs(p, {"/out/reel.mp4", {}, {}, "h264_nvenc"});
        const QString plainGraph = plain.at(plain.indexOf("-filter_complex") + 1);
        QVERIFY(!plainGraph.contains("sidechaincompress"));
        QVERIFY(plainGraph.contains("[acat][mus]amix"));
        QVERIFY(plainGraph.contains("eq=brightness=0:contrast=1:saturation=1.5"));
    }

    // Filter library: every look is a real change, amount 0 is untouched,
    // and colors stay in range.
    void filterLooksGradeWithinRange()
    {
        QVERIFY(Filters::looks().size() >= 20);
        QCOMPARE(Filters::ids().removeDuplicates(), 0);
        const Filters::Rgb samples[] = {{0, 0, 0}, {1, 1, 1}, {0.5, 0.5, 0.5}, {0.9, 0.2, 0.1}, {0.1, 0.4, 0.8}};
        for (const Filters::Look &look : Filters::looks()) {
            double change = 0.0;
            for (const Filters::Rgb &s : samples) {
                const Filters::Rgb full = Filters::apply(look, s, 1.0);
                for (double v : {full.r, full.g, full.b})
                    QVERIFY2(v >= 0.0 && v <= 1.0, qPrintable(look.id));
                change += qAbs(full.r - s.r) + qAbs(full.g - s.g) + qAbs(full.b - s.b);
                const Filters::Rgb none = Filters::apply(look, s, 0.0);
                QCOMPARE(none.r, s.r);
                QCOMPARE(none.b, s.b);
            }
            QVERIFY2(change > 0.05, qPrintable(look.id + " barely changes anything"));
        }
        QVERIFY(Filters::find("GOLDEN-HOUR"));
        QVERIFY(!Filters::find("nope"));
    }

    // The .cube is what ffmpeg's lut3d reads: a size header, then size^3 rows,
    // red changing fastest.
    void filterCubeIsWellFormed()
    {
        const Filters::Look *look = Filters::find("silver");
        QVERIFY(look);
        const QStringList lines = Filters::cubeText(*look, 1.0, 5).split('\n', Qt::SkipEmptyParts);
        QVERIFY(lines.contains("LUT_3D_SIZE 5"));
        const QStringList rows = lines.filter(QRegularExpression("^[0-9.]+ [0-9.]+ [0-9.]+$"));
        QCOMPARE(rows.size(), 125);
        // Silver is monochrome: every row is grey.
        for (const QString &row : rows) {
            const QStringList v = row.split(' ');
            QVERIFY(qAbs(v[0].toDouble() - v[2].toDouble()) < 1e-6);
        }
        // Amount 0 is the identity table; row 1 is red at its second step.
        const QStringList identity = Filters::cubeText(*look, 0.0, 5).split('\n').filter(QRegularExpression("^[0-9.]+ "));
        QCOMPARE(identity.at(1), QString("0.250000 0.000000 0.000000"));

        const QImage strip = Filters::stripImage(*look);
        QCOMPARE(strip.size(), QSize(512, 512));
        const QColor white = strip.pixelColor(7 * 64 + 63, 7 * 64 + 63);  // last tile, last cell
        QVERIFY(white.red() > 200 && white.red() == white.blue());
    }

    void filterSavesAndExports()
    {
        Project p;
        p.clips = {clip(0, 2)};
        QVERIFY(!p.setFilter("does-not-exist"));
        QVERIFY(p.setFilter("Harbor"));
        QCOMPARE(p.look.filter, QString("harbor"));
        QVERIFY(p.setLookValue("filterAmount", 1.7));
        QCOMPARE(p.look.filterAmount, 1.0);
        QVERIFY(p.setLookValue("filterAmount", 0.4));

        const Project back = Project::fromJson(p.toJson());
        QCOMPARE(back.look.filter, QString("harbor"));
        QCOMPARE(back.look.filterAmount, 0.4);
        QJsonObject unknown = p.toJson();
        QJsonObject look = unknown.value("look").toObject();
        look.insert("filter", "from-the-future");
        unknown.insert("look", look);
        QVERIFY(Project::fromJson(unknown).look.filter.isEmpty());

        // The filter goes after the sliders and before the user's own LUT.
        p.look.lut = "/luts/mine.cube";
        ExportOptions options{"/out/reel.mp4", {}, "look.cube", "libx264", "filter.cube"};
        const QStringList args = exportArgs(p, options);
        const QString graph = args.at(args.indexOf("-filter_complex") + 1);
        QVERIFY(graph.contains("[vcat]lut3d=file=filter.cube:interp=tetrahedral,lut3d=file=look.cube[vout]"));

        QTemporaryDir work;
        QString error;
        const ExportOptions prepared = prepareExport(p, "#FFD60A", work.path(), "/out/reel.mp4", &error);
        QVERIFY2(error.isEmpty() || error.contains("LUT"), qPrintable(error));  // mine.cube doesn't exist
        p.look.lut.clear();
        const ExportOptions ready = prepareExport(p, "#FFD60A", work.path(), "/out/reel.mp4", &error);
        QCOMPARE(ready.filterFile, QString("filter.cube"));
        QVERIFY(QFileInfo(work.filePath("filter.cube")).size() > 100000);
        p.look.filterAmount = 0.0;
        QVERIFY(prepareExport(p, "#FFD60A", work.path(), "/out/reel.mp4", &error).filterFile.isEmpty());
    }

    // End to end: a mono filter really turns ffmpeg's output grey.
    void filterRendersThroughFfmpeg()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=2", "-c:v", "libx264", source}));
        Project p;
        Clip c;
        c.path = source;
        c.out = 2.0;
        c.width = 320;
        c.height = 240;
        c.audio = false;
        p.clips = {c};
        QVERIFY(p.setFilter("silver"));
        QString error;
        const ExportOptions options = prepareExport(p, "#FFD60A", dir.path(), {}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QProcess proc;
        proc.setWorkingDirectory(dir.path());
        proc.start("ffmpeg", frameArgs(p, options, 1.0, dir.filePath("frame.png"), 270));
        QVERIFY(proc.waitForFinished(60000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().right(1500).constData());
        const QImage frame(dir.filePath("frame.png"));
        QVERIFY(!frame.isNull());
        int colorful = 0;
        for (int y = 0; y < frame.height(); y += 8)
            for (int x = 0; x < frame.width(); x += 8) {
                const QColor px = frame.pixelColor(x, y);
                if (qMax(qAbs(px.red() - px.green()), qAbs(px.green() - px.blue())) > 12)
                    ++colorful;
            }
        QVERIFY2(colorful < 5, qPrintable(QString("%1 colored samples left").arg(colorful)));
    }

    void mcpSetsAFilter()
    {
        QTemporaryDir dir;
        QTemporaryDir state;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=1", "-c:v", "libx264", source}));
        McpServer server(state.path());
        QJsonObject r = call(server, "create_project", {{"path", dir.filePath("f")}, {"clips", QJsonArray{source}}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));

        r = call(server, "set_look", {{"filter", "night-city"}, {"filter_amount", 0.5}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        const Project saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("f.reel"))).object(), dir.path());
        QCOMPARE(saved.look.filter, QString("night-city"));
        QCOMPARE(saved.look.filterAmount, 0.5);
        QVERIFY(call(server, "set_look", {{"filter", "bogus"}}).value("isError").toBool());
    }

    // Transitions overlap clips: the next clip starts early by the overlap,
    // which never exceeds half of either clip.
    void transitionsOverlapClips()
    {
        Project p;
        p.clips = {clip(0, 4), clip(0, 2), clip(0, 3)};
        QCOMPARE(p.duration(), 9.0);
        QVERIFY(!p.setClipTransition(0, "nope"));
        QVERIFY(p.setClipTransition(0, "Dissolve"));
        QCOMPARE(p.clips[0].transition, QString("dissolve"));
        QCOMPARE(p.overlap(0), 0.5);
        QCOMPARE(p.duration(), 8.5);
        QCOMPARE(p.clipStart(1), 3.5);
        QCOMPARE(p.clipStart(2), 5.5);
        // During the overlap the outgoing clip owns the time.
        QCOMPARE(p.locate(3.75).index, 0);
        QCOMPARE(p.locate(4.1).index, 1);
        QCOMPARE(p.locate(4.1).source, 0.6);

        // Limited by the shorter clip (2 s → at most 1 s).
        QVERIFY(p.setClipValue(0, "transitionSeconds", 5.0));
        QCOMPARE(p.clips[0].transitionSeconds, 2.0);
        QCOMPARE(p.overlap(0), 1.0);
        // The last clip has nothing to transition into.
        QVERIFY(p.setClipTransition(2, "zoom"));
        QCOMPARE(p.overlap(2), 0.0);

        // Splitting keeps the transition on the piece that ends where the clip did.
        QVERIFY(p.split(1.0));
        QVERIFY(p.clips[0].transition.isEmpty());
        QCOMPARE(p.clips[1].transition, QString("dissolve"));

        const Project back = Project::fromJson(p.toJson());
        QCOMPARE(back.clips[1].transition, QString("dissolve"));
        QCOMPARE(back.clips[1].transitionSeconds, 2.0);
        QCOMPARE(back.duration(), p.duration());
    }

    void transitionGraphChainsXfade()
    {
        Project p;
        p.clips = {clip(0, 4), clip(0, 2), clip(0, 3)};
        p.setClipTransition(0, "iris");
        const QStringList args = exportArgs(p, {"/out/reel.mp4", {}, {}, "libx264"});
        const QString graph = args.at(args.indexOf("-filter_complex") + 1);
        QVERIFY2(graph.contains("[v0][v1]xfade=transition=circleopen:duration=0.5:offset=3.5[vj1]"), qPrintable(graph));
        QVERIFY(graph.contains("[a0][a1]acrossfade=d=0.5:c1=tri:c2=tri[aj1]"));
        QVERIFY(graph.contains("[vj1][v2]concat=n=2:v=1:a=0[vcat]"));
        QVERIFY(graph.contains("[aj1][a2]concat=n=2:v=0:a=1[acat]"));
        QCOMPARE(args.at(args.indexOf("-t") + 1), QString("8.5"));
        // The transcription audio follows the same timeline.
        const QStringList audio = timelineAudioArgs(p, "/tmp/a.wav");
        QVERIFY(audio.join(' ').contains("acrossfade=d=0.5"));
        // No transitions: one plain concat as before.
        p.setClipTransition(0, {});
        const QStringList plain = exportArgs(p, {"/out/reel.mp4", {}, {}, "libx264"});
        QVERIFY(plain.at(plain.indexOf("-filter_complex") + 1).contains("[v0][a0][v1][a1][v2][a2]concat=n=3:v=1:a=1[vcat][acat]"));
    }

    // End to end: every transition renders, and the reel is shorter by the overlap.
    void transitionsRenderThroughFfmpeg()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=3", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=3", "-shortest", "-c:v", "libx264", "-c:a", "aac", source}));
        Project p;
        for (const QString &id : Transitions::ids()) {
            Clip c;
            c.path = source;
            c.out = 1.0;
            c.width = 320;
            c.height = 240;
            c.sourceDuration = 3.0;
            c.transition = id;
            c.transitionSeconds = 0.3;
            p.clips << c;
        }
        const double expected = p.clips.size() * 1.0 - (p.clips.size() - 1) * 0.3;
        QCOMPARE(p.duration(), expected);
        QProcess proc;
        proc.setWorkingDirectory(dir.path());
        proc.start("ffmpeg", exportArgs(p, {dir.filePath("reel.mp4"), {}, {}, "libx264"}));
        QVERIFY(proc.waitForFinished(300000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().right(2000).constData());
        const media::VideoInfo out = media::probe(dir.filePath("reel.mp4"));
        QVERIFY2(out.ok, qPrintable(out.error));
        QVERIFY2(qAbs(out.duration - expected) < 0.15, qPrintable(QString::number(out.duration)));

        // Clips without sound: crossfaded silence must still encode.
        Project silent;
        silent.clips = {p.clips[0], p.clips[1]};
        for (Clip &c : silent.clips)
            c.audio = false;
        QVERIFY(!exportArgs(silent, {dir.filePath("silent.mp4"), {}, {}, "libx264"}).join(' ').contains("loudnorm"));
        QProcess quiet;
        quiet.setWorkingDirectory(dir.path());
        quiet.start("ffmpeg", exportArgs(silent, {dir.filePath("silent.mp4"), {}, {}, "libx264"}));
        QVERIFY(quiet.waitForFinished(120000));
        QVERIFY2(quiet.exitCode() == 0, quiet.readAllStandardError().right(1500).constData());
    }

    // Dragging a clip down makes a layer; it saves, renders over the main
    // track inside its box only while it plays, and can go back on the track.
    void layersMoveSaveAndRender()
    {
        QTemporaryDir dir;
        const QString red = dir.filePath("red.mp4");
        const QString blue = dir.filePath("blue.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=red:s=180x320:r=30:d=3",
                           "-f", "lavfi", "-i", "sine=frequency=440:duration=3", "-shortest",
                           "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", red}));
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=blue:s=180x320:r=30:d=2",
                           "-f", "lavfi", "-i", "sine=frequency=660:duration=2", "-shortest",
                           "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", blue}));
        auto source = [](const QString &path, double duration) {
            Clip c;
            c.path = path;
            c.out = duration;
            c.width = 180;
            c.height = 320;
            c.sourceDuration = duration;
            return c;
        };
        Project p;
        p.clips = {source(red, 3.0)};
        QCOMPARE(p.moveClipToLayer(0, 0.0), -1);  // the main track keeps a picture
        Clip b = source(blue, 2.0);
        b.out = 1.0;
        b.transition = "fade";
        p.clips << b;
        QCOMPARE(p.moveClipToLayer(1, 1.0), 0);
        QCOMPARE(p.clips.size(), 1);
        QCOMPARE(p.layers.size(), 1);
        QVERIFY(p.layers[0].clip.transition.isEmpty());
        QCOMPARE(p.duration(), 3.0);  // layers don't lengthen the reel
        QVERIFY(p.setLayerValue(0, "scale", 0.5));
        QVERIFY(p.setLayerValue(0, "x", 0.25));
        QVERIFY(p.setLayerValue(0, "y", 0.25));
        QVERIFY(p.setLayerValue(0, "scale", 5.0));
        QCOMPARE(p.layers[0].scale, 1.0);
        QVERIFY(p.setLayerValue(0, "scale", 0.5));
        QVERIFY(!p.setLayerValue(0, "nope", 1.0));
        QVERIFY(!p.setLayerValue(3, "x", 1.0));

        const Project loaded = Project::fromJson(p.toJson(dir.path()), dir.path());
        QCOMPARE(loaded.layers.size(), 1);
        QCOMPARE(loaded.layers[0].clip.path, blue);
        QCOMPARE(loaded.layers[0].start, 1.0);
        QCOMPARE(loaded.layers[0].scale, 0.5);
        QCOMPARE(loaded.layers[0].x, 0.25);
        QCOMPARE(loaded.layers[0].clip.out, 1.0);

        QProcess proc;
        proc.setWorkingDirectory(dir.path());
        proc.start("ffmpeg", exportArgs(p, {dir.filePath("reel.mp4"), {}, {}, "libx264"}));
        QVERIFY(proc.waitForFinished(120000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().right(2000).constData());
        const media::VideoInfo out = media::probe(dir.filePath("reel.mp4"));
        QVERIFY2(out.ok, qPrintable(out.error));
        QVERIFY2(qAbs(out.duration - 3.0) < 0.15, qPrintable(QString::number(out.duration)));

        // In the box while it plays, red around it and before and after.
        auto frameAt = [&](double t) {
            const QString png = dir.filePath(QString("frame-%1.png").arg(t));
            QProcess f;
            f.setWorkingDirectory(dir.path());
            f.start("ffmpeg", frameArgs(p, {}, t, png, 540));
            f.waitForFinished(60000);
            return QImage(png);
        };
        auto isBlue = [](QColor c) { return c.blue() > 180 && c.red() < 80; };
        auto isRed = [](QColor c) { return c.red() > 180 && c.blue() < 80; };
        const QImage during = frameAt(1.5);
        QVERIFY(!during.isNull());
        QVERIFY(isBlue(during.pixelColor(during.width() / 4, during.height() / 4)));
        QVERIFY(isRed(during.pixelColor(during.width() * 3 / 4, during.height() * 3 / 4)));
        const QImage before = frameAt(0.5);
        QVERIFY(isRed(before.pixelColor(before.width() / 4, before.height() / 4)));
        const QImage after = frameAt(2.5);
        QVERIFY(isRed(after.pixelColor(after.width() / 4, after.height() / 4)));

        // Back on the track, after the clip it landed in the second half of.
        QCOMPARE(p.moveLayerToTrack(0, 2.0), 1);
        QVERIFY(p.layers.isEmpty());
        QCOMPARE(p.clips[1].path, blue);
    }

    // Dropping up or down: within half a row of a neighbor joins its row,
    // more than half a row past it swaps rows. A row never overlaps itself.
    void layerRowsJoinAndSwap()
    {
        auto clipOf = [](const QString &path, double duration) {
            Clip c;
            c.path = path;
            c.out = duration;
            c.sourceDuration = duration;
            return c;
        };
        Project p;
        p.clips = {clipOf("main1.mp4", 4), clipOf("main2.mp4", 4), clipOf("a.mp4", 2), clipOf("b.mp4", 2),
                   clipOf("c.mp4", 2)};
        // The rule, on screen rows (0 is the main track).
        QCOMPARE(p.dropTarget(0, 0.3, 2).action, QString("none"));
        QCOMPARE(p.dropTarget(0, 1.0, 2).action, QString("newRow"));  // no layer rows yet
        QCOMPARE(p.moveClipToLayer(2, 1.0, -1), 0);                      // a: row 0 at 1 s
        QCOMPARE(p.dropTarget(0, 0.8, 2).action, QString("join"));
        QCOMPARE(p.dropTarget(0, 0.8, 2).row, 0);
        QCOMPARE(p.dropTarget(0, 1.6, 2).action, QString("newRow"));

        // b joins row 0 dropped on top of a: it slides to the nearest free spot.
        const int b = p.moveClipToLayer(2, 1.5, 0);
        QCOMPARE(p.layerRows(), 1);
        QCOMPARE(p.layers[b].clip.path, QString("b.mp4"));
        QCOMPARE(p.layers[b].start, 3.0);  // right after a (1..3), nearer than before it
        // Moving b sideways stops at a.
        QVERIFY(p.setLayerValue(b, "start", 0.0));
        QCOMPARE(p.layers[b].start, 3.0);

        // c on a new row 1, then the layer rules from its row (screen row 2).
        const int c = p.moveClipToLayer(2, 0.0, -1);
        QCOMPARE(p.layers[c].row, 1);
        QCOMPARE(p.dropTarget(2, -0.4, c).action, QString("none"));
        QCOMPARE(p.dropTarget(2, -1.0, c).action, QString("join"));
        QCOMPARE(p.dropTarget(2, -1.0, c).row, 0);
        QCOMPARE(p.dropTarget(2, -1.6, c).action, QString("swap"));
        QCOMPARE(p.dropTarget(2, 1.0, c).action, QString("none"));  // already alone on the last row
        QCOMPARE(p.dropTarget(1, -1.0, 0).action, QString("toTrack"));
        QCOMPARE(p.dropTarget(1, -1.6, 0).action, QString("swapTrack"));
        QCOMPARE(p.dropTarget(1, 1.0, 0).action, QString("join"));
        QCOMPARE(p.dropTarget(1, 1.0, 0).row, 1);

        // Swapping c's row with row 0: c now draws under a and b, at its own time.
        const int swapped = p.swapLayerRows(c, 0);
        QCOMPARE(p.layers[swapped].clip.path, QString("c.mp4"));
        QCOMPARE(p.layers[swapped].row, 0);
        QCOMPARE(p.layers[swapped].start, 0.0);
        QCOMPARE(p.layers.last().row, 1);

        // Joining: c onto row 1 at 2 s overlaps a and b; it slides to fit.
        const int joined = p.moveLayerToRow(swapped, 1, 2.0);
        QCOMPARE(p.layerRows(), 1);  // its old row closed up
        QCOMPARE(p.layers[joined].clip.path, QString("c.mp4"));
        QCOMPARE(p.layers[joined].row, 0);
        for (int i = 1; i < p.layers.size(); ++i)
            QVERIFY(p.layers[i].start >= p.layers[i - 1].end() - 1e-9);

        // Swapping with the main track: the row becomes the track, the track a row.
        const int onTrack = p.swapRowWithTrack(0);
        QCOMPARE(p.clips.size(), 3);
        QCOMPARE(p.clips[onTrack].path, QString("a.mp4"));
        QCOMPARE(p.layerRows(), 1);
        QCOMPARE(p.layers.size(), 2);
        QCOMPARE(p.layers[0].clip.path, QString("main1.mp4"));
        QCOMPARE(p.layers[1].start, 4.0);  // main2 where it played

        // Rows save.
        const Project loaded = Project::fromJson(p.toJson());
        QCOMPARE(loaded.layers.size(), 2);
        QCOMPARE(loaded.layers[1].row, 0);
    }

    void effectsSaveAndClamp()
    {
        Project p;
        p.clips = {clip(0, 4)};
        QCOMPARE(p.addEffect("nope", 1.0), -1);
        const int i = p.addEffect("VHS", 1.0);
        QCOMPARE(i, 0);
        QCOMPARE(p.effects[0].kind, QString("vhs"));
        QCOMPARE(p.effects[0].end, 1.0 + Effects::DefaultSeconds);
        QVERIFY(p.setEffectValue(0, "intensity", 3.0));
        QCOMPARE(p.effects[0].intensity, 1.0);
        QVERIFY(p.setEffectValue(0, "start", 9.0));  // can't pass the end
        QVERIFY(p.effects[0].start < p.effects[0].end);
        QVERIFY(!p.setEffectValue(0, "colour", 1.0));
        QVERIFY(!p.setEffectValue(4, "speed", 1.0));

        QJsonObject json = p.toJson();
        QJsonArray list = json.value("effects").toArray();
        list.append(QJsonObject{{"kind", "from-the-future"}, {"start", 0}, {"end", 1}});
        json.insert("effects", list);
        const Project back = Project::fromJson(json);
        QCOMPARE(back.effects.size(), 1);
        QCOMPARE(back.effects[0].kind, QString("vhs"));
        QCOMPARE(back.effects[0].intensity, 1.0);
    }

    // Each effect is laid over the picture only while it's active.
    void effectGraphUsesEnable()
    {
        Project p;
        p.clips = {clip(0, 4)};
        p.addEffect("blur", 1.0, 2.0);
        p.addEffect("mono-flicker", 0.5, 3.0);
        const QStringList args = exportArgs(p, {"/out/reel.mp4", {}, {}, "libx264"});
        const QString graph = args.at(args.indexOf("-filter_complex") + 1);
        QVERIFY2(graph.contains("[vcat]split[fxb0][fxw0]"), qPrintable(graph));
        QVERIFY(graph.contains("[fxb0][fxd0]overlay=enable='between(t,1,2)'[vfx0]"));
        QVERIFY(graph.contains("[vfx0]split[fxb1][fxw1]"));
        // Mono flicker switches itself on and off in the enable expression.
        QVERIFY(graph.contains("overlay=enable='between(t,0.5,3)*lt(mod((t-0.5)*"));
        QVERIFY(graph.contains("[vfx]null[vout]"));
    }

    // End to end: every effect renders through ffmpeg, and changes the
    // picture only inside its time range.
    void effectsRenderThroughFfmpeg()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=640x360:rate=30:duration=3", "-c:v", "libx264", source}));
        for (const QString &kind : Effects::ids()) {
            Project p;
            Clip c;
            c.path = source;
            c.out = 3.0;
            c.width = 640;
            c.height = 360;
            c.audio = false;
            c.sourceDuration = 3.0;
            p.clips = {c};
            p.addEffect(kind, 1.0, 2.0);
            p.effects[0].intensity = 1.0;
            QImage frames[2];
            for (int k = 0; k < 2; ++k) {
                const QString png = dir.filePath(QString("%1-%2.png").arg(kind).arg(k));
                QProcess proc;
                proc.setWorkingDirectory(dir.path());
                Project shown = p;
                if (k == 0)
                    shown.effects.clear();
                // 1.25 s: 0.25 s into the effect, an "on" moment for Mono Flicker too.
                proc.start("ffmpeg", frameArgs(shown, {}, 1.25, png, 270));
                QVERIFY(proc.waitForFinished(60000));
                QVERIFY2(proc.exitCode() == 0, qPrintable(kind + ": " + proc.readAllStandardError().right(1500)));
                frames[k] = QImage(png).convertToFormat(QImage::Format_RGB32);
            }
            // Different from the plain picture at that moment.
            qint64 diff = 0;
            for (int y = 0; y < frames[0].height(); y += 4)
                for (int x = 0; x < frames[0].width(); x += 4)
                    diff += qAbs(qGray(frames[0].pixel(x, y)) - qGray(frames[1].pixel(x, y)))
                            + qAbs(qRed(frames[0].pixel(x, y)) - qRed(frames[1].pixel(x, y)));
            QVERIFY2(diff > 2000, qPrintable(QString("%1 barely changed the picture (%2)").arg(kind).arg(diff)));
        }
    }

    void bodyEffectGraph()
    {
        Project p;
        p.clips = {clip(0, 2), clip(0, 2)};
        QVERIFY(!p.setClipBody(0, "nope"));
        QVERIFY(p.setClipBody(1, "Color-Pop"));
        QCOMPARE(p.clips[1].body, QString("color-pop"));
        QCOMPARE(Project::fromJson(p.toJson()).clips[1].body, QString("color-pop"));
        p.music.path = "/music/song.mp3";

        // Without a mask the clip renders plainly.
        QString graph = exportArgs(p, {"/out/reel.mp4", {}, {}, "libx264"}).join(' ');
        QVERIFY(!graph.contains("maskedmerge"));

        // With one, it's input 3 (after two clips and the music).
        ExportOptions options{"/out/reel.mp4", {}, {}, "libx264"};
        options.masks = {QString(), "/cache/mask.mp4"};
        const QStringList args = exportArgs(p, options);
        QCOMPARE(args.at(args.indexOf("/cache/mask.mp4") - 1), QString("-i"));
        graph = args.at(args.indexOf("-filter_complex") + 1);
        QVERIFY2(graph.contains("[3:v]setpts=PTS-STARTPTS,fps=30,settb=1/30,scale=1080:1920:flags=bicubic,format=gbrp[bmask1]"), qPrintable(graph));
        QVERIFY(graph.contains("[bback1]hue=s=0.2,format=gbrp[bbg1]"));
        QVERIFY(graph.contains("[bbg1][bpic1][bmask1]maskedmerge,format=yuv420p,fps=30,settb=1/30[v1]"));
        QVERIFY(graph.contains("[v0]"));

        // The mask cache key follows the frames: change the trim, new mask.
        Clip a = clip(0, 2), b = clip(0, 2);
        QCOMPARE(Segment::maskPath(a), Segment::maskPath(b));
        b.out = 1.5;
        QVERIFY(Segment::maskPath(a) != Segment::maskPath(b));
    }

    // End to end with a fake model that calls the left half "the person":
    // the mask video lines up with the clip, and Color Pop greys only the right.
    void bodyEffectMaskRendersThroughFfmpeg()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=1080x1920:rate=30:duration=2", "-c:v", "libx264", source}));
        Clip c;
        c.path = source;
        c.out = 2.0;
        c.width = 1080;
        c.height = 1920;
        c.audio = false;
        c.sourceDuration = 2.0;
        c.body = "color-pop";
        c.bodyAmount = 1.0;

        const Segment::MaskFn leftHalf = [](const unsigned char *, unsigned char *mask) {
            for (int y = 0; y < Segment::ModelSize; ++y)
                for (int x = 0; x < Segment::ModelSize; ++x)
                    mask[y * Segment::ModelSize + x] = x < Segment::ModelSize / 2 ? 255 : 0;
        };
        const QString mask = dir.filePath("mask.mp4");
        int calls = 0;
        QString error;
        QVERIFY2(Segment::makeMask(c, leftHalf, mask, [&](double) { ++calls; return true; }, &error), qPrintable(error));
        QCOMPARE(calls, 60);  // one model run per frame: 2 s at 30 fps
        const media::VideoInfo info = media::probe(mask);
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.width, Segment::MaskWidth);
        QCOMPARE(info.height, Segment::MaskHeight);
        QVERIFY(qAbs(info.duration - 2.0) < 0.1);

        Project p;
        p.clips = {c};
        ExportOptions options{{}, {}, {}, "libx264"};
        options.masks = {mask};
        const QString png = dir.filePath("frame.png");
        QProcess proc;
        proc.start("ffmpeg", frameArgs(p, options, 1.0, png, 540));
        QVERIFY(proc.waitForFinished(60000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().right(1500).constData());
        const QImage frame = QImage(png).convertToFormat(QImage::Format_RGB32);
        auto colorful = [&](int x0, int x1) {
            int n = 0;
            for (int y = 40; y < frame.height() - 40; y += 8)
                for (int x = x0; x < x1; x += 8) {
                    const QColor px = frame.pixelColor(x, y);
                    if (qMax(qAbs(px.red() - px.green()), qAbs(px.green() - px.blue())) > 24)
                        ++n;
                }
            return n;
        };
        const int left = colorful(20, frame.width() / 2 - 30);
        const int right = colorful(frame.width() / 2 + 30, frame.width() - 20);
        QVERIFY2(left > 100 && right < 10, qPrintable(QString("left %1, right %2").arg(left).arg(right)));

        // A body effect next to a transition: the merged clip must be on the
        // same clock as the others, or xfade refuses it.
        {
            Project both = p;
            Clip next = c;
            next.body.clear();
            both.clips[0].transition = "dissolve";
            both.clips << next;
            ExportOptions withMask = options;
            withMask.masks = {mask, QString()};
            QProcess joined;
            joined.start("ffmpeg", frameArgs(both, withMask, 1.9, dir.filePath("joined.png"), 270));
            QVERIFY(joined.waitForFinished(60000));
            QVERIFY2(joined.exitCode() == 0, joined.readAllStandardError().right(1500).constData());
        }

        // Every body effect renders with the same mask.
        for (const QString &kind : Body::ids()) {
            p.clips[0].body = kind;
            QProcess each;
            each.start("ffmpeg", frameArgs(p, options, 1.0, dir.filePath(kind + ".png"), 270));
            QVERIFY(each.waitForFinished(60000));
            QVERIFY2(each.exitCode() == 0, qPrintable(kind + ": " + each.readAllStandardError().right(1500)));
        }
    }

    // The real model, when it has been downloaded: it loads and answers in range.
    void personModelRuns()
    {
        if (!QFileInfo::exists(Segment::modelPath()))
            QSKIP("The person model isn't downloaded on this machine.");
        QString error;
        const Segment::MaskFn fn = Segment::loadModel(Segment::modelPath(), &error);
        QVERIFY2(fn, qPrintable(error));
        QByteArray rgb(Segment::ModelSize * Segment::ModelSize * 3, char(128));
        QByteArray mask(Segment::ModelSize * Segment::ModelSize, '\0');
        fn(reinterpret_cast<const unsigned char *>(rgb.constData()), reinterpret_cast<unsigned char *>(mask.data()));
        // A flat grey square has no person in it.
        int person = 0;
        for (char v : mask)
            person += uchar(v) > 128;
        QVERIFY2(person < mask.size() / 4, qPrintable(QString::number(person)));
    }

    void mcpSetsABodyEffect()
    {
        QTemporaryDir dir, state;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=2", "-c:v", "libx264", source}));
        McpServer server(state.path());
        QJsonObject r = call(server, "create_project", {{"path", dir.filePath("b")}, {"clips", QJsonArray{source}}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        r = call(server, "edit_clip", {{"index", 0}, {"body", "spotlight"}, {"body_amount", 0.4}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        const Project saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("b.reel"))).object(), dir.path());
        QCOMPARE(saved.clips[0].body, QString("spotlight"));
        QCOMPARE(saved.clips[0].bodyAmount, 0.4);
        QVERIFY(call(server, "edit_clip", {{"index", 0}, {"body", "x-ray"}}).value("isError").toBool());
    }

    // End to end: ffmpeg renders a real reel from a generated landscape clip.
    void exportsAPlayableReel()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=1280x720:rate=30:duration=4", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=4", "-shortest", "-c:v", "libx264", "-c:a", "aac",
                           source}));
        const media::VideoInfo info = media::probe(source);
        QVERIFY2(info.ok, qPrintable(info.error));

        Project p;
        Clip c;
        c.path = source;
        c.out = info.duration;
        c.width = info.width;
        c.height = info.height;
        c.audio = info.audio;
        p.clips = {c};
        QVERIFY(p.split(2.0));
        p.clips[1].speed = 2.0;
        p.words = {{"it", 0.2, 0.5}, {"works", 0.5, 1.0}};
        p.look.contrast = 0.2;
        write(dir.filePath("captions.ass"), assDocument(p, "#FFD60A").toUtf8());

        QProcess proc;
        proc.setWorkingDirectory(dir.path());
        proc.start("ffmpeg", exportArgs(p, {dir.filePath("reel.mp4"), "captions.ass", {}, "libx264"}));
        QVERIFY(proc.waitForFinished(180000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().right(2000).constData());

        const media::VideoInfo out = media::probe(dir.filePath("reel.mp4"));
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.width, 1080);
        QCOMPARE(out.height, 1920);
        QVERIFY(out.audio);
        QVERIFY(qAbs(out.duration - 3.0) < 0.15);
    }

    // Transcription input: the clips' sound at timeline speed, as Whisper wants it.
    void timelineAudioIsWhisperReady()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("a.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=4", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=4", "-shortest", "-c:v", "libx264", "-c:a", "aac", source}));
        Project p;
        Clip a = clip(0, 2), b = clip(2, 4, 2.0);
        a.path = b.path = source;
        p.clips = {a, b};
        QVERIFY(runFfmpeg(timelineAudioArgs(p, dir.filePath("audio.wav"))));
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-show_entries", "stream=sample_rate,channels:format=duration",
                                "-of", "csv=p=0", dir.filePath("audio.wav")});
        QVERIFY(probe.waitForFinished(10000));
        const QString out = QString::fromUtf8(probe.readAllStandardOutput());
        QVERIFY2(out.contains("16000,1"), qPrintable(out));
        QVERIFY2(qAbs(out.split('\n').value(1).toDouble() - 3.0) < 0.1, qPrintable(out));
    }

    // Backend

    void undoAndRedoRestoreEdits()
    {
        QTemporaryDir dir;
        Project p;
        p.clips = {clip(0, 4), clip(10, 14)};
        write(dir.filePath("a.reel"), QJsonDocument(p.toJson()).toJson());

        QTemporaryDir state;
        Backend backend(nullptr, nullptr, nullptr, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("a.reel")));
        QVERIFY(!backend.dirty());
        QVERIFY(backend.split(2.0));
        QCOMPARE(backend.project().clips.size(), 3);
        QVERIFY(backend.dirty());

        // A slider drag is one step however many values it sets.
        backend.checkpoint();
        backend.setClipValue(0, "speed", 1.5);
        backend.setClipValue(0, "speed", 9.0);
        QCOMPARE(backend.project().clips[0].speed, 4.0);

        backend.undo();
        QCOMPARE(backend.project().clips[0].speed, 1.0);
        QCOMPARE(backend.project().clips.size(), 3);
        backend.undo();
        QCOMPARE(backend.project().clips.size(), 2);
        QVERIFY(!backend.canUndo());
        backend.redo();
        QCOMPARE(backend.project().clips.size(), 3);
    }

    void cutsSilencesFromAClip()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("talk.mp4");
        // A second of tone, a second and a half of silence, a second of tone.
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "color=c=gray:size=320x240:rate=30:duration=3.5", "-f", "lavfi", "-i",
                           "aevalsrc='if(between(t,1,2.5),0,0.5*sin(440*2*PI*t))':d=3.5", "-shortest",
                           "-c:v", "libx264", "-c:a", "aac", source}));
        Project p;
        Clip c = clip(0, 3.5);
        c.path = source;
        p.clips = {c};
        write(dir.filePath("talk.reel"), QJsonDocument(p.toJson()).toJson());

        QTemporaryDir state;
        Backend backend(nullptr, nullptr, nullptr, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("talk.reel")));
        backend.cutSilences(0);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 30000);
        QCOMPARE(backend.project().clips.size(), 2);
        // 1.5 s of silence goes, minus 0.1 s of padding kept on each side.
        QVERIFY2(qAbs(backend.project().duration() - 2.2) < 0.15,
                 qPrintable(QString::number(backend.project().duration())));
        backend.undo();
        QCOMPARE(backend.project().clips.size(), 1);
    }

    void savesAndExportsThroughTheDialogs()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("clip.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=640x360:rate=30:duration=2", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=2", "-shortest", "-c:v", "libx264", "-c:a", "aac",
                           source}));
        FakePicker picker;
        QTemporaryDir state;
        Backend backend(&picker, nullptr, nullptr, nullptr, state.path());
        backend.addUrls({QUrl::fromLocalFile(source)});
        QTRY_COMPARE_WITH_TIMEOUT(backend.project().clips.size(), 1, 15000);
        backend.addOverlay(0.0);

        picker.answer = dir.filePath("mine");  // the extension is added
        backend.saveAs();
        QVERIFY(QFileInfo::exists(dir.filePath("mine.reel")));
        QVERIFY(!backend.dirty());
        QCOMPARE(backend.title(), QString("mine"));

        picker.answer = dir.filePath("out.mp4");
        backend.exportVideo();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 120000);
        QVERIFY2(!backend.messageIsError(), qPrintable(backend.message()));
        QCOMPARE(backend.lastExport(), dir.filePath("out.mp4"));
        const media::VideoInfo out = media::probe(dir.filePath("out.mp4"));
        QVERIFY(out.ok);
        QCOMPARE(out.height, 1920);
    }

    // Photos

    void photosBecomeTimedSlides()
    {
        QTemporaryDir dir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("a.jpg")}));
        const media::VideoInfo info = media::probe(dir.filePath("a.jpg"));
        QVERIFY2(info.ok, qPrintable(info.error));
        QVERIFY(info.still);
        QVERIFY(!info.audio);
        // Timeline thumbnails of photos come from frame 0.
        QVERIFY(!media::thumbnail(dir.filePath("a.jpg"), 0.0, 90).isNull());
        const Clip clip = media::clipFor(info, 3.0);
        QVERIFY(clip.still);
        QCOMPARE(clip.duration(), 3.0);
        QCOMPARE(clip.motion, QString("zoom-in"));

        Project p;
        p.clips = {clip};
        QVERIFY(p.setClipValue(0, "duration", 4.5));
        QCOMPARE(p.clips[0].duration(), 4.5);
        QVERIFY(!p.setClipValue(0, "speed", 2.0));
        QVERIFY(p.setClipMotion(0, "pan-left"));
        QVERIFY(!p.setClipMotion(0, "spin"));
        const Project back = Project::fromJson(p.toJson());
        QVERIFY(back.clips[0].still);
        QCOMPARE(back.clips[0].motion, QString("pan-left"));
        QCOMPARE(back.clips[0].duration(), 4.5);
    }

    void motionFilterMovesPhotosOnly()
    {
        Clip photo = clip(0, 2);
        photo.still = true;
        photo.motion = "zoom-in";
        QVERIFY(motionFilter(photo).contains("zoompan=z='1+0.12*on/60'"));
        QVERIFY(motionFilter(photo).contains("s=1080x1920"));
        photo.motion = "pan-right";
        QVERIFY(motionFilter(photo).contains("x='(iw-iw/zoom)*on/60'"));
        photo.motion = "none";
        QVERIFY(motionFilter(photo).isEmpty());
        QVERIFY(motionFilter(clip(0, 2)).isEmpty());
    }

    void exportsASlideshowAndAPhotoSet()
    {
        QTemporaryDir dir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("wide.jpg")}));
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "mandelbrot=size=1080x1350",
                           "-frames:v", "1", dir.filePath("tall.png")}));
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=640x360:rate=30:duration=2", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=2", "-shortest", "-c:v", "libx264", "-c:a", "aac",
                           dir.filePath("video.mp4")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("wide.jpg")), 2.0),
                   media::clipFor(media::probe(dir.filePath("tall.png")), 1.5),
                   media::clipFor(media::probe(dir.filePath("video.mp4")))};
        p.clips[1].motion = "pan-right";
        p.overlays = {{"Slide one", 0, 2, 0.5, 0.2, 96, "pop"}};

        QString encoder, error;
        QVERIFY2(exportProject(p, "#FFD60A", dir.filePath("show.mp4"), &encoder, &error), qPrintable(error));
        const media::VideoInfo out = media::probe(dir.filePath("show.mp4"));
        QCOMPARE(out.width, 1080);
        QCOMPARE(out.height, 1920);
        QVERIFY(out.audio);
        QVERIFY2(qAbs(out.duration - 5.5) < 0.15, qPrintable(QString::number(out.duration)));

        const QStringList files = exportPhotoSet(p, "#FFD60A", dir.filePath("set"), &error);
        QVERIFY2(files.size() == 3, qPrintable(error));
        QCOMPARE(QFileInfo(files[0]).fileName(), QString("01.jpg"));
        QImage first(files[0]);
        QCOMPARE(first.size(), QSize(1080, 1920));
    }

    // MCP server

    void mcpSpeaksTheProtocol()
    {
        QTemporaryDir state;
        McpServer server(state.path());
        const QJsonObject init = server.handle({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
                                                {"params", QJsonObject{{"protocolVersion", "2025-06-18"}}}});
        QCOMPARE(init.value("result").toObject().value("serverInfo").toObject().value("name").toString(), QString("reelcut"));
        QVERIFY(server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}).isEmpty());

        const QJsonArray tools = server.handle({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}})
                                     .value("result").toObject().value("tools").toArray();
        QStringList names;
        for (const QJsonValue &tool : tools)
            names.append(tool.toObject().value("name").toString());
        for (const char *name : {"create_project", "add_clips", "split", "edit_clip", "cut_silences", "transcribe",
                                 "set_captions", "edit_words", "add_text", "set_look", "set_music", "undo",
                                 "preview_frame", "export", "show_in_app"})
            QVERIFY2(names.contains(name), name);

        const QJsonObject unknown = server.handle({{"jsonrpc", "2.0"}, {"id", 3}, {"method", "nope"}});
        QCOMPARE(unknown.value("error").toObject().value("code").toInt(), -32601);
        // Without a project, tools say what to do instead of failing obscurely.
        const QJsonObject noProject = call(server, "get_project", {});
        QVERIFY(noProject.value("isError").toBool());
    }

    void mcpEditsPreviewsAndExportsAReel()
    {
        QTemporaryDir dir, state;
        const QString source = dir.filePath("clip.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=1280x720:rate=30:duration=4", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=4", "-shortest", "-c:v", "libx264", "-c:a", "aac", source}));
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=220:duration=10",
                           dir.filePath("song.mp3")}));
        McpServer server(state.path());

        QJsonObject r = call(server, "create_project", {{"path", dir.filePath("demo")}, {"clips", QJsonArray{source}},
                                                         {"platform", "tiktok"}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        QVERIFY(QFileInfo::exists(dir.filePath("demo.reel")));

        // The rest use the project in use without naming it.
        QVERIFY(!call(server, "split", {{"time", 2.0}}).value("isError").toBool());
        QVERIFY(!call(server, "edit_clip", {{"index", 1}, {"speed", 2.0}, {"zoom", 9.0}}).value("isError").toBool());
        QVERIFY(call(server, "edit_clip", {{"index", 7}}).value("isError").toBool());
        QVERIFY(!call(server, "set_captions", {{"style", "pop"}, {"words", QJsonArray{
            QJsonObject{{"text", "made"}, {"start", 0.2}, {"end", 0.6}},
            QJsonObject{{"text", "by"}, {"start", 0.6}, {"end", 0.9}},
            QJsonObject{{"text", "agnet"}, {"start", 0.9}, {"end", 1.4}}}}}).value("isError").toBool());
        QVERIFY(!call(server, "edit_words", {{"edits", QJsonArray{QJsonObject{{"index", 2}, {"text", "agent"}}}}})
                     .value("isError").toBool());
        QVERIFY(!call(server, "add_text", {{"text", "Watch this"}, {"end", 1.5}, {"animation", "slide"}}).value("isError").toBool());
        QVERIFY(!call(server, "set_look", {{"preset", "warm"}, {"saturation", 0.5}}).value("isError").toBool());
        QVERIFY(!call(server, "set_music", {{"file", dir.filePath("song.mp3")}, {"volume", 0.2}}).value("isError").toBool());

        Project saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("demo.reel"))).object(), dir.path());
        QCOMPARE(saved.platform, QString("tiktok"));
        QCOMPARE(saved.clips.size(), 2);
        QCOMPARE(saved.clips[1].speed, 2.0);
        QCOMPARE(saved.clips[1].zoom, 3.0);
        QCOMPARE(saved.words[2].text, QString("agent"));
        QCOMPARE(saved.overlays[0].animation, QString("slide"));
        QCOMPARE(saved.look.temperature, 0.4);
        QCOMPARE(saved.look.saturation, 0.5);
        QCOMPARE(saved.music.volume, 0.2);

        // Undo walks back one tool call at a time.
        QVERIFY(!call(server, "undo", {}).value("isError").toBool());
        saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("demo.reel"))).object(), dir.path());
        QVERIFY(!saved.music.isSet());
        QVERIFY(!call(server, "redo", {}).value("isError").toBool());

        const QJsonObject frame = call(server, "preview_frame", {{"time", 1.0}, {"width", 270}});
        QVERIFY2(!frame.value("isError").toBool(), qPrintable(textOf(frame)));
        const QJsonObject image = frame.value("content").toArray().first().toObject();
        QCOMPARE(image.value("type").toString(), QString("image"));
        QImage png;
        QVERIFY(png.loadFromData(QByteArray::fromBase64(image.value("data").toString().toLatin1()), "PNG"));
        QCOMPARE(png.size(), QSize(270, 480));

        const QJsonObject exported = call(server, "export", {});
        QVERIFY2(!exported.value("isError").toBool(), qPrintable(textOf(exported)));
        const QJsonObject info = QJsonDocument::fromJson(textOf(exported).toUtf8()).object();
        QCOMPARE(info.value("output").toString(), dir.filePath("demo-tiktok.mp4"));
        QCOMPARE(info.value("size").toString(), QString("1080x1920"));
        QVERIFY(qAbs(info.value("duration").toDouble() - 3.0) < 0.15);
    }

    void mcpBuildsAPhotoSlideshow()
    {
        QTemporaryDir dir, state;
        QStringList photos;
        for (int i = 0; i < 3; ++i) {
            photos << dir.filePath(QString("p%1.jpg").arg(i));
            QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                               QString("color=c=0x%1:size=1200x1600").arg(i == 0 ? "aa3333" : i == 1 ? "33aa33" : "3333aa"),
                               "-frames:v", "1", photos.last()}));
        }
        McpServer server(state.path());
        QVERIFY(!call(server, "create_project", {{"path", dir.filePath("slides")}}).value("isError").toBool());
        QJsonObject r = call(server, "add_clips", {{"files", QJsonArray::fromStringList(photos)}, {"photo_seconds", 2.0},
                                                   {"motion", "none"}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        QVERIFY(!call(server, "edit_clip", {{"index", 1}, {"duration", 3.0}, {"motion", "zoom-out"}}).value("isError").toBool());
        QVERIFY(!call(server, "add_text", {{"text", "my handwriting\nthen vs now"}, {"end", 2.0}}).value("isError").toBool());

        const QJsonObject summary = QJsonDocument::fromJson(textOf(call(server, "get_project", {})).toUtf8()).object();
        QCOMPARE(summary.value("duration").toDouble(), 7.0);
        QVERIFY(summary.value("clips").toArray().at(0).toObject().value("photo").toBool());
        QCOMPARE(summary.value("clips").toArray().at(1).toObject().value("motion").toString(), QString("zoom-out"));

        const QJsonObject set = call(server, "export_photos", {});
        QVERIFY2(!set.value("isError").toBool(), qPrintable(textOf(set)));
        const QJsonObject result = QJsonDocument::fromJson(textOf(set).toUtf8()).object();
        QCOMPARE(result.value("files").toArray().size(), 3);
        QVERIFY(QFileInfo::exists(dir.filePath("slides-photos/03.jpg")));
    }

    void windowFollowsEditsMadeElsewhere()
    {
        QTemporaryDir dir, state;
        Project p;
        p.clips = {clip(0, 4)};
        write(dir.filePath("live.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        Backend backend(nullptr, nullptr, nullptr, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("live.reel")));
        // The window records what it has open, so the MCP server finds it.
        QCOMPARE(QString::fromUtf8(readAll(McpServer::openProjectFile(state.path()))), dir.filePath("live.reel"));

        McpServer server(state.path());
        QVERIFY(!call(server, "open_project", {}).value("isError").toBool());
        QVERIFY(!call(server, "split", {{"time", 1.0}}).value("isError").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(backend.project().clips.size(), 2, 5000);
        QVERIFY(!backend.dirty());
        // The outside edit is one undo away in the window.
        backend.undo();
        QCOMPARE(backend.project().clips.size(), 1);
    }

    // Dragged text snaps to the frame's center and thirds; holding Ctrl turns
    // snapping off. The picture's pan snaps back to centered.
    void previewDragSnapsToGuides()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 4.0)};
        p.clips[0].motion = "none";
        p.clips[0].zoom = 1.5;
        p.overlays = {{"snap me", 0, 4, 0.5, 0.42, 96, "none"}};
        p.overlays[0].x = 0.5;
        write(dir.filePath("snap.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("snap.reel")));
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 860);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 1.0));
        QTest::qWait(300);

        std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (QQuickItem *child : item->childItems())
                if (QQuickItem *found = find(child, name))
                    return found;
            return nullptr;
        };
        // Mouse events sent straight to the window so a drag can hold Ctrl.
        auto send = [&](QEvent::Type type, QPoint pos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
            QMouseEvent e(type, QPointF(pos), window->mapToGlobal(QPointF(pos)), Qt::LeftButton, buttons, mods);
            QCoreApplication::sendEvent(window, &e);
        };
        auto drag = [&](QPoint from, QPoint to, Qt::KeyboardModifiers mods) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton, mods);
            for (int i = 1; i <= 8; ++i) {
                send(QEvent::MouseMove, from + (to - from) * i / 8, Qt::LeftButton, mods);
                QTest::qWait(15);
            }
            send(QEvent::MouseButtonRelease, to, Qt::NoButton, mods);
            QTest::qWait(30);
        };
        auto *box = find(window->contentItem(), "overlayBox0");
        QVERIFY(box && box->isVisible());
        const QPoint text = box->mapToScene(QPointF(box->width() / 2, box->height() / 2)).toPoint();

        // A small nudge right stays on the vertical center line.
        drag(text, text + QPoint(5, 0), Qt::NoModifier);
        QTRY_COMPARE(backend.project().overlays[0].x, 0.5);

        // With Ctrl held the same nudge moves it off center.
        drag(text, text + QPoint(5, 0), Qt::ControlModifier);
        QTRY_VERIFY(backend.project().overlays[0].x > 0.505);

        // Dragging it near the middle of the frame snaps it onto y = 0.5.
        auto *frameBox = find(window->contentItem(), "clipBox");
        QVERIFY(frameBox);
        const double frameH = frameBox->height();
        const QPoint now = box->mapToScene(QPointF(box->width() / 2, box->height() / 2)).toPoint();
        const int toMiddle = int(std::round((0.5 - backend.project().overlays[0].y) * frameH)) + 4;
        drag(now, now + QPoint(0, toMiddle), Qt::NoModifier);
        QTRY_VERIFY(std::abs(backend.project().overlays[0].y - 0.5) < 1e-6);

        // The picture's pan comes back to exactly centered when dragged near it.
        const QPoint picture = frameBox->mapToScene(QPointF(frameBox->width() / 2, frameBox->height() - 40)).toPoint();
        drag(picture, picture + QPoint(-4, 0), Qt::NoModifier);
        QTRY_COMPARE(backend.project().clips[0].panX, 0.0);
        drag(picture, picture + QPoint(-4, 0), Qt::ControlModifier);
        QTRY_VERIFY(backend.project().clips[0].panX > 0.0);
    }

    // The real window, offscreen: dragging a clip down off the video lane lays
    // it over the video as a layer; its box moves in the preview; dragging
    // the layer back up puts it on the main track. One undo step each.
    void timelineDragMakesLayer()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1080x1920",
                           "-frames:v", "1", dir.filePath("a.jpg")}));
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=blue:s=1280x720",
                           "-frames:v", "1", dir.filePath("b.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("a.jpg")), 4.0),
                   media::clipFor(media::probe(dir.filePath("b.jpg")), 2.0)};
        for (Clip &c : p.clips)
            c.motion = "none";
        write(dir.filePath("layers.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("layers.reel")));
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 860);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (QQuickItem *child : item->childItems())
                if (QQuickItem *found = find(child, name))
                    return found;
            return nullptr;
        };
        auto send = [&](QEvent::Type type, QPoint pos, Qt::MouseButtons buttons) {
            QMouseEvent e(type, QPointF(pos), window->mapToGlobal(QPointF(pos)), Qt::LeftButton, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &e);
        };
        auto drag = [&](QPoint from, QPoint to) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton);
            for (int i = 1; i <= 10; ++i) {
                send(QEvent::MouseMove, from + (to - from) * i / 10, Qt::LeftButton);
                QTest::qWait(20);
            }
            send(QEvent::MouseButtonRelease, to, Qt::NoButton);
            QTest::qWait(50);
        };

        // Clip 1 (4 s to 6 s) dragged straight down: a layer starting at 4 s.
        auto *clip1 = find(window->contentItem(), "clip1");
        QVERIFY(clip1);
        const QPoint grab = clip1->mapToScene(QPointF(clip1->width() / 2, clip1->height() / 2)).toPoint();
        drag(grab, grab + QPoint(0, int(clip1->height())));
        QTRY_COMPARE(backend.project().layers.size(), 1);
        QCOMPARE(backend.project().clips.size(), 1);
        QVERIFY(std::abs(backend.project().layers[0].start - 4.0) < 0.05);
        // Only the layer is highlighted now, not the clip still on the track.
        QTRY_COMPARE(find(window->contentItem(), "clip0")->property("selected").toBool(), false);
        QTRY_COMPARE(find(window->contentItem(), "layerBar0")->property("selected").toBool(), true);
        QVERIFY(backend.canUndo());

        // The last clip on the track can't be dragged off it.
        auto *clip0 = find(window->contentItem(), "clip0");
        const QPoint grab0 = clip0->mapToScene(QPointF(clip0->width() / 2, clip0->height() / 2)).toPoint();
        drag(grab0, grab0 + QPoint(0, int(clip0->height())));
        QCOMPARE(backend.project().clips.size(), 1);

        // Earlier in time so it overlaps the main clip, then shown in the preview.
        auto *bar = find(window->contentItem(), "layerBar0");
        QVERIFY(bar && bar->isVisible());
        const QPoint barMid = bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2)).toPoint();
        const double pps = bar->width() / 2.0;
        drag(barMid, barMid - QPoint(int(pps * 3), 0));
        QTRY_VERIFY(backend.project().layers[0].start < 1.2);
        QCOMPARE(backend.project().layers.size(), 1);
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, backend.project().layers[0].start + 0.5));
        auto *slot = find(window->contentItem(), "layerSlot0");
        QVERIFY(slot);
        QTRY_VERIFY(slot->isVisible());

        // Selected, its box moves in the preview, then shrinks from a corner.
        backend.setLayerValue(0, "scale", 0.5);
        auto *box = find(window->contentItem(), "layerBox");
        QVERIFY(box);
        QTRY_VERIFY(box->isVisible());
        const double x0 = backend.project().layers[0].x;
        const QPoint mid = box->mapToScene(QPointF(box->width() / 2, box->height() / 2)).toPoint();
        drag(mid, mid + QPoint(40, 0));
        QTRY_VERIFY(backend.project().layers[0].x > x0 + 0.05);
        window->grabWindow().save(qEnvironmentVariable("REELCUT_LAYER_SHOT", dir.filePath("shot.png")));

        // A click on the empty space beside the frame drops the selection.
        auto click = [&](QPoint pos) {
            send(QEvent::MouseButtonPress, pos, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, pos, Qt::NoButton);
            QTest::qWait(50);
        };
        auto *stage = find(window->contentItem(), "stageBackground");
        QVERIFY(stage);
        click(stage->mapToScene(QPointF(4, stage->height() / 2)).toPoint());
        QTRY_COMPARE(window->property("selectionKind").toString(), QString("none"));
        QTRY_VERIFY(!box->isVisible());
        QTRY_COMPARE(find(window->contentItem(), "layerBar0")->property("selected").toBool(), false);

        // Selected again from its bar, then a click on empty timeline space
        // (the layer row, past the layer's end) drops it too.
        bar = find(window->contentItem(), "layerBar0");
        click(bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2)).toPoint());
        QTRY_COMPARE(window->property("selectionKind").toString(), QString("layer"));
        QTRY_VERIFY(box->isVisible());
        click(bar->mapToScene(QPointF(bar->width() + pps * 0.5, bar->height() / 2)).toPoint());
        QTRY_COMPARE(window->property("selectionKind").toString(), QString("none"));
        QTRY_VERIFY(!box->isVisible());
        QTRY_COMPARE(find(window->contentItem(), "layerBar0")->property("selected").toBool(), false);
        click(bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2)).toPoint());
        QTRY_COMPARE(window->property("selectionKind").toString(), QString("layer"));

        // Dragged back up onto the video lane: on the main track again.
        bar = find(window->contentItem(), "layerBar0");
        const QPoint back = bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2)).toPoint();
        drag(back, back - QPoint(0, int(bar->height() * 1.5)));
        QTRY_COMPARE(backend.project().clips.size(), 2);
        QVERIFY(backend.project().layers.isEmpty());
        QTRY_COMPARE(find(window->contentItem(), "clip1")->property("selected").toBool(), true);
        backend.undo();
        QCOMPARE(backend.project().layers.size(), 1);
    }

    // The real window: drop a clip onto an existing layer row to join it,
    // past the last row for a new one, and more than half a row past a
    // neighboring row to swap the two.
    void timelineDropJoinsAndSwapsRows()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=red:s=1080x1920",
                           "-frames:v", "1", dir.filePath("a.jpg")}));
        Project p;
        for (int i = 0; i < 4; ++i)
            p.clips << media::clipFor(media::probe(dir.filePath("a.jpg")), 2.0);
        for (Clip &c : p.clips)
            c.motion = "none";
        write(dir.filePath("rows.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("rows.reel")));
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 860);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (QQuickItem *child : item->childItems())
                if (QQuickItem *found = find(child, name))
                    return found;
            return nullptr;
        };
        auto send = [&](QEvent::Type type, QPoint pos, Qt::MouseButtons buttons) {
            QMouseEvent e(type, QPointF(pos), window->mapToGlobal(QPointF(pos)), Qt::LeftButton, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &e);
        };
        auto drag = [&](QPoint from, QPoint to) {
            send(QEvent::MouseButtonPress, from, Qt::LeftButton);
            for (int i = 1; i <= 10; ++i) {
                send(QEvent::MouseMove, from + (to - from) * i / 10, Qt::LeftButton);
                QTest::qWait(20);
            }
            send(QEvent::MouseButtonRelease, to, Qt::NoButton);
            QTest::qWait(50);
        };

        auto center = [&](const QString &name) {
            QQuickItem *item = find(window->contentItem(), name);
            return item ? item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint() : QPoint();
        };
        auto *timeline = find(window->contentItem(), "clip0")->parentItem();
        QVERIFY(timeline);
        const int videoToLayer = 38 + 6 + 20;  // video lane middle to first layer row middle
        const int pitch = 40 + 6;              // one layer row to the next

        // Clip 3 straight down onto where layer row 1 will be: a new row.
        QPoint from = center("clip3");
        drag(from, from + QPoint(0, videoToLayer));
        QTRY_COMPARE(backend.project().layers.size(), 1);
        QCOMPARE(backend.project().layerRows(), 1);
        // Clip 2 onto that row, overlapping in time: it joins and slides to fit.
        from = center("clip2");
        drag(from, from + QPoint(0, videoToLayer));
        QTRY_COMPARE(backend.project().layers.size(), 2);
        QCOMPARE(backend.project().layerRows(), 1);
        QVERIFY(backend.project().layers[1].start >= backend.project().layers[0].end() - 1e-6);
        // Clip 1 past the last row: a second row.
        from = center("clip1");
        drag(from, from + QPoint(0, videoToLayer + pitch));
        QTRY_COMPARE(backend.project().layerRows(), 2);
        const QString top = backend.project().layers.last().clip.path;
        QCOMPARE(backend.project().layers.last().row, 1);

        // The top row's video dragged up past row 1's middle by more than half
        // a row (half of the taller gap above row 1), so the rows swap.
        from = center(QString("layerBar%1").arg(backend.project().layers.size() - 1));
        drag(from, from - QPoint(0, pitch + videoToLayer * 5 / 8));
        QTRY_COMPARE(backend.project().layers.first().row, 0);
        QTRY_COMPARE(backend.project().layers.first().clip.path, top);
        QCOMPARE(backend.project().layerRows(), 2);
        QCOMPARE(backend.project().layers.last().row, 1);

        // Up just one row: it joins the other row (now row 2 on screen).
        backend.undo();
        QCOMPARE(backend.project().layers.last().row, 1);
        from = center(QString("layerBar%1").arg(backend.project().layers.size() - 1));
        drag(from, from - QPoint(0, pitch));
        QTRY_COMPARE(backend.project().layerRows(), 1);
        QCOMPARE(backend.project().layers.size(), 3);
        Q_UNUSED(timeline);
    }

    // The real window, offscreen: everything on the picture moves and resizes
    // with the mouse, one undo step per gesture, Ctrl+Z / Ctrl+Y.
    void previewItemsDragAndResize()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 4.0)};
        p.clips[0].motion = "none";
        p.clips[0].zoom = 1.5;
        p.clips.append(media::clipFor(media::probe(dir.filePath("photo.jpg")), 2.0));
        p.overlays = {{"drag me", 0, 4, 0.5, 0.3, 96, "pop"}};
        p.overlays[0].x = 0.5;
        p.words = {{"hello", 0.0, 2.0}, {"there", 2.0, 4.0}};
        write(dir.filePath("ui.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("ui.reel")));
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 860);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 1.0));
        QTest::qWait(300);

        // Repeater delegates aren't QObject children, so walk the visual tree.
        std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (QQuickItem *child : item->childItems())
                if (QQuickItem *found = find(child, name))
                    return found;
            return nullptr;
        };
        auto center = [&](const char *name) {
            auto *item = find(window->contentItem(), name);
            if (!item || !item->isVisible())
                return QPoint(-1, -1);
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        };
        auto drag = [&](QPoint from, QPoint to) {
            QTest::mousePress(window, Qt::LeftButton, {}, from);
            for (int i = 1; i <= 8; ++i) {
                QTest::mouseMove(window, from + (to - from) * i / 8);
                QTest::qWait(15);
            }
            QTest::mouseRelease(window, Qt::LeftButton, {}, to);
            QTest::qWait(30);
        };

        // Paused at the very start of a pop entrance, the text is at full size,
        // so its box fits what's on screen.
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 0.0));
        QTest::qWait(100);
        QCOMPARE(find(window->contentItem(), "overlayText0")->scale(), 1.0);
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 1.0));
        QTest::qWait(100);

        // Drag the text overlay right and down.
        const QPoint text = center("overlayBox0");
        QVERIFY(text.x() > 0);
        drag(text, text + QPoint(60, 40));
        QTRY_VERIFY(backend.project().overlays[0].x > 0.6);
        QVERIFY(backend.project().overlays[0].y > 0.33);

        // Pull its bottom-right corner out to make it bigger.
        auto *box = find(window->contentItem(), "overlayBox0");
        const QPoint corner = box->mapToScene(QPointF(box->width(), box->height())).toPoint();
        drag(corner, corner + QPoint(50, 30));
        QTRY_VERIFY(backend.project().overlays[0].size > 110);

        // Ctrl+Z undoes the resize, then the move; Ctrl+Y brings the move back.
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(backend.project().overlays[0].size, 96);
        QVERIFY(backend.project().overlays[0].x > 0.6);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(backend.project().overlays[0].x, 0.5);
        QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
        QTRY_VERIFY(backend.project().overlays[0].x > 0.6);

        // Captions move too.
        const QPoint caption = center("captionBox");
        QVERIFY(caption.x() > 0);
        drag(caption, caption + QPoint(0, -80));
        QTRY_VERIFY(backend.project().captionY < 0.6);

        // Dragging the picture pans it; the wheel zooms it.
        const QPoint picture = center("clipBox") + QPoint(0, 150);
        drag(picture, picture + QPoint(-40, 0));
        QTRY_VERIFY(backend.project().clips[0].panX > 0.05);
        QTest::wheelEvent(window, picture, QPoint(0, 240), QPoint(0, 0));
        QTRY_VERIFY(backend.project().clips[0].zoom > 1.5);

        // In the timeline, dragging a text bar moves it later without changing its length.
        auto *bar = find(window->contentItem(), "textBar0");
        QVERIFY(bar);
        const QPoint barCenter = bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2)).toPoint();
        drag(barCenter, barCenter + QPoint(-200, 0));  // can't go before 0
        QTRY_COMPARE(backend.project().overlays[0].start, 0.0);
        const Overlay moved = backend.project().overlays[0];
        QCOMPARE(moved.end - moved.start, 4.0);

        // Dragging the first clip past the second swaps them.
        auto *first = find(window->contentItem(), "clip0");
        QVERIFY(first);
        const QPoint from = first->mapToScene(QPointF(first->width() / 2, first->height() / 2)).toPoint();
        drag(from, from + QPoint(int(first->width() * 0.9), 0));
        QTRY_COMPARE(backend.project().clips[0].duration(), 2.0);
        QCOMPARE(backend.project().clips[1].duration(), 4.0);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(backend.project().clips[0].duration(), 4.0);
    }

    // The preview runs a transition: both slots hold their clips and the
    // shader shows only inside the overlap.
    void previewShowsTransitions()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        for (int i = 0; i < 3; ++i)
            p.clips.append(media::clipFor(media::probe(dir.filePath("photo.jpg")), 2.0));
        QVERIFY(p.setClipTransition(0, "iris"));
        p.clips[0].transitionSeconds = 1.0;  // clip 1 starts at 1.0
        write(dir.filePath("t.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("t.reel")));
        QCOMPARE(backend.duration(), 5.0);
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 860);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *effect = findItem(window->contentItem(), "transitionEffect");
        auto *even = findItem(window->contentItem(), "slotEven");
        auto *odd = findItem(window->contentItem(), "slotOdd");
        QVERIFY(effect && even && odd);

        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 0.5));
        QTRY_COMPARE(even->property("index").toInt(), 0);
        QCOMPARE(odd->property("index").toInt(), 1);  // preloaded
        QVERIFY(!effect->isVisible());

        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 1.5));
        QTRY_VERIFY(effect->isVisible());
        QCOMPARE(effect->property("progress").toDouble(), 0.5);
        QCOMPARE(effect->property("mode").toDouble(), double(Transitions::find("iris")->previewMode));

        // Past the overlap the second clip shows alone, and the third waits in the freed slot.
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 2.5));
        QTRY_VERIFY(!effect->isVisible());
        QCOMPARE(odd->property("index").toInt(), 1);
        QCOMPARE(even->property("index").toInt(), 2);
    }

    void mcpSetsATransition()
    {
        QTemporaryDir dir, state;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=4", "-c:v", "libx264", source}));
        McpServer server(state.path());
        QJsonObject r = call(server, "create_project", {{"path", dir.filePath("t")}, {"clips", QJsonArray{source}}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        QVERIFY(!call(server, "split", {{"time", 2.0}}).value("isError").toBool());
        r = call(server, "edit_clip", {{"index", 0}, {"transition", "flash"}, {"transition_seconds", 0.8}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        const Project saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("t.reel"))).object(), dir.path());
        QCOMPARE(saved.clips[0].transition, QString("flash"));
        QCOMPARE(saved.overlap(0), 0.8);
        QVERIFY(textOf(r).contains("\"overlap\": 0.8"));
        QVERIFY(call(server, "edit_clip", {{"index", 0}, {"transition", "spin"}}).value("isError").toBool());
    }

    // The Effects tab adds an effect at the playhead; it shows in the lane,
    // selects, drives the preview shader, and Delete removes it.
    void effectsWorkInTheEditor()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 4.0)};
        write(dir.filePath("e.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("e.reel")));
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 900);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 1.0));
        auto *add = findItem(window->contentItem(), "addEffect-glitch");
        QVERIFY(add);
        QMetaObject::invokeMethod(add, "clicked");
        QTRY_COMPARE(backend.project().effects.size(), 1);
        QCOMPARE(backend.project().effects[0].kind, QString("glitch"));
        QCOMPARE(backend.project().effects[0].start, 1.0);
        QCOMPARE(window->property("selectionKind").toString(), QString("effect"));
        QTRY_VERIFY(findItem(window->contentItem(), "effectBar0"));
        QVERIFY(findItem(window->contentItem(), "effectBar0")->isVisible());

        // The preview is inside the effect, so the effect layer is on.
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 1.5));
        QQuickItem *preview = findItem(window->contentItem(), "preview");
        QVERIFY(preview);
        QTRY_COMPARE(preview->property("activeEffect").toMap().value("kind").toString(), QString("glitch"));
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 3.5));
        QTRY_VERIFY(!preview->property("activeEffect").toMap().contains("kind"));

        // Delete removes the selected effect; undo brings it back.
        window->contentItem()->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Delete);
        QTRY_COMPARE(backend.project().effects.size(), 0);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(backend.project().effects.size(), 1);
    }

    // ---- Fisheye text ----

    void fisheyeTextSavesAndLeavesTheAssFile()
    {
        Project p;
        p.clips = {clip(0, 4)};
        p.addOverlay("plain words", 0, 2);
        const int i = p.addOverlay("bent words", 0, 2);
        QVERIFY(p.setOverlayValue(i, "style", "fisheye"));
        QVERIFY(p.setOverlayValue(i, "bulge", 1.7));
        QVERIFY(!p.setOverlayValue(i, "style", "wobbly"));
        QCOMPARE(p.overlays[i].bulge, 1.0);

        const Project back = Project::fromJson(p.toJson(QDir::tempPath()), QDir::tempPath());
        QCOMPARE(back.overlays[0].style, QString("plain"));
        QCOMPARE(back.overlays[i].style, QString("fisheye"));
        QCOMPARE(back.overlays[i].bulge, 1.0);

        // The .ass file draws the plain overlay only; the fisheye one is an image.
        const QString ass = assDocument(p, "#FF0000");
        QVERIFY(ass.contains("plain words"));
        QVERIFY(!ass.contains("bent words"));
    }

    void fisheyeTextBulgesTheMiddle()
    {
        Overlay o;
        o.text = "WIDE\nWIDE\nWIDE";
        o.size = 100;
        o.x = 0.5;
        o.y = 0.5;
        o.style = "fisheye";
        // Width of the text on one row of the frame.
        auto span = [](const QImage &img, int y) {
            int left = img.width(), right = -1;
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 200 && qRed(img.pixel(x, y)) > 200) {
                    left = qMin(left, x);
                    right = qMax(right, x);
                }
            return right - left;
        };
        o.bulge = 0.0;
        const QImage flat = FisheyeText::render(o);
        o.bulge = 1.0;
        const QImage bent = FisheyeText::render(o);
        QCOMPARE(bent.size(), QSize(1080, 1920));
        // The middle line grows, a lot more than the outer lines do.
        const int mid = 960;
        QVERIFY2(span(bent, mid) > span(flat, mid) * 1.3,
                 qPrintable(QString("middle %1 vs %2").arg(span(bent, mid)).arg(span(flat, mid))));
        // Nothing is drawn far from where the overlay sits.
        QCOMPARE(qAlpha(bent.pixel(540, 100)), 0);
        QCOMPARE(qAlpha(bent.pixel(540, 1800)), 0);
    }

    void fisheyeTextRendersThroughFfmpeg()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "color=c=0x202020:size=640x360:rate=30:duration=3", "-c:v", "libx264", source}));
        Project p;
        Clip c;
        c.path = source;
        c.out = 3.0;
        c.width = 640;
        c.height = 360;
        c.audio = false;
        c.sourceDuration = 3.0;
        p.clips = {c};
        const int i = p.addOverlay("FISHEYE", 1.0, 2.0);
        p.setOverlayValue(i, "style", "fisheye");
        p.setOverlayValue(i, "animation", "none");
        p.setOverlayValue(i, "y", 0.5);
        p.setOverlayValue(i, "size", 160);
        QString error;
        const ExportOptions options = prepareExport(p, "#FFD60A", dir.path(), dir.filePath("out.mp4"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        if (!options.assFile.isEmpty()) {
            QFile ass(dir.filePath(options.assFile));
            QVERIFY(ass.open(QIODevice::ReadOnly));
            QVERIFY(!ass.readAll().contains("FISHEYE"));
        }
        QCOMPARE(options.textImages.size(), 1);
        QVERIFY(QFileInfo::exists(options.textImages[0]));

        // Yellow pixels in the middle while it's on, none before it starts.
        auto yellow = [&](double t) {
            const QString png = dir.filePath(QString("f%1.png").arg(t));
            QProcess proc;
            proc.setWorkingDirectory(dir.path());
            proc.start("ffmpeg", frameArgs(p, options, t, png, 540));
            if (!proc.waitForFinished(60000) || proc.exitCode() != 0) {
                qWarning() << proc.readAllStandardError().right(1500);
                return -1;
            }
            const QImage img = QImage(png).convertToFormat(QImage::Format_RGB32);
            int count = 0;
            for (int y = 380; y < 580; ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QRgb px = img.pixel(x, y);
                    count += qRed(px) > 180 && qGreen(px) > 180 && qBlue(px) < 90;
                }
            return count;
        };
        const int before = yellow(0.5), during = yellow(1.5);
        QVERIFY(before >= 0 && during >= 0);
        QCOMPARE(before, 0);
        QVERIFY2(during > 500, qPrintable(QString("only %1 yellow pixels").arg(during)));

        // And the full export runs with it.
        QProcess proc;
        proc.setWorkingDirectory(dir.path());
        ExportOptions full = options;
        full.encoder = "libx264";
        proc.start("ffmpeg", exportArgs(p, full));
        QVERIFY(proc.waitForFinished(180000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().right(2000).constData());
        QVERIFY(qAbs(media::probe(dir.filePath("out.mp4")).duration - 3.0) < 0.15);
    }

    void fisheyePresetWorksInTheEditor()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 4.0)};
        write(dir.filePath("e.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("e.reel")));
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 900);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 1.0));
        auto *add = findItem(window->contentItem(), "addFisheyeText");
        QVERIFY(add);
        QMetaObject::invokeMethod(add, "clicked");
        QTRY_COMPARE(backend.project().overlays.size(), 1);
        QCOMPARE(backend.project().overlays[0].style, QString("fisheye"));
        QCOMPARE(backend.project().overlays[0].start, 1.0);
        // It's an ordinary text overlay: selected, and in the Text tab.
        QCOMPARE(window->property("selectionKind").toString(), QString("overlay"));
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 1.5));
        QTRY_VERIFY(findItem(window->contentItem(), "overlayLens0"));
        QTRY_VERIFY(findItem(window->contentItem(), "overlayLens0")->isVisible());
        QVERIFY(findItem(window->contentItem(), "overlayBox0"));

        // Enter in the text box starts a new line of the overlay.
        {
            auto *field = findItem(window->contentItem(), "overlayTextField0");
            QVERIFY(field);
            field->setProperty("text", "");
            field->forceActiveFocus();
            for (char ch : QByteArray("one"))
                QTest::keyClick(window, ch);
            QTest::keyClick(window, Qt::Key_Return);
            for (char ch : QByteArray("two"))
                QTest::keyClick(window, ch);
            QTRY_COMPARE(backend.project().overlays[0].text, QString("one\ntwo"));
            QCOMPARE(backend.project().overlays.size(), 1);  // Enter didn't trigger anything else
        }

        // Switching it to plain drops the lens.
        backend.setOverlayValue(0, "style", "plain");
        QTRY_VERIFY(!findItem(window->contentItem(), "overlayLens0")->isVisible());

        // With that text selected, the preset bends it instead of adding another.
        QMetaObject::invokeMethod(window, "select", Q_ARG(QVariant, "overlay"), Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(add, "clicked");
        QTRY_COMPARE(backend.project().overlays[0].style, QString("fisheye"));
        QCOMPARE(backend.project().overlays.size(), 1);
        QCOMPARE(backend.project().overlays[0].text, QString("one\ntwo"));
        // Undo puts the plain style back.
        backend.undo();
        QCOMPARE(backend.project().overlays[0].style, QString("plain"));

        // With something else selected, it adds new text again.
        QMetaObject::invokeMethod(window, "select", Q_ARG(QVariant, "clip"), Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(add, "clicked");
        QTRY_COMPARE(backend.project().overlays.size(), 2);
    }

    // ---- Color wheels ----

    void gradeMathFollowsTheWheels()
    {
        using namespace ColorGrade;
        auto run = [](const Grade &g, double r, double gr, double b) { return apply(params(g), {r, gr, b}); };
        auto near = [](double a, double b) { return qAbs(a - b) < 1e-6; };
        // Untouched wheels change nothing.
        const Grade neutral;
        QVERIFY(neutral.isNeutral());
        for (double v : {0.0, 0.25, 0.5, 1.0}) {
            const auto out = run(neutral, v, v * 0.5, 1 - v);
            QVERIFY(near(out[0], v) && near(out[1], v * 0.5) && near(out[2], 1 - v));
        }
        // Lift raises black but leaves white; gain moves white but leaves black.
        Grade lift;
        QVERIFY(lift.set("lift.master", 0.4));
        QVERIFY(run(lift, 0, 0, 0)[0] > 0.15);
        QVERIFY(near(run(lift, 1, 1, 1)[0], 1.0));
        Grade gain;
        QVERIFY(gain.set("gain.master", -0.5));
        QVERIFY(near(run(gain, 0, 0, 0)[0], 0.0));
        QVERIFY(run(gain, 1, 1, 1)[0] < 0.6);
        // Gamma brightens the middle, keeping both ends.
        Grade gamma;
        QVERIFY(gamma.set("gamma.master", 0.5));
        QVERIFY(run(gamma, 0.5, 0.5, 0.5)[1] > 0.6);
        QVERIFY(near(run(gamma, 1, 1, 1)[1], 1.0));
        // The puck at the top of the wheel pushes red; lower right, green; lower left, blue.
        auto push = [&](double x, double y) {
            Grade g;
            g.set("offset.x", x);
            g.set("offset.y", y);
            return run(g, 0.5, 0.5, 0.5);
        };
        auto top = push(0, 1), right = push(0.866, -0.5), left = push(-0.866, -0.5);
        QVERIFY(top[0] > top[1] && top[0] > top[2]);
        QVERIFY(right[1] > right[0] && right[1] > right[2]);
        QVERIFY(left[2] > left[0] && left[2] > left[1]);
        // Saturation 0 is grey; the puck stays in its disc; unknown keys fail.
        Grade grey;
        grey.set("saturation", 0);
        const auto g = run(grey, 1, 0, 0);
        QVERIFY(near(g[0], g[1]) && near(g[1], g[2]));
        Grade far;
        far.set("lift.x", 1);
        far.set("lift.y", 1);
        QVERIFY(qAbs(std::hypot(far.lift.x, far.lift.y) - 1.0) < 1e-9);
        QVERIFY(!far.set("lift.z", 1));
        QVERIFY(!far.set("sparkle", 1));
        QVERIFY(far.set("reset.lift", 0));
        QVERIFY(far.lift.isNeutral());

        // Saved and loaded with the project, per clip and for the reel.
        Project p;
        p.clips = {clip(0, 2)};
        QVERIFY(p.setGrade(0, "gain.x", 0.3));
        QVERIFY(p.setGrade(-1, "contrast", 1.4));
        QVERIFY(!p.setGrade(3, "contrast", 1.4));
        const Project back = Project::fromJson(p.toJson(QDir::tempPath()), QDir::tempPath());
        QCOMPARE(back.clips[0].grade.gain.x, 0.3);
        QCOMPARE(back.look.grade.contrast, 1.4);
        // A split keeps the grade on both pieces.
        Project two = p;
        two.clips[0] = clip(0, 4);
        two.clips[0].grade = p.clips[0].grade;
        QVERIFY(two.split(1.0));
        QCOMPARE(two.clips[1].grade.gain.x, 0.3);

        // The LUT of an untouched grade is the identity.
        const QStringList lines = cubeText(neutral, 2).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.last(), QString("1.000000 1.000000 1.000000"));
        QCOMPARE(lines[2], QString("0.000000 0.000000 0.000000"));
        QCOMPARE(lines[3], QString("1.000000 0.000000 0.000000"));
    }

    void gradesRenderThroughFfmpeg()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("grey.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "color=c=0x808080:size=640x360:rate=30:duration=2", "-c:v", "libx264", source}));
        auto frame = [&](const Project &p, const QString &name) {
            QString error;
            const ExportOptions options = prepareExport(p, "#FFD60A", dir.path(), dir.filePath("o.mp4"), &error);
            if (!error.isEmpty())
                qWarning() << error;
            QProcess proc;
            proc.setWorkingDirectory(dir.path());
            proc.start("ffmpeg", frameArgs(p, options, 1.0, dir.filePath(name), 270));
            if (!proc.waitForFinished(60000) || proc.exitCode() != 0)
                qWarning() << proc.readAllStandardError().right(1500);
            return QImage(dir.filePath(name)).pixel(135, 240);
        };
        Project p;
        Clip c;
        c.path = source;
        c.out = 2.0;
        c.width = 640;
        c.height = 360;
        c.audio = false;
        c.sourceDuration = 2.0;
        p.clips = {c};
        const QRgb plain = frame(p, "plain.png");
        // The clip's wheels: the offset puck at the top pushes red.
        p.setGrade(0, "offset.y", 1.0);
        const QRgb clipGraded = frame(p, "clip.png");
        QVERIFY2(qRed(clipGraded) > qRed(plain) + 20 && qBlue(clipGraded) < qBlue(plain) - 10,
                 qPrintable(QString("plain %1 graded %2").arg(QColor(plain).name(), QColor(clipGraded).name())));
        // The reel's wheels on top: gain down darkens everything.
        p.setGrade(-1, "gain.master", -0.5);
        const QRgb both = frame(p, "both.png");
        QVERIFY(qGray(both) < qGray(clipGraded) - 25);
        QVERIFY(QFileInfo::exists(dir.filePath("grade0.cube")));
        QVERIFY(QFileInfo::exists(dir.filePath("grade.cube")));
    }

    // ---- Text colors ----

    void textColorsSaveAndExport()
    {
        Overlay o;
        QCOMPARE(o.colorOf("fill"), QString("#ffffffff"));
        QCOMPARE(o.colorOf("outline"), QString("#ff000000"));
        QCOMPARE(o.colorOf("box"), QString("#00000000"));
        QVERIFY(o.setColor("fill", "#f00"));
        QCOMPARE(o.fill, QString("#ffff0000"));
        QVERIFY(o.setColor("outline", "transparent"));
        QCOMPARE(o.colorOf("outline"), QString("#00000000"));
        QVERIFY(o.setColor("box", "#80112233"));
        QVERIFY(!o.setColor("fill", "not a color"));
        QVERIFY(o.setColor("fill", "default"));
        QCOMPARE(o.colorOf("fill"), QString("#ffffffff"));
        o.style = "fisheye";
        QCOMPARE(o.colorOf("fill"), QString("#ffffee00"));

        Project p;
        p.clips = {clip(0, 4)};
        const int i = p.addOverlay("colored", 0, 2);
        QVERIFY(p.setOverlayValue(i, "fill", "#ff00ff00"));
        QVERIFY(p.setOverlayValue(i, "shadow", "#80000000"));
        const int j = p.addOverlay("boxed", 0, 2);
        QVERIFY(p.setOverlayValue(j, "box", "#ffffee00"));
        const Project back = Project::fromJson(p.toJson(QDir::tempPath()), QDir::tempPath());
        QCOMPARE(back.overlays[i].fill, QString("#ff00ff00"));
        QCOMPARE(back.overlays[j].box, QString("#ffffee00"));

        // ASS: \\1c is &HBBGGRR&, alpha counts up to transparent; a box uses HookBox.
        const QString ass = assDocument(p, "#FF0000");
        QVERIFY(ass.contains("\\1c&H00FF00&\\1a&H00&"));
        QVERIFY(ass.contains("\\4c&H000000&\\4a&H7F&\\shad5"));
        QVERIFY(ass.contains("HookBox,"));
        QVERIFY(ass.contains(",HookBox,"));
        QVERIFY(ass.contains("\\3c&H00EEFF&"));

        // Fisheye text draws in its fill color.
        Overlay f;
        f.text = "RED";
        f.style = "fisheye";
        f.size = 150;
        f.y = 0.5;
        f.setColor("fill", "#ffff0000");
        f.setColor("shadow", "transparent");
        const QImage img = FisheyeText::render(f);
        int red = 0, other = 0;
        for (int y = 860; y < 1060; y += 2)
            for (int x = 300; x < 780; x += 2) {
                const QRgb px = img.pixel(x, y);
                if (qAlpha(px) > 240)
                    (qRed(px) > 230 && qGreen(px) < 30 ? red : other)++;
            }
        QVERIFY2(red > 200 && other < red / 10, qPrintable(QString("red %1 other %2").arg(red).arg(other)));
    }

    void colorsWorkInTheEditor()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 4.0)};
        p.addOverlay("hello", 0, 3);
        write(dir.filePath("e.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("e.reel")));
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 900);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // The clip's wheels are in the Clip tab, the reel's in the Look tab.
        QMetaObject::invokeMethod(window, "select", Q_ARG(QVariant, "clip"), Q_ARG(QVariant, 0));
        QTRY_VERIFY(findItem(window->contentItem(), "clipGrade"));
        QVERIFY(findItem(window->contentItem(), "lookGrade"));
        QVERIFY(findItem(window->contentItem(), "wheel-Lift"));
        backend.setGrade(0, "gain.master", 0.3);
        QCOMPARE(backend.project().clips[0].grade.gain.master, 0.3);
        QCOMPARE(backend.clips()[0].toMap().value("grade").toMap().value("neutral").toBool(), false);
        // Reset All is one undoable step.
        backend.setGrade(0, "reset", 0);
        QVERIFY(backend.project().clips[0].grade.isNeutral());
        backend.undo();
        QCOMPARE(backend.project().clips[0].grade.gain.master, 0.3);

        // A text's Fill swatch opens the picker; the hex field sets the color.
        QMetaObject::invokeMethod(window, "select", Q_ARG(QVariant, "overlay"), Q_ARG(QVariant, 0));
        auto *swatch = findItem(window->contentItem(), "colorSwatch-Fill");
        QTRY_VERIFY(swatch);
        QMetaObject::invokeMethod(swatch, "clicked");
        QQuickItem *hex = nullptr;
        QTRY_VERIFY((hex = findItem(window->contentItem(), "hexField")) != nullptr);
        hex->setProperty("text", "#3366FF");
        QMetaObject::invokeMethod(hex, "editingFinished");
        QTRY_COMPARE(backend.project().overlays[0].fill, QString("#ff3366ff"));
        // The font picker filters as you type; Enter picks the first match.
        auto *fontButton = findItem(window->contentItem(), "overlayFont0");
        QVERIFY(fontButton);
        QMetaObject::invokeMethod(fontButton, "clicked");
        QObject *fontPopup = fontButton->findChild<QObject *>("fontPopup");
        QVERIFY(fontPopup);
        QTRY_VERIFY(fontPopup->property("opened").toBool());
        const int everything = fontButton->property("matches").toList().size();
        QVERIFY(everything > 5);
        for (char ch : QByteArray("liberation mo"))
            QTest::keyClick(window, ch);
        QTRY_COMPARE(fontButton->property("query").toString(), QString("liberation mo"));
        const QVariantList narrowed = fontButton->property("matches").toList();
        QVERIFY(narrowed.size() >= 1 && narrowed.size() < everything);
        for (const QVariant &f : narrowed)
            QVERIFY2(f.toString().contains("Liberation Mono", Qt::CaseInsensitive), qPrintable(f.toString()));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(backend.project().overlays[0].font, narrowed.first().toString());
        QTRY_VERIFY(!fontPopup->property("visible").toBool());
        auto *bold = findItem(window->contentItem(), "overlayBold0");
        QVERIFY(bold);
        QVERIFY(bold->property("checked").toBool());  // plain text is bold by default
        QMetaObject::invokeMethod(bold, "toggle");
        QMetaObject::invokeMethod(bold, "clicked");
        QTRY_COMPARE(backend.project().overlays[0].bold, 0);
        // The preview's text takes the color.
        QQuickItem *label = findItem(window->contentItem(), "overlayText0");
        QVERIFY(label);
        QCOMPARE(label->property("color").value<QColor>(), QColor("#3366ff"));
    }

    void mcpSetsColors()
    {
        QTemporaryDir dir, state;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=2", "-c:v", "libx264", source}));
        McpServer server(state.path());
        QJsonObject r = call(server, "create_project", {{"path", dir.filePath("t")}, {"clips", QJsonArray{source}}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        r = call(server, "edit_clip", {{"index", 0}, {"color_wheels", QJsonObject{{"lift.master", -0.2}, {"gain.x", 0.4}}}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        r = call(server, "set_look", {{"color_wheels", QJsonObject{{"saturation", 1.3}}}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        r = call(server, "add_text", {{"text", "hi"}, {"fill", "#ff0000"}, {"box", "#cc000000"}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        const Project saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("t.reel"))).object(), dir.path());
        QCOMPARE(saved.clips[0].grade.lift.master, -0.2);
        QCOMPARE(saved.clips[0].grade.gain.x, 0.4);
        QCOMPARE(saved.look.grade.saturation, 1.3);
        QCOMPARE(saved.overlays[0].fill, QString("#ffff0000"));
        QCOMPARE(saved.overlays[0].box, QString("#cc000000"));
        QVERIFY(call(server, "edit_clip", {{"index", 0}, {"color_wheels", QJsonObject{{"glow", 1}}}}).value("isError").toBool());
        QVERIFY(call(server, "edit_text", {{"index", 0}, {"fill", "nope"}}).value("isError").toBool());
    }

    void textTypeOptions()
    {
        Overlay o;
        QCOMPARE(o.fontOf(), QString("Noto Sans"));
        QVERIFY(o.boldOf());
        QCOMPARE(o.outlineWidthOf(), 8.0);
        o.style = "fisheye";
        QCOMPARE(o.fontOf(), QString("Liberation Serif"));
        QVERIFY(!o.boldOf());
        QCOMPARE(o.outlineWidthOf(), 0.0);
        QCOMPARE(o.colorOf("outline"), QString("#ff000000"));  // ready for the thickness slider

        Project p;
        p.clips = {clip(0, 4)};
        const int i = p.addOverlay("typed", 0, 2);
        QVERIFY(p.setOverlayValue(i, "font", "Liberation Serif"));
        QVERIFY(p.setOverlayValue(i, "bold", false));
        QVERIFY(p.setOverlayValue(i, "italic", true));
        QVERIFY(p.setOverlayValue(i, "outlineWidth", 12.5));
        QVERIFY(p.setOverlayValue(i, "shadowDistance", 40));  // clamped to 30
        QVERIFY(p.setOverlayValue(i, "letterSpacing", 5));
        QVERIFY(p.setOverlayValue(i, "shadow", "#ff000000"));
        const Project back = Project::fromJson(p.toJson(QDir::tempPath()), QDir::tempPath());
        const Overlay &b = back.overlays[i];
        QCOMPARE(b.font, QString("Liberation Serif"));
        QCOMPARE(b.bold, 0);
        QVERIFY(b.italic);
        QCOMPARE(b.outlineWidth, 12.5);
        QCOMPARE(b.shadowDistance, 30.0);
        QCOMPARE(b.letterSpacing, 5.0);
        // Back to the defaults.
        QVERIFY(p.setOverlayValue(i, "outlineWidth", -1));
        QVERIFY(p.setOverlayValue(i, "bold", "default"));
        QVERIFY(p.setOverlayValue(i, "font", "default"));
        QCOMPARE(p.overlays[i].outlineWidthOf(), 8.0);
        QVERIFY(p.overlays[i].boldOf());
        QCOMPARE(p.overlays[i].fontOf(), QString("Noto Sans"));

        // In the .ass file as override tags.
        const QString ass = assDocument(back, "#FF0000");
        QVERIFY2(ass.contains("\\fnLiberation Serif\\b0\\i1\\fsp5"), qPrintable(ass.right(400)));
        QVERIFY(ass.contains("\\bord12.5"));
        QVERIFY(ass.contains("\\shad30"));

        // Fisheye: a thicker outline covers more of the image.
        Overlay f;
        f.text = "WIDE";
        f.style = "fisheye";
        f.size = 140;
        f.y = 0.5;
        f.setColor("outline", "#ff0000ff");
        f.setColor("shadow", "transparent");
        auto blue = [](const QImage &img) {
            int n = 0;
            for (int y = 700; y < 1220; y += 2)
                for (int x = 0; x < 1080; x += 2) {
                    const QRgb px = img.pixel(x, y);
                    n += qAlpha(px) > 200 && qBlue(px) > 200 && qRed(px) < 60;
                }
            return n;
        };
        f.outlineWidth = 2;
        const int thin = blue(FisheyeText::render(f));
        f.outlineWidth = 14;
        const int thick = blue(FisheyeText::render(f));
        QVERIFY2(thick > thin * 2, qPrintable(QString("thin %1 thick %2").arg(thin).arg(thick)));
        // No outline, shadow distance 0: the shadow is a soft glow under the
        // letters; only "No shadow" removes it.
        Overlay bare;
        bare.text = "BARE";
        bare.style = "fisheye";
        bare.size = 140;
        bare.y = 0.5;
        bare.outlineWidth = 0;
        bare.shadowDistance = 0;
        auto darkAround = [](const QImage &img) {
            int dark = 0;
            for (int y = 700; y < 1220; y += 2)
                for (int x = 0; x < 1080; x += 2) {
                    const QRgb px = img.pixel(x, y);
                    dark += qAlpha(px) > 20 && qGray(qUnpremultiply(px)) < 80;
                }
            return dark;
        };
        QVERIFY(darkAround(FisheyeText::render(bare)) > 0);
        bare.noShadow = true;
        QCOMPARE(darkAround(FisheyeText::render(bare)), 0);
        // Plain text: no \shad tag with the box ticked.
        Project ns;
        ns.clips = {clip(0, 2)};
        const int n = ns.addOverlay("no shadow", 0, 1);
        ns.setOverlayValue(n, "shadow", "#ff000000");
        QVERIFY(assDocument(ns, "#FF0000").contains("\\shad5"));
        QVERIFY(ns.setOverlayValue(n, "noShadow", true));
        QVERIFY(!assDocument(ns, "#FF0000").contains("\\shad"));
        QVERIFY(Project::fromJson(ns.toJson(QDir::tempPath()), QDir::tempPath()).overlays[n].noShadow);

        // Letter thickness: the letters get heavier in their own color.
        Overlay heavy;
        heavy.text = "thin";
        heavy.style = "fisheye";
        heavy.size = 140;
        heavy.y = 0.5;
        heavy.noShadow = true;
        auto yellow = [](const QImage &img) {
            int n = 0;
            for (int y = 700; y < 1220; y += 2)
                for (int x = 0; x < 1080; x += 2) {
                    const QRgb px = img.pixel(x, y);
                    n += qAlpha(px) > 200 && qRed(px) > 200 && qGreen(px) > 200 && qBlue(px) < 80;
                }
            return n;
        };
        const int light = yellow(FisheyeText::render(heavy));
        heavy.thickness = 10;
        const int bolder = yellow(FisheyeText::render(heavy));
        QVERIFY2(bolder > light * 1.3, qPrintable(QString("light %1 heavy %2").arg(light).arg(bolder)));
        // Plain text: a fill-colored border, and with an outline a second layer on top.
        QVERIFY(ns.setOverlayValue(n, "thickness", 6));
        const QString layered = assDocument(ns, "#FF0000");
        QVERIFY2(layered.contains("\\bord11") && layered.contains("Dialogue: 1,"), qPrintable(layered.right(500)));
        QVERIFY(layered.contains("\\3c&HFFFFFF&\\3a&H00&\\bord3"));
        QVERIFY(ns.setOverlayValue(n, "outline", "transparent"));
        const QString noOutline = assDocument(ns, "#FF0000");
        QVERIFY(!noOutline.contains("Dialogue: 1,"));
        QVERIFY(noOutline.contains("\\3c&HFFFFFF&\\3a&H00&\\bord3"));
        QCOMPARE(Project::fromJson(ns.toJson(QDir::tempPath()), QDir::tempPath()).overlays[n].thickness, 6.0);
        QVERIFY(ns.setOverlayValue(n, "outline", "default"));
        QVERIFY(ns.setOverlayValue(n, "thickness", 0));

        // Letter spacing's Default box: ticked uses the font's spacing but keeps
        // the number; setting a number unticks it.
        QVERIFY(ns.overlays[n].spacingDefault);
        QVERIFY(ns.setOverlayValue(n, "letterSpacing", 8));
        QVERIFY(!ns.overlays[n].spacingDefault);
        QVERIFY(assDocument(ns, "#FF0000").contains("\\fsp8"));
        QVERIFY(ns.setOverlayValue(n, "spacingDefault", true));
        QCOMPARE(ns.overlays[n].letterSpacingOf(), 0.0);
        QCOMPARE(ns.overlays[n].letterSpacing, 8.0);
        QVERIFY(!assDocument(ns, "#FF0000").contains("\\fsp"));
        QVERIFY(ns.setOverlayValue(n, "spacingDefault", false));
        QCOMPARE(ns.overlays[n].letterSpacingOf(), 8.0);
        // Saved projects from before the box keep their spacing on.
        QJsonObject old = ns.toJson(QDir::tempPath());
        QJsonArray list = old["overlays"].toArray();
        QJsonObject first = list[n].toObject();
        first.remove("spacingDefault");
        list[n] = first;
        old["overlays"] = list;
        QCOMPARE(Project::fromJson(old, QDir::tempPath()).overlays[n].letterSpacingOf(), 8.0);
        // And it can be set from the MCP tools.
        QTemporaryDir dir, state;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=2", "-c:v", "libx264", source}));
        McpServer server(state.path());
        QVERIFY(!call(server, "create_project", {{"path", dir.filePath("t")}, {"clips", QJsonArray{source}}}).value("isError").toBool());
        QJsonObject r = call(server, "add_text", {{"text", "hi"}, {"font", "Liberation Serif"}, {"italic", true},
                                                  {"outline_width", 4}, {"letter_spacing", 2}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        const Project saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("t.reel"))).object(), dir.path());
        QCOMPARE(saved.overlays[0].font, QString("Liberation Serif"));
        QVERIFY(saved.overlays[0].italic);
        QCOMPARE(saved.overlays[0].outlineWidth, 4.0);
        QCOMPARE(saved.overlays[0].letterSpacing, 2.0);
    }

    // ---- Posting ----

    void youtubePreviewKeepsDescriptionOutOfTitle()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl("qrc:/SocialPreview.qml"));
        QScopedPointer<QObject> preview(component.create());
        QVERIFY2(preview, qPrintable(component.errorString()));
        preview->setProperty("platform", "youtube");
        preview->setProperty("caption", "An optional description");
        auto *title = preview->findChild<QObject *>("socialCaption");
        QVERIFY(title);
        QCOMPARE(title->property("text").toString(), QString("Add a title…"));
        preview->setProperty("title", "My Short");
        QCOMPARE(title->property("text").toString(), QString("My Short"));
        preview->setProperty("platform", "instagram");
        QCOMPARE(title->property("text").toString(), QString("An optional description"));
    }

    void socialPreviewEditingAndLayout()
    {
        QTemporaryDir state, themeDir;
        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 900);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *chrome = findItem(window->contentItem(), "socialPreview");
        QVERIFY(chrome);
        QVERIFY(chrome->isVisible());
        QCOMPARE(chrome->property("platform").toString(), QString("instagram"));
        backend.setPlatform("tiktok");
        QTRY_COMPARE(chrome->property("platform").toString(), QString("tiktok"));
        auto *toggle = findItem(window->contentItem(), "showAppUi");
        QVERIFY(toggle);
        toggle->setProperty("checked", false);
        QTRY_VERIFY(!chrome->isVisible());
        toggle->setProperty("checked", true);
        QTRY_VERIFY(chrome->isVisible());
        QMetaObject::invokeMethod(findItem(window->contentItem(), "editPostText"), "clicked");
        auto *caption = findItem(window->contentItem(), "previewPostCaption");
        auto *tags = findItem(window->contentItem(), "previewPostHashtags");
        QVERIFY(caption && tags);
        caption->setProperty("text", QString("A long caption ").repeated(50));
        tags->setProperty("text", "travel, #sunset #Travel");
        QTRY_COMPARE(backend.postCaption(), caption->property("text").toString());
        QCOMPARE(backend.postHashtags(), QString("travel, #sunset #Travel"));
        auto *tagLabel = findItem(chrome, "socialHashtags");
        QVERIFY(tagLabel);
        QCOMPARE(tagLabel->property("text").toString(), QString("#travel #sunset"));
        const Project restored = Project::fromJson(backend.project().toJson(state.path()), state.path());
        QCOMPARE(restored.postCaption, backend.postCaption());
        QCOMPARE(restored.postHashtags, backend.postHashtags());
        backend.undo();
        QCOMPARE(backend.postCaption(), QString());
        QCOMPARE(backend.postHashtags(), QString());
        backend.redo();
        QCOMPARE(backend.postCaption(), caption->property("text").toString());
        for (const QString &platform : Posting::ids()) {
            backend.setPlatform(platform);
            QTRY_COMPARE(chrome->property("platform").toString(), platform);
            // Each app's own chrome, not another's.
            QVERIFY2(findItem(chrome, "socialChrome-" + platform), qPrintable(platform));
        for (const QSize size : {QSize(1360, 900), QSize(1000, 700), QSize(1800, 1050)}) {
            window->resize(size);
            QTest::qWait(80);
            for (const QString &name : {QString("socialTop"), QString("socialBody"), QString("socialNav")}) {
                auto *part = findItem(chrome, name);
                QVERIFY(part);
                const QRectF rect = part->mapRectToItem(chrome, part->boundingRect());
                QVERIFY2(rect.left() >= -1 && rect.top() >= -1 && rect.right() <= chrome->width() + 1
                         && rect.bottom() <= chrome->height() + 1, qPrintable(name));
            }
            auto *nav = findItem(chrome, "socialNav");
            const qreal cellWidth = nav->width() / 5;
            for (int i = 0; i < 5; ++i) {
                auto *cell = findItem(nav, "socialNavCell" + QString::number(i));
                QVERIFY(cell);
                QVERIFY2(qAbs(cell->width() - cellWidth) < 1,
                         qPrintable(QString("%1 cell %2: %3, expected %4").arg(platform).arg(i).arg(cell->width()).arg(cellWidth)));
            }
        }
        }
    }

    void postTextAndLimits()
    {
        QCOMPARE(Posting::hashtags("#food travel, #Food #new-york;;  ##x"),
                 (QStringList{"#food", "#travel", "#newyork", "#x"}));
        QCOMPARE(Posting::text("  Hello there  ", "a b"), QString("Hello there\n\n#a #b"));
        QCOMPARE(Posting::text("", "a"), QString("#a"));
        QCOMPARE(Posting::text("Only words", ""), QString("Only words"));
        QVERIFY(Posting::find("tiktok") && Posting::find("instagram"));
        QVERIFY(!Posting::find("myspace"));

        QTemporaryDir state, themeDir;
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVariantMap check = backend.checkPost("instagram", "hi #one", "two three");
        QVERIFY(check.value("ok").toBool());
        QCOMPARE(check.value("hashtags").toInt(), 3);  // counts the one in the caption too
        QStringList many;
        for (int i = 0; i < 31; ++i)
            many << QString("t%1").arg(i);
        check = backend.checkPost("instagram", "", many.join(' '));
        QVERIFY(!check.value("ok").toBool());
        QVERIFY(backend.checkPost("tiktok", "", many.join(' ')).value("ok").toBool());  // no hashtag cap
        QVERIFY(!backend.checkPost("tiktok", QString(2300, 'a'), "").value("ok").toBool());
    }

    void postExportsOpensAndCopies()
    {
        QTemporaryDir dir, state, themeDir, movies;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 1.0)};
        write(dir.filePath("e.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("e.reel")));
        QUrl opened;
        QString revealed;
        backend.openUrl = [&](const QUrl &url) { opened = url; };
        backend.revealFile = [&](const QString &file) { revealed = file; };
        backend.postFolder = movies.path();  // not the real ~/Videos
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 900);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // Post opens the dialog; pick TikTok, type, Next.
        auto *postButton = findItem(window->contentItem(), "postButton");
        QVERIFY(postButton);
        QMetaObject::invokeMethod(postButton, "clicked");
        QQuickItem *caption = nullptr;
        QTRY_VERIFY((caption = findItem(window->contentItem(), "postCaption")) != nullptr);
        auto *tiktok = findItem(window->contentItem(), "postTo-tiktok");
        QVERIFY(tiktok);
        QMetaObject::invokeMethod(tiktok, "clicked");
        caption->setProperty("text", "My trip");
        QObject *postDialog = window->findChild<QObject *>("postDialog");
        QVERIFY(postDialog);
        findItem(window->contentItem(), "postHashtags")->setProperty("text", "travel, #sun");
        auto *next = findItem(window->contentItem(), "postNext");
        QTRY_VERIFY(next->isEnabled());
        QSignalSpy ready(&backend, &Backend::postOpened);
        QMetaObject::invokeMethod(next, "clicked");
        QVERIFY(ready.wait(120000));

        // The video was exported, TikTok's upload page opened, the file shown,
        // the text copied, and what was typed saved with the project.
        const QString file = ready.first().at(1).toString();
        QVERIFY(QFileInfo::exists(file));
        QCOMPARE(file, QDir(movies.path()).filePath("e-tiktok.mp4"));
        QCOMPARE(opened, QUrl("https://www.tiktok.com/tiktokstudio/upload"));
        QCOMPARE(revealed, file);
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("My trip\n\n#travel #sun"));
        QCOMPARE(backend.project().postHashtags, QString("travel, #sun"));
        QCOMPARE(backend.project().platform, QString("tiktok"));
        QTRY_COMPARE(postDialog->property("stage").toString(), QString("ready"));
        QFile::remove(file);
    }

    void textLinesAlignLeftCenterOrRight()
    {
        // Saved, loaded, set by name; anything else is refused.
        Project p;
        p.addOverlay("My\nhandwriting...", 0, 3);
        QCOMPARE(p.overlays[0].align, QString("center"));
        QVERIFY(p.setOverlayValue(0, "align", "left"));
        QVERIFY(!p.setOverlayValue(0, "align", "justify"));
        QCOMPARE(p.overlays[0].align, QString("left"));
        QCOMPARE(Project::fromJson(p.toJson()).overlays[0].align, QString("left"));
        QJsonObject old = p.toJson();
        QJsonArray list = old["overlays"].toArray();
        QJsonObject first = list[0].toObject();
        first.remove("align");
        list[0] = first;
        old["overlays"] = list;
        QCOMPARE(Project::fromJson(old).overlays[0].align, QString("center"));  // older projects

        // The export anchors on the block's edge: left of center for left,
        // right of it for right, lines broken where the preview breaks them.
        auto dialogueOf = [](const Project &project) {
            for (const QString &line : assDocument(project, "#ffffff").split('\n'))
                if (line.startsWith("Dialogue:") && line.contains("Hook"))
                    return line;
            return QString();
        };
        p.overlays[0].animation = "none";
        p.overlays[0].x = 0.5;
        p.setOverlayValue(0, "align", "center");
        QVERIFY(dialogueOf(p).contains("\\an5"));
        QVERIFY(dialogueOf(p).contains("\\pos(540,"));
        p.setOverlayValue(0, "align", "left");
        const QString left = dialogueOf(p);
        QVERIFY2(left.contains("\\an4"), qPrintable(left));
        QVERIFY(left.contains("My\\Nhandwriting..."));
        const int leftX = QRegularExpression("\\\\pos\\((\\d+),").match(left).captured(1).toInt();
        p.setOverlayValue(0, "align", "right");
        const QString right = dialogueOf(p);
        QVERIFY(right.contains("\\an6"));
        const int rightX = QRegularExpression("\\\\pos\\((\\d+),").match(right).captured(1).toInt();
        QVERIFY2(leftX < 540 && rightX > 540, qPrintable(QString("%1 %2").arg(leftX).arg(rightX)));
        QVERIFY(qAbs((540 - leftX) - (rightX - 540)) <= 1);  // the same block, either edge
        QVERIFY(rightX - leftX > 300);  // about as wide as "handwriting..."

        // A long line wraps into explicit lines for libass.
        p.overlays[0].text = "a much longer hook line that will certainly need wrapping across the frame";
        QVERIFY(dialogueOf(p).contains("\\N"));

        // Fisheye text, drawn here: the short line sits at the side picked.
        Overlay f;
        f.style = "fisheye";
        f.text = "WWWWWWWW\nW";
        f.fill = "#ffffffff";
        f.outlineWidth = 0;
        f.noShadow = true;
        auto shortLineX = [](const Overlay &o) {
            const QImage img = FisheyeText::render(o);
            // The bottom line's ink: the lowest row with any, then its mean x.
            for (int y = img.height() - 1; y >= 0; --y) {
                double sum = 0;
                int n = 0;
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 128) { sum += x; ++n; }
                if (n > 0)
                    return sum / n;
            }
            return -1.0;
        };
        f.align = "left";
        const double l = shortLineX(f);
        f.align = "center";
        const double c = shortLineX(f);
        f.align = "right";
        const double r = shortLineX(f);
        QVERIFY2(l < c - 50 && c < r - 50, qPrintable(QString("%1 %2 %3").arg(l).arg(c).arg(r)));
    }

    void scheduleTimesReadTheWayPeopleTypeThem()
    {
        QTemporaryDir state, themeDir;
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        const QDateTime now = QDateTime::currentDateTime();
        auto at = [&](const QString &text) {
            const QVariantMap m = backend.parseWhen(text);
            return m.value("ok").toBool() ? QDateTime::fromMSecsSinceEpoch(qint64(m.value("ms").toDouble()))
                                          : QDateTime();
        };
        const QDate tomorrow = now.date().addDays(1);
        QCOMPARE(at("tomorrow 9am"), QDateTime(tomorrow, QTime(9, 0)));
        QCOMPARE(at("Tomorrow at 9:30 PM"), QDateTime(tomorrow, QTime(21, 30)));
        QCOMPARE(at("tomorrow 18:05"), QDateTime(tomorrow, QTime(18, 5)));
        QCOMPARE(at(tomorrow.toString("yyyy-MM-dd") + " 07:15"), QDateTime(tomorrow, QTime(7, 15)));
        // A weekday is the next one, never today.
        const QDateTime friday = at("fri 10am");
        QCOMPARE(friday.date().dayOfWeek(), 5);
        QVERIFY(friday.date() > now.date() && friday.date() <= now.date().addDays(7));
        // A bare time already gone today means tomorrow.
        const QTime earlier = now.time().addSecs(-3600);
        if (earlier < now.time()) {
            QCOMPARE(at(earlier.toString("HH:mm")).date(), tomorrow);
        }
        QVERIFY(backend.parseWhen("tomorrow 9am").value("label").toString().contains("in "));

        // What can't be posted at.
        QVERIFY(!at("yesterday 9am").isValid());
        QVERIFY(!at("tomorrow").isValid());
        QVERIFY(backend.parseWhen("tomorrow").value("problem").toString().contains("time"));
        QVERIFY(!at(now.date().addDays(-2).toString("yyyy-MM-dd") + " 10:00").isValid());
        QVERIFY(backend.parseWhen(now.date().addDays(90).toString("yyyy-MM-dd") + " 10:00")
                    .value("problem").toString().contains("75 days"));
        QVERIFY(!at("tomorrow 25:00").isValid());
        QVERIFY(!at("tomorrow 13pm").isValid());
        QVERIFY(!at("").isValid());

        // The suggestion is a whole hour, an hour or two out; the buttons move it.
        const QDateTime suggested = at(backend.suggestWhen());
        QVERIFY(suggested.isValid());
        QCOMPARE(suggested.time().minute(), 0);
        QVERIFY(now.secsTo(suggested) >= 3600 && now.secsTo(suggested) <= 7200);
        QCOMPARE(at(backend.suggestWhen("tomorrow 9am", 60)), QDateTime(tomorrow, QTime(10, 0)));
        QCOMPARE(at(backend.suggestWhen("tomorrow 9am", 1440)), QDateTime(tomorrow.addDays(1), QTime(9, 0)));
    }

    void schedulerUploadsTheVideoAndSchedulesItAsTheAccount()
    {
        QTemporaryDir state, dir;
        FakeConvex convex;
        PostScheduler scheduler;
        scheduler.stateDir = state.path();
        QSignalSpy refreshed(&scheduler, &PostScheduler::postsChanged);
        scheduler.configure(convex.base() + "/", convex.key);
        QVERIFY(scheduler.configured());
        QVERIFY(refreshed.wait(5000));
        QCOMPARE(QFileInfo(scheduler.configFile()).permissions() & (QFile::ReadOther | QFile::ReadGroup),
                 QFileDevice::Permissions());

        write(dir.filePath("reel.mp4"), QByteArray(5000, 'v'));
        const QDateTime when = QDateTime::currentDateTime().addDays(1);
        QSignalSpy done(&scheduler, &PostScheduler::scheduled);
        convex.calls.clear();
        scheduler.schedule(dir.filePath("reel.mp4"), "trip", "My trip\n\n#travel", when,
                           {"17841400000000001", "reelcut_test", "good-token", "https://graph.instagram.com"});
        QVERIFY(done.wait(5000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(convex.calls.at(0).function, QString("posts:uploadUrl"));
        QCOMPARE(convex.calls.at(1).path, QString("/upload"));
        QCOMPARE(convex.calls.at(1).contentType, QString("video/mp4"));
        QCOMPARE(convex.calls.at(1).body.size(), 5000);
        const FakeConvex::Call &sched = convex.calls.at(2);
        QCOMPARE(sched.function, QString("posts:schedule"));
        QCOMPARE(sched.args.value("secret").toString(), convex.key);
        QCOMPARE(sched.args.value("storageId").toString(), QString("kg2storage"));
        QCOMPARE(sched.args.value("caption").toString(), QString("My trip\n\n#travel"));
        QCOMPARE(sched.args.value("igUserId").toString(), QString("17841400000000001"));  // exactly, as text
        QCOMPARE(sched.args.value("token").toString(), QString("good-token"));
        QCOMPARE(qint64(sched.args.value("publishAt").toDouble()), when.toMSecsSinceEpoch());
        QTRY_COMPARE(scheduler.posts().size(), 1);

        // Cancelling goes through and the list follows.
        scheduler.cancel("p1");
        QTRY_COMPARE(scheduler.posts().first().toMap().value("status").toString(), QString("cancelled"));

        // A wrong key says so.
        scheduler.configure(convex.base(), "wrong-key-0000000000");
        QTRY_COMPARE(scheduler.lastError(), QString("Wrong scheduler key."));
        done.clear();
        scheduler.schedule(dir.filePath("reel.mp4"), "trip", "x", when, {"1", "u", "t", "https://graph.instagram.com"});
        QVERIFY(done.wait(5000));
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(done.first().at(1).toString().contains("Wrong scheduler key"));
    }

    void postDialogSchedulesAnInstagramReel()
    {
        QTemporaryDir dir, state, themeDir, movies;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 1.0)};
        write(dir.filePath("s.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        FakeInstagram instagram;
        FakeConvex convex;
        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        backend.openUrl = [](const QUrl &) {};
        backend.revealFile = [](const QString &) {};
        backend.postFolder = movies.path();
        backend.instagram()->graphBase = instagram.base();
        QSignalSpy connected(backend.instagram(), &InstagramApi::connectFinished);
        backend.connectInstagram("good-token");
        QVERIFY(connected.wait(5000));
        backend.setupScheduler(convex.base(), convex.key);
        QVERIFY(backend.schedulerReady());
        QVERIFY(backend.openFile(dir.filePath("s.reel")));

        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 900);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QMetaObject::invokeMethod(findItem(window->contentItem(), "postButton"), "clicked");
        QQuickItem *caption = nullptr;
        QTRY_VERIFY((caption = findItem(window->contentItem(), "postCaption")) != nullptr);
        QMetaObject::invokeMethod(findItem(window->contentItem(), "postTo-instagram"), "clicked");
        caption->setProperty("text", "Later");
        QObject *dialog = window->findChild<QObject *>("postDialog");
        auto *later = findItem(window->contentItem(), "postScheduleLater");
        QVERIFY(later);
        QTRY_VERIFY(later->isVisible() && later->isEnabled());
        // Queueing into open slots is the default; pick a set time instead.
        QVERIFY(dialog->property("queueing").toBool());
        QMetaObject::invokeMethod(later, "clicked");
        QTRY_VERIFY(dialog->property("scheduling").toBool());
        auto *when = findItem(window->contentItem(), "postWhen");
        auto *next = findItem(window->contentItem(), "postNext");
        QCOMPARE(next->property("text").toString(), QString("Schedule"));
        // A time that can't be used holds the button.
        when->setProperty("text", "yesterday 9am");
        QTRY_VERIFY(!next->isEnabled());
        when->setProperty("text", "tomorrow 9am");
        QTRY_VERIFY(next->isEnabled());

        QSignalSpy scheduled(&backend, &Backend::postScheduled);
        convex.calls.clear();
        QMetaObject::invokeMethod(next, "clicked");
        QVERIFY(scheduled.wait(120000));
        QVERIFY2(scheduled.first().at(0).toBool(), qPrintable(scheduled.first().at(1).toString()));
        QTRY_COMPARE(dialog->property("stage").toString(), QString("scheduled"));

        const auto sched = std::find_if(convex.calls.begin(), convex.calls.end(),
                                        [](const FakeConvex::Call &c) { return c.function == "posts:schedule"; });
        QVERIFY(sched != convex.calls.end());
        QCOMPARE(qint64(sched->args.value("publishAt").toDouble()),
                 QDateTime(QDate::currentDate().addDays(1), QTime(9, 0)).toMSecsSinceEpoch());
        QCOMPARE(sched->args.value("caption").toString(), QString("Later"));
        QCOMPARE(sched->args.value("username").toString(), QString("reelcut_test"));
        QCOMPARE(sched->args.value("igUserId").toString(), QString("17841400000000001"));
        // Nothing went to Instagram now: that's the scheduler's job at 9am.
        for (const auto &call : instagram.calls)
            QVERIFY2(!call.path.endsWith("/media") && !call.path.endsWith("/media_publish"), qPrintable(call.path));
        QTRY_COMPARE(backend.scheduledPosts().size(), 1);
        QTRY_VERIFY(findItem(window->contentItem(), "scheduledPost") != nullptr);
    }

    void postDialogQueuesIntoNextOpenSlots()
    {
        QTemporaryDir dir, state, themeDir, movies;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 1.0)};
        write(dir.filePath("q.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        FakeInstagram instagram;
        FakeConvex convex;
        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        backend.openUrl = [](const QUrl &) {};
        backend.revealFile = [](const QString &) {};
        backend.postFolder = movies.path();
        backend.instagram()->graphBase = instagram.base();
        QSignalSpy connected(backend.instagram(), &InstagramApi::connectFinished);
        backend.connectInstagram("good-token");
        QVERIFY(connected.wait(5000));
        backend.setupScheduler(convex.base(), convex.key);
        QVERIFY(backend.openFile(dir.filePath("q.reel")));
        QVariantMap plan = backend.property("schedulePreferences").toMap();
        plan["instagram"] = QVariantMap{{"startMinute", 0}, {"spacingMinutes", 60}};
        QVERIFY(backend.setProperty("schedulePreferences", plan));

        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 900);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QMetaObject::invokeMethod(findItem(window->contentItem(), "postButton"), "clicked");
        QQuickItem *caption = nullptr;
        QTRY_VERIFY((caption = findItem(window->contentItem(), "postCaption")) != nullptr);
        QMetaObject::invokeMethod(findItem(window->contentItem(), "postTo-instagram"), "clicked");
        caption->setProperty("text", "Queued");
        QObject *dialog = window->findChild<QObject *>("postDialog");
        // Queueing is the default: no publishing now.
        QVERIFY(dialog->property("queueing").toBool());
        dialog->setProperty("queueCount", 3);
        const QVariantList expected = backend.nextOpenSlots({"instagram"}, 3);
        QCOMPARE(expected.size(), 3);
        QTRY_COMPARE(dialog->property("queueSlots").toList(), expected);
        auto *next = findItem(window->contentItem(), "postNext");
        QCOMPARE(next->property("text").toString(), QString("Add to queue"));
        QTRY_VERIFY(next->isEnabled());

        QSignalSpy scheduled(&backend, &Backend::postScheduled);
        QSignalSpy published(&backend, &Backend::postPublished);
        convex.calls.clear();
        QMetaObject::invokeMethod(next, "clicked");
        QVERIFY(scheduled.wait(120000));
        QVERIFY2(scheduled.first().at(0).toBool(), qPrintable(scheduled.first().at(1).toString()));
        QVERIFY(published.isEmpty());
        QList<qint64> times;
        for (const auto &call : convex.calls)
            if (call.function == "posts:schedule") times << qint64(call.args.value("publishAt").toDouble());
        QCOMPARE(times.size(), 3);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(times[i], qint64(expected[i].toMap().value("publishAt").toDouble()));
        for (const auto &call : instagram.calls)
            QVERIFY2(!call.path.endsWith("/media") && !call.path.endsWith("/media_publish"), qPrintable(call.path));
        // Those slots are taken now: the next three are later ones.
        QTRY_COMPARE(backend.scheduledPosts().size(), 3);
        const QVariantList after = backend.nextOpenSlots({"instagram"}, 1);
        QCOMPARE(after.size(), 1);
        QVERIFY(after.first().toMap().value("publishAt").toDouble() > expected.last().toMap().value("publishAt").toDouble());
    }

    void instagramUnifiedLoginConnectsAndPublishes()
    {
        QTemporaryDir state, dir;
        FakeInstagram fake;
        fake.unified = true;
        InstagramApi api;
        api.stateDir = state.path();
        api.graphBase = fake.base();
        api.pollIntervalMs = 20;
        QSignalSpy connected(&api, &InstagramApi::connectFinished);
        api.connectToken("  EAA-test-token\n");
        QVERIFY(connected.wait(5000));
        QVERIFY2(connected.first().at(0).toBool(), qPrintable(connected.first().at(1).toString()));
        QCOMPARE(api.username(), QString("unified_test"));
        QCOMPARE(fake.calls.first().path, QString("/me/assigned_instagram_accounts"));
        QCOMPARE(fake.calls.first().query.queryItemValue("fields"), QString("id,username"));

        write(dir.filePath("reel.mp4"), QByteArray(5000, 'v'));
        QSignalSpy done(&api, &InstagramApi::published);
        fake.calls.clear();
        api.publish(dir.filePath("reel.mp4"), "Unified test");
        QVERIFY(done.wait(10000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(fake.calls.first().path, QString("/17841400000000001/media"));
        QCOMPARE(done.first().at(2).toUrl(), QUrl("https://www.instagram.com/reel/abc/"));

        // Both rejected token routes must preserve the working connection.
        for (const QString &badToken : {QString("bad-token"), QString("EAA-bad-token")}) {
            connected.clear();
            api.connectToken(badToken);
            QVERIFY(connected.wait(5000));
            QVERIFY(!connected.first().at(0).toBool());
            QCOMPARE(api.username(), QString("unified_test"));
            done.clear();
            api.publish(dir.filePath("reel.mp4"), "Still connected");
            QVERIFY(done.wait(10000));
            QVERIFY(done.first().at(0).toBool());
        }

        // Reload an older token: Unified Login has a different renewal API.
        QFile account(api.accountFile());
        QVERIFY(account.open(QIODevice::ReadOnly));
        QJsonObject saved = QJsonDocument::fromJson(account.readAll()).object();
        account.close();
        saved["issued"] = QDateTime::currentDateTimeUtc().addDays(-8).toString(Qt::ISODate);
        write(api.accountFile(), QJsonDocument(saved).toJson());
        fake.calls.clear();
        InstagramApi again;
        again.stateDir = state.path();
        again.graphBase = fake.base();
        again.pollIntervalMs = 20;
        QTRY_VERIFY(again.connected());
        QCOMPARE(again.username(), QString("unified_test"));
        QSignalSpy reloadedDone(&again, &InstagramApi::published);
        again.publish(dir.filePath("reel.mp4"), "After restart");
        QVERIFY(reloadedDone.wait(10000));
        QVERIFY(reloadedDone.first().at(0).toBool());
        QCOMPARE(fake.calls.first().path, QString("/17841400000000001/media"));
        for (const auto &call : fake.calls)
            QVERIFY(!call.path.contains("refresh_access_token"));
    }

    void instagramUnifiedLoginRejectsAmbiguousAccounts_data()
    {
        QTest::addColumn<int>("count");
        QTest::newRow("no account") << 0;
        QTest::newRow("multiple accounts") << 2;
    }

    void instagramUnifiedLoginRejectsAmbiguousAccounts()
    {
        QFETCH(int, count);
        QTemporaryDir state;
        FakeInstagram fake;
        fake.unified = true;
        fake.assignedAccounts = count;
        InstagramApi api;
        api.stateDir = state.path();
        api.graphBase = fake.base();
        QSignalSpy connected(&api, &InstagramApi::connectFinished);
        api.connectToken("EAA-test-token");
        QVERIFY(connected.wait(5000));
        QVERIFY(!connected.first().at(0).toBool());
        QVERIFY(connected.first().at(1).toString().contains(count ? "more than one" : "no Instagram", Qt::CaseInsensitive));
        QVERIFY(!api.connected());
        QVERIFY(!QFileInfo::exists(api.accountFile()));
    }

    // Instagram's API, against a stand-in server: connect, then publish a reel.
    void instagramPublishesThroughItsApi()
    {
        QTemporaryDir state, dir;
        FakeInstagram fake;
        InstagramApi api;
        api.stateDir = state.path();
        api.graphBase = fake.base();
        api.pollIntervalMs = 20;

        QSignalSpy connected(&api, &InstagramApi::connectFinished);
        api.connectToken("bad-token");
        QVERIFY(connected.wait(5000));
        QVERIFY(!connected.takeFirst().at(0).toBool());
        QVERIFY(!api.connected());
        api.connectToken("good-token");
        QVERIFY(connected.wait(5000));
        QVERIFY2(connected.first().at(0).toBool(), qPrintable(connected.first().at(1).toString()));
        QCOMPARE(api.username(), QString("reelcut_test"));
        // Kept for next time, readable only by you.
        const auto perms = QFile::permissions(api.accountFile());
        QVERIFY(perms & QFileDevice::ReadOwner);
        QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther)));
        InstagramApi again;
        again.stateDir = state.path();
        QTRY_VERIFY(again.connected());

        write(dir.filePath("reel.mp4"), QByteArray(5000, 'v'));
        QSignalSpy done(&api, &InstagramApi::published);
        fake.calls.clear();
        api.publish(dir.filePath("reel.mp4"), "My trip + more\n\n#travel");
        QVERIFY(done.wait(10000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(done.first().at(2).toUrl(), QUrl("https://www.instagram.com/reel/abc/"));
        // The calls, in order, as the API wants them.
        const auto &c = fake.calls;
        QCOMPARE(c[0].path, QString("/17841400000000001/media"));
        QCOMPARE(c[0].query.queryItemValue("media_type"), QString("REELS"));
        QCOMPARE(c[0].query.queryItemValue("upload_type"), QString("resumable"));
        QCOMPARE(c[0].query.queryItemValue("caption", QUrl::FullyDecoded), QString("My trip + more\n\n#travel"));
        QCOMPARE(c[1].path, QString("/upload/C1"));
        QCOMPARE(c[1].body.size(), 5000);
        QCOMPARE(c[1].headers.value("file_size"), QString("5000"));
        QCOMPARE(c[2].path, QString("/C1"));  // IN_PROGRESS
        QCOMPARE(c[3].path, QString("/C1"));  // FINISHED
        QCOMPARE(c[4].path, QString("/17841400000000001/media_publish"));
        QCOMPARE(c[4].query.queryItemValue("creation_id"), QString("C1"));
        QCOMPARE(c[5].path, QString("/M1"));

        // Instagram refusing the video stops with its reason.
        fake.containerStatus = "ERROR";
        fake.inProgressFor = 0;
        done.clear();
        api.publish(dir.filePath("reel.mp4"), "x");
        QVERIFY(done.wait(10000));
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(done.first().at(1).toString().contains("bad video"));

        api.disconnect();
        QVERIFY(!api.connected());
        QVERIFY(!QFileInfo::exists(api.accountFile()));
    }

    void postDialogPublishesToInstagram()
    {
        QTemporaryDir dir, state, themeDir, movies;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 1.0)};
        write(dir.filePath("e.reel"), QJsonDocument(p.toJson(dir.path())).toJson());
        FakeInstagram fake;

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("e.reel")));
        QUrl opened;
        backend.openUrl = [&](const QUrl &url) { opened = url; };
        backend.revealFile = [](const QString &) {};
        backend.postFolder = movies.path();
        backend.instagram()->graphBase = fake.base();
        backend.instagram()->pollIntervalMs = 20;
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 900);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // Connect through the dialog's Connect page.
        QMetaObject::invokeMethod(findItem(window->contentItem(), "postButton"), "clicked");
        QObject *dialog = window->findChild<QObject *>("postDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(findItem(window->contentItem(), "postTo-instagram"));
        QMetaObject::invokeMethod(findItem(window->contentItem(), "postTo-instagram"), "clicked");
        QMetaObject::invokeMethod(findItem(window->contentItem(), "instagramConnect"), "clicked");
        QTRY_COMPARE(dialog->property("stage").toString(), QString("connect"));
        findItem(window->contentItem(), "instagramToken")->setProperty("text", "good-token");
        QMetaObject::invokeMethod(findItem(window->contentItem(), "instagramConnectGo"), "clicked");
        QTRY_COMPARE(backend.instagramUser(), QString("reelcut_test"));
        QTRY_COMPARE(dialog->property("stage").toString(), QString("write"));
        QVERIFY(dialog->property("publishing").toBool());

        // Publish now (not the default): export, then the API, then the post's link.
        QMetaObject::invokeMethod(findItem(window->contentItem(), "postNow"), "clicked");
        findItem(window->contentItem(), "postCaption")->setProperty("text", "Hello");
        QSignalSpy published(&backend, &Backend::postPublished);
        QMetaObject::invokeMethod(findItem(window->contentItem(), "postNext"), "clicked");
        QVERIFY(published.wait(120000));
        QVERIFY2(published.first().at(0).toBool(), qPrintable(published.first().at(1).toString()));
        QTRY_COMPARE(dialog->property("stage").toString(), QString("published"));
        QVERIFY(dialog->property("publishedOk").toBool());
        QVERIFY(opened.isEmpty());  // no browser: the API posted it
        QCOMPARE(fake.calls.last().path, QString("/M1"));

        // When Instagram refuses it, the by-hand way opens instead.
        fake.containerStatus = "ERROR";
        fake.inProgressFor = 0;
        dialog->setProperty("stage", "write");
        published.clear();
        QMetaObject::invokeMethod(findItem(window->contentItem(), "postNext"), "clicked");
        QVERIFY(published.wait(120000));
        QVERIFY(!published.first().at(0).toBool());
        QCOMPARE(opened, QUrl("https://www.instagram.com/"));
        backend.disconnectInstagram();
    }

    // Facebook Pages through the Reels Publishing API, against a stand-in.
    void automationInventoryCoversFunctionalApis()
    {
        const auto inventory=AutomationRegistry::inventory();
        QCOMPARE(inventory.value("unsupported").toArray().size(),0);
        QVERIFY(inventory.value("exposedMethods").toInt()>80);
        QVERIFY(inventory.value("mappedMethods").toInt()>=8);
        QCOMPARE(inventory.value("coveragePercent").toInt(),100);
        if(!qEnvironmentVariableIsEmpty("REELCUT_MCP_INVENTORY"))write(qEnvironmentVariable("REELCUT_MCP_INVENTORY"),QJsonDocument(inventory).toJson());
        const auto tools=AutomationRegistry::tools();
        bool posting=false,accounts=false,layers=false;
        for (const auto &tool:tools) {
            const auto name=tool.toObject().value("name").toString();
            posting |= name=="app_backend_post_to";
            accounts |= name=="app_accounts_connect_token";
            layers |= name=="app_backend_move_clip_to_layer";
        }
        QVERIFY(posting&&accounts&&layers);
    }

    void automationLiveCallsUseBackendAndRejectUnknownMethods()
    {
        QTemporaryDir state; Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
        AutomationRegistry registry; registry.add("backend",&backend); registry.add("accounts",backend.accountSettings());
        QTest::qWait(1);
        auto changed=registry.invoke("backend","setPlatform",{{"platform","facebook"}});
        QVERIFY2(!changed.value("isError").toBool(),qPrintable(QString::fromUtf8(QJsonDocument(changed).toJson())));
        QCOMPARE(backend.platform(),QString("facebook"));
        auto bad=registry.invoke("backend","deleteLater",{});QVERIFY(bad.value("isError").toBool());
        bad=registry.invoke("backend","setPlatform",{{"platform",99}});QVERIFY(bad.value("isError").toBool());
        bad=registry.invoke("accounts","token",{});QVERIFY(bad.value("isError").toBool());
        const auto snapshot=registry.state("accounts");
        QVERIFY(!snapshot.contains("token"));
        QCOMPARE(snapshot.value("entries").toArray().size(),4);
    }

    void automationLocalSocketControlsRealBackend()
    {
        QTemporaryDir state; Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
        AppBridge bridge(state.path()); bridge.registry().add("backend",&backend); bridge.registry().add("accounts",backend.accountSettings());
        QVERIFY(bridge.listen());
        auto result=AppBridgeClient::call(state.path(),{{"action","invoke"},{"component","backend"},{"method","setPlatform"},{"arguments",QJsonObject{{"platform","youtube"}}}});
        QVERIFY2(!result.value("isError").toBool(),qPrintable(QString::fromUtf8(QJsonDocument(result).toJson())));
        QCOMPARE(backend.platform(),QString("youtube"));
        auto instances=AppBridgeClient::instances(state.path());QCOMPARE(instances.size(),1);
        auto status=AppBridgeClient::call(state.path(),{{"action","state"},{"component","backend"}});
        QCOMPARE(status.value("state").toObject().value("platform").toString(),QString("youtube"));
        AppBridge second(state.path());second.registry().add("backend",&backend);QVERIFY(second.listen());
        auto ambiguous=AppBridgeClient::call(state.path(),{{"action","state"},{"component","backend"}});
        QVERIFY(ambiguous.value("isError").toBool());
        auto explicitCall=AppBridgeClient::call(state.path(),{{"action","state"},{"component","backend"}},bridge.instanceId());
        QVERIFY(!explicitCall.value("isError").toBool());
    }

    void automationUiControlsLiveViewsAndPostDraft()
    {
        QTemporaryDir state,themeDir;
        QImage image(64,64,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(state.filePath("photo.png")));
        Project project;project.clips={media::clipFor(media::probe(state.filePath("photo.png")),4.0)};
        write(state.filePath("draft.reel"),QJsonDocument(project.toJson(state.path())).toJson());
        Theme theme(themeDir.path());Backend backend(nullptr,nullptr,&theme,nullptr,state.path());QVERIFY(backend.openFile(state.filePath("draft.reel")));
        HiggsfieldCredits credits;credits.stateDir=state.path();
        Stats stats(backend.instagram(),backend.facebook(),backend.youtube());stats.stateDir=state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme",&theme);engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("downloads",testDownloads());engine.rootContext()->setContextProperty("higgsfield",&credits);engine.rootContext()->setContextProperty("stats",&stats);
        engine.load(QUrl("qrc:/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());window->resize(1360,860);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        AppBridge bridge(state.path());bridge.attach(&backend,testDownloads(),&stats,&credits,window);QVERIFY(bridge.listen());
        auto action=[&](const QString &name,const QJsonObject &args){return AppBridgeClient::call(state.path(),{{"action","invoke"},{"component","ui"},{"method",name},{"arguments",args}});};
        auto settings=action("navigate",{{"page","settings"},{"platform","facebook"}});
        QVERIFY2(!settings.value("isError").toBool(),qPrintable(QString::fromUtf8(QJsonDocument(settings).toJson())));
        QCOMPARE(settings.value("result").toObject().value("page").toString(),QString("settings"));
        auto *token=findItem(window->contentItem(),"settingsToken");QVERIFY(token);token->setProperty("text","UNSUBMITTED-UI-SECRET");
        auto snapshot=action("state",{});QVERIFY(!QJsonDocument(snapshot).toJson().contains("UNSUBMITTED-UI-SECRET"));
        QTest::qWait(80);
        auto privateShot=AppBridgeClient::call(state.path(),{{"action","screenshot"}});
        QVERIFY(privateShot.value("redactedFields").toArray().contains("settingsToken"));
        QVERIFY(!QImage::fromData(QByteArray::fromBase64(privateShot.value("data").toString().toLatin1())).isNull());
        auto tab=action("settingsTab",{{"tab","scheduling"}});
        QVERIFY(!tab.value("isError").toBool());
        QCOMPARE(tab.value("result").toObject().value("settings").toObject().value("tab").toString(),QString("scheduling"));
        auto timing=action("scheduleTiming",{{"fields",QJsonObject{{"facebook",QJsonObject{{"start","06:00"},{"spacingMinutes",90}}}}},{"apply",true}});
        QVERIFY2(!timing.value("isError").toBool(),qPrintable(QString::fromUtf8(QJsonDocument(timing).toJson())));
        QCOMPARE(timing.value("result").toObject().value("draft").toObject().value("facebook").toObject().value("start").toString(),QString("06:00"));
        QVERIFY(!action("navigate",{{"page","editor"},{"platform",""}}).value("isError").toBool());
        QVERIFY(!action("seek",{{"seconds",1.25}}).value("isError").toBool());QCOMPARE(window->property("playhead").toDouble(),1.25);
        QVERIFY(!action("setOptions",{{"options",QJsonObject{{"safeZones",false},{"showAppUi",false},{"timelineZoom",2}}}}).value("isError").toBool());
        QVERIFY(!window->property("safeZones").toBool());
        QVERIFY(!action("setOptions",{{"options",QJsonObject{{"musicHeight",320}}}}).value("isError").toBool());
        const auto selectionBefore=window->property("selectionKind");
        QVERIFY(!action("select",{{"kind","music"},{"index",0}}).value("isError").toBool());
        QCOMPARE(window->property("selectionKind"),selectionBefore);
        QVERIFY(action("setOptions",{{"options",QJsonObject{{"safeZones",true},{"unknown",42}}}}).value("isError").toBool());
        QVERIFY(!window->property("safeZones").toBool()); // invalid batches are atomic
        QVERIFY(!action("postOpen",{{"platform","youtube"}}).value("isError").toBool());
        auto draft=action("postDraft",{{"fields",QJsonObject{{"caption","A draft, not a post"},{"title","Test"},{"platforms",QJsonArray{"youtube"}}}}});
        QVERIFY2(!draft.value("isError").toBool(),qPrintable(QString::fromUtf8(QJsonDocument(draft).toJson())));
        QCOMPARE(draft.value("result").toObject().value("caption").toString(),QString("A draft, not a post"));
        QVERIFY(!backend.busy());
        QVERIFY(action("postDraft",{{"fields",QJsonObject{{"platforms",QJsonArray{"youtube","tiktok"}}}}}).value("isError").toBool());
        auto ordered=action("postDraft",{{"fields",QJsonObject{{"platforms",QJsonArray{"youtube","facebook","instagram"}}}}});
        QCOMPARE(ordered.value("result").toObject().value("platforms").toArray(),QJsonArray({"instagram","facebook","youtube"}));
        QVERIFY(action("postDraft",{{"fields",QJsonObject{{"token","forbidden"}}}}).value("isError").toBool());
        QVERIFY(action("postSubmit",{{"unknown","must not submit"}}).value("isError").toBool());
        QVERIFY(!action("closeDialogs",{}).value("isError").toBool());
        QVERIFY(!action("scheduleDay",{{"date","2027-03-14"}}).value("isError").toBool());
        QVERIFY(!action("statsFilter",{{"options",QJsonObject{{"platform","facebook"},{"periodDays",7},{"sortKey","views"}}}}).value("isError").toBool());
        auto invalidDate=action("scheduleDay",{{"date","2027-02-30"}});QVERIFY(invalidDate.value("isError").toBool());
        auto calendarSlots=action("scheduleSlots",{{"platform","facebook"}});QVERIFY(calendarSlots.value("result").toObject().value("slots").isArray());
        FakeHttp fake;fake.answer=[](const FakeHttp::Call &){return FakeHttp::json({{"data",QJsonObject{{"is_valid",false}}}});};
        backend.facebook()->graphBase=fake.base();
        auto connected=AppBridgeClient::call(state.path(),{{"action","invoke"},{"component","accounts"},{"method","connectToken"},{"arguments",QJsonObject{{"platform","facebook"},{"token","iVBOR"}}}});
        QVERIFY(!connected.value("isError").toBool());
        QTRY_VERIFY(!backend.accountSettings()->busy());
        QTest::qWait(200);
        const auto shot=AppBridgeClient::call(state.path(),{{"action","screenshot"},{"arguments",QJsonObject{{"width",540}}}});
        QVERIFY(!shot.value("isError").toBool());QCOMPARE(shot.value("mimeType").toString(),QString("image/png"));
        QVERIFY(!QImage::fromData(QByteArray::fromBase64(shot.value("data").toString().toLatin1())).isNull());
        const auto color=AppBridgeClient::call(state.path(),{{"action","sample_color"},{"arguments",QJsonObject{{"x",0.5},{"y",0.5}}}});
        QVERIFY2(!color.value("isError").toBool(),qPrintable(QString::fromUtf8(QJsonDocument(color).toJson())));
        QVERIFY(QColor(color.value("color").toString()).isValid());
        window->grabWindow().save("/tmp/reelcut-mcp-ui.png");
    }

    void automationSecretsAndAsyncEventsAreSafe()
    {
        QTemporaryDir state;Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
        HiggsfieldCredits credits;credits.stateDir=state.path();Stats stats(backend.instagram(),backend.facebook(),backend.youtube());stats.stateDir=state.path();
        Downloads downloads(nullptr,state.filePath("downloads"));
        FakeHttp api;api.answer=[](const FakeHttp::Call &){return FakeHttp::json({{"data",QJsonObject{{"is_valid",false}}}});};
        backend.facebook()->graphBase=api.base();
        AppBridge bridge(state.path());bridge.attach(&backend,&downloads,&stats,&credits,nullptr);QVERIFY(bridge.listen());QTest::qWait(1);
        auto rejected=AppBridgeClient::call(state.path(),{{"action","invoke"},{"component","backend"},{"method","notReal"},{"arguments",QJsonObject{{"token","text"}}}});
        QVERIFY(rejected.value("isError").toBool());
        auto ordinary=AppBridgeClient::call(state.path(),{{"action","invoke"},{"component","backend"},{"method","notify"},{"arguments",QJsonObject{{"text","ordinary text"},{"error",false}}}});
        QVERIFY(!ordinary.value("isError").toBool());
        auto ordinaryState=AppBridgeClient::call(state.path(),{{"action","state"},{"component","backend"}});
        QCOMPARE(ordinaryState.value("state").toObject().value("message").toString(),QString("ordinary text"));
        backend.addOverlay(0);
        auto edited=AppBridgeClient::call(state.path(),{{"action","invoke"},{"component","backend"},{"method","setOverlayValue"},{"arguments",QJsonObject{{"index",0},{"key","text"},{"value","ordinary text"}}}});
        QVERIFY(!edited.value("isError").toBool());
        auto editorState=AppBridgeClient::call(state.path(),{{"action","state"},{"component","backend"}});
        QCOMPARE(editorState.value("state").toObject().value("overlays").toArray().first().toObject().value("text").toString(),QString("ordinary text"));
        const QString secret="MCP-CREDENTIAL-SECRET-123";
        auto accepted=AppBridgeClient::call(state.path(),{{"action","invoke"},{"component","accounts"},{"method","connectToken"},{"arguments",QJsonObject{{"platform","facebook"},{"token",secret}}}});
        QVERIFY(!accepted.value("isError").toBool());QVERIFY(!QJsonDocument(accepted).toJson().contains(secret.toUtf8()));
        QTRY_VERIFY(!backend.accountSettings()->busy());
        backend.notify("Reflected credential: "+secret,true);
        auto snapshot=AppBridgeClient::call(state.path(),{{"action","state"},{"component","backend"}});
        QVERIFY(!QJsonDocument(snapshot).toJson().contains(secret.toUtf8()));
        QVERIFY(snapshot.value("state").toObject().value("message").toString().contains("[hidden]"));
        auto events=AppBridgeClient::call(state.path(),{{"action","events"},{"after",0}});
        QVERIFY(!QJsonDocument(events).toJson().contains(secret.toUtf8()));
        bool completion=false;for(const auto &event:events.value("events").toArray())completion|=event.toObject().value("type")=="account_connection";
        QVERIFY(completion);
        auto readonly=AppBridgeClient::call(state.path(),{{"action","set_property"},{"component","backend"},{"property","accountSettings"},{"value",123}});
        QVERIFY(readonly.value("isError").toBool());
        auto tooLarge=AppBridgeClient::call(state.path(),{{"action","invoke"},{"component","backend"},{"method","notify"},{"arguments",QJsonObject{{"text",QString(1024*1024+1,'x')},{"error",true}}}});
        QVERIFY(tooLarge.value("isError").toBool());
        QVERIFY(!(QFileInfo(bridge.socketPath()).permissions() & QFileDevice::ReadOther));
        QVERIFY(!(QFileInfo(bridge.socketPath()).permissions() & QFileDevice::WriteGroup));
    }

    void automationFileEquivalentsPreserveUnsavedEdits()
    {
        QTemporaryDir state;Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());AutomationRegistry registry;registry.add("backend",&backend);
        backend.setPostText("Unsaved caption","tags");QVERIFY(backend.dirty());
        auto refused=registry.invoke("backend","newProject",{});QVERIFY(refused.value("isError").toBool());QVERIFY(backend.dirty());
        auto saved=registry.invoke("backend","selectFiles",{{"purpose","save_project"},{"paths",QJsonArray{state.filePath("saved.reel")}}});
        QVERIFY2(!saved.value("isError").toBool(),qPrintable(QString::fromUtf8(QJsonDocument(saved).toJson())));
        QVERIFY(saved.value("result").toBool());QVERIFY(QFile::exists(state.filePath("saved.reel")));QVERIFY(!backend.dirty());
        backend.setPostText("Another change","tags");
        auto discarded=registry.invoke("backend","newProject",{{"discard_unsaved",true}});QVERIFY(!discarded.value("isError").toBool());QVERIFY(!backend.dirty());
    }

    void mcpAdvertisesLiveToolsWithoutRequiringAProject()
    {
        QTemporaryDir state; McpServer mcp(state.path());
        auto response=mcp.handle({{"jsonrpc","2.0"},{"id",1},{"method","tools/list"}});
        const auto tools=response.value("result").toObject().value("tools").toArray();
        bool editor=false,publish=false,stats=false,downloads=false;
        for (const auto &t:tools) {
            const auto tool=t.toObject();const auto name=tool.value("name").toString();
            editor |= name=="edit_clip";
            if (name=="app_backend_post_to") {publish=true;QVERIFY(!tool.value("annotations").toObject().value("readOnlyHint").toBool());}
            stats |= name=="app_stats_refresh";
            downloads |= name=="app_downloads_download";
        }
        QVERIFY(editor&&publish&&stats&&downloads);
        const auto missing=call(mcp,"get_app_state",{});
        QVERIFY(missing.value("isError").toBool());
    }

    void statsPermissionNoticeOffersDirectRepairLinks()
    {
        QTemporaryDir state, themeDir; seedOne(state.path(), "facebook");
        FakeHttp fake;
        fake.answer = [](const FakeHttp::Call &c) {
            if (c.path.endsWith("/insights")) return FakeHttp::json({{"error",QJsonObject{{"message","Missing permission"},{"code",200}}}},403);
            if (c.path.endsWith("/video_reels")) return FakeHttp::json({{"data",QJsonArray{}}});
            return FakeHttp::json({{"followers_count",3}});
        };
        StatsRig rig(state.path(),fake.base()); QVERIFY(rig.refreshed());
        QCOMPARE(rig.account("facebook").value("needsPermission").toString(),QString("read_insights"));
        Theme theme(themeDir.path()); Backend backend(nullptr,nullptr,&theme,nullptr,state.path());
        QUrl opened; backend.openUrl=[&](const QUrl &url){opened=url;};
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("stats",&rig.stats);
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("theme",&theme);
        QQmlComponent component(&engine,QUrl("qrc:/StatsPage.qml"));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object,qPrintable(component.errorString()));
        auto *page=qobject_cast<QQuickItem *>(object.get()); QVERIFY(page);
        QQuickWindow window;window.resize(1100,740);page->setParentItem(window.contentItem());page->setSize(QSizeF(1100,740));window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(findItem(page,"statsTokenTools-facebook"));
        auto *permissions=findItem(page,"statsAppPermissions-facebook"); QVERIFY(permissions); QVERIFY(permissions->isVisible());
        QMetaObject::invokeMethod(permissions,"clicked");
        QCOMPARE(opened,QUrl("https://developers.facebook.com/apps/"));
        QMetaObject::invokeMethod(findItem(page,"statsTokenTools-facebook"),"clicked");
        QCOMPARE(opened,QUrl("https://developers.facebook.com/tools/explorer/"));
        QMetaObject::invokeMethod(findItem(page,"statsTokenDebugger-facebook"),"clicked");
        QCOMPARE(opened,QUrl("https://developers.facebook.com/tools/debug/accesstoken/"));
        QSignalSpy settings(page,SIGNAL(manageAccount(QString)));
        QMetaObject::invokeMethod(findItem(page,"statsAccountSettings-facebook"),"clicked");
        QCOMPARE(settings.size(),1); QCOMPARE(settings.first().at(0).toString(),QString("facebook"));
        auto *text=findItem(page,"statsNoticeText-facebook"); QVERIFY(text);
        QVERIFY(text->property("text").toString().contains("Settings"));
        QVERIFY(!text->property("text").toString().contains("Post dialog"));
        window.grabWindow().save("/tmp/reelcut-token-help.png");
        page->setParentItem(nullptr);
    }

    void accountSettingsUsesPublishersAndPersistsExpiry()
    {
        QTemporaryDir state;
        FakeHttp server;
        bool invalidPage = false;
        server.answer = [&](const FakeHttp::Call &c) {
            if (c.path == "/debug_token") {
                const auto input = c.query.queryItemValue("input_token");
                return FakeHttp::json({{"data", QJsonObject{{"is_valid", input != "bad-user" && !(invalidPage && input == "page-private")},
                    {"expires_at", 0}, {"data_access_expires_at", 2000000000}}}});
            }
            if (c.path == "/me/accounts") return FakeHttp::json({{"data", QJsonArray{QJsonObject{{"id", "100"}, {"name", "My Page"}, {"access_token", "page-private"}}}}});
            if (c.query.queryItemValue("fields").contains("user_id")) return FakeHttp::json({{"user_id", "17841400000000001"}, {"username", "my_ig"}});
            return FakeHttp::json({{"id", "100"}, {"name", "My Page"}});
        };
        {
            Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
            backend.instagram()->graphBase = server.base(); backend.facebook()->graphBase = server.base();
            QTest::qWait(1);
            backend.connectInstagram("legacy-private");
            QTRY_VERIFY(!backend.accountSettings()->busy());
            QCOMPARE(backend.instagramUser(), QString("my_ig"));
            backend.connectFacebook("user-private");
            QTRY_VERIFY(!backend.accountSettings()->busy());
            QCOMPARE(backend.facebookPage(), QString("My Page"));
            const auto entry = backend.accountSettings()->entries()[1].toMap();
            QCOMPARE(entry.value("expiresAt").toLongLong(), 0LL);
            QCOMPARE(entry.value("dataAccessExpiresAt").toLongLong(), 2000000000LL);
            QVERIFY(!QJsonDocument::fromVariant(backend.accountSettings()->entries()).toJson().contains("page-private"));
            QVERIFY(!readAll(state.filePath("account-status.json")).contains("page-private"));
            backend.connectFacebook("bad-user"); QTRY_VERIFY(!backend.accountSettings()->busy());
            QCOMPARE(backend.facebook()->pageToken(), QString("page-private"));
            QCOMPARE(backend.accountSettings()->entries()[1].toMap().value("status").toString(), QString("Connected"));
            invalidPage = true;
            backend.connectFacebook("user-private"); QTRY_VERIFY(!backend.accountSettings()->busy());
            QCOMPARE(backend.facebook()->pageToken(), QString("page-private"));
            QCOMPARE(backend.accountSettings()->entries()[1].toMap().value("status").toString(), QString("Connected"));
        }
        Backend reloaded(nullptr,nullptr,nullptr,nullptr,state.path());
        QTest::qWait(10);
        QCOMPARE(reloaded.facebookPage(), QString("My Page"));
        QCOMPARE(reloaded.instagramUser(), QString("my_ig"));
        QCOMPARE(reloaded.accountSettings()->entries()[1].toMap().value("expiresAt").toLongLong(), 0LL);
    }

    void facebookSettingsAcceptsPageTokenAndRejectsPagination()
    {
        QTemporaryDir state; FakeHttp server; bool paginated = false;
        server.answer = [&](const FakeHttp::Call &c) {
            if (c.path == "/me/accounts") {
                if (!paginated) return FakeHttp::error("Not a User token");
                return FakeHttp::json({{"data", QJsonArray{QJsonObject{{"id","101"},{"name","First only"},{"access_token","candidate"}}}},
                    {"paging", QJsonObject{{"next","https://graph.facebook.com/next"}}}});
            }
            if (c.path == "/debug_token") return FakeHttp::json({{"data", QJsonObject{{"is_valid",true},{"expires_at",0}}}});
            return FakeHttp::json({{"id","100"},{"name","Existing Page"},{"category","Creator"}});
        };
        FacebookApi api; api.stateDir=state.path(); api.graphBase=server.base(); QTest::qWait(1);
        QSignalSpy done(&api,&FacebookApi::connectFinished);
        api.connectToken("page-private"); QTRY_COMPARE(done.size(),1);
        QVERIFY(done.first().at(0).toBool()); QCOMPARE(api.pageId(),QString("100"));
        paginated=true; done.clear(); api.connectToken("user-private"); QTRY_COMPARE(done.size(),1);
        QVERIFY(!done.first().at(0).toBool()); QCOMPARE(api.pageId(),QString("100"));
    }

    void accountSettingsYouTubeRefreshExpiry()
    {
        QTemporaryDir state;
        write(state.filePath("youtube.json"), QJsonDocument(QJsonObject{{"clientId", "client"}, {"clientSecret", "secret"},
            {"refreshToken", "refresh-private"}, {"channelId", "channel-1"}, {"channelTitle", "My Channel"}}).toJson());
        FakeHttp server;
        bool failChannels = false;
        server.answer = [&](const FakeHttp::Call &c) {
            if (failChannels && c.path == "/channels") return FakeHttp::error("temporarily unavailable", 503);
            if (c.path == "/token") return FakeHttp::json({{"access_token", "yt-private"}, {"expires_in", 3600}, {"refresh_token_expires_in", 7200}});
            return FakeHttp::json({{"items", QJsonArray{QJsonObject{{"id", "channel-1"}, {"snippet", QJsonObject{{"title", "My Channel"}}}}}}});
        };
        Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
        backend.youtube()->tokenUrl = server.base() + "/token"; backend.youtube()->apiBase = server.base();
        QTest::qWait(1);
        backend.accountSettings()->testConnection("youtube"); QTRY_VERIFY(!backend.accountSettings()->busy());
        const auto entry = backend.accountSettings()->entries()[2].toMap();
        const auto now = QDateTime::currentSecsSinceEpoch();
        QVERIFY(entry.value("expiresAt").toLongLong() > now + 3500);
        QVERIFY(entry.value("dataAccessExpiresAt").toLongLong() > now + 7100);
        QCOMPARE(entry.value("status").toString(), QString("Connected"));
        QVERIFY(!QJsonDocument::fromVariant(backend.accountSettings()->entries()).toJson().contains("yt-private"));
        failChannels = true;
        backend.accountSettings()->testConnection("youtube"); QTRY_VERIFY(!backend.accountSettings()->busy());
        QCOMPARE(backend.accountSettings()->entries()[2].toMap().value("status").toString(), QString("Connected"));
        backend.disconnectYouTube();
        QVERIFY(!backend.youtube()->connected());
        QVERIFY(!backend.youtube()->accessExpires().isValid());
    }

    void accountSettingsPageAccessibleWithoutProject()
    {
        QTemporaryDir state, themeDir;
        Theme theme(themeDir.path()); Backend backend(nullptr,nullptr,&theme,nullptr,state.path());
        HiggsfieldCredits higgsfield; higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme",&theme);
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("downloads",testDownloads());
        engine.rootContext()->setContextProperty("higgsfield",&higgsfield);
        engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1100,740); window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *toggle=findItem(window->contentItem(),"settingsButton"); QVERIFY(toggle); QVERIFY(toggle->isEnabled());
        QMetaObject::invokeMethod(toggle,"clicked");
        QTRY_VERIFY(findItem(window->contentItem(),"settingsPlatform"));
        auto *picker=findItem(window->contentItem(),"settingsPlatform");
        QCOMPARE(picker->property("count").toInt(),4);
        picker->setProperty("currentIndex",2);
        QTRY_VERIFY(findItem(window->contentItem(),"settingsYouTubeClient")->isVisible());
        QVERIFY(!findItem(window->contentItem(),"settingsToken")->isVisible());
        picker->setProperty("currentIndex",3);
        QTRY_VERIFY(findItem(window->contentItem(),"browserAccountHint")->isVisible());
        picker->setProperty("currentIndex",1); QTest::qWait(250);
        window->grabWindow().save("/tmp/reelcut-social-settings.png");
        auto *tabs=findItem(window->contentItem(),"settingsTabs"); tabs->setProperty("currentIndex",1);
        QTRY_VERIFY(findItem(window->contentItem(),"settingsSchedulerUrl")->isVisible());
    }

    void facebookConnectsAndPublishes()
    {
        QTemporaryDir dir, state;
        QFile video(dir.filePath("reel.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly));
        video.write(QByteArray(5000, 'v'));
        video.close();
        FakeFacebook fake;
        FacebookApi api;
        api.stateDir = state.path();
        api.graphBase = fake.base();
        api.pollIntervalMs = 10;
        QSignalSpy connected(&api, &FacebookApi::connectFinished);
        api.connectToken("EAA-user-token");
        QVERIFY(connected.wait());
        QVERIFY2(connected.first().at(0).toBool(), qPrintable(connected.first().at(1).toString()));
        QVERIFY(api.connected());
        QCOMPARE(api.account(), QString("Page 1"));
        QCOMPARE(api.pageId(), QString("1000000000000001"));
        QCOMPARE(QFile::permissions(api.accountFile()) & 0x0077, QFileDevice::Permissions());
        QVERIFY(!readAll(api.accountFile()).contains("EAA-user-token"));  // only the Page's token is kept

        // Kept across restarts.
        FacebookApi again;
        again.stateDir = state.path();
        QTRY_VERIFY(again.connected());
        QCOMPARE(again.account(), QString("Page 1"));

        // Publish now: start, upload the bytes, finish PUBLISHED, wait, link.
        QSignalSpy done(&api, &FacebookApi::published);
        api.publish({video.fileName(), "Hello + 100% #fb &", "", {}});
        QVERIFY(done.wait());
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(done.first().at(2).toUrl(), QUrl("https://www.facebook.com/reel/V1/"));
        QCOMPARE(fake.finishedState, QString("published"));
        const auto finish = fake.callsTo("/1000000000000001/video_reels").last();
        QCOMPARE(finish.query.queryItemValue("description", QUrl::FullyDecoded), QString("Hello + 100% #fb &"));
        QVERIFY(!finish.query.hasQueryItem("scheduled_publish_time"));
        QCOMPARE(fake.callsTo("/video-upload/v26.0/V1").size(), 1);

        // Scheduled: SCHEDULED with the time in Unix seconds.
        fake.processingFor = 0;
        done.clear();
        const QDateTime when = QDateTime::currentDateTimeUtc().addDays(2);
        api.publish({video.fileName(), "Later", "", when});
        QVERIFY(done.wait());
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(fake.finishedState, QString("scheduled"));
        QCOMPARE(fake.callsTo("/1000000000000001/video_reels").last().query.queryItemValue("scheduled_publish_time"),
                 QString::number(when.toSecsSinceEpoch()));

        // Facebook refusing the video stops with its reason.
        fake.failWith = "Unsupported aspect ratio";
        done.clear();
        api.publish({video.fileName(), "Bad", "", {}});
        QVERIFY(done.wait());
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(done.first().at(1).toString().contains("Unsupported aspect ratio"));
    }

    void facebookPagePickingAndErrors()
    {
        QTemporaryDir state;
        FakeFacebook fake;
        FacebookApi api;
        api.stateDir = state.path();
        api.graphBase = fake.base();

        // Several Pages: pick one.
        fake.pages = 3;
        QSignalSpy found(&api, &FacebookApi::pagesFound);
        QSignalSpy connected(&api, &FacebookApi::connectFinished);
        api.connectToken("EAA-user-token");
        QVERIFY(found.wait());
        const QVariantList pages = found.first().at(0).toList();
        QCOMPARE(pages.size(), 3);
        QCOMPARE(pages[1].toMap().value("name").toString(), QString("Page 2"));
        QVERIFY(!api.connected());
        api.choosePage("1000000000000002");
        QTRY_COMPARE(connected.size(), 1);
        QVERIFY(connected.first().at(0).toBool());
        QCOMPARE(api.account(), QString("Page 2"));

        // No Pages, or a bad token: an error, and the connection stays.
        fake.pages = 0;
        connected.clear();
        api.connectToken("EAA-user-token");
        QVERIFY(connected.wait());
        QVERIFY(!connected.first().at(0).toBool());
        QVERIFY(connected.first().at(1).toString().contains("pages_manage_posts"));
        connected.clear();
        api.connectToken("wrong");
        QVERIFY(connected.wait());
        QVERIFY(!connected.first().at(0).toBool());
        QCOMPARE(api.account(), QString("Page 2"));
        api.disconnect();
        QVERIFY(!api.connected());
        QVERIFY(!QFile::exists(api.accountFile()));
    }

    // YouTube: sign in through the browser and a loopback address, then a
    // resumable upload, against a stand-in for Google.
    void youtubeSignsInAndUploads()
    {
        QTemporaryDir dir, state;
        QFile video(dir.filePath("reel.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly));
        video.write(QByteArray(7000, 'y'));
        video.close();
        FakeGoogle fake;
        auto point = [&](YouTubeApi &api) {
            api.stateDir = state.path();
            api.authBase = fake.base() + "/auth";
            api.tokenUrl = fake.base() + "/token";
            api.apiBase = fake.base() + "/youtube/v3";
            api.uploadBase = fake.base() + "/upload/youtube/v3";
        };
        YouTubeApi api;
        point(api);
        QUrl opened;
        api.openUrl = [&](const QUrl &url) { opened = url; };
        QSignalSpy connected(&api, &YouTubeApi::connectFinished);
        QNetworkAccessManager browser;

        // A redirect with the wrong state is refused.
        api.connectClient(" cid.apps.googleusercontent.com ", "csecret");
        QTRY_VERIFY(opened.isValid());
        QUrlQuery auth(opened);
        QCOMPARE(opened.path(), QString("/auth"));
        QCOMPARE(auth.queryItemValue("client_id"), QString("cid.apps.googleusercontent.com"));
        QCOMPARE(auth.queryItemValue("access_type"), QString("offline"));
        QCOMPARE(auth.queryItemValue("prompt", QUrl::FullyDecoded), QString("select_account consent"));
        QCOMPARE(auth.queryItemValue("code_challenge_method"), QString("S256"));
        QVERIFY(auth.queryItemValue("scope", QUrl::FullyDecoded).contains("https://www.googleapis.com/auth/youtube.upload"));
        QVERIFY(auth.queryItemValue("scope", QUrl::FullyDecoded).contains("https://www.googleapis.com/auth/yt-analytics.readonly"));
        const QUrl redirect(auth.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        QCOMPARE(redirect.host(), QString("127.0.0.1"));
        QUrl wrong = redirect;
        wrong.setQuery("code=C0DE&state=forged");
        browser.get(QNetworkRequest(wrong));
        QVERIFY(connected.wait());
        QVERIFY(!connected.first().at(0).toBool());
        QVERIFY(!api.connected());

        // The real way back: code and state, then the code is exchanged with
        // the PKCE verifier, and the channel looked up.
        connected.clear();
        opened.clear();
        api.connectClient("cid.apps.googleusercontent.com", "csecret");
        QTRY_VERIFY(opened.isValid());
        auth = QUrlQuery(opened);
        QUrl back(auth.queryItemValue("redirect_uri", QUrl::FullyDecoded));
        back.setQuery("state=" + auth.queryItemValue("state") + "&code=C0DE&scope=x");
        QNetworkReply *page = browser.get(QNetworkRequest(back));
        QVERIFY(connected.wait());
        QVERIFY2(connected.first().at(0).toBool(), qPrintable(connected.first().at(1).toString()));
        QTRY_VERIFY(page->isFinished());
        QVERIFY(page->readAll().contains("Reelcut"));
        const QByteArray challenge = QCryptographicHash::hash(fake.lastVerifier.toLatin1(), QCryptographicHash::Sha256)
                                         .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        QCOMPARE(auth.queryItemValue("code_challenge"), QString(challenge));
        QCOMPARE(api.account(), QString("JP Runs"));
        QCOMPARE(QFile::permissions(api.accountFile()) & 0x0077, QFileDevice::Permissions());

        // Publish now: public, the #Shorts tag added once, the bytes, a link.
        QSignalSpy done(&api, &YouTubeApi::published);
        api.publish({video.fileName(), "Lake run\n\n#run", "Sunset run", {}});
        QVERIFY(done.wait());
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(done.first().at(2).toUrl(), QUrl("https://youtube.com/shorts/VID1"));
        QCOMPARE(fake.uploaded.size(), 7000);
        QJsonObject snippet = fake.metadata.value("snippet").toObject();
        QJsonObject status = fake.metadata.value("status").toObject();
        QCOMPARE(snippet.value("title").toString(), QString("Sunset run"));
        QCOMPARE(snippet.value("description").toString(), QString("Lake run\n\n#run #Shorts"));
        QCOMPARE(status.value("privacyStatus").toString(), QString("public"));
        QCOMPARE(status.value("selfDeclaredMadeForKids").toBool(true), false);
        QVERIFY(!status.contains("publishAt"));

        // After a restart there's no access token: it's refreshed first.
        // Scheduled: private until publishAt, in UTC.
        YouTubeApi again;
        point(again);
        QTRY_VERIFY(again.connected());
        QSignalSpy againDone(&again, &YouTubeApi::published);
        const QDateTime when = QDateTime(QDate(2030, 1, 2), QTime(9, 30), QTimeZone::UTC);
        again.publish({video.fileName(), "Already #shorts", "Later", when});
        QVERIFY(againDone.wait());
        QVERIFY2(againDone.first().at(0).toBool(), qPrintable(againDone.first().at(1).toString()));
        QCOMPARE(fake.callsTo("/token").last().query.queryItemValue("grant_type"), QString("refresh_token"));
        status = fake.metadata.value("status").toObject();
        QCOMPARE(status.value("privacyStatus").toString(), QString("private"));
        QCOMPARE(status.value("publishAt").toString(), QString("2030-01-02T09:30:00Z"));
        QCOMPARE(fake.metadata.value("snippet").toObject().value("description").toString(), QString("Already #shorts"));

        // A refresh token Google no longer honors disconnects, saying why.
        fake.revoke = true;
        YouTubeApi revoked;
        point(revoked);
        QTRY_VERIFY(revoked.connected());
        QSignalSpy revokedDone(&revoked, &YouTubeApi::published);
        revoked.publish({video.fileName(), "x", "x", {}});
        QVERIFY(revokedDone.wait());
        QVERIFY(!revokedDone.first().at(0).toBool());
        QVERIFY(revokedDone.first().at(1).toString().contains("connect", Qt::CaseInsensitive));
        QVERIFY(!revoked.connected());
    }

    void youtubeRefusesUploadToDifferentChannel()
    {
        QTemporaryDir state, dir;
        seedAccounts(state.path());
        QFile video(dir.filePath("reel.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly));
        video.write("video");
        video.close();
        FakeGoogle fake;
        fake.channelId = "UC-other-channel";
        YouTubeApi api;
        api.stateDir = state.path();
        api.tokenUrl = fake.base() + "/token";
        api.apiBase = fake.base() + "/youtube/v3";
        api.uploadBase = fake.base() + "/upload/youtube/v3";
        QTRY_VERIFY(api.connected());
        QSignalSpy done(&api, &YouTubeApi::published);
        api.publish({video.fileName(), "Description", "Title", {}});
        QVERIFY(done.wait());
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(done.first().at(1).toString().contains("channel", Qt::CaseInsensitive));
        QVERIFY(fake.callsTo("/upload/youtube/v3/videos").isEmpty());
        QVERIFY(fake.uploaded.isEmpty());
    }

    // Reel stats from Instagram: reels only, insights under their metric
    // keys, and one metric at a time when the whole call is refused.
    void statsReadsInstagramReels()
    {
        QTemporaryDir state;
        seedOne(state.path(), "instagram");
        FakeHttp fake;
        const QString user = "/17841400000000001";
        fake.answer = [&](const FakeHttp::Call &c) -> FakeHttp::Reply {
            if (c.query.queryItemValue("access_token") != "good-token")
                return FakeHttp::error("Invalid OAuth access token.");
            if (c.path == user)
                return FakeHttp::json({{"followers_count", 1200}, {"media_count", 3}});
            if (c.path == user + "/media" && c.query.queryItemValue("after") == "P2")
                return FakeHttp::json({{"data", QJsonArray{igMedia("r2", "2026-10-01T10:00:00+0000", 4, 1)}}});
            if (c.path == user + "/media")
                return FakeHttp::json({{"data", QJsonArray{igMedia("r1", "2026-11-10T10:00:00+0000", 50, 5),
                                                           igMedia("p1", "2026-11-09T10:00:00+0000", 9, 9, "FEED")}},
                                       {"paging", QJsonObject{{"next", fake.base() + user + "/media?after=P2&access_token=good-token"}}}});
            if (c.path == user + "/insights") {
                auto days = [](double a, double b) {
                    return QJsonArray{QJsonObject{{"value", a}, {"end_time", "2026-11-15T08:00:00+0000"}},
                                      QJsonObject{{"value", b}, {"end_time", "2026-11-16T08:00:00+0000"}}};
                };
                return FakeHttp::json({{"data", QJsonArray{QJsonObject{{"name", "follower_count"}, {"values", days(2, 3)}},
                                                           QJsonObject{{"name", "reach"}, {"values", days(400, 500)}}}}});
            }
            if (c.path == "/r1/insights")
                return FakeHttp::json({{"data", QJsonArray{
                    insight("views", 1000, true), insight("reach", 800), insight("likes", 50, true), insight("comments", 5),
                    insight("shares", 7), insight("saved", 9), insight("total_interactions", 71),
                    insight("ig_reels_avg_watch_time", 4500), insight("ig_reels_video_view_total_time", 4500000)}}});
            if (c.path == "/p1/insights")
                return FakeHttp::error("not a reel");
            if (c.path == "/r2/insights") {
                const QString metric = c.query.queryItemValue("metric");
                if (metric.contains(',') || metric == "ig_reels_avg_watch_time")
                    return FakeHttp::json({{"error", QJsonObject{{"message", "(#100) ig_reels_avg_watch_time is not available"},
                                                                 {"code", 100}}}}, 400);
                return FakeHttp::json({{"data", QJsonArray{insight(metric, 10)}}});
            }
            return FakeHttp::error("unexpected " + c.path, 404);
        };
        StatsRig rig(state.path(), fake.base());
        QVERIFY(rig.instagram.connected());
        QCOMPARE(rig.stats.accounts().size(), 3);
        QVERIFY(rig.refreshed());
        QVERIFY(rig.stats.refreshed().isValid());
        QCOMPARE(rig.stats.status(), QString());

        // Reels only, newest first.
        const QVariantList reels = rig.stats.reels();
        QCOMPARE(reels.size(), 2);
        QCOMPARE(reels[0].toMap().value("id").toString(), QString("r1"));
        QCOMPARE(reels[1].toMap().value("id").toString(), QString("r2"));
        QVERIFY(fake.callsTo("/p1/insights").isEmpty());
        const QString asked = fake.callsTo("/r1/insights").first().query.queryItemValue("metric");
        QCOMPARE(asked, QString("views,reach,likes,comments,shares,saved,total_interactions,ig_reels_avg_watch_time,"
                                "ig_reels_video_view_total_time"));

        const QVariantMap r1 = reels[0].toMap();
        QCOMPARE(r1.value("platform").toString(), QString("instagram"));
        QCOMPARE(r1.value("caption").toString(), QString("Reel r1"));
        QCOMPARE(r1.value("thumbnail").toString(), QString("https://cdn/r1.jpg"));
        QCOMPARE(r1.value("permalink").toString(), QString("https://www.instagram.com/reel/r1/"));
        QCOMPARE(r1.value("published").toLongLong(),
                 QDateTime(QDate(2026, 11, 10), QTime(10, 0), QTimeZone::UTC).toMSecsSinceEpoch());
        const QVariantMap m1 = r1.value("metrics").toMap();
        QCOMPARE(m1.value("views").toDouble(), 1000.0);
        QCOMPARE(m1.value("reach").toDouble(), 800.0);
        QCOMPARE(m1.value("likes").toDouble(), 50.0);
        QCOMPARE(m1.value("comments").toDouble(), 5.0);
        QCOMPARE(m1.value("shares").toDouble(), 7.0);
        QCOMPARE(m1.value("saves").toDouble(), 9.0);
        QCOMPARE(m1.value("interactions").toDouble(), 71.0);
        QCOMPARE(m1.value("avgWatch").toDouble(), 4.5);       // ms -> s
        QCOMPARE(m1.value("totalWatch").toDouble(), 4500.0);
        QVERIFY(qAbs(r1.value("engagementRate").toDouble() - 0.071) < 1e-9);

        // r2's combined call was refused: every metric but the failing one, one at a time.
        const QVariantMap m2 = rig.reel("r2").value("metrics").toMap();
        QCOMPARE(m2.value("views").toDouble(), 10.0);
        QCOMPARE(m2.value("saves").toDouble(), 10.0);
        QCOMPARE(m2.value("totalWatch").toDouble(), 0.01);
        QVERIFY(!m2.contains("avgWatch"));
        QCOMPARE(fake.callsTo("/r2/insights").size(), 1 + 9);

        // The account: followers, and the daily gains counted back from today.
        const QVariantMap ig = rig.account("instagram");
        QVERIFY(ig.value("connected").toBool());
        QCOMPARE(ig.value("account").toString(), QString("reelcut_test"));
        QCOMPARE(ig.value("followers").toDouble(), 1200.0);
        QCOMPARE(ig.value("reelCount").toInt(), 2);
        QCOMPARE(ig.value("error").toString(), QString());
        QCOMPARE(ig.value("needsPermission").toString(), QString());
        const QVariantList followers = ig.value("followerSeries").toList();
        QCOMPARE(followers.size(), 2);
        QCOMPARE(followers[0].toMap().value("v").toDouble(), 1197.0);
        QCOMPARE(followers[1].toMap().value("v").toDouble(), 1200.0);
        QCOMPARE(ig.value("viewSeries").toList().last().toMap().value("v").toDouble(), 500.0);

        // Platforms not connected say so, without an error.
        for (const QString &platform : {QString("facebook"), QString("youtube")}) {
            const QVariantMap a = rig.account(platform);
            QVERIFY(!a.value("connected").toBool());
            QCOMPARE(a.value("error").toString(), QString());
        }

        // Kept for next time, readable only by you, and read back.
        QCOMPARE(QFile::permissions(rig.stats.statsFile()) & 0x0077, QFileDevice::Permissions());
        StatsRig again(state.path(), fake.base());
        QCOMPARE(again.stats.reels().size(), 2);
        QCOMPARE(again.reel("r1").value("metrics").toMap().value("avgWatch").toDouble(), 4.5);
        QCOMPARE(again.account("instagram").value("followers").toDouble(), 1200.0);
        QCOMPARE(again.stats.history("instagram", "r1").size(), 1);
    }

    // Without the insights permission: say which one, stop asking, and keep
    // the likes and comments the reel list gave.
    void statsKeepBasicCountsWithoutInsightsPermission()
    {
        QTemporaryDir state;
        seedOne(state.path(), "instagram");
        FakeHttp fake;
        const QString user = "/17841400000000001";
        fake.answer = [&](const FakeHttp::Call &c) -> FakeHttp::Reply {
            if (c.path == user)
                return FakeHttp::json({{"followers_count", 10}});
            if (c.path == user + "/media") {
                QJsonArray media;
                for (int i = 0; i < 8; ++i)
                    media.append(igMedia(QString("r%1").arg(i), QString("2026-11-0%1T10:00:00+0000").arg(i + 1), 10 + i, i));
                return FakeHttp::json({{"data", media}});
            }
            if (c.path.endsWith("/insights"))
                return FakeHttp::json({{"error", QJsonObject{{"message", "(#10) Application does not have permission for this action"},
                                                             {"code", 10}}}}, 403);
            return FakeHttp::error("unexpected " + c.path, 404);
        };
        StatsRig rig(state.path(), fake.base());
        QVERIFY(rig.refreshed());
        const QVariantMap ig = rig.account("instagram");
        QCOMPARE(ig.value("needsPermission").toString(), QString("instagram_business_manage_insights"));
        QCOMPARE(ig.value("error").toString(), QString());
        QCOMPARE(rig.stats.reels().size(), 8);
        const QVariantMap m = rig.reel("r3").value("metrics").toMap();
        QCOMPARE(m.value("likes").toDouble(), 13.0);
        QCOMPARE(m.value("comments").toDouble(), 3.0);
        QVERIFY(!m.contains("views"));
        QVERIFY(!rig.reel("r3").contains("engagementRate"));
        // At most the four already out; none after the refusal.
        int insightCalls = 0;
        for (const auto &c : fake.calls)
            if (c.path.endsWith("/insights") && c.path != user + "/insights")
                ++insightCalls;
        QVERIFY2(insightCalls <= 4, qPrintable(QString::number(insightCalls)));
    }

    // A Facebook Page's reels: video_insights, with the social actions map.
    void statsReadsFacebookReels()
    {
        QTemporaryDir state;
        seedOne(state.path(), "facebook");
        FakeHttp fake;
        const QString page = "/1000000000000001";
        fake.answer = [&](const FakeHttp::Call &c) -> FakeHttp::Reply {
            if (c.query.queryItemValue("access_token") != "page-token-1")
                return FakeHttp::error("Invalid OAuth access token.");
            if (c.path == page)
                return FakeHttp::json({{"followers_count", 321}, {"fan_count", 300}});
            if (c.path == page + "/insights") {
                if (c.query.queryItemValue("metric").contains(','))  // one retired metric spoils the call
                    return FakeHttp::json({{"error", QJsonObject{{"message", "(#100) The value must be a valid insights metric"}, {"code", 100}}}}, 400);
                if (c.query.queryItemValue("metric") != "page_follows")
                    return FakeHttp::json({{"error", QJsonObject{{"message", "(#100) The value must be a valid insights metric"}, {"code", 100}}}}, 400);
                return FakeHttp::json({{"data", QJsonArray{QJsonObject{{"name", "page_follows"}, {"values", QJsonArray{
                    QJsonObject{{"value", 320}, {"end_time", "2026-11-15T08:00:00+0000"}},
                    QJsonObject{{"value", 321}, {"end_time", "2026-11-16T08:00:00+0000"}}}}}}}});
            }
            if (c.path == page + "/video_reels")
                return FakeHttp::json({{"data", QJsonArray{QJsonObject{
                    {"id", "R1"}, {"description", "Lake run"}, {"title", "Run"}, {"created_time", "2026-11-12T09:30:00+0000"},
                    {"permalink_url", "/reel/R1/"}, {"length", 12.5}, {"picture", "https://cdn/R1.jpg"}}}}});
            if (c.path == "/R1/video_insights")
                return FakeHttp::json({{"data", QJsonArray{
                    insight("blue_reels_play_count", 90), insight("fb_reels_total_plays", 120), insight("fb_reels_replay_count", 30),
                    insight("post_impressions_unique", 80), insight("post_video_avg_time_watched", 6200),
                    insight("post_video_view_time", 744000), insight("post_video_followers", 4),
                    insight("post_video_social_actions", QJsonObject{{"LIKE", 10}, {"COMMENT", 2}, {"SHARE", 3}})}}});
            return FakeHttp::error("unexpected " + c.path, 404);
        };
        StatsRig rig(state.path(), fake.base());
        QVERIFY(rig.refreshed());
        const QVariantMap reel = rig.reel("R1");
        QCOMPARE(reel.value("platform").toString(), QString("facebook"));
        QCOMPARE(reel.value("caption").toString(), QString("Lake run"));
        QCOMPARE(reel.value("title").toString(), QString("Run"));
        QCOMPARE(reel.value("permalink").toString(), QString("https://www.facebook.com/reel/R1/"));
        QCOMPARE(reel.value("duration").toDouble(), 12.5);
        const QVariantMap m = reel.value("metrics").toMap();
        QCOMPARE(m.value("views").toDouble(), 120.0);
        QCOMPARE(m.value("initialPlays").toDouble(), 90.0);
        QCOMPARE(m.value("replays").toDouble(), 30.0);
        QCOMPARE(m.value("reach").toDouble(), 80.0);
        QCOMPARE(m.value("avgWatch").toDouble(), 6.2);
        QCOMPARE(m.value("totalWatch").toDouble(), 744.0);
        QCOMPARE(m.value("followsGained").toDouble(), 4.0);
        QCOMPARE(m.value("likes").toDouble(), 10.0);
        QCOMPARE(m.value("comments").toDouble(), 2.0);
        QCOMPARE(m.value("shares").toDouble(), 3.0);
        const QVariantMap fb = rig.account("facebook");
        QCOMPARE(fb.value("followers").toDouble(), 321.0);
        QCOMPARE(fb.value("followerSeries").toList().size(), 2);
        QVERIFY(fb.value("viewSeries").toList().isEmpty());
        QCOMPARE(fb.value("error").toString(), QString());
        QVERIFY(!rig.account("instagram").value("connected").toBool());
    }

    // YouTube: Shorts picked by length, Data API counts plus Analytics'
    // extras, and the reconnect hint when Analytics isn't allowed.
    void statsReadsYouTubeShorts()
    {
        QTemporaryDir state;
        seedOne(state.path(), "youtube");
        FakeHttp fake;
        bool analyticsDenied = false;
        fake.answer = [&](const FakeHttp::Call &c) -> FakeHttp::Reply {
            if (c.path == "/token")
                return FakeHttp::json({{"access_token", "ya29-stats"}, {"expires_in", 3599}});
            if (c.headers.value("authorization") != "Bearer ya29-stats")
                return FakeHttp::error("Request had invalid authentication credentials.", 401);
            if (c.path == "/youtube/v3/channels")
                return FakeHttp::json({{"items", QJsonArray{QJsonObject{
                    {"id", "UC123"}, {"contentDetails", QJsonObject{{"relatedPlaylists", QJsonObject{{"uploads", "UU123"}}}}},
                    {"statistics", QJsonObject{{"subscriberCount", "500"}, {"hiddenSubscriberCount", false}}}}}}});
            if (c.path == "/youtube/v3/playlistItems") {
                auto item = [](const QString &id) { return QJsonObject{{"contentDetails", QJsonObject{{"videoId", id}}}}; };
                if (c.query.queryItemValue("pageToken") == "P2")
                    return FakeHttp::json({{"items", QJsonArray{item("v3")}}});
                return FakeHttp::json({{"items", QJsonArray{item("v1"), item("v2")}}, {"nextPageToken", "P2"}});
            }
            if (c.path == "/youtube/v3/videos") {
                const QHash<QString, QString> durations{{"v1", "PT45S"}, {"v2", "PT10M"}, {"v3", "PT1M30S"}};
                QJsonArray items;
                for (const QString &id : c.query.queryItemValue("id").split(','))
                    items.append(QJsonObject{
                        {"id", id},
                        {"status", QJsonObject{{"privacyStatus", "public"}}},
                        {"snippet", QJsonObject{{"title", "Short " + id}, {"description", "About " + id},
                                                {"publishedAt", id == "v1" ? "2026-11-14T12:00:00Z" : "2026-11-01T12:00:00Z"},
                                                {"thumbnails", QJsonObject{{"high", QJsonObject{{"url", "https://i.ytimg/" + id}}}}}}},
                        {"statistics", QJsonObject{{"viewCount", "2000"}, {"likeCount", "150"}, {"commentCount", "12"}}},
                        {"contentDetails", QJsonObject{{"duration", durations.value(id)}}}});
                return FakeHttp::json({{"items", items}});
            }
            if (c.path == "/v2/reports") {
                if (analyticsDenied)
                    return FakeHttp::json({{"error", QJsonObject{
                        {"code", 403}, {"message", "Request had insufficient authentication scopes."},
                        {"errors", QJsonArray{QJsonObject{{"reason", "insufficientPermissions"}}}},
                        {"status", "PERMISSION_DENIED"},
                        {"details", QJsonArray{QJsonObject{{"reason", "ACCESS_TOKEN_SCOPE_INSUFFICIENT"}}}}}}}, 403);
                auto headers = [](const QStringList &names) {
                    QJsonArray out;
                    for (const QString &n : names)
                        out.append(QJsonObject{{"name", n}});
                    return out;
                };
                if (c.query.queryItemValue("dimensions") == "day") {
                    if (c.query.hasQueryItem("filters"))
                        return FakeHttp::json({{"columnHeaders", headers({"day", "views"})},
                                               {"rows", QJsonArray{QJsonArray{"2026-11-14", 700}, QJsonArray{"2026-11-15", 900}}}});
                    return FakeHttp::json({{"columnHeaders", headers({"day", "views", "subscribersGained", "subscribersLost"})},
                                           {"rows", QJsonArray{QJsonArray{"2026-11-15", 1000, 5, 1}, QJsonArray{"2026-11-16", 1500, 10, 2}}}});
                }
                return FakeHttp::json({{"columnHeaders", headers({"video", "views", "estimatedMinutesWatched", "averageViewDuration",
                                                                  "averageViewPercentage", "likes", "comments", "shares",
                                                                  "subscribersGained", "subscribersLost"})},
                                       {"rows", QJsonArray{QJsonArray{"v1", 1990, 50, 31, 68.9, 149, 12, 25, 6, 1},
                                                           QJsonArray{"v3", 1980, 40, 22, 30.5, 140, 11, 9, 2, 0}}}});
            }
            return FakeHttp::error("unexpected " + c.path, 404);
        };
        StatsRig rig(state.path(), fake.base());
        QVERIFY(rig.youtube.connected());
        QVERIFY(rig.refreshed());

        // v2 is ten minutes long: not a Short.
        QCOMPARE(rig.stats.reels().size(), 2);
        QCOMPARE(rig.stats.reels().first().toMap().value("privacyStatus").toString(),QString("public"));
        QVERIFY(std::any_of(fake.calls.begin(),fake.calls.end(),[](const FakeHttp::Call &c){return c.path=="/youtube/v3/videos" && c.query.queryItemValue("part").contains("status");}));
        QVERIFY(rig.reel("v2").isEmpty());
        const QString filter = fake.callsTo("/v2/reports").first().query.queryItemValue("filters", QUrl::FullyDecoded);
        QVERIFY(filter.isEmpty() || !filter.contains("v2"));
        bool askedForBoth = false;
        for (const auto &c : fake.callsTo("/v2/reports"))
            askedForBoth = askedForBoth || c.query.queryItemValue("filters", QUrl::FullyDecoded) == "video==v1,v3";
        QVERIFY(askedForBoth);
        const QVariantMap v1 = rig.reel("v1");
        QCOMPARE(v1.value("title").toString(), QString("Short v1"));
        QCOMPARE(v1.value("caption").toString(), QString("About v1"));
        QCOMPARE(v1.value("duration").toDouble(), 45.0);
        QCOMPARE(v1.value("permalink").toString(), QString("https://youtube.com/shorts/v1"));
        QCOMPARE(v1.value("thumbnail").toString(), QString("https://i.ytimg/v1"));
        QCOMPARE(rig.stats.reels().first().toMap().value("id").toString(), QString("v1"));  // newest first
        const QVariantMap m = v1.value("metrics").toMap();
        QCOMPARE(m.value("views").toDouble(), 2000.0);   // the Data API's live count
        QCOMPARE(m.value("likes").toDouble(), 150.0);
        QCOMPARE(m.value("comments").toDouble(), 12.0);
        QCOMPARE(m.value("shares").toDouble(), 25.0);
        QCOMPARE(m.value("totalWatch").toDouble(), 3000.0);  // minutes -> seconds
        QCOMPARE(m.value("avgWatch").toDouble(), 31.0);
        QCOMPARE(m.value("avgViewPct").toDouble(), 68.9);
        QCOMPARE(m.value("followsGained").toDouble(), 6.0);
        QCOMPARE(m.value("subsLost").toDouble(), 1.0);
        QCOMPARE(rig.reel("v3").value("duration").toDouble(), 90.0);
        const QVariantMap yt = rig.account("youtube");
        QCOMPARE(yt.value("followers").toDouble(), 500.0);
        QCOMPARE(yt.value("reelCount").toInt(), 2);
        QCOMPARE(yt.value("needsPermission").toString(), QString());
        const QVariantList followers = yt.value("followerSeries").toList();
        QCOMPARE(followers.size(), 2);
        QCOMPARE(followers[0].toMap().value("v").toDouble(), 492.0);
        QCOMPARE(followers[1].toMap().value("v").toDouble(), 500.0);
        QCOMPARE(yt.value("viewSeries").toList().last().toMap().value("v").toDouble(), 1500.0);

        // Views per day of one Short.
        QSignalSpy daily(&rig.stats, &Stats::dailyLoaded);
        rig.stats.loadDaily("youtube", "v1");
        QVERIFY(daily.wait());
        QCOMPARE(daily.first().at(1).toString(), QString("v1"));
        const QVariantList points = daily.first().at(2).toList();
        QCOMPARE(points.size(), 2);
        QCOMPARE(points[1].toMap().value("v").toDouble(), 900.0);

        // Connected before Reelcut asked for Analytics: the Data API's counts stay.
        analyticsDenied = true;
        QVERIFY(rig.refreshed());
        QCOMPARE(rig.account("youtube").value("needsPermission").toString(), QString("yt-analytics.readonly"));
        QCOMPARE(rig.account("youtube").value("error").toString(), QString());
        const QVariantMap denied = rig.reel("v1").value("metrics").toMap();
        QCOMPARE(denied.value("views").toDouble(), 2000.0);
        QCOMPARE(denied.value("likes").toDouble(), 150.0);
        QVERIFY(!denied.contains("shares"));

        // Analytics refusing a day report: the recorded views instead.
        daily.clear();
        rig.stats.loadDaily("youtube", "v1");
        QVERIFY(daily.wait());
        QCOMPARE(daily.first().at(2).toList().size(), 2);  // two snapshots: the second refresh changed metrics
    }

    // History: a snapshot only when a number moved, at most 400 per reel.
    void statsHistoryGrowsOnChangeAndIsCapped()
    {
        QTemporaryDir state;
        seedOne(state.path(), "instagram");
        FakeHttp fake;
        double views = 100;
        fake.answer = [&](const FakeHttp::Call &c) -> FakeHttp::Reply {
            if (c.path == "/17841400000000001/media")
                return FakeHttp::json({{"data", QJsonArray{igMedia("r1", "2026-11-10T10:00:00+0000", 5, 1)}}});
            if (c.path == "/r1/insights")
                return FakeHttp::json({{"data", QJsonArray{insight("views", views)}}});
            return FakeHttp::json({});
        };
        {
            StatsRig rig(state.path(), fake.base());
            QVERIFY(rig.refreshed());
            QVERIFY(rig.refreshed());
            QCOMPARE(rig.stats.history("instagram", "r1").size(), 1);
            views = 150;
            QVERIFY(rig.refreshed());
            const QVariantList history = rig.stats.history("instagram", "r1");
            QCOMPARE(history.size(), 2);
            QCOMPARE(history[0].toMap().value("metrics").toMap().value("views").toDouble(), 100.0);
            QCOMPARE(history[1].toMap().value("metrics").toMap().value("views").toDouble(), 150.0);
            QVERIFY(history[1].toMap().value("t").toLongLong() >= history[0].toMap().value("t").toLongLong());
            QSignalSpy daily(&rig.stats, &Stats::dailyLoaded);
            rig.stats.loadDaily("instagram", "r1");
            QVERIFY(daily.wait());
            QCOMPARE(daily.first().at(2).toList().size(), 2);
        }

        // 400 kept already: the oldest makes way.
        QJsonArray full;
        for (int i = 0; i < 400; ++i)
            full.append(QJsonObject{{"t", double(1700000000000LL + i)}, {"metrics", QJsonObject{{"views", i}}}});
        QJsonObject saved = Net::load(state.filePath("stats.json"));
        saved["history"] = QJsonObject{{"instagram:r1", full}};
        QVERIFY(Net::savePrivate(state.filePath("stats.json"), saved));
        views = 999;
        StatsRig rig(state.path(), fake.base());
        QCOMPARE(rig.stats.history("instagram", "r1").size(), 400);
        QVERIFY(rig.refreshed());
        const QVariantList history = rig.stats.history("instagram", "r1");
        QCOMPARE(history.size(), 400);
        QCOMPARE(history.first().toMap().value("metrics").toMap().value("views").toDouble(), 1.0);
        QCOMPARE(history.last().toMap().value("metrics").toMap().value("views").toDouble(), 999.0);
    }

    // A broken stats.json is ignored, and the next refresh writes a good one.
    void statsIgnoreACorruptFile()
    {
        for (const QByteArray &contents : {QByteArray("{not json"),
                                           QByteArray(R"({"reels":"x","accounts":[5,{"platform":"tiktok"}],)"
                                                      R"("history":{"instagram:1":[1,"a",{"t":"x"}]},"refreshed":"y"})"),
                                           QByteArray(R"([1,2,3])")}) {
            QTemporaryDir state;
            write(state.filePath("stats.json"), contents);
            FakeHttp fake;
            fake.answer = [](const FakeHttp::Call &) { return FakeHttp::error("nothing here", 404); };
            StatsRig rig(state.path(), fake.base());
            QVERIFY(rig.stats.reels().isEmpty());
            QVERIFY(!rig.stats.refreshed().isValid());
            QCOMPARE(rig.stats.accounts().size(), 3);
            QVERIFY(rig.stats.history("instagram", "1").isEmpty());
            QCOMPARE(rig.stats.metricInfo().size(), 14);
            // Nothing connected: done at once, every platform saying so.
            QVERIFY(rig.refreshed());
            for (const QVariant &a : rig.stats.accounts()) {
                QVERIFY(!a.toMap().value("connected").toBool());
                QCOMPARE(a.toMap().value("error").toString(), QString());
            }
            QVERIFY(fake.calls.isEmpty());
            QVERIFY(QJsonDocument::fromJson(readAll(rig.stats.statsFile())).isObject());
        }
    }

    // Accounts as they sit on disk once connected, for tests that start there.
    static void seedAccounts(const QString &state)
    {
        QVERIFY(Net::savePrivate(state + "/instagram.json",
                                 {{"token", "good-token"}, {"userId", "17841400000000001"}, {"username", "reelcut_test"},
                                  {"issued", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}));
        QVERIFY(Net::savePrivate(state + "/facebook.json",
                                 {{"pageId", "1000000000000001"}, {"pageName", "Page 1"}, {"pageToken", "page-token-1"}}));
        QVERIFY(Net::savePrivate(state + "/youtube.json",
                                 {{"clientId", "cid.apps.googleusercontent.com"}, {"clientSecret", "csecret"},
                                  {"refreshToken", "1//refresh"}, {"channelId", "UC123"}, {"channelTitle", "JP Runs"}}));
    }

    void checkPostsMergesLimits()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 1.0)};
        write(dir.filePath("e.reel"), QJsonDocument(p.toJson(dir.path())).toJson());
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("e.reel")));

        QVariantMap check = backend.checkPosts({"instagram", "youtube"}, "Hi", "a b", "A title");
        QVERIFY2(check.value("ok").toBool(), qPrintable(check.value("problems").toStringList().join("; ")));
        QCOMPARE(check.value("maxChars").toInt(), 2200);   // the stricter of 2200 and 5000
        QCOMPARE(check.value("maxHashtags").toInt(), 30);  // of 30 and 60
        QVERIFY(backend.checkPosts({"youtube"}, "Hi", "", "  ").value("problems").toStringList().contains("YouTube needs a title."));
        QVERIFY(!backend.checkPosts({"youtube"}, "Hi", "", QString(101, 'x')).value("ok").toBool());
        QVERIFY(!backend.checkPosts({"youtube"}, "a <b>", "", "Title").value("ok").toBool());
        QVERIFY(backend.checkPosts({"instagram"}, "a <b>", "", "").value("ok").toBool());  // only YouTube minds
        // A one-second reel is too short for Facebook.
        check = backend.checkPosts({"facebook"}, "Hi", "", "");
        QVERIFY(!check.value("ok").toBool());
        QVERIFY(check.value("problems").toStringList().join(" ").contains("at least 3 seconds"));
        QVERIFY(!backend.checkPosts({}, "Hi", "", "").value("ok").toBool());

        // Scheduling windows.
        const double now = double(QDateTime::currentMSecsSinceEpoch());
        QVERIFY(!backend.checkWhen({"facebook"}, now + 5 * 60000).value("ok").toBool());
        QVERIFY(backend.checkWhen({"facebook", "youtube"}, now + 60 * 60000).value("ok").toBool());
        QVERIFY(!backend.checkWhen({"facebook"}, now + 30.0 * 24 * 3600000).value("ok").toBool());
        QVERIFY(!backend.checkWhen({"instagram"}, now + 60 * 60000).value("ok").toBool());  // no scheduler set up
    }

    void crossPostsToEveryCheckedPlatform()
    {
        QTemporaryDir dir, state, themeDir, movies;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 4.0)};
        write(dir.filePath("e.reel"), QJsonDocument(p.toJson(dir.path())).toJson());
        seedAccounts(state.path());
        FakeInstagram instagram;
        FakeFacebook facebook;
        facebook.processingFor = 0;
        FakeGoogle google;

        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("e.reel")));
        QUrl opened;
        backend.openUrl = [&](const QUrl &url) { opened = url; };
        backend.revealFile = [](const QString &) {};
        backend.postFolder = movies.path();
        backend.instagram()->graphBase = instagram.base();
        backend.instagram()->pollIntervalMs = 10;
        backend.facebook()->graphBase = facebook.base();
        backend.facebook()->pollIntervalMs = 10;
        backend.youtube()->tokenUrl = google.base() + "/token";
        backend.youtube()->apiBase = google.base() + "/youtube/v3";
        backend.youtube()->uploadBase = google.base() + "/upload/youtube/v3";
        QTRY_COMPARE(backend.youtubeChannel(), QString("JP Runs"));
        QCOMPARE(backend.facebookPage(), QString("Page 1"));
        QCOMPARE(backend.instagramUser(), QString("reelcut_test"));

        // One export, then each platform in the usual order.
        QSignalSpy each(&backend, &Backend::platformPosted);
        QSignalSpy all(&backend, &Backend::postPublished);
        backend.postTo({"youtube", "instagram", "facebook"}, "Hello", "trip", "My run");
        QVERIFY(all.wait(120000));
        QVERIFY2(all.first().at(0).toBool(), qPrintable(all.first().at(1).toString()));
        QCOMPARE(each.size(), 3);
        QCOMPARE(each.at(0).at(0).toString(), QString("instagram"));
        QCOMPARE(each.at(1).at(0).toString(), QString("facebook"));
        QCOMPARE(each.at(2).at(0).toString(), QString("youtube"));
        QCOMPARE(each.at(2).at(3).toUrl(), QUrl("https://youtube.com/shorts/VID1"));
        QVERIFY(QFileInfo::exists(QDir(movies.path()).filePath("e-post.mp4")));
        QCOMPARE(QDir(movies.path()).entryList({"*.mp4"}).size(), 1);
        QCOMPARE(google.metadata.value("snippet").toObject().value("title").toString(), QString("My run"));
        QCOMPARE(google.metadata.value("snippet").toObject().value("description").toString(), QString("Hello\n\n#trip #Shorts"));
        QVERIFY(opened.isEmpty());
        QCOMPARE(backend.project().platform, QString("instagram"));  // several: the design stays as it was

        // Facebook failing doesn't stop YouTube; its page opens to finish by hand.
        facebook.failWith = "Unsupported duration";
        each.clear();
        all.clear();
        backend.postTo({"facebook", "youtube"}, "Again", "", "Again");
        QVERIFY(all.wait(120000));
        QVERIFY(!all.first().at(0).toBool());
        QCOMPARE(each.size(), 2);
        QVERIFY(!each.at(0).at(1).toBool());
        QVERIFY(each.at(1).at(1).toBool());
        QCOMPARE(opened, QUrl("https://www.facebook.com/reels/create"));
        facebook.failWith.clear();

        // Too soon for Facebook: refused before anything is exported.
        all.clear();
        const double soon = double(QDateTime::currentMSecsSinceEpoch() + 5 * 60000);
        backend.postTo({"facebook"}, "Soon", "", "", soon);
        QVERIFY(backend.message().contains("10 minutes"));
        QVERIFY(!backend.busy());

        // Scheduled on Facebook and YouTube: they hold it, and it's listed.
        QSignalSpy scheduled(&backend, &Backend::postScheduled);
        const double later = double(QDateTime::currentMSecsSinceEpoch() + 2 * 24 * 3600000LL);
        backend.postTo({"facebook", "youtube"}, "Later", "", "Later", later);
        QVERIFY(scheduled.wait(120000));
        QVERIFY2(scheduled.first().at(0).toBool(), qPrintable(scheduled.first().at(1).toString()));
        QCOMPARE(facebook.finishedState, QString("scheduled"));
        QCOMPARE(google.metadata.value("status").toObject().value("privacyStatus").toString(), QString("private"));
        QStringList listed;
        for (const QVariant &v : backend.scheduledPosts())
            listed << v.toMap().value("platform").toString();
        QCOMPARE(listed, (QStringList{"facebook", "youtube"}));
        // Remembered after a restart.
        Backend again(nullptr, nullptr, &theme, nullptr, state.path());
        QCOMPARE(again.scheduledPosts().size(), 2);
    }

    void postDialogCrossPosts()
    {
        QTemporaryDir dir, state, themeDir, movies;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x360",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 4.0)};
        write(dir.filePath("e.reel"), QJsonDocument(p.toJson(dir.path())).toJson());
        seedAccounts(state.path());
        FakeInstagram instagram;
        FakeFacebook facebook;
        facebook.processingFor = 0;
        FakeGoogle google;

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("e.reel")));
        backend.openUrl = [](const QUrl &) {};
        backend.revealFile = [](const QString &) {};
        backend.postFolder = movies.path();
        backend.instagram()->graphBase = instagram.base();
        backend.instagram()->pollIntervalMs = 10;
        backend.facebook()->graphBase = facebook.base();
        backend.facebook()->pollIntervalMs = 10;
        backend.youtube()->tokenUrl = google.base() + "/token";
        backend.youtube()->apiBase = google.base() + "/youtube/v3";
        backend.youtube()->uploadBase = google.base() + "/upload/youtube/v3";
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir = state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("downloads", testDownloads());
        engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 900);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(backend.youtubeChannel(), QString("JP Runs"));

        QMetaObject::invokeMethod(findItem(window->contentItem(), "postButton"), "clicked");
        QObject *dialog = window->findChild<QObject *>("postDialog");
        QTRY_VERIFY(findItem(window->contentItem(), "postTo-facebook"));
        auto click = [&](const QString &name) { QMetaObject::invokeMethod(findItem(window->contentItem(), name), "clicked"); };
        auto selected = [&] { return dialog->property("selected").toStringList(); };
        QCOMPARE(selected(), QStringList{"instagram"});
        click("postTo-instagram");  // the only one: stays
        QCOMPARE(selected(), QStringList{"instagram"});
        click("postTo-youtube");
        click("postTo-facebook");
        QCOMPARE(selected(), (QStringList{"instagram", "facebook", "youtube"}));
        click("postTo-tiktok");  // by hand only: on its own
        QCOMPARE(selected(), QStringList{"tiktok"});
        click("postTo-instagram");
        QCOMPARE(selected(), QStringList{"instagram"});
        click("postTo-facebook");
        click("postTo-youtube");
        QVERIFY(dialog->property("publishing").toBool());

        // YouTube needs a title: the field is there, and empty holds Next.
        auto *title = findItem(window->contentItem(), "postTitle");
        QVERIFY(title && title->isVisible());
        auto *next = findItem(window->contentItem(), "postNext");
        title->setProperty("text", "");
        findItem(window->contentItem(), "postCaption")->setProperty("text", "Everywhere");
        QTRY_VERIFY(!next->isEnabled());
        title->setProperty("text", "Everywhere at once");
        QCOMPARE(next->property("text").toString(), QString("Add to queue"));
        QMetaObject::invokeMethod(findItem(window->contentItem(), "postNow"), "clicked");
        QTRY_VERIFY(next->isEnabled());
        QCOMPARE(next->property("text").toString(), QString("Publish"));

        QSignalSpy published(&backend, &Backend::postPublished);
        QMetaObject::invokeMethod(next, "clicked");
        QVERIFY(published.wait(120000));
        QVERIFY2(published.first().at(0).toBool(), qPrintable(published.first().at(1).toString()));
        QTRY_COMPARE(dialog->property("stage").toString(), QString("published"));
        QCOMPARE(dialog->property("results").toList().size(), 3);
        QCOMPARE(google.metadata.value("snippet").toObject().value("title").toString(), QString("Everywhere at once"));

        // Without a Facebook Page connected, several can't go at once.
        backend.disconnectFacebook();
        dialog->setProperty("stage", "write");
        QTRY_VERIFY(dialog->property("blocked").toBool());
        QTRY_VERIFY(!next->isEnabled());
        QVERIFY(findItem(window->contentItem(), "facebookConnect"));
    }

    void scheduleLogicKeepsDatesSlotsAndStatusesHonest()
    {
        QFile file(":/ScheduleLogic.js");
        QVERIFY2(file.open(QIODevice::ReadOnly), "ScheduleLogic must be available to the Schedule page");
        QString source = QString::fromUtf8(file.readAll());
        source.remove(".pragma library");
        QJSEngine js;
        QJSValue loaded = js.evaluate(source, "ScheduleLogic.js");
        QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        const QJSValue result = js.evaluate(R"JS(
            function expect(ok, message) { if (!ok) throw new Error(message); }
            var day = new Date(2026, 11, 31, 16).getTime();
            var start = dayStart(day);
            expect(new Date(start).getHours() === 0, 'local midnight');
            expect(new Date(addDays(start, 1)).getFullYear() === 2027, 'calendar year boundary');
            var targets = slots(day, 480, 90);
            expect(targets.length === 10, 'ten slots');
            expect(targets[0].minute === 480 && targets[9].minute === 1290, '08:00 to 21:30');
            expect(new Date(targets[0].ms).getDate() === 31, 'slots stay on selected date');
            expect(slots(day, 480, 120).length === 0, 'overflow must not silently become tomorrow');
            expect(slots(day, -1, 90).length === 0 && slots(day, 480, 0).length === 0, 'invalid cadence');
            expect(slots(day, 540, 90, 6).length === 6 && slots(day, 540, 90, 6)[5].minute === 990, 'six YouTube slots');
            expect(slots(day, 480, 120, 4).length === 4, 'fewer slots fit where ten would overflow');
            expect(slots(day, 480, 90, 0).length === 0 && slots(day, 480, 90, 11).length === 0, 'one to ten slots');
            expect(platformSummary('youtube', [], slots(day, 540, 90, 6)).target === 6, 'summary counts the plan');
            var at = targets[0].ms;
            var scheduled = [
                {platform:'youtube',id:'local',publishAt:at,title:'Same caption',status:'scheduled',link:'https://youtube.com/shorts/VID1'},
                {platform:'facebook',id:'past',publishAt:at,status:'scheduled',title:'Same caption'},
                {platform:'instagram',id:'failed',publishAt:at+3600000,status:'failed',error:'Token expired'},
                {platform:'instagram',id:'cancel',publishAt:at+7200000,status:'cancelled'},
                {platform:'facebook',id:'tomorrow',publishAt:addDays(at,1),status:'scheduled'}
            ];
            var published = [
                {platform:'youtube',id:'VID1',privacyStatus:'public',published:at+10000,title:'Same caption',permalink:'https://www.youtube.com/watch?v=VID1',thumbnail:'https://example.test/t.jpg'},
                {platform:'facebook',id:'actual',published:at+60000,title:'Same caption',permalink:'https://facebook.com/reel/other'}
            ];
            var actual = dayPosts(day, scheduled, published, at+1800000);
            expect(actual.length === 5, 'only same-day posts, merge known links, never merge captions');
            expect(actual.filter(function(p){return p.platform==='youtube';})[0].status === 'published', 'published link confirms schedule');
            expect(actual.filter(function(p){return p.id==='past';})[0].status === 'awaiting', 'elapsed time cannot confirm publication');
            expect(actual.filter(function(p){return p.id==='failed';})[0].error === 'Token expired', 'failure retained');
            var summary = platformSummary('instagram', actual, targets);
            expect(summary.failed === 1 && summary.covered === 0, 'failed and cancelled do not fill the goal');
            var cluster = layoutCards([{minute:480},{minute:490},{minute:600}]);
            expect(cluster[0].columns === 2 && cluster[1].columns === 2 && cluster[2].columns === 1, 'nearby cards remain accessible');
            var dst = slots(new Date(2027,2,14).getTime(),120,90);
            if (new Date(2027,2,14,2).getHours() !== 2)
                expect(dst.length === 10 && dst[0].valid === false, 'nonexistent DST time is disabled, never moved');
            true;
        )JS");
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QVERIFY(result.toBool());
    }

    void scheduleCalendarReviewRegressions()
    {
        QFile file(":/ScheduleLogic.js");QVERIFY(file.open(QIODevice::ReadOnly));
        QString source=QString::fromUtf8(file.readAll());source.remove(".pragma library");
        QJSEngine js;QVERIFY(!js.evaluate(source).isError());
        auto result=js.evaluate(R"JS(
            var errors=[];
            function check(ok,msg){if(!ok) errors.push(msg);}
            var at=new Date(2027,8,5,8).getTime();
            check(linkKey('facebook','https://www.facebook.com/watch/?v=111')!==linkKey('facebook','https://www.facebook.com/watch/?v=222'),'distinct Facebook watch videos');
            check(linkKey('facebook','https://www.facebook.com/watch/?v=111')===linkKey('facebook','https://facebook.com/reel/111'),'watch/reel identities match');
            check(linkKey('facebook','https://facebook.com/custom?id=1')!==linkKey('facebook','https://facebook.com/custom?id=2'),'unknown URL query identity preserved');
            var scheduled=[{platform:'youtube',id:'local',publishAt:at+3600000,status:'scheduled',link:'https://youtube.com/shorts/PRIVATE'}];
            var uploaded=[{platform:'youtube',id:'PRIVATE',published:at-86400000,permalink:'https://youtube.com/watch?v=PRIVATE',privacyStatus:'private'}];
            check(dayPosts(at,scheduled,uploaded,at).length===1,'private upload must not erase future schedule');
            check(dayPosts(at-86400000,scheduled,uploaded,at).length===0,'private upload is not published');
            uploaded[0].privacyStatus='public';uploaded[0].published=at;
            check(dayPosts(at,scheduled,uploaded,at+7200000)[0].status==='published','public evidence confirms YouTube');
            check(dayPosts(at,[{platform:'instagram',id:'late',publishAt:at,status:'scheduled'}],[],at+3600000)[0].status==='scheduled','preserve Instagram server status');
            check(slots(at,480,75).length===0,'unsupported cadence');
            var date=dayStart(at),next=new Date(2027,8,6,0,30).getTime();
            check(dayPosts(date,[],[{platform:'instagram',id:'tomorrow',published:next}],at).length===0,'next day early post excluded');
            check(new Date(addDays(date,1)).getHours()===0,'date navigation does not carry DST gap hour');
            var prompt=remixPrompt('https://www.tiktok.com/@old','Acme (acme.io)','Hinglish');
            check(prompt.indexOf('https://www.tiktok.com/@old -> Acme (acme.io)')>=0,'remix prompt names source and brand');
            check(prompt.indexOf('Hooks in Hinglish')>=0,'remix prompt carries the language');
            check(prompt.indexOf('~/Videos/Acme remixes/references/')>=0,'remix folder uses the brand name');
            check(prompt.indexOf('app_backend_add_placeholder')>=0 && prompt.indexOf('post nothing')>=0,'remix prompt only adds placeholders');
            check(remixPrompt('','','').indexOf('TextInk (textink.ca)')>=0 && remixPrompt('','','').indexOf('Hooks in English')>=0,'remix prompt defaults');
            var picked=remixPrompt('https://tiktok.com/@a/video/1\n https://instagram.com/reel/B ','','','reels');
            check(picked.indexOf('Remake these 2 reels')>=0 && picked.indexOf('   - https://tiktok.com/@a/video/1')>=0 && picked.indexOf('   - https://instagram.com/reel/B')>=0,'remix prompt lists picked reels');
            check(picked.indexOf('--flat-playlist')<0,'reel mode skips profile listing');
            var edit=editPrompt({title:'TextInk remix · cortisol',platform:'instagram',ms:new Date(2026,10,20,8,0).getTime(),
                referenceUrl:'https://instagram.com/reel/ABC',reference:'/v/ref.mp4',folder:'/v/cortisol',caption:'Try textink.ca',
                guide:['Shot 1 (1 s): notebook','','Shot 2 (2 s): iPad','Folder: /v/cortisol']});
            check(edit.indexOf('Original reel: https://instagram.com/reel/ABC')>=0,'edit prompt carries the original link');
            check(edit.indexOf('Raw clips: /v/cortisol')>=0 && edit.indexOf('Reference copy: /v/ref.mp4')>=0,'edit prompt names folder and reference');
            check(edit.indexOf('   1. Shot 1 (1 s): notebook')>=0 && edit.indexOf('   2. Shot 2 (2 s): iPad')>=0,'edit prompt lists the guide steps');
            check(edit.indexOf('   3. ')<0,'blank and folder steps left out of the plan');
            check(edit.indexOf('instagram 2026-11-20 08:00')>=0,'edit prompt names the slot');
            check(edit.indexOf('Do not post or schedule')>=0,'edit prompt never posts');
            check(editPrompt({}).indexOf('(none written')>=0,'edit prompt without a guide');
            check(remixPrompt('https://tiktok.com/@a/video/1','','','reels').indexOf('Remake this reel')>=0,'single reel wording');
            errors.join('; ');
        )JS");
        QVERIFY2(result.toString().isEmpty(),qPrintable(result.toString()));
    }

    void schedulePreferencesPersistAndCalendarRetainsUnconfirmedPosts()
    {
        QTemporaryDir state;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto post = [&](QString id, qint64 at) {
            return QJsonObject{{"id",id},{"platform","youtube"},{"publishAt",double(at)},
                               {"status","scheduled"},{"title",id}};
        };
        write(state.filePath("scheduled-direct.json"), QJsonDocument(QJsonObject{{"posts",QJsonArray{
            post("future",now+3600000),post("past",now-3600000),post("old",now-100LL*86400000)}}}).toJson());
        Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
        QVERIFY2(backend.metaObject()->indexOfProperty("schedulePreferences") >= 0, "persist cadence preferences");
        QVERIFY2(backend.metaObject()->indexOfProperty("calendarPosts") >= 0, "calendar history is separate from the pending list");
        QCOMPARE(backend.scheduledPosts().size(),1);
        QCOMPARE(backend.property("calendarPosts").toList().size(),2);
        QVariantMap defaults=backend.property("schedulePreferences").toMap();
        QCOMPARE(defaults.value("instagram").toMap().value("startMinute").toInt(),480);
        QCOMPARE(defaults.value("youtube").toMap().value("spacingMinutes").toInt(),90);
        QVariantMap changed=defaults;
        // YouTube defaults to fewer slots (its per-channel upload cap); the rest keep ten.
        QCOMPARE(defaults.value("youtube").toMap().value("slotsPerDay").toInt(),6);
        QCOMPARE(defaults.value("instagram").toMap().value("slotsPerDay").toInt(),10);
        changed["youtube"]=QVariantMap{{"startMinute",300},{"spacingMinutes",120},{"slotsPerDay",4}};
        QVERIFY(backend.setProperty("schedulePreferences",changed));
        QCOMPARE(backend.property("schedulePreferences").toMap(),changed);
        Backend again(nullptr,nullptr,nullptr,nullptr,state.path());
        QCOMPARE(again.property("schedulePreferences").toMap(),changed);
        QVariantMap bad=changed;
        bad["youtube"]=QVariantMap{{"startMinute",600},{"spacingMinutes",120},{"slotsPerDay",10}};
        backend.setProperty("schedulePreferences",bad);
        QCOMPARE(backend.property("schedulePreferences").toMap(),changed);
        bad["youtube"]=QVariantMap{{"startMinute",300},{"spacingMinutes",75}};
        backend.setProperty("schedulePreferences",bad);
        QCOMPARE(backend.property("schedulePreferences").toMap(),changed);
        bad["youtube"]=QVariantMap{{"startMinute",300},{"spacingMinutes",120},{"slotsPerDay",11}};
        backend.setProperty("schedulePreferences",bad);
        QCOMPARE(backend.property("schedulePreferences").toMap(),changed);
        // Ten two-hour slots from 08:00 overflow midnight.
        bad["youtube"]=QVariantMap{{"startMinute",480},{"spacingMinutes",120},{"slotsPerDay",10}};
        backend.setProperty("schedulePreferences",bad);
        QCOMPARE(backend.property("schedulePreferences").toMap(),changed);
        write(state.filePath("schedule-plan.json"),"{broken");
        Backend corrupt(nullptr,nullptr,nullptr,nullptr,state.path());
        QCOMPARE(corrupt.property("schedulePreferences").toMap(),defaults);
    }

    void nextOpenSlotsFillTodayThenRollIntoNextDays()
    {
        QTemporaryDir state;
        // YouTube's plan: 00:00, 01:00 ... 09:00 every day. One slot tomorrow is taken.
        const QDate tomorrow=QDate::currentDate().addDays(1);
        const qint64 takenAt=QDateTime(tomorrow,QTime(2,0)).toMSecsSinceEpoch();
        write(state.filePath("scheduled-direct.json"), QJsonDocument(QJsonObject{{"posts",QJsonArray{
            QJsonObject{{"id","taken"},{"platform","youtube"},{"publishAt",double(takenAt)},{"status","scheduled"},{"title","t"}}}}}).toJson());
        Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
        QVariantMap plan=backend.property("schedulePreferences").toMap();
        plan["youtube"]=QVariantMap{{"startMinute",0},{"spacingMinutes",60},{"slotsPerDay",10}};
        QVERIFY(backend.setProperty("schedulePreferences",plan));

        const QVariantList open=backend.nextOpenSlots({"youtube"},25);
        QCOMPARE(open.size(),25);
        const qint64 earliest=QDateTime::currentMSecsSinceEpoch()+5*60*1000;
        qint64 last=0;
        QMap<QDate,int> perDay;
        for (const QVariant &v : open) {
            const QVariantMap slot=v.toMap();
            QCOMPARE(slot.value("platform").toString(),QString("youtube"));
            const qint64 at=qint64(slot.value("publishAt").toDouble());
            QVERIFY(at>=earliest);  // never in the past, nor too soon to upload
            QVERIFY(at>last);       // soonest first, no repeats
            QVERIFY(at!=takenAt);   // skips what's already scheduled
            const QDateTime t=QDateTime::fromMSecsSinceEpoch(at);
            QCOMPARE(t.time().minute(),0);
            QVERIFY(t.time().hour()<=9);  // only the plan's slots
            perDay[t.date()]++;
            last=at;
        }
        // Ten slots a day at most, so 25 spill over into later days.
        for (int n : perDay) QVERIFY(n<=10);
        QVERIFY(perDay.size()>=3);
        QCOMPARE(perDay.value(tomorrow),9);  // tomorrow's 02:00 was taken
        // Platforms that can't take a scheduled post give none.
        QVERIFY(backend.nextOpenSlots({"tiktok"},3).isEmpty());
        QVERIFY(backend.nextOpenSlots({"youtube"},0).isEmpty());
        // Fewer slots per day: only the plan's first slots each day are offered.
        plan["youtube"]=QVariantMap{{"startMinute",0},{"spacingMinutes",60},{"slotsPerDay",3}};
        QVERIFY(backend.setProperty("schedulePreferences",plan));
        for (const QVariant &v : backend.nextOpenSlots({"youtube"},9))
            QVERIFY(QDateTime::fromMSecsSinceEpoch(qint64(v.toMap().value("publishAt").toDouble())).time().hour()<=2);
        plan["youtube"]=QVariantMap{{"startMinute",0},{"spacingMinutes",60},{"slotsPerDay",10}};
        QVERIFY(backend.setProperty("schedulePreferences",plan));
        // Several platforms: each gets its own slots.
        plan["facebook"]=QVariantMap{{"startMinute",30},{"spacingMinutes",60}};
        QVERIFY(backend.setProperty("schedulePreferences",plan));
        const QVariantList both=backend.nextOpenSlots({"facebook","youtube"},2);
        QCOMPARE(both.size(),4);
        int fb=0;
        for (const QVariant &v : both) if (v.toMap().value("platform")=="facebook") {
            ++fb;
            QCOMPARE(QDateTime::fromMSecsSinceEpoch(qint64(v.toMap().value("publishAt").toDouble())).time().minute(),30);
        }
        QCOMPARE(fb,2);
    }

    void placeholdersHoldSlotsWithoutPostingAndPersist()
    {
        QTemporaryDir state;
        const qint64 at=QDateTime(QDate::currentDate().addDays(2),QTime(8,0)).toMSecsSinceEpoch();
        QString id;
        {
            Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
            QVariantMap plan=backend.property("schedulePreferences").toMap();
            plan["instagram"]=QVariantMap{{"startMinute",480},{"spacingMinutes",90},{"slotsPerDay",10}};
            QVERIFY(backend.setProperty("schedulePreferences",plan));
            // Missing title or unknown platform: refused.
            QVERIFY(backend.addPlaceholder({{"platform","instagram"},{"publishAt",double(at)}}).isEmpty());
            QVERIFY(backend.addPlaceholder({{"platform","myspace"},{"publishAt",double(at)},{"title","x"}}).isEmpty());
            id=backend.addPlaceholder({{"platform","instagram"},{"publishAt",double(at)},{"title","Remix 1"},
                {"reference","/videos/original.mp4"},{"note","TextInk remix"},{"guide",QStringList{"Film A","","Film B"}}});
            QVERIFY(id.startsWith("placeholder-"));
            // The slot is taken: a second placeholder there is refused.
            QVERIFY(backend.addPlaceholder({{"platform","instagram"},{"publishAt",double(at)},{"title","Remix 2"}}).isEmpty());
            bool found=false;
            for (const QVariant &v : backend.calendarPosts()) {
                const QVariantMap p=v.toMap();
                if (p.value("id")!=id) continue;
                found=true;
                QCOMPARE(p.value("status").toString(),QString("placeholder"));
                QCOMPARE(p.value("reference").toString(),QString("/videos/original.mp4"));
                QCOMPARE(p.value("guide").toStringList(),QStringList({"Film A","Film B"}));  // blank steps dropped
            }
            QVERIFY(found);
            // No folder given and none in the guide: nothing to open.
            QVERIFY(!backend.openPlaceholderFolder(id));
            QVERIFY(!backend.openPlaceholderFolder("nope"));
            for (const QVariant &v : backend.nextOpenSlots({"instagram"},30))
                QVERIFY(qint64(v.toMap().value("publishAt").toDouble())!=at);
        }
        // Saved, and a file that claims another status still loads as a placeholder.
        QFile file(state.filePath("schedule-placeholders.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray saved=file.readAll(); file.close();
        saved.replace("\"placeholder\"","\"scheduled\"");
        write(state.filePath("schedule-placeholders.json"),saved);
        Backend reloaded(nullptr,nullptr,nullptr,nullptr,state.path());
        int count=0;
        for (const QVariant &v : reloaded.calendarPosts())
            if (v.toMap().value("id")==id) { ++count; QCOMPARE(v.toMap().value("status").toString(),QString("placeholder")); }
        QCOMPARE(count,1);
        // The clip folder: taken from a "Folder:" guide step, made and opened on click.
        {
            Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
            QUrl opened; backend.openUrl=[&](const QUrl &url){opened=url;};
            const QString folder=state.filePath("remixes/clip folder");
            const QString fid=backend.addPlaceholder({{"platform","instagram"},{"publishAt",double(at+86400000)},
                {"title","Remix F"},{"guide",QStringList{"Film A","Folder: "+folder}}});
            QVERIFY(!fid.isEmpty());
            QVERIFY(!QDir(folder).exists());
            QVERIFY(backend.openPlaceholderFolder(fid));
            QVERIFY(QDir(folder).exists());
            QCOMPARE(opened,QUrl::fromLocalFile(folder));
            QVERIFY(backend.removePlaceholder(fid));
        }
        QVERIFY(!reloaded.removePlaceholder("nope"));
        QVERIFY(reloaded.removePlaceholder(id));
        for (const QVariant &v : reloaded.calendarPosts()) QVERIFY(v.toMap().value("id")!=id);
        Backend after(nullptr,nullptr,nullptr,nullptr,state.path());
        for (const QVariant &v : after.calendarPosts()) QVERIFY(v.toMap().value("id")!=id);
    }

    void scheduleUndoBringsBackPlaceholdersAndTiming()
    {
        QTemporaryDir state;
        Backend backend(nullptr,nullptr,nullptr,nullptr,state.path());
        QVERIFY(!backend.undoSchedule());
        const qint64 at=QDateTime(QDate::currentDate().addDays(3),QTime(9,30)).toMSecsSinceEpoch();
        auto has=[&](const QString &id) {
            for (const QVariant &v : backend.calendarPosts()) if (v.toMap().value("id")==id) return true;
            return false;
        };
        const QString id=backend.addPlaceholder({{"platform","instagram"},{"publishAt",double(at)},{"title","Keep me"},
                                                 {"guide",QStringList{"Film it"}}});
        QVERIFY(backend.removePlaceholder(id));
        QVERIFY(!has(id));
        QVERIFY(backend.property("canUndoSchedule").toBool());
        QCOMPARE(backend.property("scheduleUndoText").toString(),QString("remove placeholder"));
        // Undo the removal: the same placeholder, guide and all, is back and saved.
        QVERIFY(backend.undoSchedule());
        QVERIFY(has(id));
        { Backend reloaded(nullptr,nullptr,nullptr,nullptr,state.path());
          bool found=false;
          for (const QVariant &v : reloaded.calendarPosts()) if (v.toMap().value("id")==id) { found=true; QCOMPARE(v.toMap().value("guide").toStringList(),QStringList{"Film it"}); }
          QVERIFY(found); }
        QVERIFY(backend.redoSchedule());
        QVERIFY(!has(id));
        QVERIFY(backend.undoSchedule());   // removal undone again
        QVERIFY(backend.undoSchedule());   // then the add itself
        QVERIFY(!has(id));
        QVERIFY(!backend.property("canUndoSchedule").toBool());
        QVERIFY(backend.redoSchedule());   // add again
        QVERIFY(has(id));
        // Timing changes undo too, and a new action clears redo.
        const QVariantMap before=backend.property("schedulePreferences").toMap();
        QVariantMap plan=before;
        plan["instagram"]=QVariantMap{{"startMinute",600},{"spacingMinutes",60},{"slotsPerDay",5}};
        QVERIFY(backend.setProperty("schedulePreferences",plan));
        QVERIFY(backend.undoSchedule());
        QCOMPARE(backend.property("schedulePreferences").toMap(),before);
        QVERIFY(backend.property("canRedoSchedule").toBool());
        QVERIFY(backend.removePlaceholder(id));
        QVERIFY(!backend.property("canRedoSchedule").toBool());
        // A removed placeholder whose slot was filled since stays in the history.
        const QString other=backend.addPlaceholder({{"platform","instagram"},{"publishAt",double(at)},{"title","Took the slot"}});
        QVERIFY(!other.isEmpty());
        QVERIFY(backend.removePlaceholder(other));
        QVERIFY(backend.addPlaceholder({{"platform","instagram"},{"publishAt",double(at)},{"title","Newer"}}).size()>0);
        QVERIFY(backend.undoSchedule());   // undoes "Newer" add
        QVERIFY(backend.undoSchedule());   // brings back "Took the slot"
    }

    void schedulePageShowsDailySlotsAndDoesNotPostFromAnEmptyProject()
    {
        QTemporaryDir state, themeDir;
        Theme theme(themeDir.path());
        Backend backend(nullptr,nullptr,&theme,nullptr,state.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("theme",&theme);
        QQmlComponent component(&engine,QUrl("qrc:/SchedulePage.qml"));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page=qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);
        QQuickWindow window;
        window.resize(1360,860);
        page->setParentItem(window.contentItem());
        page->setSize(QSizeF(1360,860));
        const QDate tomorrow=QDate::currentDate().addDays(1);
        const double selected=double(QDateTime(tomorrow,QTime(0,0)).toMSecsSinceEpoch());
        page->setProperty("selectedDate",selected);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_COMPARE(page->property("targetSlotCount").toInt(),36);  // 10 + 10 + 6 (YouTube) + 10
        QCOMPARE(page->property("dailyCount").toInt(),0);
        QVERIFY(findItem(page,"scheduleColumn-instagram"));
        QVERIFY(findItem(page,"scheduleColumn-facebook"));
        QVERIFY(findItem(page,"scheduleColumn-youtube"));
        QVERIFY(findItem(page,"scheduleColumn-tiktok"));
        QSignalSpy requested(page,SIGNAL(scheduleRequested(QString,double)));
        const double at=double(QDateTime(tomorrow,QTime(8,0)).toMSecsSinceEpoch());
        QVERIFY(QMetaObject::invokeMethod(page,"chooseSlot",Q_ARG(QVariant,"instagram"),Q_ARG(QVariant,at)));
        QCOMPARE(requested.size(),0);
        QVERIFY(page->property("notice").toString().contains("reel",Qt::CaseInsensitive));
        QVERIFY(!backend.busy());
        QVERIFY(backend.scheduledPosts().isEmpty());
        // TikTok has no posting API: its slots are reminders and never open the Post dialog.
        QVERIFY(QMetaObject::invokeMethod(page,"chooseSlot",Q_ARG(QVariant,"tiktok"),Q_ARG(QVariant,at)));
        QCOMPARE(requested.size(),0);
        QVERIFY(page->property("notice").toString().contains("TikTok"));

        auto *next=findItem(page,"scheduleNextDay");
        QVERIFY(next);
        QMetaObject::invokeMethod(next,"clicked");
        QTRY_COMPARE(qint64(page->property("selectedDate").toDouble()),QDateTime(tomorrow.addDays(1),QTime(0,0)).toMSecsSinceEpoch());
        // Clicking the header date opens a month calendar; choosing a day jumps straight to it.
        auto *dateButton=findItem(page,"scheduleDateButton");
        QVERIFY(dateButton);
        QMetaObject::invokeMethod(dateButton,"clicked");
        QObject *picker=page->findChild<QObject*>("scheduleDatePicker");
        QVERIFY(picker);
        QTRY_VERIFY(picker->property("visible").toBool());
        QTRY_VERIFY(findItem(window.contentItem(),"scheduleMonthGrid"));
        const QDate far=QDate::currentDate().addMonths(5);
        QVERIFY(QMetaObject::invokeMethod(picker,"choose",Q_ARG(QVariant,far.year()),Q_ARG(QVariant,far.month()-1),Q_ARG(QVariant,far.day())));
        QTRY_COMPARE(qint64(page->property("selectedDate").toDouble()),QDateTime(far,QTime(0,0)).toMSecsSinceEpoch());
        QTRY_VERIFY(!picker->property("visible").toBool());
        // A real click on a day cell jumps there too.
        QMetaObject::invokeMethod(dateButton,"clicked");
        QTRY_VERIFY(picker->property("visible").toBool());
        auto *grid=findItem(window.contentItem(),"scheduleMonthGrid");
        QTRY_VERIFY(grid->height()>100);
        QQuickItem *cell=nullptr;
        const QDate target(far.year(),far.month(),15);
        for (auto *c: grid->property("contentItem").value<QQuickItem*>()->childItems())
            if (c->property("d").toDateTime().date()==target) cell=c;
        QVERIFY(cell);
        QTest::qWait(400); // let the popup's enter transition finish
        QTest::mouseClick(&window,Qt::LeftButton,{},cell->mapToScene(QPointF(cell->width()/2,cell->height()/2)).toPoint());
        QTRY_COMPARE(qint64(page->property("selectedDate").toDouble()),QDateTime(target,QTime(0,0)).toMSecsSinceEpoch());
        QTRY_VERIFY(!picker->property("visible").toBool());
        page->setProperty("selectedDate",selected);
        const QVariantList scheduled{
            QVariantMap{{"platform","instagram"},{"id","ig1"},{"publishAt",at},{"title","Handwriting reveal"},{"status","scheduled"}},
            QVariantMap{{"platform","facebook"},{"id","fb1"},{"publishAt",at+35*60000},{"title","Screen recording"},{"status","failed"},{"error","Connection needs attention"}},
            QVariantMap{{"platform","youtube"},{"id","yt1"},{"publishAt",at+60*60000},{"title","Product walkthrough"},{"status","scheduled"}}
        };
        const QVariantList published{
            QVariantMap{{"platform","instagram"},{"id","ig0"},{"published",at-3600000},{"title","Your handwriting, typed"},{"permalink","https://instagram.com/reel/demo/"}}
        };
        page->setProperty("scheduledRecords",scheduled);
        page->setProperty("publishedReels",published);
        QTRY_COMPARE(page->property("dailyCount").toInt(),4);
        QVERIFY(findItem(page,"scheduleCard-facebook-fb1"));
        QVERIFY(findItem(page,"scheduleFallback-facebook-fb1"));
        QVERIFY(findItem(page,"scheduleCard-instagram-ig0"));

        // Invalid cadence cannot alter saved settings or silently move slots to tomorrow.
        page->setProperty("timingOpen",true);
        QTRY_VERIFY(findItem(page,"scheduleStart-instagram"));
        findItem(page,"scheduleStart-instagram")->setProperty("text","23:30");
        QTRY_VERIFY(!findItem(page,"scheduleApplyTiming")->isEnabled());
        QCOMPARE(backend.property("schedulePreferences").toMap().value("instagram").toMap().value("startMinute").toInt(),480);
        findItem(page,"scheduleStart-instagram")->setProperty("text","07:00");
        QTRY_VERIFY(findItem(page,"scheduleApplyTiming")->isEnabled());
        QMetaObject::invokeMethod(findItem(page,"scheduleApplyTiming"),"clicked");
        QTRY_COMPARE(backend.property("schedulePreferences").toMap().value("instagram").toMap().value("startMinute").toInt(),420);
        QVERIFY(QFile::remove(state.filePath("schedule-plan.json")));
        QVERIFY(QDir().mkpath(state.filePath("schedule-plan.json")));
        findItem(page,"scheduleStart-instagram")->setProperty("text","06:00");
        QMetaObject::invokeMethod(findItem(page,"scheduleApplyTiming"),"clicked");
        QVERIFY(!page->property("notice").toString().contains("saved",Qt::CaseInsensitive));
        QCOMPARE(backend.schedulePreferences().value("instagram").toMap().value("startMinute").toInt(),420);
        page->setProperty("notice",QString());
        page->setProperty("timingOpen",false);
        QTest::qWait(150);
        window.grabWindow().save("/tmp/reelcut-schedule-page.png");
        window.resize(980,700);
        page->setSize(QSizeF(980,700));
        QTest::qWait(150);
        for (const QString name : {"scheduleColumn-instagram","scheduleColumn-facebook","scheduleColumn-youtube","scheduleColumn-tiktok"}) {
            auto *column=findItem(page,name);
            QVERIFY(column->width() > 200);
            const QPointF topLeft=column->mapToItem(page,QPointF());
            QVERIFY(topLeft.x()>=0 && topLeft.x()+column->width()<=page->width()+1);
        }
        window.grabWindow().save("/tmp/reelcut-schedule-page-narrow.png");
        page->setProperty("timingOpen",true);
        QTest::qWait(100);
        for (const QString name : {"scheduleStart-instagram","scheduleGap-facebook","scheduleGap-youtube","scheduleApplyTiming"}) {
            auto *control=findItem(page,name);QVERIFY(control);
            const QPointF pos=control->mapToItem(page,QPointF());
            QVERIFY2(pos.x()>=0 && pos.x()+control->width()<=page->width()+1,qPrintable(name));
        }
        window.grabWindow().save("/tmp/reelcut-schedule-timing-narrow.png");
        page->setParentItem(nullptr);
    }

    void scheduleNavigationPrefillsWithoutPublishing()
    {
        QTemporaryDir dir, state, themeDir;
        QImage photo(64,64,QImage::Format_RGB32);
        photo.fill(Qt::blue);
        QVERIFY(photo.save(dir.filePath("photo.png")));
        Project project;
        project.clips={media::clipFor(media::probe(dir.filePath("photo.png")),1.0)};
        write(dir.filePath("schedule.reel"),QJsonDocument(project.toJson(dir.path())).toJson());
        Theme theme(themeDir.path());
        Backend backend(nullptr,nullptr,&theme,nullptr,state.path());
        QVERIFY(backend.openFile(dir.filePath("schedule.reel")));
        HiggsfieldCredits higgsfield;
        higgsfield.stateDir=state.path();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme",&theme);
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("downloads",testDownloads());
        engine.rootContext()->setContextProperty("higgsfield",&higgsfield);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360,860);window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *toggle=findItem(window->contentItem(),"scheduleToggle");
        QVERIFY(toggle);
        window->setProperty("statsVisible",true);
        toggle->setProperty("checked",true);
        QMetaObject::invokeMethod(toggle,"toggled");
        QTRY_VERIFY(window->property("scheduleVisible").toBool());
        QVERIFY(!window->property("statsVisible").toBool());
        QVERIFY(window->property("typing").toBool());
        auto *page=findItem(window->contentItem(),"schedulePage");
        QVERIFY(page && page->isVisible());
        const QDate tomorrow=QDate::currentDate().addDays(1);
        const double at=double(QDateTime(tomorrow,QTime(9,0)).toMSecsSinceEpoch());
        page->setProperty("selectedDate",double(QDateTime(tomorrow,QTime(0,0)).toMSecsSinceEpoch()));
        const int clips=backend.clips().size();
        QTest::keyClick(window,Qt::Key_S);
        QCOMPARE(backend.clips().size(),clips);
        QVERIFY(QMetaObject::invokeMethod(page,"chooseSlot",Q_ARG(QVariant,"youtube"),Q_ARG(QVariant,at)));
        QObject *dialog=window->findChild<QObject *>("postDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("platform").toString(),QString("youtube"));
        QCOMPARE(dialog->property("stage").toString(),QString("connect"));
        QVERIFY(dialog->property("scheduleLater").toBool());
        QCOMPARE(findItem(window->contentItem(),"postWhen")->property("text").toString(),
                 QDateTime(tomorrow,QTime(9,0)).toString("yyyy-MM-dd HH:mm"));
        QCOMPARE(backend.parseWhen(findItem(window->contentItem(),"postWhen")->property("text").toString()).value("ms").toDouble(),at);
        // Backing out of account setup must never change this request into Publish/Next.
        dialog->setProperty("stage","write");
        QVERIFY(!findItem(window->contentItem(),"postNext")->isEnabled());
        QVERIFY(!backend.busy());
        QVERIFY(backend.calendarPosts().isEmpty());
        QMetaObject::invokeMethod(dialog,"close");
        QVERIFY(QMetaObject::invokeMethod(dialog,"openFor",Q_ARG(QVariant,"youtube")));
        QVERIFY(!dialog->property("scheduleFromCalendar").toBool());
        QVERIFY(!dialog->property("scheduleLater").toBool());
        QMetaObject::invokeMethod(dialog,"close");
        FakeInstagram instagram;
        backend.instagram()->graphBase=instagram.base();
        QSignalSpy connected(backend.instagram(),&InstagramApi::connectFinished);
        backend.connectInstagram("good-token");
        QVERIFY(connected.wait(5000));
        QVERIFY(QMetaObject::invokeMethod(dialog,"openScheduled",Q_ARG(QVariant,"instagram"),Q_ARG(QVariant,at)));
        QCOMPARE(dialog->property("stage").toString(),QString("scheduler"));
        dialog->setProperty("stage","write");
        QVERIFY(!findItem(window->contentItem(),"postNext")->isEnabled());
        QMetaObject::invokeMethod(dialog,"close");
        FakeConvex convex;
        backend.setupScheduler(convex.base(),convex.key);
        QVERIFY(QMetaObject::invokeMethod(dialog,"openScheduled",Q_ARG(QVariant,"instagram"),Q_ARG(QVariant,at)));
        QCOMPARE(dialog->property("stage").toString(),QString("write"));
        QVERIFY(dialog->property("scheduling").toBool());
        QTRY_VERIFY(findItem(window->contentItem(),"postNext")->isEnabled());
        QCOMPARE(findItem(window->contentItem(),"postNext")->property("text").toString(),QString("Schedule"));
        QMetaObject::invokeMethod(findItem(window->contentItem(),"postQueue"),"clicked");
        QVERIFY(!dialog->property("scheduleFromCalendar").toBool());
        QVERIFY(dialog->property("queueing").toBool());
        QVERIFY(!backend.busy());
        for (const auto &call : instagram.calls) QVERIFY(!call.path.contains("/media"));
        QMetaObject::invokeMethod(dialog,"close");
        QTest::qWait(200);
        window->grabWindow().save("/tmp/reelcut-schedule-main.png");
        window->resize(980,700);
        QTest::qWait(200);
        window->grabWindow().save("/tmp/reelcut-schedule-main-narrow.png");
        QTest::keyClick(window,Qt::Key_Escape);
        QTRY_VERIFY(!window->property("scheduleVisible").toBool());
        window->setProperty("scheduleVisible",true);
        window->setProperty("statsVisible",true);
        QVERIFY(!window->property("scheduleVisible").toBool());
    }

    void higgsfieldCountsSpending()
    {
        auto t = [](const char *action, double credits, const char *at) {
            return QJsonObject{{"display_name", "x"}, {"action", action}, {"credits", credits}, {"created_at", at}};
        };
        const QJsonArray items{t("spend", -0.5, "2026-11-06T14:09:18.319304Z"), t("refund", 7.5, "2026-11-06T14:05:00Z"),
                               t("spend", -7.5, "2026-11-06T14:04:00Z"), t("grant", 500, "2026-11-06T14:03:00Z"), t("deduct", -10, "2026-11-06T14:02:00Z"),
                               t("spend", -5.82, "2026-11-06T05:46:52.196460Z")};
        const QDateTime afternoon(QDate(2026, 11, 6), QTime(14, 0), QTimeZone::UTC);
        // The refund cancels its spend; grants and expiring daily credits aren't spending.
        QCOMPARE(HiggsfieldCredits::spentFrom(items, afternoon), 0.5);
        QCOMPARE(HiggsfieldCredits::spentFrom(items, afternoon.addDays(-1)), 6.32);
        QCOMPARE(HiggsfieldCredits::spentFrom(items, afternoon.addDays(1)), 0.0);
        QCOMPARE(HiggsfieldCredits::spentFrom(items, QDateTime()), 6.32);  // all time
        // A poll's fresh page goes ahead of the history, without repeats.
        const QJsonArray merged = HiggsfieldCredits::mergeNewest(
            QJsonArray{t("spend", -1, "2026-11-06T15:00:00Z"), items.first()}, items);
        QCOMPARE(merged.size(), items.size() + 1);
        QCOMPARE(merged.first().toObject().value("credits").toDouble(), -1.0);
    }

    void higgsfieldReadsMcpReplies()
    {
        const QJsonObject balance{{"credits", 649.27}, {"subscription_plan_type", "plus"}};
        const QJsonObject reply{
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"result", QJsonObject{{"content", QJsonArray{QJsonObject{
                                                   {"type", "text"},
                                                   {"text", QString::fromUtf8(QJsonDocument(balance).toJson())}}}}}}};
        const QByteArray json = QJsonDocument(reply).toJson(QJsonDocument::Compact);
        QCOMPARE(HiggsfieldCredits::toolResult(HiggsfieldCredits::rpcMessage(json)).value("credits").toDouble(), 649.27);
        // The same answer as server-sent events, after a progress notice.
        const QByteArray sse = QByteArray("event: message\r\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}\r\n\r\n")
                               + "event: message\r\ndata: " + json + "\r\n\r\n";
        QCOMPARE(HiggsfieldCredits::toolResult(HiggsfieldCredits::rpcMessage(sse)).value("subscription_plan_type").toString(),
                 QString("plus"));
        // Structured content wins over text.
        const QJsonObject structured{{"result", QJsonObject{{"structuredContent", QJsonObject{{"credits", 3}}},
                                                            {"content", QJsonArray{}}}}};
        QCOMPARE(HiggsfieldCredits::toolResult(structured).value("credits").toInt(), 3);
    }

    void mcpManagesEffects()
    {
        QTemporaryDir dir, state;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=3", "-c:v", "libx264", source}));
        McpServer server(state.path());
        QJsonObject r = call(server, "create_project", {{"path", dir.filePath("e")}, {"clips", QJsonArray{source}}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        r = call(server, "add_effect", {{"kind", "shake"}, {"start", 0.5}, {"end", 2.0}, {"intensity", 0.9}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        QVERIFY(textOf(r).contains("\"kind\": \"shake\""));
        QVERIFY(!call(server, "edit_effect", {{"index", 0}, {"start", 2.5}, {"end", 2.9}}).value("isError").toBool());
        Project saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("e.reel"))).object(), dir.path());
        QCOMPARE(saved.effects[0].start, 2.5);
        QCOMPARE(saved.effects[0].end, 2.9);
        QCOMPARE(saved.effects[0].intensity, 0.9);
        QVERIFY(call(server, "add_effect", {{"kind", "explode"}}).value("isError").toBool());
        QVERIFY(call(server, "edit_effect", {{"index", 3}}).value("isError").toBool());
        QVERIFY(!call(server, "remove_effect", {{"index", 0}}).value("isError").toBool());
        saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("e.reel"))).object(), dir.path());
        QVERIFY(saved.effects.isEmpty());
    }

private:
    // Repeater delegates aren't QObject children, so walk the visual tree.
    static QQuickItem *findItem(QQuickItem *item, const QString &name)
    {
        if (item->objectName() == name)
            return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }

    static QJsonObject call(McpServer &server, const QString &tool, const QJsonObject &args)
    {
        return server.handle({{"jsonrpc", "2.0"}, {"id", 9}, {"method", "tools/call"},
                              {"params", QJsonObject{{"name", tool}, {"arguments", args}}}})
            .value("result").toObject();
    }

    static QString textOf(const QJsonObject &result)
    {
        for (const QJsonValue &item : result.value("content").toArray())
            if (item.toObject().value("type") == "text")
                return item.toObject().value("text").toString();
        return {};
    }

    static QByteArray readAll(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(ReelcutTests)
#include "reelcut_tests.moc"
