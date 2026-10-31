#include "jobs.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QFile>
#include <QRegularExpression>

#include "captions.h"
#include "transcript.h"

Job::Job(QObject *parent) : QObject(parent) {
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Job::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        const QByteArray chunk = m_process.readAllStandardError();
        m_errors += chunk;
        static const QRegularExpression percent(R"(progress\s*=\s*(\d+)%)");
        auto match = percent.globalMatch(QString::fromUtf8(chunk));
        double last = -1.0;
        while (match.hasNext())
            last = match.next().captured(1).toDouble() / 100.0;
        if (last >= 0.0)
            emit progress(last);
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readOutput();
        emit finished(!m_cancelled && status == QProcess::NormalExit && code == 0, m_output, m_errors);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit finished(false, {}, tr("Could not start %1.").arg(m_process.program()).toUtf8());
    });
}

Job::~Job() {
    if (isRunning()) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}

void Job::start(const QString &program, const QStringList &args, double expectedSeconds,
                const QString &workingDirectory) {
    m_expected = expectedSeconds;
    m_cancelled = false;
    m_output.clear();
    m_errors.clear();
    m_pending.clear();
    m_process.setWorkingDirectory(workingDirectory);
    m_process.start(program, args);
}

void Job::cancel() {
    if (!isRunning())
        return;
    m_cancelled = true;
    m_process.kill();
}

void Job::readOutput() {
    m_pending += m_process.readAllStandardOutput();
    int newline;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        const QByteArray line = m_pending.left(newline).trimmed();
        m_pending.remove(0, newline + 1);
        m_output += line + '\n';
        if (m_expected > 0.0 && line.startsWith("out_time_us=")) {
            const double seconds = line.mid(12).toDouble() / 1e6;
            emit progress(qBound(0.0, seconds / m_expected, 1.0));
        }
    }
}

ExportOptions prepareExport(const Project &project, const QString &accent, const QString &workDir,
                            const QString &output, QString *error) {
    ExportOptions options;
    options.output = output;
    if (!project.words.isEmpty() || !project.overlays.isEmpty()) {
        QFile ass(QDir(workDir).filePath("captions.ass"));
        if (!ass.open(QIODevice::WriteOnly) || ass.write(assDocument(project, accent).toUtf8()) < 0) {
            *error = QObject::tr("Could not write the captions file.");
            return {};
        }
        options.assFile = "captions.ass";
    }
    if (!project.look.lut.isEmpty()) {
        const QString copy = QDir(workDir).filePath("look.cube");
        QFile::remove(copy);
        if (!QFile::copy(project.look.lut, copy)) {
            *error = QObject::tr("Could not read the LUT %1.").arg(project.look.lut);
            return {};
        }
        options.lutFile = "look.cube";
    }
    return options;
}

QString tail(const QByteArray &errors, int lines) {
    const QStringList all = QString::fromUtf8(errors).trimmed().split('\n');
    return all.mid(qMax(0, all.size() - lines)).join('\n');
}

QStringList silenceArgs(const Clip &clip, double thresholdDb, double minSilence) {
    return {"-hide_banner", "-nostdin", "-ss", num(clip.in), "-to", num(clip.out), "-i", clip.path, "-vn",
            "-af", QString("silencedetect=n=%1dB:d=%2").arg(num(thresholdDb, 1), num(minSilence, 2)), "-f", "null", "-"};
}

QVector<Range> clipKeepRanges(const Clip &clip, const QByteArray &log, bool *foundSilence) {
    // With -ss before the input, times in the log start at the clip's in point.
    QVector<Range> silences = parseSilences(log);
    for (Range &silence : silences) {
        silence.first += clip.in;
        silence.second += clip.in;
    }
    if (foundSilence)
        *foundSilence = !silences.isEmpty();
    return keepRanges(clip.in, clip.out, silences);
}

QString whisperModelPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/reelcut/models/ggml-base.bin";
}

QString whisperModelUrl() {
    return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin";
}

QStringList whisperArgs(const QString &wav, const QString &base) {
    const int threads = qBound(1, QThread::idealThreadCount() - 1, 8);
    return {"-m", whisperModelPath(), "-f", wav, "-l", "auto", "-ml", "1", "-sow", "-oj", "-of", base,
            "-np", "-pp", "-t", QString::number(threads)};
}

ToolResult runTool(const QString &program, const QStringList &args, const QString &workingDirectory, int timeoutMs) {
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.start(program, args);
    ToolResult result;
    if (!process.waitForStarted(10000)) {
        result.errors = QObject::tr("Could not start %1.").arg(program).toUtf8();
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        result.errors = QObject::tr("%1 took too long.").arg(program).toUtf8();
        return result;
    }
    result.output = process.readAllStandardOutput();
    result.errors = process.readAllStandardError();
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return result;
}

