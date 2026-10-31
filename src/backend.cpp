#include "backend.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>

#include "captions.h"
#include "filepicker.h"
#include "media.h"
#include "silence.h"
#include "theme.h"
#include "thumbprovider.h"
#include "transcript.h"

namespace {
constexpr int kUndoLimit = 200;
const char *kModelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin";

QString modelPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/reelcut/models/ggml-base.bin";
}

QString fileName(const QString &path) {
    return QFileInfo(path).fileName();
}

// A name that doesn't overwrite anything: reel.mp4, reel-2.mp4, ...
QString freePath(const QString &folder, const QString &base, const QString &suffix) {
    QString path = QDir(folder).filePath(base + suffix);
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QDir(folder).filePath(QString("%1-%2%3").arg(base).arg(n).arg(suffix));
    return path;
}
}  // namespace

Backend::Backend(FilePicker *picker, ThumbProvider *thumbs, Theme *theme, QObject *parent)
    : QObject(parent), m_picker(picker), m_thumbs(thumbs), m_theme(theme) {
    if (m_picker) {
        connect(m_picker, &FilePicker::selected, this, &Backend::picked);
        connect(m_picker, &FilePicker::failed, this, [this](const QString &error) { setMessage(error, true); });
    }
    connect(&m_job, &Job::progress, this, [this](double fraction) {
        m_progress = fraction;
        emit jobChanged();
    });
    connect(&m_job, &Job::finished, this, [this](bool ok, const QByteArray &out, const QByteArray &err) {
        auto then = std::move(m_then);
        m_then = nullptr;
        if (then)
            then(ok, out, err);
    });
}

Backend::~Backend() {
    if (m_download)
        m_download->abort();
}

// ---- State for QML ----

QVariantList Backend::clips() const {
    QVariantList list;
    double start = 0.0;
    for (const Clip &c : m_project.clips) {
        list.append(QVariantMap{
            {"path", c.path}, {"name", fileName(c.path)}, {"in", c.in}, {"out", c.out},
            {"speed", c.speed}, {"zoom", c.zoom}, {"panX", c.panX}, {"panY", c.panY},
            {"width", c.width}, {"height", c.height}, {"audio", c.audio},
            {"sourceDuration", c.sourceDuration}, {"start", start}, {"duration", c.duration()},
            {"thumb", m_thumbKeys.value(c.path, -1)}, {"offline", !QFileInfo::exists(c.path)},
        });
        start += c.duration();
    }
    return list;
}

QVariantList Backend::captionLines() const {
    // Lines carry each word's index into the transcript, so QML can edit it.
    QVariantList list;
    int index = 0;
    for (const CaptionLine &line : groupLines(m_project.words)) {
        QVariantList words;
        for (const Word &w : line.words)
            words.append(QVariantMap{{"text", w.text}, {"start", w.start}, {"end", w.end}, {"index", index++}});
        list.append(QVariantMap{{"start", line.start}, {"end", line.end}, {"words", words}});
    }
    return list;
}

QVariantList Backend::overlays() const {
    QVariantList list;
    for (const Overlay &o : m_project.overlays) {
        list.append(QVariantMap{{"text", o.text}, {"start", o.start}, {"end", o.end}, {"y", o.y},
                                {"size", o.size}, {"animation", o.animation}});
    }
    return list;
}

QVariantMap Backend::music() const {
    const Music &m = m_project.music;
    return {{"set", m.isSet()}, {"path", m.path}, {"name", fileName(m.path)}, {"volume", m.volume},
            {"offset", m.offset}, {"duck", m.duck}};
}

QVariantMap Backend::look() const {
    const Look &l = m_project.look;
    return {{"exposure", l.exposure}, {"contrast", l.contrast}, {"saturation", l.saturation},
            {"temperature", l.temperature}, {"lut", l.lut}, {"lutName", fileName(l.lut)}};
}

QString Backend::captionFont() const {
    return ::captionFont();
}

