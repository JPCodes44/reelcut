#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct Effect;

// Video effects: things that happen to the picture for a stretch of the
// timeline (a shake, a pulse, a VHS look), placed on their own track.
//
// Export: each effect runs on a copy of the picture and is laid back over the
// original only while it's active (overlay with enable='between(t,a,b)'),
// which works for any ffmpeg filter, even ones with no timeline support.
// Preview: src/shaders/effect.frag redraws each one on the GPU.
// See docs/effects.md.
namespace Effects {

struct Kind {
    QString id;
    QString label;
    int previewMode;     // the branch in effect.frag
    bool usesSpeed;      // whether the speed slider does anything
};

const QVector<Kind> &kinds();
QStringList ids();
const Kind *find(const QString &id);

// The ffmpeg filter chain that draws `effect` on a 1080x1920 picture, in
// timeline seconds. `in` and `out` are the pad labels to read and write.
// Returns statements to join with ';'.
QStringList exportChain(const Effect &effect, const QString &in, const QString &out, int serial);

constexpr double DefaultSeconds = 1.5;

}  // namespace Effects
