#pragma once

#include <QString>
#include <QStringList>

#include "project.h"

struct ExportOptions {
    QString output;
    // Both live in a private work folder with plain names, because ffmpeg's
    // filter syntax would need any other path escaped twice over.
    QString assFile;  // captions and overlays; empty for none
    QString lutFile;  // copy of the project's LUT; empty for none
    QString encoder = "h264_nvenc";  // or libx264
};

// The ffmpeg arguments that render the whole reel to options.output:
// trimmed, sped, framed clips back to back, the look, burned-in captions,
// music ducked under the clips' sound, loudness normalized, H.264 + AAC.
// Progress goes to stdout as key=value lines.
QStringList exportArgs(const Project &project, const ExportOptions &options);

// The clips' sound alone, at timeline speed, as 16 kHz mono WAV for
// transcription, so the words come back in timeline time.
QStringList timelineAudioArgs(const Project &project, const QString &wav);

// One finished frame at timeline `time` as a PNG `width` wide: everything the
// export draws (framing, look, LUT, captions, text), without the audio.
QStringList frameArgs(const Project &project, const ExportOptions &options, double time, const QString &png,
                      int width = 540);

// scale+crop that fills the 9:16 frame with a clip, honoring zoom and pan.
QString framingFilter(const Clip &clip);

// The slow zoom or pan over a photo, or empty for none (and for videos).
QString motionFilter(const Clip &clip);

// atempo stages for a speed; each stage only goes down to 0.5.
QString atempoChain(double speed);

// Number formatting shared by every filter, fixed and locale-free.
QString num(double value, int decimals = 3);
