#include "songgen.h"
#include "modalusage.h"
#include "publisher.h"
#include "songrequest.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QUrlQuery>

static QJsonObject parse(const QByteArray &body) { return QJsonDocument::fromJson(body).object(); }

// Answers that are known at once still arrive after the caller returns, like
// the network ones, so a caller can connect first and wait.
void SongGenerator::later(const std::function<void()> &f) { QTimer::singleShot(0, this, f); }

SongGenerator::SongGenerator(ModalUsage *usage, const QString &stateDir, const QString &songsDir, QObject *parent)
    : QObject(parent), m_usage(usage), m_net(new QNetworkAccessManager(this)), m_stateDir(stateDir),
      m_songsDir(!songsDir.isEmpty() ? songsDir
                 : QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Reelcut songs") {
    const QJsonObject c = Net::load(Net::stateFile(m_stateDir, "modal.json"));
    m_endpoint = c.value("endpoint").toString();
    m_tokenId = c.value("tokenId").toString();
    m_tokenSecret = c.value("tokenSecret").toString();
    m_poll.setSingleShot(true);
    connect(&m_poll, &QTimer::timeout, this, &SongGenerator::poll);
    reload();
}

void SongGenerator::configure(const QString &endpoint, const QString &tokenId, const QString &tokenSecret) {
    QString e = endpoint.trimmed();
    while (e.endsWith('/')) e.chop(1);
    m_endpoint = e;
    m_tokenId = tokenId.trimmed();
    if (!tokenSecret.trimmed().isEmpty()) m_tokenSecret = tokenSecret.trimmed();  // blank keeps the old one
    Net::savePrivate(Net::stateFile(m_stateDir, "modal.json"),
                     {{"endpoint", m_endpoint}, {"tokenId", m_tokenId}, {"tokenSecret", m_tokenSecret}});
    emit configChanged();
}

QNetworkRequest SongGenerator::request(const QString &path) const {
    QNetworkRequest r(QUrl(m_endpoint + path));
    r.setRawHeader("Modal-Key", m_tokenId.toUtf8());
    r.setRawHeader("Modal-Secret", m_tokenSecret.toUtf8());
    r.setTransferTimeout(timeoutMs);
    return r;
}

QString SongGenerator::explain(QNetworkReply *reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401 || status == 403)
        return tr("Modal token rejected. Check the token ID and secret in the Modal meter.");
    if (status == 404)
        return tr("Modal app not deployed: run modal deploy modal/acestep_app.py");
    const QJsonObject o = parse(reply->peek(4096));
    const QString server = o.value("error").toString();
    if (!server.isEmpty()) return server;
    return reply->errorString();
}

void SongGenerator::setStage(const QString &stage) { m_stage = stage; emit stateChanged(); }

void SongGenerator::testConnection() {
    if (!configured()) { later([this] { emit connectionTested(false, tr("Fill in the endpoint, token ID and secret.")); }); return; }
    QNetworkReply *reply = m_net->get(request("/health"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError;
        emit connectionTested(ok, ok ? tr("Connected. The GPU is up.") : explain(reply));
    });
}

QString SongGenerator::audioFor(const QString &path) {
    static const QStringList video{"mp4", "mov", "webm", "mkv", "m4v"};
    if (path.isEmpty() || !video.contains(QFileInfo(path).suffix().toLower())) return path;
    const QString out = m_folder + "/" + QFileInfo(path).completeBaseName() + ".mp3";
    QProcess ff;
    ff.start("ffmpeg", {"-y", "-hide_banner", "-loglevel", "error", "-i", path, "-vn", "-q:a", "2", out});
    if (!ff.waitForFinished(120000) || ff.exitCode() != 0) {
        m_lastError = tr("Couldn't take the audio out of %1: %2").arg(QFileInfo(path).fileName(),
                      QString::fromUtf8(ff.readAllStandardError()).trimmed());
        return {};
    }
    return out;
}

