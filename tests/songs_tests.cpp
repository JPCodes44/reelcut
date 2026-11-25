#include "songrequest.h"
#include "modalusage.h"
#include <QTemporaryDir>
#include "songgen.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QtTest>

static QString field(const SongRequest::Form &f, const QString &key) {
    for (const auto &p : f.fields) if (p.first == key) return p.second;
    return QStringLiteral("<missing>");
}

// A tiny stand-in for the ACE-Step API: answers by path with canned bodies
// and remembers each request's path and headers.
class FakeAce : public QObject {
public:
    QTcpServer server;
    QStringList paths;
    QList<QByteArray> heads, bodies;
    int status = 200;          // for every reply
    int pollsBeforeDone = 1;   // /query_result answers "running" this many times
    int taskStatus = 1;        // then this
    FakeAce() {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto *buf = new QByteArray;
                connect(s, &QTcpSocket::disconnected, s, [s, buf] { delete buf; s->deleteLater(); });
                connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    buf->append(s->readAll());
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray head = buf->left(end);
                    int length = 0;
                    for (const QByteArray &line : head.split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    if (buf->size() < end + 4 + length) return;
                    const QString path = QString::fromUtf8(head.split(' ').value(1));
                    paths << path; heads << head; bodies << buf->mid(end + 4, length);
                    buf->clear();
                    const QByteArray body = answer(path);
                    s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
            }
        });
    }
    QByteArray answer(const QString &path) {
        if (path.startsWith("/v1/audio")) return QByteArray("ID3fake-mp3-") + path.toUtf8();
        if (path == "/health") return R"({"data":{"status":"ok"},"code":200})";
        if (path == "/release_task") return R"({"data":{"task_id":"t1","status":"queued"},"code":200})";
        if (path == "/format_input") return R"({"data":{"caption":"c","lyrics":"[Verse]\nla la"},"code":200})";
        if (path == "/query_result") {
            if (pollsBeforeDone-- > 0) return R"({"data":[{"task_id":"t1","status":0,"result":"[]"}],"code":200})";
            const QString result = taskStatus == 1
                ? R"([{"file":"/v1/audio?path=%2Ftmp%2Fa.mp3","status":1},{"file":"/v1/audio?path=%2Ftmp%2Fb.mp3","status":1}])"
                : R"([{"status":2,"error":"CUDA out of memory"}])";
            return QJsonDocument(QJsonObject{{"code", 200}, {"data", QJsonArray{QJsonObject{
                {"task_id", "t1"}, {"status", taskStatus}, {"result", result}}}}}).toJson(QJsonDocument::Compact);
        }
        return "{}";
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(server.serverPort()); }
};

