#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

// The platforms a reel is made for, and getting a finished reel onto them.
// Instagram, Facebook and YouTube can be published through their APIs
// (src/publisher.h). Without an account connected, and always for TikTok,
// Reelcut does everything around posting by hand: exports the video, opens
// the upload page, shows the file to drag in, and puts the caption and
// hashtags on the clipboard to paste.
namespace Posting {

// What each app's buttons and text leave clear, in 1080x1920 pixels: the
// edges of the clear area. Preview.qml draws it as a dashed outline.
struct SafeZone { double left, top, right, bottom; };

struct Target {
    QString id;     // also the project's platform: instagram, facebook, youtube, tiktok
    QString label;
    QString url;    // where posting by hand starts
    int maxChars;   // caption, hashtags included
    int maxHashtags;  // 0 for no limit
    QString steps;  // what's left to do there, as one line per step
    SafeZone safe;
    int minSeconds = 0;  // how long a reel may be; 0 for no limit
    int maxSeconds = 0;

    // For QML: id, label, the limits, and safeLeft/Top/Right/Bottom.
    QVariantMap toMap() const;
};

// In the order they're offered: instagram, facebook, youtube, tiktok.
const QVector<Target> &targets();
const Target *find(const QString &id);
QStringList ids();
// A known platform id, or "instagram" for anything else.
QString normalize(const QString &id);

// Hashtags from whatever was typed: "#food travel, #Food #new-york" gives
// {"#food", "#travel", "#newyork"}. Letters, digits and _ are kept; the rest
// of each word is dropped; repeats (ignoring case) go.
QStringList hashtags(const QString &typed);

// The text to paste: the caption, a blank line, then the hashtags.
QString text(const QString &caption, const QString &typed);

}  // namespace Posting
