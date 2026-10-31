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
    QString filterFile;  // the filter library look baked into a .cube; empty for none
    // Per clip: the person mask video for its body effect (absolute path), or empty.
    QStringList masks;
    // Per text overlay: its fisheye image (absolute path, see src/fisheyetext.h),
    // or empty for a plain overlay, which the .ass file draws.
    QStringList textImages;
    // Color wheels baked into .cube files (src/colorgrade.h): per clip (empty
    // for an ungraded clip), and the whole reel's (empty for none).
    QStringList clipGrades;
    QString gradeFile;
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

// One clip's picture as the export draws it, from input pad `input` (e.g.
// "[0:v]"): trimmed, sped, framed, moved, 30 fps, without a label. The person
// mask job reads exactly these frames so its mask lines up.
QString clipVideoChain(const Clip &clip, const QString &input);

// scale+crop that fills the 9:16 frame with a clip, honoring zoom and pan.
QString framingFilter(const Clip &clip);

// The slow zoom or pan over a photo, or empty for none (and for videos).
QString motionFilter(const Clip &clip);

// atempo stages for a speed; each stage only goes down to 0.5.
QString atempoChain(double speed);

// Number formatting shared by every filter, fixed and locale-free.
QString num(double value, int decimals = 3);
