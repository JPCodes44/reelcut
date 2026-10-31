#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Transitions between two clips. A transition overlaps the end of one clip
// with the start of the next for its duration, so the reel gets shorter by
// that much, the way every editor's cross-dissolve works.
//
// Each transition is drawn twice: by ffmpeg's xfade filter on export, and by
// src/shaders/transition.frag in the preview, which reimplements the same
// motion with the same 0..1 progress. See docs/effects.md.
namespace Transitions {

struct Kind {
    QString id;       // stored in projects
    QString label;    // shown in the editor
    QString xfade;    // ffmpeg xfade transition name
    int previewMode;  // the branch in transition.frag that draws it
};

const QVector<Kind> &kinds();
QStringList ids();
const Kind *find(const QString &id);

constexpr double DefaultSeconds = 0.5;
constexpr double MinSeconds = 0.1;
constexpr double MaxSeconds = 2.0;

}  // namespace Transitions