bool SongGenerator::generate(const QVariantMap &input) {
    if (m_busy || !configured()) return false;
    m_busy = true;
    m_lastError.clear();
    m_request = input;
    m_saved.clear();
    m_taskId.clear();
    m_pollErrors = 0;
    m_folder = m_songsDir + "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm") + "-" + SongRequest::slug(input);
    for (int n = 2; QFileInfo::exists(m_folder); ++n)
        m_folder = m_songsDir + "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm") + "-" + SongRequest::slug(input) + "-" + QString::number(n);
    QDir().mkpath(m_folder);
    setStage(tr("Preparing"));

    QVariantMap prepared = input;
    for (const char *key : {"reference", "remix"}) {
        const QString path = input.value(key).toString();
        if (path.isEmpty()) continue;
        const QString audio = audioFor(path);
        if (audio.isEmpty()) { fail(m_lastError); return true; }
        prepared[key] = audio;
    }
    const SongRequest::Form form = SongRequest::build(prepared);
    m_sent = {};
    for (const auto &f : form.fields) m_sent.insert(f.first, f.second);

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (const auto &f : form.fields) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"%1\"").arg(f.first));
        part.setBody(f.second.toUtf8());
        multi->append(part);
    }
    auto attach = [&](const QString &name, const QString &path) {
        if (path.isEmpty()) return true;
        auto *file = new QFile(path, multi);
        if (!file->open(QIODevice::ReadOnly)) return false;
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(path).name());
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QString("form-data; name=\"%1\"; filename=\"%2\"").arg(name, QFileInfo(path).fileName()));
        part.setBodyDevice(file);
        multi->append(part);
        return true;
    };
    if (!attach("reference_audio", form.referencePath) || !attach("src_audio", form.sourcePath)) {
        delete multi;
        fail(tr("Couldn't read an attached file."));
        return true;
    }

    if (m_usage) m_usage->requestStarted();
    m_clock.start();
    setStage(tr("Starting GPU (first time ~1–2 min)"));
    // A GET first: it wakes the GPU (Modal answers a long wait with a redirect,
    // which only a GET can follow) and says which GPU the cost is counted at.
    QNetworkReply *wake = m_net->get(request("/reelcut/gpu"));
    multi->setParent(this);
    connect(wake, &QNetworkReply::finished, this, [this, wake, multi] {
        wake->deleteLater();
        const int status = wake->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (wake->error() != QNetworkReply::NoError && status != 404) { delete multi; fail(explain(wake)); return; }
        const QString gpu = parse(wake->readAll()).value("gpu").toString();
        if (m_usage && !gpu.isEmpty()) m_usage->setGpu(gpu);
        send(multi);
    });
    return true;
}

void SongGenerator::send(QHttpMultiPart *multi) {
    setStage(tr("Sending"));
    QNetworkReply *reply = m_net->post(request("/release_task"), multi);
    multi->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { fail(explain(reply)); return; }
        m_taskId = parse(reply->readAll()).value("data").toObject().value("task_id").toString();
        if (m_taskId.isEmpty()) { fail(tr("ACE-Step didn't return a task.")); return; }
        setStage(tr("Generating"));
        m_poll.start(pollMs);
    });
}

void SongGenerator::poll() {
    if (m_clock.elapsed() > timeoutMs) { fail(tr("Timed out after %1 minutes.").arg(timeoutMs / 60000)); return; }
    QNetworkRequest r = request("/query_result");
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray body = QJsonDocument(QJsonObject{{"task_id_list", QJsonArray{m_taskId}}}).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_net->post(r, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 || status == 403 || status == 404 || ++m_pollErrors > 3) { fail(explain(reply)); return; }
            m_poll.start(pollMs);
            return;
        }
        m_pollErrors = 0;
        const QJsonObject task = parse(reply->readAll()).value("data").toArray().first().toObject();
        const int status = task.value("status").toInt();
        if (status == 0) { m_poll.start(pollMs); return; }
        const QJsonArray results = QJsonDocument::fromJson(task.value("result").toString().toUtf8()).array();
        if (status == 2) {
            QString why = results.first().toObject().value("error").toString();
            if (why.isEmpty()) why = task.value("error").toString();
            fail(why.isEmpty() ? tr("ACE-Step failed without saying why.") : why);
            return;
        }
        QStringList files;
        for (const auto &r : results) {
            const QString file = r.toObject().value("file").toString();
            if (!file.isEmpty()) files << file;
        }
        if (files.isEmpty()) { fail(tr("ACE-Step finished but returned no audio.")); return; }
        setStage(tr("Downloading"));
        download(files, 0);
    });
}

void SongGenerator::download(const QStringList &files, int index) {
    if (index >= files.size()) { finish(); return; }
    QNetworkReply *reply = m_net->get(request(files[index]));
    connect(reply, &QNetworkReply::finished, this, [this, reply, files, index] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { fail(explain(reply)); return; }
        const QString path = m_folder + QString("/take-%1.mp3").arg(index + 1);
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(reply->readAll()) < 0) { fail(tr("Couldn't save %1").arg(path)); return; }
        m_saved << path;
        download(files, index + 1);
    });
}

