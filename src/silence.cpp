#include "silence.h"

#include <QRegularExpression>

#include <limits>

QVector<Range> parseSilences(const QByteArray &log) {
    static const QRegularExpression startRe(R"(silence_start:\s*(-?[0-9.]+))");
    static const QRegularExpression endRe(R"(silence_end:\s*(-?[0-9.]+))");
    const QString text = QString::fromUtf8(log);

    QVector<Range> silences;
    double open = -1.0;
    bool isOpen = false;
    for (const QString &line : text.split('\n')) {
        const auto s = startRe.match(line);
        if (s.hasMatch()) {
            open = qMax(0.0, s.captured(1).toDouble());
            isOpen = true;
            continue;
        }
        const auto e = endRe.match(line);
        if (e.hasMatch() && isOpen) {
            silences.append({open, e.captured(1).toDouble()});
            isOpen = false;
        }
    }
    if (isOpen)
        silences.append({open, std::numeric_limits<double>::infinity()});
    return silences;
}

QVector<Range> keepRanges(double start, double end, const QVector<Range> &silences,
                          double padding, double minKeep) {
    // Shrink every silence by the padding; what's between them is kept.
    QVector<Range> kept;
    double cursor = start;
    for (const Range &silence : silences) {
        const double cutFrom = qMax(start, silence.first + padding);
        const double cutTo = qMin(end, silence.second - padding);
        if (cutTo <= cutFrom)
            continue;
        if (cutFrom > cursor)
            kept.append({cursor, cutFrom});
        cursor = qMax(cursor, cutTo);
    }
    if (end > cursor)
        kept.append({cursor, end});

    QVector<Range> result;
    for (const Range &range : kept)
        if (range.second - range.first >= minKeep)
            result.append(range);
    return result;
}
