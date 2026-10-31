#pragma once

#include <QByteArray>
#include <QPair>
#include <QVector>

using Range = QPair<double, double>;

// Silences reported by ffmpeg's silencedetect filter, in the log's own
// seconds. A silence still open when the input ends runs to infinity.
QVector<Range> parseSilences(const QByteArray &log);

// The parts of [start, end] worth keeping around the given silences. Each kept
// part grows by `padding` into the silence on both sides, so words aren't
// clipped, and parts shorter than `minKeep` are dropped.
QVector<Range> keepRanges(double start, double end, const QVector<Range> &silences,
                          double padding = 0.1, double minKeep = 0.15);