double Backend::captionScale() const {
    QFont font(::captionFont());
    font.setBold(true);
    font.setPixelSize(1000);
    const QFontMetricsF metrics(font);
    const double height = metrics.ascent() + metrics.descent();
    return height > 0 ? 1000.0 / height : 1.0;
}

QString Backend::title() const {
    return m_path.isEmpty() ? tr("Untitled reel") : QFileInfo(m_path).completeBaseName();
}

QString Backend::whisperState() const {
    if (QStandardPaths::findExecutable("whisper-cli").isEmpty())
        return "no-whisper";
    if (!QFileInfo::exists(modelPath()))
        return "no-model";
    return "ready";
}

// ---- Undo ----

void Backend::checkpoint() {
    m_undo.append(m_project.toJson());
    if (m_undo.size() > kUndoLimit)
        m_undo.removeFirst();
    m_redo.clear();
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_project.toJson());
    restore(m_undo.takeLast());
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_project.toJson());
    restore(m_redo.takeLast());
}

void Backend::restore(const QJsonObject &json) {
    m_project = Project::fromJson(json);
    changed();
}

void Backend::changed() {
    m_dirty = true;
    emit projectChanged();
}

// ---- Clips ----

void Backend::addMedia() {
    if (m_picker)
        m_picker->open(FilePicker::Media);
}

void Backend::addUrls(const QList<QUrl> &urls) {
    QStringList paths;
    for (const QUrl &url : urls)
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    if (paths.isEmpty())
        return;

    // Probing runs ffprobe per file, so it stays off the UI thread.
    ++m_probing;
    setBusy(tr("Reading clips…"));
    auto *watcher = new QFutureWatcher<QList<media::VideoInfo>>(this);
    connect(watcher, &QFutureWatcher<QList<media::VideoInfo>>::finished, this, [this, watcher] {
        const QList<media::VideoInfo> infos = watcher->result();
        watcher->deleteLater();
        if (--m_probing == 0)
            clearBusy();

        QStringList failures;
        bool added = false;
        for (const media::VideoInfo &info : infos) {
            if (!info.ok) {
                failures.append(QString("%1: %2").arg(fileName(info.path), info.error));
                continue;
            }
            if (!added)
                checkpoint();
            added = true;
            Clip clip;
            clip.path = info.path;
            clip.in = 0.0;
            clip.out = info.duration;
            clip.sourceDuration = info.duration;
            clip.width = info.width;
            clip.height = info.height;
            clip.audio = info.audio;
            thumbKey(info.path);
            m_project.clips.append(clip);
        }
        if (added)
            changed();
        if (!failures.isEmpty())
            setMessage(tr("Some files could not be added:\n%1").arg(failures.join('\n')), true);
    });
    watcher->setFuture(QtConcurrent::run([paths] {
        QList<media::VideoInfo> infos;
        for (const QString &path : paths)
            infos.append(media::probe(path));
        return infos;
    }));
}

int Backend::thumbKey(const QString &path) {
    auto it = m_thumbKeys.find(path);
    if (it != m_thumbKeys.end())
        return *it;
    const int key = m_thumbKeys.size() + 1;
    m_thumbKeys.insert(path, key);
    if (m_thumbs)
        m_thumbs->setVideo(key, path);
    return key;
}

void Backend::removeClip(int index) {
    if (index < 0 || index >= m_project.clips.size())
        return;
    checkpoint();
    m_project.clips.remove(index);
    changed();
}

void Backend::moveClip(int from, int to) {
    const int n = m_project.clips.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    checkpoint();
    m_project.clips.move(from, to);
    changed();
}

bool Backend::split(double time) {
    Project next = m_project;
    if (!next.split(time))
        return false;
    checkpoint();
    m_project = next;
    changed();
    return true;
}

