#include "songrequest.h"
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

namespace SongRequest {

static QString text(const QVariantMap &r, const char *key) { return r.value(key).toString().trimmed(); }
static QString number(double v) { return QString::number(v, 'g', 10); }

Form build(const QVariantMap &r) {
    Form f;
    QStringList parts;
    for (const char *key : {"prompt", "style", "imageStyle"})
        if (!text(r, key).isEmpty()) parts << text(r, key);
    f.caption = parts.join(". ");

    const int versions = r.contains("versions") ? qBound(1, r.value("versions").toInt(), 4) : 2;
    f.fields << qMakePair(QStringLiteral("audio_format"), QStringLiteral("mp3"))
             << qMakePair(QStringLiteral("batch_size"), QString::number(versions))
             << qMakePair(QStringLiteral("thinking"), QStringLiteral("true"));
    const double duration = r.value("duration").toDouble();
    if (duration > 0)
        f.fields << qMakePair(QStringLiteral("audio_duration"), number(qBound(10.0, duration, 240.0)));

    const bool instrumental = r.value("instrumental").toBool();
    const QString lyrics = r.value("lyrics").toString().trimmed();
    const QString remix = text(r, "remix");
    if (!remix.isEmpty()) {
        f.sourcePath = remix;
        const QString mode = text(r, "remixMode");
        const QString task = mode == "extend" ? "complete" : mode == "repaint" ? "repaint" : "cover";
        f.fields << qMakePair(QStringLiteral("task_type"), task);
        if (task == "repaint") {
            const double end = r.value("repaintEnd").toDouble();
            f.fields << qMakePair(QStringLiteral("repainting_start"), number(qMax(0.0, r.value("repaintStart").toDouble())))
                     << qMakePair(QStringLiteral("repainting_end"), end > 0 ? number(end) : QStringLiteral("-1"));
        }
    }
    if (instrumental) {
        f.fields << qMakePair(QStringLiteral("prompt"), f.caption)
                 << qMakePair(QStringLiteral("lyrics"), QStringLiteral("[Instrumental]"));
    } else if (!lyrics.isEmpty() || !remix.isEmpty()) {
        f.fields << qMakePair(QStringLiteral("prompt"), f.caption);
        if (!lyrics.isEmpty()) f.fields << qMakePair(QStringLiteral("lyrics"), lyrics);
    } else {
        f.fields << qMakePair(QStringLiteral("sample_mode"), QStringLiteral("true"))
                 << qMakePair(QStringLiteral("sample_query"), f.caption);
    }
    const QString reference = text(r, "reference");
    if (!reference.isEmpty()) f.referencePath = reference;
    return f;
}

QString slug(const QVariantMap &r) {
    QString source = text(r, "prompt");
    if (source.isEmpty()) source = text(r, "style");
    const QStringList words = source.toLower()
        .replace(QRegularExpression("[^a-z0-9- ]"), " ")
        .split(' ', Qt::SkipEmptyParts);
    return words.isEmpty() ? QStringLiteral("random") : words.mid(0, 4).join('-');
}

}
