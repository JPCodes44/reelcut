#pragma once

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVector>

// One stretch of a source video on the timeline. Times are in seconds of the
// source; the clip plays `out - in` of it at `speed`.
struct Clip {
    QString path;
    double in = 0.0;
    double out = 0.0;
    double speed = 1.0;
    // Framing inside the 9:16 frame: zoom 1 covers the frame exactly, pan -1..1
    // slides the crop from one edge of the scaled video to the other.
    double zoom = 1.0;
    double panX = 0.0;
    double panY = 0.0;
    int width = 0;
    int height = 0;
    bool audio = true;
    double sourceDuration = 0.0;

    double duration() const { return (out - in) / speed; }
};

// A caption word, in timeline seconds.
struct Word {
    QString text;
    double start = 0.0;
    double end = 0.0;
};

// Text on top of the video for a stretch of the timeline, such as a hook.
struct Overlay {
    QString text;
    double start = 0.0;
    double end = 3.0;
    double y = 0.2;  // center, as a fraction of the frame height
    int size = 96;   // pixels at 1080x1920
    QString animation = "pop";  // none, fade, pop, slide
};

struct Music {
    QString path;
    double volume = 0.35;
    double offset = 0.0;  // where in the song the reel starts
    bool duck = true;     // lower the music while clips have sound

    bool isSet() const { return !path.isEmpty(); }
};

// The look applied over the whole reel. Sliders run -1..1, 0 is untouched.
struct Look {
    double exposure = 0.0;
    double contrast = 0.0;
    double saturation = 0.0;
    double temperature = 0.0;  // positive is warmer
    QString lut;               // path to a .cube file, or empty

    bool isNeutral() const {
        return exposure == 0.0 && contrast == 0.0 && saturation == 0.0 && temperature == 0.0;
    }
};

struct Project {
    QVector<Clip> clips;
    Music music;
    QVector<Overlay> overlays;
    QVector<Word> words;
    QString captionStyle = "highlight";  // bold, highlight, pop, box
    double captionY = 0.68;
    Look look;
    QString platform = "instagram";      // instagram, tiktok

    // Frame size and rate of every export.
    static constexpr int Width = 1080;
    static constexpr int Height = 1920;
    static constexpr int Fps = 30;

    double duration() const;
    double clipStart(int index) const;

    // Which clip plays at timeline time t, and where in its source.
    struct Position {
        int index = -1;
        double source = 0.0;
    };
    Position locate(double t) const;

    // Splits the clip under t in two; false when t is on a clip edge or off the end.
    bool split(double t);
    // Replaces a clip with copies of it covering only the given source ranges,
    // so cutting silences keeps framing and speed.
    void replaceClipWithRanges(int index, const QVector<QPair<double, double>> &ranges);

    // Paths are stored relative to the project file's folder, so a project
    // moved with its media still opens.
    QJsonObject toJson(const QString &baseDir = {}) const;
    static Project fromJson(const QJsonObject &json, const QString &baseDir = {});
};