void Backend::setClipValue(int index, const QString &key, double value) {
    if (index < 0 || index >= m_project.clips.size())
        return;
    Clip &c = m_project.clips[index];
    // Never shorter than a frame, never past the source.
    const double frame = 1.0 / Project::Fps;
    if (key == "in")
        c.in = qBound(0.0, value, c.out - frame);
    else if (key == "out")
        c.out = qBound(c.in + frame, value, c.sourceDuration > 0 ? c.sourceDuration : value);
    else if (key == "speed")
        c.speed = qBound(0.25, value, 4.0);
    else if (key == "zoom")
        c.zoom = qBound(1.0, value, 3.0);
    else if (key == "panX")
        c.panX = qBound(-1.0, value, 1.0);
    else if (key == "panY")
        c.panY = qBound(-1.0, value, 1.0);
    else
        return;
    changed();
}

QVariantMap Backend::locate(double time) const {
    const Project::Position at = m_project.locate(time);
    return {{"index", at.index}, {"source", at.source}};
}

void Backend::cutSilences(int index) {
    if (busy() || index < 0 || index >= m_project.clips.size())
        return;
    const Clip clip = m_project.clips[index];
    if (!clip.audio) {
        setMessage(tr("This clip has no sound to find silences in."), true);
        return;
    }
    setBusy(tr("Finding silences…"));
    m_then = [this, index, clip](bool ok, const QByteArray &, const QByteArray &errors) {
        clearBusy();
        if (!ok) {
            if (!m_job.cancelled())
                setMessage(tr("Finding silences failed:\n%1").arg(tail(errors)), true);
            return;
        }
        // With -ss before the input, times in the log start at the clip's in point.
        QVector<Range> silences = parseSilences(errors);
        for (Range &s : silences) {
            s.first += clip.in;
            s.second += clip.in;
        }
        const QVector<Range> keep = keepRanges(clip.in, clip.out, silences);
        if (silences.isEmpty() || keep.isEmpty() || (keep.size() == 1 && keep[0] == Range(clip.in, clip.out))) {
            setMessage(tr("No silences long enough to cut."), false);
            return;
        }
        // The clip may have moved while ffmpeg ran; only cut it if it's unchanged.
        if (index >= m_project.clips.size() || m_project.clips[index].path != clip.path
            || m_project.clips[index].in != clip.in || m_project.clips[index].out != clip.out)
            return;
        checkpoint();
        const double before = m_project.duration();
        m_project.replaceClipWithRanges(index, keep);
        changed();
        setMessage(tr("Cut %1 s of silence.").arg(QLocale().toString(before - m_project.duration(), 'f', 1)), false);
    };
    m_job.start("ffmpeg", {"-hide_banner", "-nostdin", "-ss", num(clip.in), "-to", num(clip.out), "-i", clip.path,
                           "-vn", "-af", "silencedetect=n=-35dB:d=0.4", "-f", "null", "-"});
}

// ---- Captions ----

void Backend::transcribe() {
    if (busy())
        return;
    if (m_project.clips.isEmpty()) {
        setMessage(tr("Add a clip first."), true);
        return;
    }
    const QString state = whisperState();
    if (state == "no-whisper") {
        setMessage(tr("Captions need whisper.cpp. Install it with:\nomarchy-pkg-add whisper-cpp"), true);
        return;
    }
    if (state == "no-model") {
        downloadModel();
        return;
    }
    startTranscription();
}

