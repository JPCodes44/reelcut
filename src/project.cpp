#include "project.h"

#include <QColor>
#include <QDir>
#include <QJsonArray>

#include "filters.h"
#include "bodyeffects.h"
#include "effects.h"
#include "transitions.h"

namespace {
// Shorter than a frame, a cut would leave an empty clip behind.
constexpr double kMinClip = 1.0 / Project::Fps;

QString storePath(const QString &path, const QString &baseDir) {
    if (path.isEmpty() || baseDir.isEmpty())
        return path;
    return QDir(baseDir).relativeFilePath(path);
}

QString loadPath(const QString &path, const QString &baseDir) {
    if (path.isEmpty() || baseDir.isEmpty() || QDir::isAbsolutePath(path))
        return path;
    return QDir::cleanPath(QDir(baseDir).absoluteFilePath(path));
}
}  // namespace

QStringList Clip::motions() {
    return {"none", "zoom-in", "zoom-out", "pan-left", "pan-right"};
}

QVector<double> Project::clipStarts() const {
    QVector<double> starts;
    double start = 0.0;
    for (int i = 0; i < clips.size(); ++i) {
        starts.append(start);
        start += clips[i].duration() - overlap(i);
    }
    starts.append(start);
    return starts;
}

double Project::overlap(int index) const {
    if (index < 0 || index + 1 >= clips.size())
        return 0.0;
    const Clip &a = clips[index];
    if (a.transition.isEmpty() || !Transitions::find(a.transition))
        return 0.0;
    const double limit = qMin(a.duration(), clips[index + 1].duration()) / 2.0;
    return qMax(0.0, qMin(a.transitionSeconds, limit));
}

double Project::duration() const {
    return clipStarts().constLast();
}

double Project::clipStart(int index) const {
    const QVector<double> starts = clipStarts();
    return starts.value(qBound(0, index, int(starts.size()) - 1));
}

Project::Position Project::locate(double t) const {
    const QVector<double> starts = clipStarts();
    for (int i = 0; i < clips.size(); ++i) {
        const Clip &clip = clips[i];
        // During a transition the outgoing clip owns the time; the incoming
        // one is already playing underneath. The last clip owns the very end,
        // so the playhead can rest there.
        if (t < starts[i] + clip.duration() || i == clips.size() - 1) {
            const double local = qBound(0.0, t - starts[i], clip.duration());
            return {i, clip.in + local * clip.speed};
        }
    }
    return {};
}

bool Project::split(double t) {
    const Position at = locate(t);
    if (at.index < 0)
        return false;
    const Clip clip = clips[at.index];
    if (at.source - clip.in < kMinClip || clip.out - at.source < kMinClip)
        return false;
    Clip first = clip, second = clip;
    first.out = at.source;
    // The transition belongs to the clip's end, which is now the second piece.
    first.transition.clear();
    second.in = at.source;
    clips[at.index] = first;
    clips.insert(at.index + 1, second);
    return true;
}

void Project::replaceClipWithRanges(int index, const QVector<QPair<double, double>> &ranges) {
    if (index < 0 || index >= clips.size())
        return;
    const Clip clip = clips[index];
    QVector<Clip> pieces;
    for (const auto &range : ranges) {
        Clip piece = clip;
        piece.in = qMax(clip.in, range.first);
        piece.out = qMin(clip.out, range.second);
        if (piece.out - piece.in >= kMinClip)
            pieces.append(piece);
    }
    if (pieces.isEmpty())
        return;
    clips.remove(index);
    for (int i = 0; i < pieces.size(); ++i)
        clips.insert(index + i, pieces[i]);
}

