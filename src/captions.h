#pragma once

#include <QString>
#include <QVector>

#include "project.h"

// A caption line: the few words shown together.
struct CaptionLine {
    double start = 0.0;
    double end = 0.0;
    QVector<Word> words;

    QString text() const;
};

// Groups words into short lines, as reels show them: a new line starts after
// `maxWords` words, past `maxChars` characters, or after a pause of `maxGap`.
QVector<CaptionLine> groupLines(const QVector<Word> &words, int maxWords = 3, int maxChars = 18,
                                double maxGap = 0.6);

// ASS subtitle time, H:MM:SS.cc.
QString assTime(double seconds);

// The complete .ass file burned into the export: captions in the project's
// style and every text overlay with its animation. `accent` is #RRGGBB, used
// for the highlighted word. The font is shared with the preview.
QString assDocument(const Project &project, const QString &accent);

// The font family captions and overlays use, on screen and in the export.
QString captionFont();
