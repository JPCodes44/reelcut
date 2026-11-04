#include "posting.h"

#include <QObject>
#include <QRegularExpression>
#include <QSet>

namespace Posting {

const QVector<Target> &targets() {
    // Caption limits as each site documents them; TikTok's app allows longer,
    // but 2,200 is what every way of posting accepts. YouTube ignores every
    // hashtag when there are more than 60. Facebook Reels run 3 to 90
    // seconds through its API; YouTube counts up to 3 minutes as a Short.
    static const QVector<Target> list = {
        {"instagram", "Instagram", "https://www.instagram.com/", 2200, 30,
         QObject::tr("In Instagram, click Create (+), then Post.\n"
                     "Drag the video from the file window into it, then Next.\n"
                     "Click the caption box and paste (Ctrl+V), then Share."),
         {40, 220, 1080 - 130, 1920 - 420}},
        {"facebook", "Facebook", "https://www.facebook.com/reels/create", 2200, 0,
         QObject::tr("On Facebook, drag the video from the file window into the reel page.\n"
                     "Press Next, click the description box and paste (Ctrl+V).\n"
                     "Press Share now."),
         {65, 270, 1080 - 120, 1920 - 670}, 3, 90},
        {"youtube", "YouTube", "https://studio.youtube.com/", 5000, 60,
         QObject::tr("In YouTube Studio, press Create, then Upload videos.\n"
                     "Drag the video from the file window into it.\n"
                     "Type a title, paste the description (Ctrl+V), then Publish."),
         {48, 290, 1080 - 190, 1920 - 520}, 0, 180},
        {"tiktok", "TikTok", "https://www.tiktok.com/tiktokstudio/upload", 2200, 0,
         QObject::tr("Drag the video from the file window into the TikTok page.\n"
                     "Click the description box and paste (Ctrl+V).\n"
                     "Check the cover and settings, then press Post."),
         {40, 160, 1080 - 150, 1920 - 480}},
    };
    return list;
}

QVariantMap Target::toMap() const {
    return {{"id", id}, {"label", label}, {"maxChars", maxChars}, {"maxHashtags", maxHashtags},
            {"minSeconds", minSeconds}, {"maxSeconds", maxSeconds}, {"safeLeft", safe.left},
            {"safeTop", safe.top}, {"safeRight", safe.right}, {"safeBottom", safe.bottom}};
}

const Target *find(const QString &id) {
    for (const Target &t : targets())
        if (t.id == id)
            return &t;
    return nullptr;
}

QStringList ids() {
    QStringList out;
    for (const Target &t : targets())
        out << t.id;
    return out;
}

QString normalize(const QString &id) {
    return find(id) ? id : QStringLiteral("instagram");
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