bool downloadWhisperModel(QString *error) {
    const QString path = whisperModelPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile part(path + ".part");
    if (!part.open(QIODevice::WriteOnly)) {
        *error = QObject::tr("Could not write to %1.").arg(part.fileName());
        return false;
    }
    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(whisperModelUrl())};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] { part.write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    part.write(reply->readAll());
    part.close();
    const bool ok = reply->error() == QNetworkReply::NoError;
    if (!ok)
        *error = QObject::tr("Downloading the speech model failed: %1").arg(reply->errorString());
    reply->deleteLater();
    if (!ok) {
        part.remove();
        return false;
    }
    QFile::remove(path);
    return part.rename(path);
}

QVector<Word> transcribeProject(const Project &project, QString *error) {
    if (QStandardPaths::findExecutable("whisper-cli").isEmpty()) {
        *error = QObject::tr("Captions need whisper.cpp: omarchy-pkg-add whisper-cpp");
        return {};
    }
    if (!QFileInfo::exists(whisperModelPath()) && !downloadWhisperModel(error))
        return {};
    QTemporaryDir work;
    const QString wav = work.filePath("audio.wav"), base = work.filePath("words");
    const ToolResult audio = runTool("ffmpeg", timelineAudioArgs(project, wav));
    if (!audio.ok) {
        *error = QObject::tr("Preparing audio failed:\n%1").arg(tail(audio.errors));
        return {};
    }
    const ToolResult whisper = runTool("whisper-cli", whisperArgs(wav, base));
    QFile json(base + ".json");
    if (!whisper.ok || !json.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Transcription failed:\n%1").arg(tail(whisper.errors));
        return {};
    }
    const QVector<Word> words = parseWhisperJson(json.readAll());
    if (words.isEmpty())
        *error = QObject::tr("No speech was found in the clips.");
    return words;
}

bool cutSilences(Project &project, int index, double thresholdDb, double minSilence, double *removed, QString *error) {
    if (index < 0 || index >= project.clips.size()) {
        *error = QObject::tr("There is no clip %1.").arg(index);
        return false;
    }
    const Clip clip = project.clips[index];
    *removed = 0.0;
    if (!clip.audio)
        return true;
    const ToolResult result = runTool("ffmpeg", silenceArgs(clip, thresholdDb, minSilence));
    if (!result.ok) {
        *error = QObject::tr("Finding silences failed:\n%1").arg(tail(result.errors));
        return false;
    }
    bool found = false;
    const QVector<Range> keep = clipKeepRanges(clip, result.errors, &found);
    if (!found || keep.isEmpty())
        return true;
    const double before = project.duration();
    project.replaceClipWithRanges(index, keep);
    *removed = before - project.duration();
    return true;
}

bool exportProject(const Project &project, const QString &accent, const QString &output, QString *encoder,
                   QString *error) {
    if (project.clips.isEmpty()) {
        *error = QObject::tr("The project has no clips.");
        return false;
    }
    for (const Clip &clip : project.clips) {
        if (!QFileInfo::exists(clip.path)) {
            *error = QObject::tr("%1 is missing.").arg(clip.path);
            return false;
        }
    }
    QTemporaryDir work;
    ExportOptions options = prepareExport(project, accent, work.path(), output, error);
    if (!error->isEmpty())
        return false;
    QByteArray errors;
    // Not every NVIDIA setup can encode (drivers, sessions in use); the CPU always can.
    for (const QString candidate : {"h264_nvenc", "libx264"}) {
        options.encoder = candidate;
        const ToolResult result = runTool("ffmpeg", exportArgs(project, options), work.path());
        if (result.ok) {
            if (encoder)
                *encoder = candidate;
            return true;
        }
        errors = result.errors;
    }
    QFile::remove(output);
    *error = QObject::tr("Export failed:\n%1").arg(tail(errors, 15));
    return false;
}

QByteArray renderFrame(const Project &project, const QString &accent, double time, int width, QString *error) {
    if (project.clips.isEmpty()) {
        *error = QObject::tr("The project has no clips.");
        return {};
    }
    QTemporaryDir work;
    const ExportOptions options = prepareExport(project, accent, work.path(), {}, error);
    if (!error->isEmpty())
        return {};
    const QString png = work.filePath("frame.png");
    const ToolResult result = runTool("ffmpeg", frameArgs(project, options, time, png, width), work.path(), 120000);
    QFile file(png);
    if (!result.ok || !file.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Rendering the frame failed:\n%1").arg(tail(result.errors));
        return {};
    }
    return file.readAll();
}