void SongGenerator::finish() {
    const double seconds = m_clock.elapsed() / 1000.0;
    if (m_usage) m_usage->requestFinished(seconds, m_saved.size());
    QJsonArray files;
    for (const QString &f : m_saved) files << QFileInfo(f).fileName();
    const QJsonObject record{{"request", QJsonObject::fromVariantMap(m_request)}, {"sent", m_sent},
                             {"taskId", m_taskId}, {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                             {"billedSeconds", seconds}, {"estimatedCost", seconds * (m_usage ? m_usage->ratePerSecond() : ModalUsage::rateFor({}))},
                             {"gpu", m_usage ? m_usage->gpu() : QString()},
                             {"files", files}};
    QFile out(m_folder + "/request.json");
    if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(record).toJson());
    out.close();
    m_busy = false;
    setStage({});
    reload();
    emit finished(m_folder);
}

void SongGenerator::fail(const QString &message) {
    m_poll.stop();
    if (m_clock.isValid() && m_usage) m_usage->requestFinished(m_clock.elapsed() / 1000.0, 0);
    m_clock.invalidate();
    if (QDir(m_folder).isEmpty()) QDir().rmdir(m_folder);
    m_lastError = message;
    m_busy = false;
    setStage({});
    emit failed(message);
}

void SongGenerator::reload() {
    QVariantList list;
    const QFileInfoList dirs = QDir(m_songsDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo &d : dirs) {
        QFile f(d.filePath() + "/request.json");
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject o = parse(f.readAll());
        QStringList files;
        for (const auto &name : o.value("files").toArray()) files << d.filePath() + "/" + name.toString();
        const QJsonObject req = o.value("request").toObject();
        QStringList caption;
        for (const char *k : {"prompt", "style"}) if (!req.value(k).toString().isEmpty()) caption << req.value(k).toString();
        list << QVariantMap{{"folder", d.filePath()}, {"name", d.fileName()}, {"files", files},
                            {"created", o.value("createdAt").toString()}, {"seconds", o.value("billedSeconds").toDouble()},
                            {"cost", o.value("estimatedCost").toDouble()},
                            {"caption", caption.isEmpty() ? tr("Random song") : caption.join(" · ")}};
    }
    m_songs = list;
    emit songsChanged();
}

QString SongGenerator::readTextFile(const QString &path) const {
    QFile f(path);
    return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.read(64 * 1024)) : QString();
}

void SongGenerator::writeLyrics(const QVariantMap &input) {
    if (!configured()) { later([this] { emit lyricsWritten({}, tr("Connect Modal first (click the Modal meter).")); }); return; }
    const SongRequest::Form form = SongRequest::build(input);
    QNetworkRequest r = request("/format_input");
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    if (m_usage) m_usage->requestStarted();
    auto *clock = new QElapsedTimer; clock->start();
    QNetworkReply *reply = m_net->post(r, Net::formBody({{"prompt", form.caption}, {"lyrics", input.value("lyrics").toString()}}));
    connect(reply, &QNetworkReply::finished, this, [this, reply, clock] {
        reply->deleteLater();
        if (m_usage) m_usage->requestFinished(clock->elapsed() / 1000.0, 0);
        delete clock;
        if (reply->error() != QNetworkReply::NoError) { emit lyricsWritten({}, explain(reply)); return; }
        const QString lyrics = parse(reply->readAll()).value("data").toObject().value("lyrics").toString().trimmed();
        emit lyricsWritten(lyrics, lyrics.isEmpty() ? tr("ACE-Step returned no lyrics.") : QString());
    });
}

void SongGenerator::describeImages(const QStringList &paths) {
    QString program = claudeCommand;
    if (program.isEmpty()) program = QStandardPaths::findExecutable("claude");
    for (const QString &c : {QDir::homePath() + "/.local/share/mise/shims/claude", QDir::homePath() + "/.local/bin/claude", QString("/usr/bin/claude")})
        if (program.isEmpty() && QFileInfo(c).isExecutable()) program = c;
    if (program.isEmpty() || !QFileInfo(program).isExecutable()) { later([this] { emit imagesDescribed({}, tr("Claude Code isn't installed, so images were skipped.")); }); return; }
    const QString prompt = tr("Look at these images and describe the song they suggest in one short line for a music "
                              "generator: genre, mood, tempo in bpm, and main instruments. Reply with that line only.\n")
                           + paths.join('\n');
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus) {
        p->deleteLater();
        const QString text = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        if (code != 0 || text.isEmpty())
            emit imagesDescribed({}, tr("Claude Code couldn't describe the images: %1").arg(QString::fromUtf8(p->readAllStandardError()).trimmed()));
        else
            emit imagesDescribed(text, {});
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        p->deleteLater();
        emit imagesDescribed({}, tr("Couldn't start Claude Code."));
    });
    p->start(program, {"-p", prompt, "--allowedTools", "Read", "--output-format", "text"});
}