QJsonObject Project::toJson(const QString &baseDir) const {
    QJsonArray clipArray;
    for (const Clip &c : clips) {
        QJsonObject clip{
            {"path", storePath(c.path, baseDir)}, {"in", c.in}, {"out", c.out}, {"speed", c.speed},
            {"zoom", c.zoom}, {"panX", c.panX}, {"panY", c.panY}, {"width", c.width},
            {"height", c.height}, {"audio", c.audio}, {"sourceDuration", c.sourceDuration},
            {"still", c.still}, {"motion", c.motion}, {"transition", c.transition},
            {"transitionSeconds", c.transitionSeconds}, {"body", c.body}, {"bodyAmount", c.bodyAmount},
        };
        if (!c.grade.isNeutral())
            clip["grade"] = c.grade.toJson();
        clipArray.append(clip);
    }
    QJsonArray overlayArray;
    for (const Overlay &o : overlays) {
        overlayArray.append(QJsonObject{
            {"text", o.text}, {"start", o.start}, {"end", o.end}, {"x", o.x}, {"y", o.y},
            {"size", o.size}, {"animation", o.animation}, {"style", o.style}, {"bulge", o.bulge},
            {"fill", o.fill}, {"outline", o.outline}, {"shadow", o.shadow}, {"box", o.box},
            {"font", o.font}, {"bold", o.bold}, {"italic", o.italic}, {"outlineWidth", o.outlineWidth},
            {"shadowDistance", o.shadowDistance}, {"letterSpacing", o.letterSpacing},
            {"spacingDefault", o.spacingDefault}, {"noShadow", o.noShadow}, {"thickness", o.thickness},
        });
    }
    QJsonArray effectArray;
    for (const Effect &e : effects)
        effectArray.append(QJsonObject{{"kind", e.kind}, {"start", e.start}, {"end", e.end},
                                       {"intensity", e.intensity}, {"speed", e.speed}});
    QJsonArray wordArray;
    for (const Word &w : words)
        wordArray.append(QJsonObject{{"text", w.text}, {"start", w.start}, {"end", w.end}});

    return {
        {"version", 1},
        {"clips", clipArray},
        {"music", QJsonObject{{"path", storePath(music.path, baseDir)}, {"volume", music.volume},
                              {"offset", music.offset}, {"duck", music.duck}}},
        {"overlays", overlayArray},
        {"effects", effectArray},
        {"words", wordArray},
        {"captionStyle", captionStyle},
        {"captionX", captionX},
        {"captionY", captionY},
        {"captionAuto", captionAuto},
        {"captionScale", captionScale},
        {"look", QJsonObject{{"exposure", look.exposure}, {"contrast", look.contrast},
                             {"saturation", look.saturation}, {"temperature", look.temperature},
                             {"lut", storePath(look.lut, baseDir)}, {"filter", look.filter},
                             {"filterAmount", look.filterAmount}, {"grade", look.grade.toJson()}}},
        {"platform", platform},
        {"post", QJsonObject{{"caption", postCaption}, {"hashtags", postHashtags}}},
    };
}

