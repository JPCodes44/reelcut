#include "backend.h"

#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QFutureWatcher>
#include <QImage>
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
#include "bodyeffects.h"
#include "effects.h"
#include "filters.h"
#include "segment.h"
#include "transitions.h"
#include "mcpserver.h"
#include "media.h"
#include "silence.h"
#include "theme.h"
#include "thumbprovider.h"
#include "transcript.h"

namespace {
constexpr int kUndoLimit = 200;
QString modelPath() {
    return whisperModelPath();
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

Backend::Backend(FilePicker *picker, ThumbProvider *thumbs, Theme *theme, QObject *parent, const QString &stateDir)
    : QObject(parent), m_picker(picker), m_thumbs(thumbs), m_theme(theme), m_stateDir(stateDir) {
    m_reloadDebounce = new QTimer(this);
    m_reloadDebounce->setSingleShot(true);
    m_reloadDebounce->setInterval(120);
    connect(m_reloadDebounce, &QTimer::timeout, this, &Backend::reloadIfChangedOutside);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, m_reloadDebounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, m_reloadDebounce, qOverload<>(&QTimer::start));
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
    const QVector<double> starts = m_project.clipStarts();
    for (int i = 0; i < m_project.clips.size(); ++i) {
        const Clip &c = m_project.clips[i];
        const double start = starts[i];
        const Transitions::Kind *kind = Transitions::find(c.transition);
        list.append(QVariantMap{
            {"path", c.path}, {"name", fileName(c.path)}, {"in", c.in}, {"out", c.out},
            {"speed", c.speed}, {"zoom", c.zoom}, {"panX", c.panX}, {"panY", c.panY},
            {"width", c.width}, {"height", c.height}, {"audio", c.audio},
            {"sourceDuration", c.sourceDuration}, {"start", start}, {"duration", c.duration()},
            {"thumb", m_thumbKeys.value(c.path, -1)}, {"offline", !QFileInfo::exists(c.path)},
            {"still", c.still}, {"motion", c.motion},
            // The transition into the next clip; overlap is its effective length.
            {"transition", c.transition}, {"transitionName", kind ? kind->label : QString()},
            {"transitionSeconds", c.transitionSeconds}, {"overlap", m_project.overlap(i)},
            {"transitionMode", kind ? kind->previewMode : -1},
            // The body effect, and its person mask once made (for the preview).
            {"body", c.body}, {"bodyAmount", c.bodyAmount},
            {"bodyMode", Body::find(c.body) ? Body::find(c.body)->previewMode : -1},
            {"mask", !c.body.isEmpty() && Segment::hasMask(c) ? QUrl::fromLocalFile(Segment::maskPath(c)).toString() : QString()},
            {"grade", c.grade.toMap()},
        });
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
        list.append(QVariantMap{{"text", o.text}, {"start", o.start}, {"end", o.end}, {"x", o.x}, {"y", o.y},
                                {"size", o.size}, {"animation", o.animation}, {"style", o.style},
                                {"bulge", o.bulge}, {"fill", o.fill}, {"outline", o.outline}, {"shadow", o.shadow},
                                {"box", o.box}, {"fillColor", o.colorOf("fill")}, {"outlineColor", o.colorOf("outline")},
                                {"shadowColor", o.colorOf("shadow")}, {"boxColor", o.colorOf("box")},
                                {"font", o.font}, {"bold", o.bold}, {"italic", o.italic},
                                {"outlineWidth", o.outlineWidth}, {"shadowDistance", o.shadowDistance},
                                {"letterSpacing", o.letterSpacing}, {"fontFamily", o.fontOf()},
                                {"isBold", o.boldOf()}, {"outlineWidthValue", o.outlineWidthOf()},
                                {"shadowDistanceValue", o.shadowDistanceOf()}});
    }
    return list;
}

QVariantList Backend::bodyKinds() const {
    QVariantList list;
    for (const Body::Kind &k : Body::kinds())
        list << QVariantMap{{"id", k.id}, {"label", k.label}, {"mode", k.previewMode}};
    return list;
}

QVariantList Backend::effectKinds() const {
    QVariantList list;
    for (const Effects::Kind &k : Effects::kinds())
        list << QVariantMap{{"id", k.id}, {"label", k.label}, {"mode", k.previewMode}, {"usesSpeed", k.usesSpeed}};
    return list;
}

QVariantList Backend::effects() const {
    QVariantList list;
    for (const Effect &e : m_project.effects) {
        const Effects::Kind *kind = Effects::find(e.kind);
        list << QVariantMap{{"kind", e.kind}, {"label", kind ? kind->label : e.kind}, {"start", e.start},
                            {"end", e.end}, {"intensity", e.intensity}, {"speed", e.speed},
                            {"mode", kind ? kind->previewMode : -1}, {"usesSpeed", kind && kind->usesSpeed}};
    }
    return list;
}

QVariantMap Backend::music() const {
    const Music &m = m_project.music;
    return {{"set", m.isSet()}, {"path", m.path}, {"name", fileName(m.path)}, {"volume", m.volume},
            {"offset", m.offset}, {"duck", m.duck}};
}

namespace {
// The preview shader's strip LUT for a look, written once to the cache.
QString filterStrip(const Filters::Look &look) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/filters";
    const QString path = dir + "/" + look.id + ".png";
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(dir);
        if (!Filters::stripImage(look).save(path))
            return {};
    }
    return QUrl::fromLocalFile(path).toString();
}
}  // namespace

