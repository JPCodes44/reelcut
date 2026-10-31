#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

#include "exportgraph.h"
#include "project.h"

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