class SongsTests : public QObject {
    Q_OBJECT
private slots:
    void emptyRequestIsRandomSample() {
        const auto f = SongRequest::build({});
        QCOMPARE(field(f, "sample_mode"), QString("true"));
        QCOMPARE(field(f, "sample_query"), QString(""));
        QCOMPARE(field(f, "batch_size"), QString("2"));
        QCOMPARE(field(f, "audio_format"), QString("mp3"));
        QCOMPARE(field(f, "thinking"), QString("true"));
        QCOMPARE(field(f, "audio_duration"), QString("<missing>"));
        QCOMPARE(field(f, "prompt"), QString("<missing>"));
        QVERIFY(f.referencePath.isEmpty() && f.sourcePath.isEmpty());
    }
    void promptAndStyleWithoutLyricsWritesWords() {
        const auto f = SongRequest::build({{"prompt", "a song about a cat"}, {"style", "pop"}, {"imageStyle", "neon, 120 bpm"}});
        QCOMPARE(field(f, "sample_mode"), QString("true"));
        QCOMPARE(field(f, "sample_query"), QString("a song about a cat. pop. neon, 120 bpm"));
        QCOMPARE(f.caption, QString("a song about a cat. pop. neon, 120 bpm"));
    }
    void lyricsAreSung() {
        const auto f = SongRequest::build({{"style", "rock"}, {"lyrics", "[Verse]\nhello"}});
        QCOMPARE(field(f, "sample_mode"), QString("<missing>"));
        QCOMPARE(field(f, "prompt"), QString("rock"));
        QCOMPARE(field(f, "lyrics"), QString("[Verse]\nhello"));
    }
    void instrumentalDropsLyrics() {
        const auto f = SongRequest::build({{"lyrics", "words"}, {"instrumental", true}});
        QCOMPARE(field(f, "lyrics"), QString("[Instrumental]"));
        QCOMPARE(field(f, "sample_mode"), QString("<missing>"));
    }
    void lengthAndVersionsAreClamped() {
        auto f = SongRequest::build({{"duration", 5.0}, {"versions", 9}});
        QCOMPARE(field(f, "audio_duration"), QString("10"));
        QCOMPARE(field(f, "batch_size"), QString("4"));
        f = SongRequest::build({{"duration", 999.0}, {"versions", 0}});
        QCOMPARE(field(f, "audio_duration"), QString("240"));
        QCOMPARE(field(f, "batch_size"), QString("1"));
    }
    void referenceIsAFile() {
        const auto f = SongRequest::build({{"reference", "/tmp/ref.mp3"}});
        QCOMPARE(f.referencePath, QString("/tmp/ref.mp3"));
    }
    void remixModes() {
        auto f = SongRequest::build({{"remix", "/tmp/a.mp3"}, {"remixMode", "cover"}, {"style", "jazz"}});
        QCOMPARE(f.sourcePath, QString("/tmp/a.mp3"));
        QCOMPARE(field(f, "task_type"), QString("cover"));
        QCOMPARE(field(f, "prompt"), QString("jazz"));
        QCOMPARE(field(f, "sample_mode"), QString("<missing>"));
        f = SongRequest::build({{"remix", "/tmp/a.mp3"}, {"remixMode", "extend"}});
        QCOMPARE(field(f, "task_type"), QString("complete"));
        f = SongRequest::build({{"remix", "/tmp/a.mp3"}, {"remixMode", "repaint"}, {"repaintStart", 4.0}, {"repaintEnd", 0.0}});
        QCOMPARE(field(f, "task_type"), QString("repaint"));
        QCOMPARE(field(f, "repainting_start"), QString("4"));
        QCOMPARE(field(f, "repainting_end"), QString("-1"));
    }
    void slugs() {
        QCOMPARE(SongRequest::slug({{"prompt", "A Song about my CAT, Milo!!"}}), QString("a-song-about-my"));
        QCOMPARE(SongRequest::slug({{"style", "lo-fi"}}), QString("lo-fi"));
        QCOMPARE(SongRequest::slug({}), QString("random"));
    }
    void usageCostAndLevels() {
        QTemporaryDir dir;
        ModalUsage u(dir.path());
        QCOMPARE(u.limit(), 30.0);
        QCOMPARE(u.level(), QString("ok"));
        u.requestStarted();
        u.requestFinished(1000, 2);
        QVERIFY(qAbs(u.spent() - 1000 * ModalUsage::kRatePerSecond) < 1e-9);
        QCOMPARE(u.songCount(), 2);
        u.setLimit(u.spent() / 0.85);  QCOMPARE(u.level(), QString("warn"));
        u.setLimit(u.spent() / 1.0);   QCOMPARE(u.level(), QString("over"));
        u.setLimit(u.spent() / 0.5);   QCOMPARE(u.level(), QString("ok"));
    }

