#include "posting.h"

#include <QObject>
#include <QRegularExpression>
#include <QSet>

namespace Posting {

const QVector<Target> &targets() {
    // Limits as both sites document them for captions; TikTok's app allows
    // longer, but 2,200 is what every way of posting accepts.
    static const QVector<Target> list = {
        {"tiktok", "TikTok", "https://www.tiktok.com/tiktokstudio/upload", 2200, 0,
         QObject::tr("Drag the video from the file window into the TikTok page.\n"
                     "Click the description box and paste (Ctrl+V).\n"
                     "Check the cover and settings, then press Post.")},
        {"instagram", "Instagram", "https://www.instagram.com/", 2200, 30,
         QObject::tr("In Instagram, click Create (+), then Post.\n"
                     "Drag the video from the file window into it, then Next.\n"
                     "Click the caption box and paste (Ctrl+V), then Share.")},
    };
    return list;
}

const Target *find(const QString &id) {
    for (const Target &t : targets())
        if (t.id == id)
            return &t;
    return nullptr;
}

QStringList hashtags(const QString &typed) {
    static const QRegularExpression separators("[\\s,;]+");
    static const QRegularExpression notTagChar("[^\\p{L}\\p{N}_]");
    QStringList out;
    QSet<QString> seen;
    for (QString word : typed.split(separators, Qt::SkipEmptyParts)) {
        word.remove(notTagChar);
        if (word.isEmpty() || seen.contains(word.toLower()))
            continue;
        seen.insert(word.toLower());
        out << "#" + word;
    }
    return out;
}

QString text(const QString &caption, const QString &typed) {
    const QString tags = hashtags(typed).join(' ');
    const QString body = caption.trimmed();
    if (tags.isEmpty())
        return body;
    return body.isEmpty() ? tags : body + "\n\n" + tags;
}

}  // namespace Posting
