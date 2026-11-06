#pragma once

#include <QJsonObject>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QVariant>
#include <QVector>

#include "colorgrade.h"

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
    // A photo held on screen for `out - in` seconds, optionally moving:
    // none, zoom-in, zoom-out, pan-left, pan-right.
    bool still = false;
    QString motion = "none";
    // A Transitions id into the next clip, or empty for a cut.
    QString transition;
    double transitionSeconds = 0.5;
    // A body effect (src/bodyeffects.h) over this clip, or empty, and how strong.
    QString body;
    double bodyAmount = 0.8;
    // Color wheels for this clip alone (src/colorgrade.h), before the reel's.
    ColorGrade::Grade grade;

    double duration() const { return (out - in) / speed; }

    static constexpr double PhotoSeconds = 2.5;
    static constexpr double MaxPhotoSeconds = 30.0;
    static QStringList motions();
};

// A video or photo laid over the main track, made by dragging a clip down
// off it. Its clip keeps its own trim, speed and framing (zoom and pan inside
// its box); the layer places that box on the frame. Layers sit on rows
// (`row` 0, 1, ...): higher rows draw on top, and the videos on one row
// never overlap in time.
// Transitions, body effects, photo motion and clip color wheels belong to the
// main track and are not used here. A layer is cut off where the reel ends.
struct Layer {
    Clip clip;
    double start = 0.0;  // timeline seconds
    double scale = 1.0;  // the box, as a fraction of the frame: 0.1..1
    double x = 0.5;      // the box's center, as fractions of the frame
    double y = 0.5;
    int row = 0;

    double end() const { return start + clip.duration(); }
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
    double x = 0.5;  // center, as a fraction of the frame width
    double y = 0.2;  // center, as a fraction of the frame height
    int size = 96;   // pixels at 1080x1920
    QString animation = "pop";  // none, fade, pop, slide
    // "plain" (white, outlined, via the .ass file) or "fisheye" (yellow
    // serif, bulged through a lens; see src/fisheyetext.h).
    QString style = "plain";
    double bulge = 0.6;  // fisheye strength, 0..1
    // Colors, as "#AARRGGBB"; empty means the style's default, alpha 0 is
    // transparent. Parts: fill, outline, shadow, box (a panel behind it).
    QString fill, outline, shadow, box;
    // Type. Empty / -1 means the style's default (see the *Of() below).
    QString font;               // a font family
    int bold = -1;              // -1 default, 0 regular, 1 bold
    bool italic = false;
    double outlineWidth = -1;   // pixels at 1080x1920, 0..30
    double shadowDistance = -1; // pixels at 1080x1920, 0..30
    double letterSpacing = 0;   // extra pixels between letters, -20..60, when not spacingDefault
    bool spacingDefault = true; // the "Default" box: the font's own spacing, letterSpacing kept for later
    bool noShadow = false;      // the "No shadow" box: no shadow whatever its color
    double thickness = 0;       // letters made heavier in their own fill color, pixels 0..20

    QString fontOf() const;
    bool boldOf() const;
    double outlineWidthOf() const;
    double shadowDistanceOf() const;
    double letterSpacingOf() const { return spacingDefault ? 0.0 : letterSpacing; }

    static QStringList colorParts();
    // "" or "default" for the style's default, "transparent", or any color
    // QColor reads (#rgb, #rrggbb, #aarrggbb, names). False if unreadable.
    bool setColor(const QString &part, const QString &value);
    // The color a part actually draws in, defaults filled in: "#AARRGGBB".
    QString colorOf(const QString &part) const;
};

// A video effect over a stretch of the timeline (see src/effects.h).
struct Effect {
    QString kind;            // an Effects id
    double start = 0.0;      // timeline seconds
    double end = 1.5;
    double intensity = 0.6;  // 0..1
    double speed = 0.5;      // 0..1, for effects that move
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
    QString filter;            // a Filters look id, or empty for none
    double filterAmount = 1.0; // 0..1, how much of the filter shows
    ColorGrade::Grade grade;   // color wheels over the whole reel

    bool isNeutral() const {
        return exposure == 0.0 && contrast == 0.0 && saturation == 0.0 && temperature == 0.0;
    }
};

struct Project {
    QVector<Clip> clips;
    // Drawn over the clips, kept sorted by row then start, so this is also
    // the order they draw in. See normalizeLayers().
    QVector<Layer> layers;
    Music music;
    QVector<Overlay> overlays;
    QVector<Effect> effects;
    QVector<Word> words;
    QString captionStyle = "highlight";  // bold, highlight, pop, box
    // Captions start at the platform's home spot (captionHome) and follow it
    // when the platform changes, until they're moved by hand.
    // = captionHome("instagram"): the middle of its safe zone, 190px above it.
    double captionX = (40 + 950) / 2 / 1080.0;
    double captionY = (1500 - 190) / 1920.0;
    bool captionAuto = true;
    double captionScale = 1.0;  // 0.5 to 2 times the style's size
    Look look;
    QString platform = "instagram";      // an id from Posting::targets(): instagram, facebook, youtube, tiktok

