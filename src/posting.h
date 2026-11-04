#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Getting a finished reel onto TikTok or Instagram. Neither site lets another
// app put a file into its upload page (browsers only take files from the
// person), so Reelcut does everything around that: exports the video, opens
// the upload page, shows the file to drag in, and puts the caption and
// hashtags on the clipboard to paste.
namespace Posting {

struct Target {
    QString id;     // also the project's platform: "tiktok", "instagram"
    QString label;
    QString url;    // where posting starts
    int maxChars;   // caption, hashtags included
    int maxHashtags;  // 0 for no limit
    QString steps;  // what's left to do there, as one line per step
};

const QVector<Target> &targets();
const Target *find(const QString &id);

// Hashtags from whatever was typed: "#food travel, #Food #new-york" gives
// {"#food", "#travel", "#newyork"}. Letters, digits and _ are kept; the rest
// of each word is dropped; repeats (ignoring case) go.
QStringList hashtags(const QString &typed);

// The text to paste: the caption, a blank line, then the hashtags.
QString text(const QString &caption, const QString &typed);

}  // namespace Posting
