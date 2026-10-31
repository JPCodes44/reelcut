// Probing and thumbnails follow Omacut (MIT, David Heinemeier Hansson).
#include "media.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace media {

namespace {
// Upper bound on a probe so a hung/unresponsive file (e.g. a stalled
// network mount) can't stall the worker thread running it forever. Callers
// keep probe() off the UI thread; this bounds even that wait.
constexpr int kProbeTimeoutMs = 15000;
// Poll granularity while waiting on a thumbnail child, so cancellation is prompt.
constexpr int kThumbPollMs = 50;
// A thumbnail child that never finishes (a stalled file) must not occupy a
// thumbnail worker forever; single frames take nowhere near this long.
constexpr int kThumbTimeoutMs = 30000;

// Streamed WebM/MKV captures (e.g. from a browser's MediaRecorder) have no
// duration in the header; scan the video stream's packet timestamps. That
// reads the whole file, so it's only the last resort: a couple of hours of
// cached video takes under a second, a slow disk up to the probe timeout.
double probePacketDuration(const QString &ffprobe, const QString &path, int streamIndex) {
    QProcess proc;
    proc.start(ffprobe, {
        "-v", "error",
        "-select_streams", QString::number(streamIndex),
        "-show_entries", "packet=pts_time,duration_time",
        "-of", "csv=p=0",
        path,
    });
    if (!proc.waitForFinished(kProbeTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(-1);
        return 0.0;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return 0.0;

    // From the first timestamp, which a cut or a capture can start well past 0.
    // A single packet is a still image, not a video.
    double first = 0.0, end = 0.0;
    int packets = 0;
    for (const QByteArray &raw : proc.readAllStandardOutput().split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const QList<QByteArray> fields = line.split(',');
        bool ptsOk = false;
        const double pts = fields.value(0).toDouble(&ptsOk);
        if (!ptsOk)
            continue;
        bool durOk = false;
        const double pktDur = fields.value(1).toDouble(&durOk);
        first = packets == 0 ? pts : qMin(first, pts);
        end = qMax(end, pts + (durOk && pktDur > 0.0 ? pktDur : 0.0));
        ++packets;
    }
    return packets > 1 ? end - first : 0.0;
}
}

QString toolPath(const QString &tool) {
    return QStandardPaths::findExecutable(tool);
}

VideoInfo probe(const QString &path) {
    VideoInfo info;
    info.path = path;

    const QString ffprobe = toolPath("ffprobe");
    if (ffprobe.isEmpty()) {
        info.error = "`ffprobe` was not found on your PATH. Install ffmpeg.";
        return info;
    }

    QProcess proc;
    proc.start(ffprobe, {
        "-v", "error",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        path,
    });
    if (!proc.waitForFinished(kProbeTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(-1);
        info.error = "ffprobe timed out reading this file.";
        return info;
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        info.error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (info.error.isEmpty())
            info.error = "ffprobe failed.";
        return info;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    const QJsonObject root = doc.object();
    QJsonObject stream;
    for (const QJsonValue &value : root.value("streams").toArray()) {
        const QJsonObject candidate = value.toObject();
        const QString type = candidate.value("codec_type").toString();
        // Skip embedded cover art, which ffprobe lists as a single-frame video stream.
        if (type == "video" && stream.isEmpty()
                && candidate.value("disposition").toObject().value("attached_pic").toInt() != 1)
            stream = candidate;
        else if (type == "audio")
            info.audio = true;
    }
    if (stream.isEmpty()) {
        info.error = "No video stream found in this file.";
        return info;
    }

    info.width = stream.value("width").toInt();
    info.height = stream.value("height").toInt();
    // Phones record sideways and say how to turn it upright, in a display
    // matrix or the older rotate tag.
    int rotation = stream.value("tags").toObject().value("rotate").toString().toInt();
    for (const QJsonValue &sideData : stream.value("side_data_list").toArray()) {
        if (sideData.toObject().contains("rotation"))
            rotation = sideData.toObject().value("rotation").toInt();
    }
    // Sized as displayed: widened for non-square pixels, like PAL's 16:15.
    const QStringList sar = stream.value("sample_aspect_ratio").toString().split(':');
    if (sar.size() == 2 && sar[0].toInt() > 0 && sar[1].toInt() > 0)
        info.width = qRound(double(info.width) * sar[0].toInt() / sar[1].toInt());
    if (qAbs(rotation) % 180 == 90)
        std::swap(info.width, info.height);

    info.start = root.value("format").toObject().value("start_time").toString().toDouble();

    // Duration can live on the stream, on the container, or (for streamed
    // WebM captures) only on the last video packet.
    QString durationStr = stream.value("duration").toString();
    if (durationStr.isEmpty())
        durationStr = root.value("format").toObject().value("duration").toString();
    if (!durationStr.isEmpty()) {
        info.duration = durationStr.toDouble();
    } else {
        info.duration = probePacketDuration(ffprobe, path, stream.value("index").toInt());
        if (info.duration <= 0.0) {
            info.error = "Could not determine the video duration.";
            return info;
        }
    }

    info.ok = info.duration > 0.0;
    if (!info.ok)
        info.error = "Video has a zero or invalid duration.";
    return info;
}

QImage thumbnail(const QString &path, double time, int height,
                 const std::function<bool()> &stopRequested) {
    const QString ffmpeg = toolPath("ffmpeg");
    if (ffmpeg.isEmpty())
        return {};

    QProcess proc;
    proc.start(ffmpeg, {
        "-loglevel", "error",
        "-ss", QString::number(qMax(time, 0.0), 'f', 3),
        "-i", path,
        "-frames:v", "1",
        "-vf", QString("scale=-1:%1").arg(height),
        "-f", "image2pipe",
        "-vcodec", "mjpeg",
        "pipe:1",
    });

    // Poll instead of waitForFinished(-1) so a stop request can kill the child
    // promptly — otherwise a worker thread would block until ffmpeg finishes
    // on its own. The deadline bounds the wait even if nobody asks to stop.
    QElapsedTimer deadline;
    deadline.start();
    while (!proc.waitForFinished(kThumbPollMs)) {
        if (proc.state() == QProcess::NotRunning)
            break;  // failed to start, or exited between polls
        if ((stopRequested && stopRequested())
            || deadline.elapsed() > kThumbTimeoutMs) {
            proc.kill();
            proc.waitForFinished(-1);
            return {};
        }
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};

    const QByteArray data = proc.readAllStandardOutput();
    QImage img;
    img.loadFromData(data, "JPEG");
    return img;
}

}  // namespace media
