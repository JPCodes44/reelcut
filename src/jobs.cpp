#include "jobs.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include "captions.h"

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