Project Project::fromJson(const QJsonObject &json, const QString &baseDir) {
    Project p;
    for (const auto &value : json.value("clips").toArray()) {
        const QJsonObject o = value.toObject();
        Clip c;
        c.path = loadPath(o.value("path").toString(), baseDir);
        c.in = o.value("in").toDouble();
        c.out = o.value("out").toDouble();
        c.speed = qBound(0.25, o.value("speed").toDouble(1.0), 4.0);
        c.zoom = qBound(1.0, o.value("zoom").toDouble(1.0), 3.0);
        c.panX = qBound(-1.0, o.value("panX").toDouble(), 1.0);
        c.panY = qBound(-1.0, o.value("panY").toDouble(), 1.0);
        c.width = o.value("width").toInt();
        c.height = o.value("height").toInt();
        c.audio = o.value("audio").toBool(true);
        c.sourceDuration = o.value("sourceDuration").toDouble(c.out);
        c.still = o.value("still").toBool(false);
        c.motion = Clip::motions().contains(o.value("motion").toString()) ? o.value("motion").toString() : "none";
        const Transitions::Kind *kind = Transitions::find(o.value("transition").toString());
        c.transition = kind ? kind->id : QString();
        c.transitionSeconds = qBound(Transitions::MinSeconds, o.value("transitionSeconds").toDouble(Transitions::DefaultSeconds),
                                     Transitions::MaxSeconds);
        const Body::Kind *body = Body::find(o.value("body").toString());
        c.body = body ? body->id : QString();
        c.bodyAmount = qBound(0.0, o.value("bodyAmount").toDouble(c.bodyAmount), 1.0);
        c.grade = ColorGrade::Grade::fromJson(o.value("grade").toObject());
        if (c.still) {
            c.audio = false;
            c.speed = 1.0;
            c.sourceDuration = Clip::MaxPhotoSeconds;
        }
        if (c.out > c.in)
            p.clips.append(c);
    }
    const QJsonObject m = json.value("music").toObject();
    p.music.path = loadPath(m.value("path").toString(), baseDir);
    p.music.volume = m.value("volume").toDouble(p.music.volume);
    p.music.offset = m.value("offset").toDouble();
    p.music.duck = m.value("duck").toBool(true);
    for (const auto &value : json.value("overlays").toArray()) {
        const QJsonObject o = value.toObject();
        Overlay overlay;
        overlay.text = o.value("text").toString();
        overlay.start = o.value("start").toDouble();
        overlay.end = o.value("end").toDouble(overlay.start + 3.0);
        overlay.x = qBound(0.0, o.value("x").toDouble(overlay.x), 1.0);
        overlay.y = o.value("y").toDouble(overlay.y);
        overlay.size = o.value("size").toInt(overlay.size);
        overlay.animation = o.value("animation").toString(overlay.animation);
        overlay.style = o.value("style").toString() == "fisheye" ? "fisheye" : "plain";
        overlay.bulge = qBound(0.0, o.value("bulge").toDouble(overlay.bulge), 1.0);
        for (const QString &part : Overlay::colorParts())
            overlay.setColor(part, o.value(part).toString());
        overlay.font = o.value("font").toString();
        overlay.bold = qBound(-1, o.value("bold").toInt(-1), 1);
        overlay.italic = o.value("italic").toBool();
        overlay.outlineWidth = qMin(30.0, o.value("outlineWidth").toDouble(-1));
        overlay.shadowDistance = qMin(30.0, o.value("shadowDistance").toDouble(-1));
        overlay.letterSpacing = qBound(-20.0, o.value("letterSpacing").toDouble(), 60.0);
        // Projects from before the box: spacing that was set stays on.
        overlay.spacingDefault = o.value("spacingDefault").toBool(overlay.letterSpacing == 0);
        overlay.noShadow = o.value("noShadow").toBool();
        overlay.thickness = qBound(0.0, o.value("thickness").toDouble(), 20.0);
        p.overlays.append(overlay);
    }
    for (const auto &value : json.value("effects").toArray()) {
        const QJsonObject o = value.toObject();
        const Effects::Kind *kind = Effects::find(o.value("kind").toString());
        if (!kind)
            continue;  // from a newer version; dropped rather than kept broken
        Effect e;
        e.kind = kind->id;
        e.start = qMax(0.0, o.value("start").toDouble());
        e.end = qMax(e.start + kMinClip, o.value("end").toDouble(e.start + Effects::DefaultSeconds));
        e.intensity = qBound(0.0, o.value("intensity").toDouble(e.intensity), 1.0);
        e.speed = qBound(0.0, o.value("speed").toDouble(e.speed), 1.0);
        p.effects.append(e);
    }
    for (const auto &value : json.value("words").toArray()) {
        const QJsonObject o = value.toObject();
        p.words.append({o.value("text").toString(), o.value("start").toDouble(), o.value("end").toDouble()});
    }
    p.captionStyle = json.value("captionStyle").toString(p.captionStyle);
    p.captionX = qBound(0.0, json.value("captionX").toDouble(p.captionX), 1.0);
    p.captionY = json.value("captionY").toDouble(p.captionY);
    p.captionScale = qBound(0.5, json.value("captionScale").toDouble(p.captionScale), 2.0);
    const QJsonObject l = json.value("look").toObject();
    p.look.exposure = l.value("exposure").toDouble();
    p.look.contrast = l.value("contrast").toDouble();
    p.look.saturation = l.value("saturation").toDouble();
    p.look.temperature = l.value("temperature").toDouble();
    p.look.lut = loadPath(l.value("lut").toString(), baseDir);
    // An id this version doesn't know (a newer file) is dropped, not kept broken.
    const Filters::Look *filter = Filters::find(l.value("filter").toString());
    p.look.filter = filter ? filter->id : QString();
    p.look.filterAmount = qBound(0.0, l.value("filterAmount").toDouble(1.0), 1.0);
    p.look.grade = ColorGrade::Grade::fromJson(l.value("grade").toObject());
    p.platform = json.value("platform").toString(p.platform) == "tiktok" ? "tiktok" : "instagram";
    // Files from before captionAuto: captions still on the old fixed default
    // count as never moved.
    p.captionAuto = json.contains("captionAuto") ? json.value("captionAuto").toBool()
                                                 : (qFuzzyCompare(p.captionX, 0.5) && qFuzzyCompare(p.captionY, 0.68));
    if (p.captionAuto) {
        const QPointF home = captionHome(p.platform);
        p.captionX = home.x();
        p.captionY = home.y();
    }
    p.postCaption = json.value("post").toObject().value("caption").toString();
    p.postHashtags = json.value("post").toObject().value("hashtags").toString();
    return p;
}