QVariantMap Backend::look() const {
    const Look &l = m_project.look;
    const Filters::Look *filter = Filters::find(l.filter);
    return {{"exposure", l.exposure}, {"contrast", l.contrast}, {"saturation", l.saturation},
            {"temperature", l.temperature}, {"lut", l.lut}, {"lutName", fileName(l.lut)},
            {"filter", l.filter}, {"filterName", filter ? filter->label : QString()},
            {"filterAmount", l.filterAmount}, {"filterStrip", filter ? filterStrip(*filter) : QString()},
            {"grade", l.grade.toMap()}};
}

QVariantList Backend::transitions() const {
    QVariantList list;
    for (const Transitions::Kind &k : Transitions::kinds())
        list << QVariantMap{{"id", k.id}, {"label", k.label}, {"mode", k.previewMode}};
    return list;
}

QVariantList Backend::filters() const {
    QVariantList list;
    for (const Filters::Look &look : Filters::looks()) {
        QVariantList colors;
        for (const QColor &c : Filters::swatch(look))
            colors << c;
        list << QVariantMap{{"id", look.id}, {"label", look.label}, {"swatch", colors}};
    }
    return list;
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
            const Clip clip = media::clipFor(info);
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
    if (m_project.setClipValue(index, key, value))
        changed();
}

void Backend::setClipMotion(int index, const QString &motion) {
    if (index < 0 || index >= m_project.clips.size() || m_project.clips[index].motion == motion)
        return;
    checkpoint();
    if (m_project.setClipMotion(index, motion))
        changed();
}

void Backend::setClipTransition(int index, const QString &id) {
    if (index < 0 || index >= m_project.clips.size() || m_project.clips[index].transition == id ||
        (!id.isEmpty() && !Transitions::find(id)))
        return;
    checkpoint();
    m_project.setClipTransition(index, id);
    changed();
}

void Backend::setGrade(int index, const QString &key, double value) {
    // Undo steps come from the wheels' and sliders' presses (checkpoint()),
    // so a drag is one step; resets make their own.
    if (key.startsWith("reset"))
        checkpoint();
    if (m_project.setGrade(index, key, value))
        changed();
}