void Backend::downloadModel() {
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QDir().mkpath(QFileInfo(modelPath()).absolutePath());
    auto *part = new QFile(modelPath() + ".part", this);
    if (!part->open(QIODevice::WriteOnly)) {
        setMessage(tr("Could not write to %1.").arg(part->fileName()), true);
        part->deleteLater();
        return;
    }
    setBusy(tr("Downloading the speech model (once)…"), 0.0);
    QNetworkRequest request{QUrl(kModelUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_download = m_network->get(request);
    connect(m_download, &QNetworkReply::readyRead, this, [this, part] { part->write(m_download->readAll()); });
    connect(m_download, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_progress = total > 0 ? double(got) / total : -1.0;
        emit jobChanged();
    });
    connect(m_download, &QNetworkReply::finished, this, [this, part] {
        part->write(m_download->readAll());
        part->close();
        const bool ok = m_download->error() == QNetworkReply::NoError;
        const QString error = m_download->errorString();
        m_download->deleteLater();
        m_download = nullptr;
        clearBusy();
        if (!ok) {
            part->remove();
            part->deleteLater();
            if (!error.contains("cancel", Qt::CaseInsensitive))
                setMessage(tr("Downloading the speech model failed: %1").arg(error), true);
            return;
        }
        QFile::remove(modelPath());
        part->rename(modelPath());
        part->deleteLater();
        emit jobChanged();
        startTranscription();
    });
}

void Backend::startTranscription() {
    m_work = std::make_unique<QTemporaryDir>();
    const QString wav = m_work->filePath("audio.wav");
    const Project snapshot = m_project;
    setBusy(tr("Preparing audio…"), 0.0);
    m_then = [this, wav, snapshot](bool ok, const QByteArray &, const QByteArray &errors) {
        if (!ok) {
            clearBusy();
            if (!m_job.cancelled())
                setMessage(tr("Preparing audio failed:\n%1").arg(tail(errors)), true);
            return;
        }
        setBusy(tr("Transcribing…"), 0.0);
        const QString base = m_work->filePath("words");
        m_then = [this, base, snapshot](bool ok, const QByteArray &, const QByteArray &errors) {
            clearBusy();
            QFile json(base + ".json");
            if (!ok || !json.open(QIODevice::ReadOnly)) {
                if (!m_job.cancelled())
                    setMessage(tr("Transcription failed:\n%1").arg(tail(errors)), true);
                return;
            }
            const QVector<Word> words = parseWhisperJson(json.readAll());
            if (words.isEmpty()) {
                setMessage(tr("No speech was found in the clips."), false);
                return;
            }
            // Timings are for the edit that was transcribed; a changed edit
            // still gets them, but says so.
            checkpoint();
            m_project.words = words;
            changed();
            const bool stale = snapshot.toJson().value("clips") != m_project.toJson().value("clips");
            setMessage(stale ? tr("Captions added. The clips changed while transcribing, so check the timing.")
                             : tr("Captions added. Click a word to fix it."), false);
        };
        const int threads = qBound(1, QThread::idealThreadCount() - 1, 8);
        m_job.start("whisper-cli", {"-m", modelPath(), "-f", m_work->filePath("audio.wav"), "-l", "auto",
                                    "-ml", "1", "-sow", "-oj", "-of", base, "-np", "-pp",
                                    "-t", QString::number(threads)});
    };
    m_job.start("ffmpeg", timelineAudioArgs(snapshot, wav), snapshot.duration());
}

void Backend::setWordText(int index, const QString &text) {
    if (index < 0 || index >= m_project.words.size())
        return;
    const QString trimmed = text.simplified();
    if (trimmed.isEmpty())
        m_project.words.remove(index);
    else
        m_project.words[index].text = trimmed;
    changed();
}

void Backend::clearCaptions() {
    if (m_project.words.isEmpty())
        return;
    checkpoint();
    m_project.words.clear();
    changed();
}

void Backend::setCaptionStyle(const QString &style) {
    if (style == m_project.captionStyle)
        return;
    checkpoint();
    m_project.captionStyle = style;
    changed();
}

void Backend::setCaptionY(double y) {
    m_project.captionY = qBound(0.1, y, 0.9);
    changed();
}

// ---- Overlays ----

int Backend::addOverlay(double time) {
    checkpoint();
    Overlay overlay;
    overlay.text = tr("Your hook here");
    overlay.start = qMax(0.0, time);
    overlay.end = qMin(overlay.start + 3.0, qMax(m_project.duration(), overlay.start + 1.0));
    m_project.overlays.append(overlay);
    changed();
    return m_project.overlays.size() - 1;
}

void Backend::setOverlayValue(int index, const QString &key, const QVariant &value) {
    if (index < 0 || index >= m_project.overlays.size())
        return;
    Overlay &o = m_project.overlays[index];
    if (key == "text")
        o.text = value.toString();
    else if (key == "start")
        o.start = qBound(0.0, value.toDouble(), o.end - 0.1);
    else if (key == "end")
        o.end = qMax(o.start + 0.1, value.toDouble());
    else if (key == "y")
        o.y = qBound(0.05, value.toDouble(), 0.95);
    else if (key == "size")
        o.size = qBound(40, value.toInt(), 200);
    else if (key == "animation")
        o.animation = value.toString();
    else
        return;
    changed();
}

void Backend::removeOverlay(int index) {
    if (index < 0 || index >= m_project.overlays.size())
        return;
    checkpoint();
    m_project.overlays.remove(index);
    changed();
}

// ---- Music and look ----

void Backend::chooseMusic() {
    if (m_picker)
        m_picker->open(FilePicker::Music);
}

void Backend::removeMusic() {
    if (!m_project.music.isSet())
        return;
    checkpoint();
    m_project.music = {};
    changed();
}

void Backend::setMusicValue(const QString &key, const QVariant &value) {
    Music &m = m_project.music;
    if (key == "volume")
        m.volume = qBound(0.0, value.toDouble(), 1.5);
    else if (key == "offset")
        m.offset = qMax(0.0, value.toDouble());
    else if (key == "duck")
        m.duck = value.toBool();
    else
        return;
    changed();
}

void Backend::setLookValue(const QString &key, double value) {
    Look &l = m_project.look;
    const double v = qBound(-1.0, value, 1.0);
    if (key == "exposure")
        l.exposure = v;
    else if (key == "contrast")
        l.contrast = v;
    else if (key == "saturation")
        l.saturation = v;
    else if (key == "temperature")
        l.temperature = v;
    else
        return;
    changed();
}

void Backend::chooseLut() {
    if (m_picker)
        m_picker->open(FilePicker::Lut);
}

void Backend::clearLut() {
    if (m_project.look.lut.isEmpty())
        return;
    checkpoint();
    m_project.look.lut.clear();
    changed();
}

void Backend::setPlatform(const QString &platform) {
    if (platform == m_project.platform)
        return;
    checkpoint();
    m_project.platform = platform;
    changed();
}

// ---- Files ----

void Backend::picked(int kind, const QList<QUrl> &urls) {
    const QString path = urls.first().toLocalFile();
    switch (kind) {
    case FilePicker::Media:
        addUrls(urls);
        break;
    case FilePicker::Music:
        checkpoint();
        m_project.music.path = path;
        changed();
        break;
    case FilePicker::Lut:
        checkpoint();
        m_project.look.lut = path;
        changed();
        break;
    case FilePicker::OpenProject:
        openFile(path);
        break;
    case FilePicker::SaveProject:
        writeProject(path.endsWith(".reel") ? path : path + ".reel");
        break;
    case FilePicker::Export: {
        const QString output = path.endsWith(".mp4", Qt::CaseInsensitive) ? path : path + ".mp4";
        runExport(output, "h264_nvenc");
        break;
    }
    }
}

void Backend::newProject() {
    m_project = {};
    m_path.clear();
    m_undo.clear();
    m_redo.clear();
    m_dirty = false;
    emit projectChanged();
}

void Backend::open() {
    if (m_picker)
        m_picker->open(FilePicker::OpenProject);
}

bool Backend::openFile(const QString &path) {
    QFile file(path);
    QJsonParseError parseError;
    const QJsonDocument doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll(), &parseError)
                                                             : QJsonDocument();
    if (!doc.isObject()) {
        setMessage(tr("%1 is not a Reelcut project.").arg(fileName(path)), true);
        return false;
    }
    m_project = Project::fromJson(doc.object(), QFileInfo(path).absolutePath());
    m_path = QFileInfo(path).absoluteFilePath();
    m_undo.clear();
    m_redo.clear();
    m_dirty = false;
    for (const Clip &clip : m_project.clips)
        thumbKey(clip.path);
    emit projectChanged();
    return true;
}