    // Where captions sit by default on a platform, as frame fractions (center):
    // in the middle of the safe zone across, and just above the app's own
    // caption and description area. The safe zone matches Preview.qml.
    static QPointF captionHome(const QString &platform);
    // Changes the platform. Captions on their home spot move to the new one;
    // captions placed by hand stay, nudged back inside the new safe zone.
    void setPlatform(const QString &name);
    // The last caption and hashtags typed for posting (src/posting.h).
    QString postCaption;
    QString postHashtags;

    // Frame size and rate of every export.
    static constexpr int Width = 1080;
    static constexpr int Height = 1920;
    static constexpr int Fps = 30;

    double duration() const;
    double clipStart(int index) const;
    // How long clip `index` overlaps the next one: its transition's length,
    // limited to half of either clip so a transition never swallows a clip.
    // 0 for a cut and for the last clip.
    double overlap(int index) const;

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

    // Edits with the same limits everywhere (the window, the MCP server).
    // Each returns false for an unknown key or index; values are clamped.
    bool setClipValue(int index, const QString &key, double value);
    bool setClipMotion(int index, const QString &motion);
    // A transition into the next clip; empty removes it. False if unknown.
    bool setClipTransition(int index, const QString &id);
    // A body effect on a clip; empty removes it. False if unknown.
    bool setClipBody(int index, const QString &id);
    bool setOverlayValue(int index, const QString &key, const QVariant &value);
    // How many layer rows there are (rows are numbered 0..layerRows()-1).
    int layerRows() const;
    // Sorts the layers by row and start and closes up empty rows.
    void normalizeLayers();
    // Puts `layer` on its row, slid to the nearest place where it doesn't
    // overlap the videos already there; a row past the last makes a new top
    // row. Returns its index.
    int placeLayer(Layer layer);

    // What dropping something dragged up or down the timeline does. Rows on
    // screen: 0 is the main track, 1.. are layer rows 0... `fromRow` is where
    // the drag started, `moved` how many rows it moved (negative is up).
    // Within half a row of a neighboring row it joins that row; more than
    // half a row past it, a layer swaps rows with it. Main-track clips join
    // the row they're dropped on, or a new top row past the last.
    struct Drop {
        QString action;  // none, join, newRow, toTrack, swap, swapTrack
        int row = -1;    // the layer row joined or swapped with, when there is one
    };
    Drop dropTarget(int fromRow, double moved, int index) const;

    // Takes clip `index` off the main track onto layer row `row` (a new top
    // row when past the last), starting at `start`. Returns the layer's index,
    // or -1 for a bad index or the last clip on the main track, which has to
    // stay so there is a picture underneath.
    int moveClipToLayer(int index, double start, int row = -1);
    // Moves a layer onto `row` (a new top row when past the last), at `start`.
    int moveLayerToRow(int index, int row, double start);
    // Swaps the stacking order of two layer rows; returns `index`'s new index.
    int swapLayerRows(int index, int otherRow);
    // Swaps the stacking order of the layer row holding `index` and the main
    // track: that row's videos become the main track, back to back, and the
    // main track's clips become a layer row at their old times. Returns the
    // main-track index of the clip that was layer `index`.
    int swapRowWithTrack(int index);
    // Puts a layer back on the main track where timeline time `time` falls;
    // returns its clip index, or -1 for a bad index.
    int moveLayerToTrack(int index, double time);
    // start, in, out, scale, x, y, zoom, panX, panY; values are clamped.
    bool setLayerValue(int index, const QString &key, double value);
    bool setLookValue(const QString &key, double value);
    // Color wheels: index -1 is the whole reel. Keys as ColorGrade::Grade::set.
    bool setGrade(int index, const QString &key, double value);
    // A look from the filter library by id; empty removes it. False if unknown.
    bool setFilter(const QString &id);
    // x, y (fractions of the frame) or scale.
    bool setCaptionValue(const QString &key, double value);
    bool setMusicValue(const QString &key, const QVariant &value);
    // Times on the timeline where each clip starts, then the end.
    QVector<double> clipStarts() const;
    // Natural, Warm, Cool, Punchy, Film or Mono (any case); false if unknown.
    bool applyPreset(const QString &name);
    static QStringList presets();
    int addOverlay(const QString &text, double start, double end = -1.0);
    // An effect from the library at `start`; -1 if the kind is unknown.
    int addEffect(const QString &kind, double start, double end = -1.0);
    // start, end, intensity, speed; values are clamped.
    bool setEffectValue(int index, const QString &key, double value);

    // Paths are stored relative to the project file's folder, so a project
    // moved with its media still opens.
    QJsonObject toJson(const QString &baseDir = {}) const;
    static Project fromJson(const QJsonObject &json, const QString &baseDir = {});
};
