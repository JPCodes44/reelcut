#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

#include "exportgraph.h"
#include "project.h"
#include "silence.h"

// One command-line tool run in the background. Progress is read from
// ffmpeg's `-progress pipe:1` (out_time_us against a known length) or from
// whisper-cli's "progress = N%" lines.
class Job : public QObject {
    Q_OBJECT

public:
    explicit Job(QObject *parent = nullptr);
    ~Job() override;

    void start(const QString &program, const QStringList &args, double expectedSeconds = 0.0,
               const QString &workingDirectory = {});
    void cancel();
    bool cancelled() const { return m_cancelled; }
    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }

signals:
    void progress(double fraction);
    void finished(bool ok, const QByteArray &output, const QByteArray &errors);

private:
    void readOutput();

    QProcess m_process;
    double m_expected = 0.0;
    bool m_cancelled = false;
    QByteArray m_output;
    QByteArray m_errors;
    QByteArray m_pending;
};

// Writes what an export needs next to it in workDir (the captions .ass and a
// copy of the LUT under plain names) and returns options pointing at them.
// Paths in the options are relative to workDir, which must be the working
// directory ffmpeg runs in.
ExportOptions prepareExport(const Project &project, const QString &accent, const QString &workDir,
                            const QString &output, QString *error);

// The last lines of a tool's error output, for showing what went wrong.
QString tail(const QByteArray &errors, int lines = 40);

// ---- Shared steps, for the window, the CLI and the MCP server ----

// ffmpeg arguments that report a clip's silences on stderr.
QStringList silenceArgs(const Clip &clip, double thresholdDb = -35.0, double minSilence = 0.4);
// What to keep of a clip, from silenceArgs' log (whose times start at the clip's in point).
QVector<Range> clipKeepRanges(const Clip &clip, const QByteArray &log, bool *foundSilence = nullptr);

// Where the Whisper speech model lives, and how whisper-cli is run on a
// timeline WAV so it writes one word per segment to <base>.json.
QString whisperModelPath();
QString whisperModelUrl();
QStringList whisperArgs(const QString &wav, const QString &base);

struct ToolResult {
    bool ok = false;
    QByteArray output;
    QByteArray errors;
};
// Runs a tool to the end (or until timeoutMs) and collects what it printed.
ToolResult runTool(const QString &program, const QStringList &args, const QString &workingDirectory = {},
                   int timeoutMs = -1);

// Blocking versions of the window's background jobs. Each returns false (or
// nothing) and sets *error when it fails.
bool downloadWhisperModel(QString *error);
QVector<Word> transcribeProject(const Project &project, QString *error);
bool cutSilences(Project &project, int index, double thresholdDb, double minSilence, double *removed, QString *error);
bool exportProject(const Project &project, const QString &accent, const QString &output, QString *encoder,
                   QString *error);
QByteArray renderFrame(const Project &project, const QString &accent, double time, int width, QString *error);

// One 1080x1920 JPG per clip (01.jpg, 02.jpg, ...) in `folder`, drawn like the
// export draws that moment (framing, look, LUT, captions, text) without
// motion: a photo set for TikTok's photo mode. Returns the files written.
QStringList exportPhotoSet(const Project &project, const QString &accent, const QString &folder, QString *error);