void Backend::save() {
    if (m_path.isEmpty())
        saveAs();
    else
        writeProject(m_path);
}

void Backend::saveAs() {
    if (!m_picker)
        return;
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    m_picker->save(FilePicker::SaveProject,
                   QUrl::fromLocalFile(QDir(QDir(folder).exists() ? folder : QDir::homePath()).filePath(title() + ".reel")));
}

bool Backend::writeProject(const QString &path) {
    // Written beside the target and renamed over it, so a crash never leaves half a project.
    QSaveFile file(path);
    const QByteArray data = QJsonDocument(m_project.toJson(QFileInfo(path).absolutePath())).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        setMessage(tr("Could not save %1.").arg(path), true);
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    m_dirty = false;
    emit projectChanged();
    setMessage(tr("Saved %1.").arg(fileName(path)), false);
    return true;
}

void Backend::exportVideo() {
    if (busy() || !m_picker)
        return;
    if (m_project.clips.isEmpty()) {
        setMessage(tr("Add a clip first."), true);
        return;
    }
    for (const Clip &clip : m_project.clips) {
        if (!QFileInfo::exists(clip.path)) {
            setMessage(tr("%1 is missing, so the reel can't be exported.").arg(clip.path), true);
            return;
        }
    }
    QString folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (!QDir(folder).exists())
        folder = QDir::homePath();
    const QString suffix = m_project.platform == "tiktok" ? "-tiktok" : "-reel";
    m_picker->save(FilePicker::Export, QUrl::fromLocalFile(freePath(folder, title() + suffix, ".mp4")));
}

