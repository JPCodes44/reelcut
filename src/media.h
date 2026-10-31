#pragma once

#include <QImage>
#include <QString>

#include <functional>

// Thin wrappers around the ffprobe and ffmpeg command-line tools.
namespace media {

struct VideoInfo {
    QString path;
    double duration = 0.0;  // seconds
    // Where the timestamps start. Packet times include it; seeks and the
    // player's positions count from it as 0.
    double start = 0.0;
    // As displayed: swapped for video rotated a quarter turn.
    int width = 0;
    int height = 0;
    bool audio = false;
    bool ok = false;
    QString error;
};

// Probe a file for a usable video stream and duration (runs ffprobe).
VideoInfo probe(const QString &path);

// Grab a single frame at `time` seconds, scaled to `height` px. Returns a
// null QImage on failure, or when `stopRequested` turns true meanwhile.
QImage thumbnail(const QString &path, double time, int height = 90,
                 const std::function<bool()> &stopRequested = nullptr);

// Locate a tool on PATH; returns empty string if missing.
QString toolPath(const QString &tool);

}  // namespace media
