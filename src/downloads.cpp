#include "downloads.h"

#include <QClipboard>
#include <QDateTime>
#include <QDrag>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QPixmap>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {

const QStringList videoSuffixes = {"mp4", "mov", "mkv", "webm", "m4v"};

// The last few lines of yt-dlp's errors, without its "ERROR: [site] id:" prefix noise.
QString lastError(const QByteArray &errors) {
    const QStringList lines = QString::fromUtf8(errors).split('\n', Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i)
        if (lines[i].startsWith("ERROR:"))
            return lines[i].mid(6).trimmed();
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

}  // namespace

Downloads::Downloads(QObject *parent, const QString &folder) : QObject(parent), m_folder(folder) {
    if (m_folder.isEmpty()) {
        QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (videos.isEmpty())
            videos = QDir::homePath() + "/Videos";
        m_folder = videos + "/Reelcut Downloads";
    }
    QDir().mkpath(m_folder);
    m_watcher.addPath(m_folder);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Downloads::rescan);
    rescan();
}

Downloads::~Downloads() {
    for (Running *run : std::as_const(m_running)) {
        run->cancelled = true;
        run->process->disconnect();
        run->process->kill();
        run->process->waitForFinished(2000);
        delete run;
    }
}

bool Downloads::available() const { return !QStandardPaths::findExecutable("yt-dlp").isEmpty(); }

QVariantList Downloads::active() const {
    QVariantList list;
    for (const Running *run : m_running)
        list.append(QVariantMap{{"id", run->id}, {"link", run->link}, {"title", run->title},
                                {"progress", run->progress}});
    return list;
}

QString Downloads::titleFor(const QString &fileName) {
    QString base = QFileInfo(fileName).completeBaseName();
    static const QRegularExpression idSuffix(R"(\s*\[[^\[\]]+\]$)");
    base.remove(idSuffix);
    return base.isEmpty() ? QFileInfo(fileName).completeBaseName() : base;
}

QStringList Downloads::ytDlpArgs(const QString &link, const QString &folder) {
    return {
        "--no-playlist", "--newline", "--color", "never", "--quiet", "--progress",
        "--progress-template", "download:RCPROG %(progress._percent_str)s",
        "--print", "before_dl:RCTITLE %(title)s",
        "--print", "after_move:RCFILE %(filepath)s",
        // H.264 + AAC in MP4 when the site has it: it decodes fastest while editing.
        "-f", "bv*[height<=1920][vcodec^=avc1]+ba[ext=m4a]/bv*[height<=1920]+ba/b[height<=1920]/b",
        "--merge-output-format", "mp4",
        "--write-thumbnail", "--convert-thumbnails", "jpg",
        "-P", folder,
        "-o", "%(title).80B [%(id)s].%(ext)s",
        "--", link,
    };
}

bool Downloads::download(const QString &text) {
    const QString link = text.trimmed();
    const QUrl url = QUrl::fromUserInput(link);
    if (link.isEmpty() || !url.isValid() || (url.scheme() != "http" && url.scheme() != "https")
        || !url.host().contains('.')) {
        emit failed(tr("That doesn't look like a link. Paste a YouTube, Instagram, TikTok or Facebook URL."));
        return false;
    }
    if (!available()) {
        emit failed(tr("Downloading needs yt-dlp. Install it with: sudo pacman -S yt-dlp"));
        return false;
    }

    auto *run = new Running;
    run->id = m_nextId++;
    run->link = url.toString();
    run->process = new QProcess(this);
    m_running.append(run);
    const int id = run->id;

    connect(run->process, &QProcess::readyReadStandardOutput, this, [this, id] {
        if (Running *r = find(id))
            readOutput(*r);
    });
    connect(run->process, &QProcess::readyReadStandardError, this, [this, id] {
        if (Running *r = find(id))
            r->errors += r->process->readAllStandardError();
    });
    connect(run->process, &QProcess::finished, this, [this, id](int code, QProcess::ExitStatus status) {
        Running *r = find(id);
        if (!r)
            return;
        readOutput(*r);
        r->errors += r->process->readAllStandardError();
        m_running.removeOne(r);
        r->process->deleteLater();
        const bool ok = status == QProcess::NormalExit && code == 0 && QFileInfo::exists(r->file);
        if (ok)
            emit finished(r->file, titleFor(r->file));
        else if (!r->cancelled) {
            const QString why = lastError(r->errors);
            QString message = tr("Couldn't download %1").arg(r->link);
            if (!why.isEmpty())
                message += ":\n" + why;
            if (why.contains("login", Qt::CaseInsensitive) || why.contains("cookies", Qt::CaseInsensitive))
                message += "\n" + tr("The site wants you signed in; private posts can't be downloaded.");
            emit failed(message);
        }
        delete r;
        emit activeChanged();
        rescan();
    });
    connect(run->process, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        Running *r = find(id);
        if (!r)
            return;
        m_running.removeOne(r);
        r->process->deleteLater();
        delete r;
        emit activeChanged();
        emit failed(tr("Couldn't start yt-dlp."));
    });

    run->process->start("yt-dlp", ytDlpArgs(run->link, m_folder));
    emit activeChanged();
    return true;
}