void Backend::runExport(const QString &output, const QString &encoder) {
    m_work = std::make_unique<QTemporaryDir>();
    QString error;
    const ExportOptions prepared =
        prepareExport(m_project, m_theme ? m_theme->accent() : "#FFD60A", m_work->path(), output, &error);
    if (!error.isEmpty()) {
        setMessage(error, true);
        return;
    }
    ExportOptions options = prepared;
    options.encoder = encoder;
    setBusy(tr("Exporting…"), 0.0);
    m_then = [this, output, encoder](bool ok, const QByteArray &, const QByteArray &errors) {
        clearBusy();
        if (ok) {
            m_lastExport = output;
            setMessage(tr("Exported %1.").arg(fileName(output)), false);
            return;
        }
        QFile::remove(output);
        if (m_job.cancelled())
            return;
        // Not every NVIDIA setup can encode (drivers, sessions in use); the CPU always can.
        if (encoder == "h264_nvenc") {
            runExport(output, "libx264");
            return;
        }
        setMessage(tr("Export failed:\n%1").arg(tail(errors)), true);
    };
    m_job.start("ffmpeg", exportArgs(m_project, options), m_project.duration(), m_work->path());
}

void Backend::cancelJob() {
    if (m_download) {
        m_download->abort();
        return;
    }
    m_job.cancel();
}

void Backend::showExport() {
    if (!m_lastExport.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastExport).absolutePath()));
}

void Backend::dismissMessage() {
    setMessage({}, false);
}

void Backend::setBusy(const QString &text, double progress) {
    m_busyText = text;
    m_progress = progress;
    emit jobChanged();
}

void Backend::clearBusy() {
    m_busyText.clear();
    m_progress = -1.0;
    emit jobChanged();
}

void Backend::setMessage(const QString &text, bool error) {
    m_message = text;
    m_messageIsError = error;
    emit messageChanged();
}
