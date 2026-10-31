#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct Clip;

// Body effects: effects that treat the person and the background
// differently (blur the background, drain its color, outline the person).
//
// They need to know which pixels are the person. src/segment.cpp runs a
// segmentation model over the clip once and saves the answer as a small
// black-and-white mask video (white = person). On export, ffmpeg's
// maskedmerge picks each pixel from the untouched picture or the changed
// background by that mask. See docs/effects.md.
namespace Body {

struct Kind {
    QString id;
    QString label;
    int previewMode;  // the branch in src/shaders/body.frag
};

const QVector<Kind> &kinds();
QStringList ids();
const Kind *find(const QString &id);

// ffmpeg statements turning the clip's framed picture `in` into `out`, using
// the mask video read from `mask` (an input pad like "[5:v]").
QStringList exportStatements(const Clip &clip, const QString &in, const QString &mask, const QString &out, int serial);

}  // namespace Body