    void usageIdleTail() {
        QTemporaryDir dir;
        ModalUsage u(dir.path());
        QDateTime t = QDateTime::fromString("2026-11-25T10:00:00Z", Qt::ISODate);
        u.now = [&] { return t; };
        u.requestStarted();
        u.requestFinished(10, 1);           // tail opens at 10:00:00
        t = t.addSecs(30);
        u.requestStarted();                 // new request 30 s in: 30 s idle recorded
        QCOMPARE(u.idleSeconds(), 30.0);
        u.requestFinished(10, 1);
        u.endTail();                        // the timer ran out: full 120 s
        QCOMPARE(u.idleSeconds(), 150.0);
        u.requestStarted(); u.requestFinished(5, 0);
        t = t.addSecs(45);
        u.flushTail();                      // quitting 45 s into the tail
        QCOMPARE(u.idleSeconds(), 195.0);
        QCOMPARE(u.busySeconds(), 25.0);
    }
    void usageMonthRolloverAndPersistence() {
        QTemporaryDir dir;
        QDateTime t = QDateTime::fromString("2026-11-30T23:00:00Z", Qt::ISODate);
        {
            ModalUsage u(dir.path());
            u.now = [&] { return t; };
            u.setLimit(12);
            u.requestStarted(); u.requestFinished(100, 1); u.flushTail();
        }
        {
            ModalUsage u(dir.path());
            u.now = [&] { return t; };
            u.reload();
            QCOMPARE(u.songCount(), 1);
            QCOMPARE(u.limit(), 12.0);
            QVERIFY(QFile::exists(dir.filePath("modal-status.json")));
            t = QDateTime::fromString("2026-12-01T09:00:00Z", Qt::ISODate);
            u.requestStarted(); u.requestFinished(10, 1); u.flushTail();
            QCOMPARE(u.songCount(), 1);     // November's song is gone
            QCOMPARE(u.limit(), 12.0);      // the limit survives
        }
    }
    void generatorNeedsConfig() {
        QTemporaryDir state, songs;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        QVERIFY(!g.configured());
        QVERIFY(!g.generate({}));
    }
    void generatorWritesTakesAndRequest() {
        QTemporaryDir state, songs;
        FakeAce ace;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.pollMs = 10;
        g.configure(ace.base(), "wk-id", "ws-secret");
        QVERIFY(g.configured());
        QVERIFY(QFile::exists(state.filePath("modal.json")));
        QSignalSpy done(&g, &SongGenerator::finished);
        QVERIFY(g.generate({{"prompt", "cat song"}, {"versions", 2}}));
        QVERIFY(g.busy());
        QVERIFY(done.wait(5000));
        const QString folder = done.first().first().toString();
        QVERIFY(folder.endsWith("-cat-song"));
        QVERIFY(QFile::exists(folder + "/take-1.mp3"));
        QVERIFY(QFile::exists(folder + "/take-2.mp3"));
        const QJsonObject req = QJsonDocument::fromJson([&] { QFile f(folder + "/request.json"); f.open(QIODevice::ReadOnly); return f.readAll(); }()).object();
        QCOMPARE(req.value("taskId").toString(), QString("t1"));
        QCOMPARE(req.value("request").toObject().value("prompt").toString(), QString("cat song"));
        QVERIFY(req.value("estimatedCost").toDouble() >= 0);
        QVERIFY(ace.heads.first().contains("Modal-Key: wk-id"));
        QVERIFY(ace.heads.first().contains("Modal-Secret: ws-secret"));
        QVERIFY(ace.bodies.first().contains("name=\"sample_query\""));
        QCOMPARE(g.songs().size(), 1);
        QCOMPARE(usage.songCount(), 2);
        QVERIFY(!g.busy());
    }
    void generatorUploadsAttachments() {
        QTemporaryDir state, songs, files;
        FakeAce ace;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.pollMs = 10;
        g.configure(ace.base(), "k", "s");
        QFile ref(files.filePath("ref.mp3")); ref.open(QIODevice::WriteOnly); ref.write("REFDATA"); ref.close();
        QSignalSpy done(&g, &SongGenerator::finished);
        QVERIFY(g.generate({{"reference", ref.fileName()}}));
        QVERIFY(done.wait(5000));
        QVERIFY(ace.bodies.first().contains("name=\"reference_audio\"; filename=\"ref.mp3\""));
        QVERIFY(ace.bodies.first().contains("REFDATA"));
    }
    void generatorReportsTaskFailure() {
        QTemporaryDir state, songs;
        FakeAce ace; ace.taskStatus = 2;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.pollMs = 10;
        g.configure(ace.base(), "k", "s");
        QSignalSpy failed(&g, &SongGenerator::failed);
        QVERIFY(g.generate({}));
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().first().toString().contains("CUDA out of memory"));
        QVERIFY(!g.busy());
        QCOMPARE(usage.songCount(), 0);
    }
    void generatorExplainsAuthAndDeploy() {
        QTemporaryDir state, songs;
        FakeAce ace;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.configure(ace.base(), "k", "s");
        QSignalSpy failed(&g, &SongGenerator::failed);
        ace.status = 401;
        QVERIFY(g.generate({}));
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.last().first().toString().contains("token"));
        ace.status = 404;
        QVERIFY(g.generate({}));
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.last().first().toString().contains("modal deploy"));
    }
    void generatorTestsConnectionAndWritesLyrics() {
        QTemporaryDir state, songs;
        FakeAce ace;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.configure(ace.base(), "k", "s");
        QSignalSpy tested(&g, &SongGenerator::connectionTested);
        g.testConnection();
        QVERIFY(tested.wait(5000));
        QCOMPARE(tested.first().first().toBool(), true);
        QSignalSpy lyrics(&g, &SongGenerator::lyricsWritten);
        g.writeLyrics({{"prompt", "cat"}});
        QVERIFY(lyrics.wait(5000));
        QCOMPARE(lyrics.first().first().toString(), QString("[Verse]\nla la"));
    }
    void generatorDescribesImagesWithClaude() {
        QTemporaryDir state, songs, bin;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        const QString fake = bin.filePath("claude");
        QFile f(fake); f.open(QIODevice::WriteOnly);
        f.write("#!/bin/sh\necho \"dreamy synthwave, 90 bpm\"\n"); f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        g.claudeCommand = fake;
        QSignalSpy described(&g, &SongGenerator::imagesDescribed);
        g.describeImages({"/tmp/a.png"});
        QVERIFY(described.wait(5000));
        QCOMPARE(described.first().first().toString(), QString("dreamy synthwave, 90 bpm"));
        g.claudeCommand = bin.filePath("missing");
        g.describeImages({"/tmp/a.png"});
        QVERIFY(described.wait(5000));
        QVERIFY(!described.last().at(1).toString().isEmpty());
    }
    void songsQmlLoads() {
        QTemporaryDir state, songs;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        QQuickStyle::setStyle("Material");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("modalUsage", &usage);
        engine.rootContext()->setContextProperty("songs", &g);
        engine.rootContext()->setContextProperty("backend", nullptr);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &w) {
            for (const auto &e : w) warnings << e.toString();
        });
        engine.loadData(R"(import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
ApplicationWindow { width: 1200; height: 900; visible: true; Material.theme: Material.Dark
  header: ToolBar { ModalMeter {} }
  SongsPage { anchors.fill: parent } })", QUrl("qrc:/SongsTestWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        auto *generate = engine.rootObjects().first()->findChild<QObject *>("songGenerate");
        QVERIFY(generate);
        QCOMPARE(generate->property("enabled").toBool(), false);   // not configured yet
        // REELCUT_SONGS_GRAB=page.png saves a picture of the page, to look at it.
        if (const QByteArray grab = qgetenv("REELCUT_SONGS_GRAB"); !grab.isEmpty()) {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(QTest::qWaitForWindowExposed(window));
            QTest::qWait(500);
            QVERIFY(window->grabWindow().save(QString::fromLocal8Bit(grab)));
        }
    }
};

QTEST_MAIN(SongsTests)
#include "songs_tests.moc"