QString Backend::pixelColor(const QVariant &image, double x, double y) const {
    const QImage img = image.value<QImage>();
    if (img.isNull())
        return {};
    const int px = qBound(0, qRound(x * (img.width() - 1)), img.width() - 1);
    const int py = qBound(0, qRound(y * (img.height() - 1)), img.height() - 1);
    return QColor(img.pixel(px, py)).name(QColor::HexArgb);
}

void Backend::setClipBody(int index, const QString &id) {
    if (index < 0 || index >= m_project.clips.size() || m_project.clips[index].body == id ||
        (!id.isEmpty() && !Body::find(id)))
        return;
    checkpoint();
    m_project.setClipBody(index, id);
    changed();
    if (!id.isEmpty() && !busy())
        findPeople();
}

void Backend::findPeople(std::function<void()> then) {
    bool missing = false;
    for (const Clip &clip : m_project.clips)
        missing = missing || (Body::find(clip.body) && !Segment::hasMask(clip));
    if (!missing) {
        if (then)
            then();
        return;
    }
    const Project project = m_project;
    m_stopMasks = std::make_shared<std::atomic_bool>(false);
    auto stop = m_stopMasks;
    setBusy(QFileInfo::exists(Segment::modelPath()) ? tr("Finding the person…") : tr("Downloading the person model (once)…"), 0.0);
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, stop, then] {
        const QString error = watcher->result();
        watcher->deleteLater();
        clearBusy();
        emit projectChanged();  // the clips now carry their masks
        if (*stop)
            return;
        if (!error.isEmpty()) {
            setMessage(error, true);
            return;
        }
        if (then)
            then();
    });
    watcher->setFuture(QtConcurrent::run([this, project, stop] {
        QString error;
        Segment::ensureMasks(project, [this, stop](double f) {
            QMetaObject::invokeMethod(this, [this, f] { setBusy(tr("Finding the person…"), f); }, Qt::QueuedConnection);
            return !*stop;
        }, &error);
        return *stop ? QString() : error;
    }));
}

void Backend::exportPhotos() {
    if (busy())
        return;
    if (m_project.clips.isEmpty()) {
        setMessage(tr("Add a clip first."), true);
        return;
    }
    QString parent = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (!QDir(parent).exists())
        parent = QDir::homePath();
    const QString folder = freePath(parent, title() + "-photos", QString());
    const Project project = m_project;
    const QString accent = m_theme ? m_theme->accent() : "#FFD60A";
    setBusy(tr("Rendering photos…"));
    auto *watcher = new QFutureWatcher<QPair<QStringList, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<QStringList, QString>>::finished, this, [this, watcher, folder] {
        const auto result = watcher->result();
        watcher->deleteLater();
        clearBusy();
        if (result.first.isEmpty()) {
            setMessage(result.second, true);
            return;
        }
        m_lastExport = result.first.first();
        setMessage(tr("Saved %n photos to %1.", "", result.first.size()).arg(QFileInfo(folder).fileName()), false);
    });
    watcher->setFuture(QtConcurrent::run([project, accent, folder] {
        QString error;
        const QStringList files = exportPhotoSet(project, accent, folder, &error);
        return qMakePair(files, error);
    }));
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
        bool found = false;
        const QVector<Range> keep = clipKeepRanges(clip, errors, &found);
        if (!found || keep.isEmpty() || (keep.size() == 1 && keep[0] == Range(clip.in, clip.out))) {
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
    m_job.start("ffmpeg", silenceArgs(clip));
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
    QNetworkRequest request{QUrl(whisperModelUrl())};
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
        m_job.start("whisper-cli", whisperArgs(m_work->filePath("audio.wav"), base));
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
    setCaptionValue("y", y);
}

void Backend::setCaptionValue(const QString &key, double value) {
    if (m_project.setCaptionValue(key, value))
        changed();
}

// ---- Overlays ----

int Backend::addOverlay(double time) {
    checkpoint();
    const int index = m_project.addOverlay(tr("Your hook here"), time);
    changed();
    return index;
}

