#include "project.h"

#include <QDir>
#include <QJsonArray>

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

double Project::duration() const {
    double total = 0.0;
    for (const Clip &clip : clips)
        total += clip.duration();
    return total;
}

double Project::clipStart(int index) const {
    double start = 0.0;
    for (int i = 0; i < index && i < clips.size(); ++i)
        start += clips[i].duration();
    return start;
}

Project::Position Project::locate(double t) const {
    double start = 0.0;
    for (int i = 0; i < clips.size(); ++i) {
        const Clip &clip = clips[i];
        const double end = start + clip.duration();
        // The last clip owns the very end, so the playhead can rest there.
        if (t < end || i == clips.size() - 1) {
            const double local = qBound(0.0, t - start, clip.duration());
            return {i, clip.in + local * clip.speed};
        }
        start = end;
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
        clipArray.append(QJsonObject{
            {"path", storePath(c.path, baseDir)}, {"in", c.in}, {"out", c.out}, {"speed", c.speed},
            {"zoom", c.zoom}, {"panX", c.panX}, {"panY", c.panY}, {"width", c.width},
            {"height", c.height}, {"audio", c.audio}, {"sourceDuration", c.sourceDuration},
        });
    }
    QJsonArray overlayArray;
    for (const Overlay &o : overlays) {
        overlayArray.append(QJsonObject{
            {"text", o.text}, {"start", o.start}, {"end", o.end}, {"y", o.y},
            {"size", o.size}, {"animation", o.animation},
        });
    }
    QJsonArray wordArray;
    for (const Word &w : words)
        wordArray.append(QJsonObject{{"text", w.text}, {"start", w.start}, {"end", w.end}});

    return {
        {"version", 1},
        {"clips", clipArray},
        {"music", QJsonObject{{"path", storePath(music.path, baseDir)}, {"volume", music.volume},
                              {"offset", music.offset}, {"duck", music.duck}}},
        {"overlays", overlayArray},
        {"words", wordArray},
        {"captionStyle", captionStyle},
        {"captionY", captionY},
        {"look", QJsonObject{{"exposure", look.exposure}, {"contrast", look.contrast},
                             {"saturation", look.saturation}, {"temperature", look.temperature},
                             {"lut", storePath(look.lut, baseDir)}}},
        {"platform", platform},
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
        overlay.y = o.value("y").toDouble(overlay.y);
        overlay.size = o.value("size").toInt(overlay.size);
        overlay.animation = o.value("animation").toString(overlay.animation);
        p.overlays.append(overlay);
    }
    for (const auto &value : json.value("words").toArray()) {
        const QJsonObject o = value.toObject();
        p.words.append({o.value("text").toString(), o.value("start").toDouble(), o.value("end").toDouble()});
    }
    p.captionStyle = json.value("captionStyle").toString(p.captionStyle);
    p.captionY = json.value("captionY").toDouble(p.captionY);
    const QJsonObject l = json.value("look").toObject();
    p.look.exposure = l.value("exposure").toDouble();
    p.look.contrast = l.value("contrast").toDouble();
    p.look.saturation = l.value("saturation").toDouble();
    p.look.temperature = l.value("temperature").toDouble();
    p.look.lut = loadPath(l.value("lut").toString(), baseDir);
    p.platform = json.value("platform").toString(p.platform);
    return p;
}