bool Project::setClipValue(int index, const QString &key, double value) {
    if (index < 0 || index >= clips.size())
        return false;
    Clip &c = clips[index];
    if (key == "duration") {
        // Photos have no source length; their duration is simply how long they show.
        c.out = c.in + qBound(kMinClip * c.speed, value * c.speed, (c.sourceDuration > 0 ? c.sourceDuration : c.out) - c.in);
        return true;
    }
    if (c.still && key == "speed")
        return false;
    // Never shorter than a frame, never past the source.
    if (key == "in")
        c.in = qBound(0.0, value, c.out - kMinClip);
    else if (key == "out")
        c.out = qBound(c.in + kMinClip, value, c.sourceDuration > 0 ? c.sourceDuration : qMax(value, c.in + kMinClip));
    else if (key == "speed")
        c.speed = qBound(0.25, value, 4.0);
    else if (key == "zoom")
        c.zoom = qBound(1.0, value, 3.0);
    else if (key == "panX")
        c.panX = qBound(-1.0, value, 1.0);
    else if (key == "panY")
        c.panY = qBound(-1.0, value, 1.0);
    else if (key == "transitionSeconds")
        c.transitionSeconds = qBound(Transitions::MinSeconds, value, Transitions::MaxSeconds);
    else if (key == "bodyAmount")
        c.bodyAmount = qBound(0.0, value, 1.0);
    else
        return false;
    return true;
}

bool Project::setClipBody(int index, const QString &id) {
    if (index < 0 || index >= clips.size())
        return false;
    if (id.isEmpty()) {
        clips[index].body.clear();
        return true;
    }
    const Body::Kind *kind = Body::find(id);
    if (!kind)
        return false;
    clips[index].body = kind->id;
    return true;
}

bool Project::setClipTransition(int index, const QString &id) {
    if (index < 0 || index >= clips.size())
        return false;
    if (id.isEmpty()) {
        clips[index].transition.clear();
        return true;
    }
    const Transitions::Kind *kind = Transitions::find(id);
    if (!kind)
        return false;
    clips[index].transition = kind->id;
    return true;
}

bool Project::setClipMotion(int index, const QString &motion) {
    if (index < 0 || index >= clips.size() || !Clip::motions().contains(motion))
        return false;
    clips[index].motion = motion;
    return true;
}

bool Project::setGrade(int index, const QString &key, double value) {
    if (index == -1)
        return look.grade.set(key, value);
    if (index < 0 || index >= clips.size())
        return false;
    return clips[index].grade.set(key, value);
}

QStringList Overlay::colorParts() { return {"fill", "outline", "shadow", "box"}; }

bool Overlay::setColor(const QString &part, const QString &value) {
    QString *slot = part == "fill" ? &fill : part == "outline" ? &outline : part == "shadow" ? &shadow
                  : part == "box" ? &box : nullptr;
    if (!slot)
        return false;
    const QString v = value.trimmed();
    if (v.isEmpty() || v == "default") {
        slot->clear();
        return true;
    }
    if (v == "transparent") {
        *slot = "#00000000";
        return true;
    }
    const QColor color = QColor::fromString(v);
    if (!color.isValid())
        return false;
    *slot = color.name(QColor::HexArgb);
    return true;
}

// Plain text: bold Noto Sans with an 8 px outline, like the captions.
// Fisheye: regular Liberation Serif, a black outline 0 px wide until set.
QString Overlay::fontOf() const {
    return !font.isEmpty() ? font : style == "fisheye" ? QStringLiteral("Liberation Serif") : QStringLiteral("Noto Sans");
}
bool Overlay::boldOf() const { return bold >= 0 ? bold == 1 : style != "fisheye"; }
double Overlay::outlineWidthOf() const { return outlineWidth >= 0 ? outlineWidth : style == "fisheye" ? 0 : 8; }
double Overlay::shadowDistanceOf() const { return shadowDistance >= 0 ? shadowDistance : style == "fisheye" ? 6 : 5; }