int Backend::addFisheyeText(double time) {
    checkpoint();
    const int index = m_project.addOverlay(tr("Your text here"), time);
    Overlay &o = m_project.overlays[index];
    o.style = "fisheye";
    o.animation = "none";
    o.size = 92;
    o.y = 0.33;
    changed();
    return index;
}

void Backend::setOverlayValue(int index, const QString &key, const QVariant &value) {
    if (m_project.setOverlayValue(index, key, value))
        changed();
}

void Backend::removeOverlay(int index) {
    if (index < 0 || index >= m_project.overlays.size())
        return;
    checkpoint();
    m_project.overlays.remove(index);
    changed();
}

// ---- Video effects ----

int Backend::addEffect(const QString &kind, double time) {
    if (!Effects::find(kind))
        return -1;
    checkpoint();
    const int index = m_project.addEffect(kind, time);
    changed();
    return index;
}

void Backend::setEffectValue(int index, const QString &key, double value) {
    if (m_project.setEffectValue(index, key, value))
        changed();
}

void Backend::removeEffect(int index) {
    if (index < 0 || index >= m_project.effects.size())
        return;
    checkpoint();
    m_project.effects.remove(index);
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
    if (m_project.setMusicValue(key, value))
        changed();
}

void Backend::setLookValue(const QString &key, double value) {
    if (m_project.setLookValue(key, value))
        changed();
}

void Backend::applyPreset(const QString &name) {
    checkpoint();
    if (m_project.applyPreset(name))
        changed();
}

void Backend::setFilter(const QString &id) {
    if (id == m_project.look.filter || (!id.isEmpty() && !Filters::find(id)))
        return;
    checkpoint();
    m_project.setFilter(id);
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
    m_onDisk.clear();
    watchProject();
    recordOpenProject();
    emit projectChanged();
}

void Backend::open() {
    if (m_picker)
        m_picker->open(FilePicker::OpenProject);
}

bool Backend::openFile(const QString &path) {
    QFile file(path);
    const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
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
    m_onDisk = bytes;
    watchProject();
    recordOpenProject();
    emit projectChanged();
    return true;
}

void Backend::watchProject() {
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (m_path.isEmpty())
        return;
    // Saves replace the file by renaming over it, which drops a file watch,
    // so the folder is watched too and the file re-armed after each change.
    m_watcher.addPath(QFileInfo(m_path).absolutePath());
    if (QFileInfo::exists(m_path))
        m_watcher.addPath(m_path);
}

void Backend::reloadIfChangedOutside() {
    watchProject();
    QFile file(m_path);
    if (m_path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return;
    const QByteArray bytes = file.readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (bytes == m_onDisk || !doc.isObject())
        return;
    // Someone else (usually the MCP server) edited the project. Take their
    // version, with ours one undo away, so nothing is lost either way.
    checkpoint();
    m_project = Project::fromJson(doc.object(), QFileInfo(m_path).absolutePath());
    m_onDisk = bytes;
    m_dirty = false;
    for (const Clip &clip : m_project.clips)
        thumbKey(clip.path);
    emit projectChanged();
    setMessage(tr("Updated from outside Reelcut. Ctrl+Z undoes it."), false);
}

void Backend::recordOpenProject() {
    const QString state = McpServer::openProjectFile(m_stateDir);
    QDir().mkpath(QFileInfo(state).absolutePath());
    QSaveFile file(state);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_path.toUtf8());
        file.commit();
    }
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
    m_onDisk = data;
    m_dirty = false;
    watchProject();
    recordOpenProject();
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
    // Body effects need their person masks before ffmpeg can use them.
    for (const Clip &clip : m_project.clips) {
        if (Body::find(clip.body) && !Segment::hasMask(clip)) {
            findPeople([this, output, encoder] { runExport(output, encoder); });
            return;
        }
    }
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
    if (m_stopMasks)
        *m_stopMasks = true;
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