void Downloads::readOutput(Running &run) {
    run.pending += run.process->readAllStandardOutput();
    bool changed = false;
    int newline;
    while ((newline = run.pending.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(run.pending.left(newline)).trimmed();
        run.pending.remove(0, newline + 1);
        if (line.startsWith("RCPROG")) {
            bool ok = false;
            const double percent = line.mid(6).trimmed().remove('%').toDouble(&ok);
            if (ok) {
                run.progress = std::clamp(percent / 100.0, 0.0, 1.0);
                changed = true;
            }
        } else if (line.startsWith("RCTITLE ")) {
            run.title = line.mid(8);
            changed = true;
        } else if (line.startsWith("RCFILE ")) {
            run.file = line.mid(7);
        }
    }
    if (changed)
        emit activeChanged();
}

Downloads::Running *Downloads::find(int id) {
    for (Running *run : std::as_const(m_running))
        if (run->id == id)
            return run;
    return nullptr;
}

void Downloads::cancel(int id) {
    Running *run = find(id);
    if (!run)
        return;
    run->cancelled = true;
    run->process->terminate();
    // yt-dlp leaves .part files behind; clear this run's away once it's gone.
    const QString folder = m_folder;
    connect(run->process, &QProcess::finished, this, [this, folder] {
        if (!m_running.isEmpty())
            return;  // another download's parts may be among them
        const QFileInfoList parts = QDir(folder).entryInfoList({"*.part", "*.ytdl", "*.part-Frag*"}, QDir::Files);
        for (const QFileInfo &part : parts)
            if (part.lastModified().secsTo(QDateTime::currentDateTime()) < 24 * 3600)
                QFile::remove(part.absoluteFilePath());
    });
}

void Downloads::remove(const QString &path) {
    const QFileInfo info(path);
    // Only ever delete inside the library folder.
    if (QDir(info.absolutePath()).canonicalPath() != QDir(m_folder).canonicalPath())
        return;
    QFile::remove(info.absoluteFilePath());
    QFile::remove(info.absolutePath() + "/" + info.completeBaseName() + ".jpg");
    rescan();
}

QMimeData *Downloads::mimeFor(const QString &path) {
    const QUrl url = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
    auto *mime = new QMimeData;
    mime->setUrls({url});                // text/uri-list: browsers, Electron apps, file managers
    mime->setText(url.toLocalFile());    // text/plain: terminals and text fields get the path
    // File managers' own format, so pasting in Nautilus/Dolphin copies the file.
    mime->setData("x-special/gnome-copied-files", "copy\n" + url.toEncoded());
    return mime;
}

bool Downloads::startDrag(const QString &path, const QUrl &thumb) {
    if (!QFileInfo::exists(path))
        return false;
    auto *drag = new QDrag(this);
    drag->setMimeData(mimeFor(path));
    const QImage image(thumb.toLocalFile());
    if (!image.isNull()) {
        const QPixmap pixmap = QPixmap::fromImage(image.scaled(96, 170, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
    }
    // Offering move too keeps apps that only ask for "move" happy; the file is never deleted.
    // Qt owns the QDrag once exec() runs: it deletes it itself, and on Wayland
    // only once the receiving app has finished reading the data. Deleting it
    // here as well could free the mime data mid-transfer.
    // exec() spins its own event loop; a rescan in it would rebuild the
    // library's tiles, destroying the one whose DragHandler started the drag.
    m_dragging = true;
    const Qt::DropAction action = drag->exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::CopyAction);
    m_dragging = false;
    if (m_rescanPending)
        QTimer::singleShot(0, this, &Downloads::rescan);
    return action != Qt::IgnoreAction;
}

void Downloads::copyFile(const QString &path) {
    if (QFileInfo::exists(path))
        QGuiApplication::clipboard()->setMimeData(mimeFor(path));
}

void Downloads::copyPath(const QString &path) {
    QGuiApplication::clipboard()->setText(QFileInfo(path).absoluteFilePath());
}

void Downloads::showFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder)); }

void Downloads::rescan() {
    if (m_dragging) {
        m_rescanPending = true;
        return;
    }
    m_rescanPending = false;
    QDir dir(m_folder);
    QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time);
    QVariantList items;
    for (const QFileInfo &file : std::as_const(files)) {
        if (!videoSuffixes.contains(file.suffix().toLower()))
            continue;
        // A merge in progress writes "x.temp.mp4" or "x.f137.mp4"; those aren't done yet.
        static const QRegularExpression partial(R"(\.(temp|f\d+)$)");
        if (partial.match(file.completeBaseName()).hasMatch())
            continue;
        const QString thumb = file.absolutePath() + "/" + file.completeBaseName() + ".jpg";
        items.append(QVariantMap{
            {"path", file.absoluteFilePath()},
            {"url", QUrl::fromLocalFile(file.absoluteFilePath())},
            {"title", titleFor(file.fileName())},
            {"thumb", QFileInfo::exists(thumb) ? QUrl::fromLocalFile(thumb) : QUrl()},
            {"size", file.size()},
        });
    }
    if (items != m_items) {
        m_items = items;
        emit itemsChanged();
    }
}