QString Overlay::colorOf(const QString &part) const {
    const QString set = part == "fill" ? fill : part == "outline" ? outline : part == "shadow" ? shadow : box;
    if (!set.isEmpty())
        return set;
    const bool fisheye = style == "fisheye";
    // Plain text: white with a black outline. Fisheye: yellow with a soft shadow.
    if (part == "fill")
        return fisheye ? "#ffffee00" : "#ffffffff";
    // Both styles have a black outline; fisheye's is 0 px wide until the
    // thickness slider moves (outlineWidthOf), so that slider always shows.
    if (part == "outline")
        return "#ff000000";
    if (part == "shadow")
        return fisheye ? "#8c000000" : "#00000000";
    return "#00000000";
}

bool Project::setOverlayValue(int index, const QString &key, const QVariant &value) {
    if (index < 0 || index >= overlays.size())
        return false;
    Overlay &o = overlays[index];
    if (key == "text")
        o.text = value.toString();
    else if (key == "start")
        o.start = qBound(0.0, value.toDouble(), o.end - 0.1);
    else if (key == "end")
        o.end = qMax(o.start + 0.1, value.toDouble());
    else if (key == "x")
        o.x = qBound(0.0, value.toDouble(), 1.0);
    else if (key == "y")
        o.y = qBound(0.05, value.toDouble(), 0.95);
    else if (key == "size")
        o.size = qBound(40, value.toInt(), 200);
    else if (key == "animation") {
        const QString animation = value.toString();
        if (!QStringList{"none", "fade", "pop", "slide"}.contains(animation))
            return false;
        o.animation = animation;
    } else if (key == "style") {
        const QString style = value.toString();
        if (style != "plain" && style != "fisheye")
            return false;
        o.style = style;
    } else if (key == "bulge")
        o.bulge = qBound(0.0, value.toDouble(), 1.0);
    else if (Overlay::colorParts().contains(key))
        return o.setColor(key, value.toString());
    else if (key == "font")
        o.font = value.toString() == "default" ? QString() : value.toString().trimmed();
    else if (key == "bold") {
        const QString v = value.toString();
        o.bold = v == "default" || v == "-1" ? -1 : value.toBool() ? 1 : 0;
    } else if (key == "italic")
        o.italic = value.toBool();
    else if (key == "outlineWidth")
        o.outlineWidth = value.toDouble() < 0 ? -1 : qBound(0.0, value.toDouble(), 30.0);
    else if (key == "shadowDistance")
        o.shadowDistance = value.toDouble() < 0 ? -1 : qBound(0.0, value.toDouble(), 30.0);
    else if (key == "letterSpacing") {
        // "default" ticks the Default box; a number sets the spacing and unticks it.
        if (value.toString() == "default")
            o.spacingDefault = true;
        else {
            o.letterSpacing = qBound(-20.0, value.toDouble(), 60.0);
            o.spacingDefault = false;
        }
    } else if (key == "spacingDefault")
        o.spacingDefault = value.toBool();
    else if (key == "noShadow")
        o.noShadow = value.toBool();
    else if (key == "thickness")
        o.thickness = qBound(0.0, value.toDouble(), 20.0);
    else
        return false;
    return true;
}

bool Project::setLookValue(const QString &key, double value) {
    const double v = qBound(-1.0, value, 1.0);
    if (key == "exposure")
        look.exposure = v;
    else if (key == "contrast")
        look.contrast = v;
    else if (key == "saturation")
        look.saturation = v;
    else if (key == "temperature")
        look.temperature = v;
    else if (key == "filterAmount")
        look.filterAmount = qBound(0.0, value, 1.0);
    else
        return false;
    return true;
}

bool Project::setFilter(const QString &id) {
    if (id.isEmpty()) {
        look.filter.clear();
        return true;
    }
    const Filters::Look *found = Filters::find(id);
    if (!found)
        return false;
    look.filter = found->id;
    return true;
}

namespace {
// Safe zones in 1080x1920 pixels: what each app's buttons and text leave
// clear. Keep in step with the dashed outline in Preview.qml.
struct SafeZone { double left, top, right, bottom; };
SafeZone safeZone(const QString &platform) {
    if (platform == "tiktok")
        return {40, 160, 1080 - 150, 1920 - 480};
    return {40, 220, 1080 - 130, 1920 - 420};
}
}

QPointF Project::captionHome(const QString &platform) {
    const SafeZone z = safeZone(platform);
    // 190px above the bottom of the safe zone leaves room for two lines of
    // captions before the app's own text starts.
    return {(z.left + z.right) / 2 / 1080.0, (z.bottom - 190) / 1920.0};
}

void Project::setPlatform(const QString &name) {
    platform = name == "tiktok" ? "tiktok" : "instagram";
    if (captionAuto) {
        const QPointF home = captionHome(platform);
        captionX = home.x();
        captionY = home.y();
        return;
    }
    // Moved by hand: keep the spot, but not under the new app's buttons. The
    // center stays a caption's half-height (about 90px) inside the zone.
    const SafeZone z = safeZone(platform);
    captionX = qBound((z.left + 120) / 1080.0, captionX, (z.right - 120) / 1080.0);
    captionY = qBound((z.top + 90) / 1920.0, captionY, (z.bottom - 90) / 1920.0);
}

bool Project::setCaptionValue(const QString &key, double value) {
    if (key == "x") {
        captionX = qBound(0.0, value, 1.0);
        captionAuto = false;
    } else if (key == "y") {
        captionY = qBound(0.05, value, 0.95);
        captionAuto = false;
    } else if (key == "scale") {
        captionScale = qBound(0.5, value, 2.0);
    } else {
        return false;
    }
    return true;
}

bool Project::setMusicValue(const QString &key, const QVariant &value) {
    if (key == "volume")
        music.volume = qBound(0.0, value.toDouble(), 1.5);
    else if (key == "offset")
        music.offset = qMax(0.0, value.toDouble());
    else if (key == "duck")
        music.duck = value.toBool();
    else
        return false;
    return true;
}

namespace {
struct Preset {
    const char *name;
    double exposure, contrast, saturation, temperature;
};
constexpr Preset kPresets[] = {
    {"Natural", 0, 0, 0, 0},       {"Warm", 0.05, 0.1, 0.1, 0.4},  {"Cool", 0, 0.1, 0, -0.4},
    {"Punchy", 0.05, 0.35, 0.35, 0}, {"Film", -0.05, 0.2, -0.25, 0.15}, {"Mono", 0, 0.25, -1, 0},
};
}  // namespace

QStringList Project::presets() {
    QStringList names;
    for (const Preset &preset : kPresets)
        names.append(preset.name);
    return names;
}

bool Project::applyPreset(const QString &name) {
    for (const Preset &preset : kPresets) {
        if (name.compare(preset.name, Qt::CaseInsensitive) == 0) {
            look.exposure = preset.exposure;
            look.contrast = preset.contrast;
            look.saturation = preset.saturation;
            look.temperature = preset.temperature;
            return true;
        }
    }
    return false;
}

int Project::addEffect(const QString &kind, double start, double end) {
    const Effects::Kind *found = Effects::find(kind);
    if (!found)
        return -1;
    Effect effect;
    effect.kind = found->id;
    effect.start = qMax(0.0, start);
    effect.end = end > effect.start ? end : effect.start + Effects::DefaultSeconds;
    effects.append(effect);
    return effects.size() - 1;
}

bool Project::setEffectValue(int index, const QString &key, double value) {
    if (index < 0 || index >= effects.size())
        return false;
    Effect &e = effects[index];
    if (key == "start")
        e.start = qBound(0.0, value, e.end - kMinClip);
    else if (key == "end")
        e.end = qMax(e.start + kMinClip, value);
    else if (key == "intensity")
        e.intensity = qBound(0.0, value, 1.0);
    else if (key == "speed")
        e.speed = qBound(0.0, value, 1.0);
    else
        return false;
    return true;
}

int Project::addOverlay(const QString &text, double start, double end) {
    Overlay overlay;
    overlay.text = text;
    overlay.start = qMax(0.0, start);
    overlay.end = end > overlay.start ? end : qMin(overlay.start + 3.0, qMax(duration(), overlay.start + 1.0));
    overlays.append(overlay);
    return overlays.size() - 1;
}
