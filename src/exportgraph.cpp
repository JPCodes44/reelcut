#include "exportgraph.h"

#include "bodyeffects.h"
#include "effects.h"
#include "transitions.h"

#include <algorithm>
#include <cmath>

namespace {
const QString kAudioFormat = "aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo";

int even(double value) {
    const int rounded = int(std::ceil(value));
    return rounded + (rounded % 2);
}

// One clip's video and audio chains, labelled [v<i>] and [a<i>]. A clip
// with a body effect and a mask (maskInputs[i] >= 0) goes through
// Body::exportStatements on its way to [v<i>].
QStringList clipChains(const Project &project, bool withVideo, bool withAudio, const QString &audioFormat,
                       const QVector<int> &maskInputs = {}, const QStringList &grades = {}) {
    QStringList chains;
    for (int i = 0; i < project.clips.size(); ++i) {
        const Clip &c = project.clips[i];
        if (withVideo) {
            const int mask = maskInputs.value(i, -1);
            QString picture = clipVideoChain(c, QString("[%1:v]").arg(i));
            // The clip's own color wheels come first, before its body effect.
            if (!grades.value(i).isEmpty())
                picture += QString(",lut3d=file=%1:interp=tetrahedral,format=yuv420p").arg(grades.value(i));
            if (mask >= 0 && Body::find(c.body)) {
                chains << picture + QString("[vb%1]").arg(i);
                chains << Body::exportStatements(c, QString("[vb%1]").arg(i), QString("[%1:v]").arg(mask),
                                                 QString("[v%1]").arg(i), i);
            } else {
                chains << picture + QString("[v%1]").arg(i);
            }
        }
        if (!withAudio)
            continue;
        if (c.audio && !c.still) {
            QString chain = QString("[%1:a]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS")
                                .arg(i).arg(num(c.in), num(c.out));
            if (c.speed != 1.0)
                chain += "," + atempoChain(c.speed);
            chains << chain + "," + audioFormat + QString("[a%1]").arg(i);
        } else {
            // A clip without sound still needs its length of silence, or the
            // audio would slide out of sync with the video after it.
            chains << QString("anullsrc=r=48000:cl=stereo,atrim=duration=%1,%2[a%3]")
                          .arg(num(c.duration()), audioFormat).arg(i);
        }
    }
    return chains;
}

// Joins the clip chains into [vcat] and/or [acat]. Without transitions
// this is one concat. With them it is a chain, two at a time: xfade (video)
// and acrossfade (sound) where a clip has a transition into the next, a
// two-input concat at plain cuts. xfade's offset is where the transition
// starts on the timeline, which is where the next clip starts.
QStringList joinClips(const Project &project, bool withVideo, bool withAudio) {
    const int n = project.clips.size();
    bool anyOverlap = false;
    for (int i = 0; i + 1 < n; ++i)
        anyOverlap = anyOverlap || project.overlap(i) > 0.0;
    if (!anyOverlap || n < 2) {
        QString inputs;
        for (int i = 0; i < n; ++i)
            inputs += (withVideo ? QString("[v%1]").arg(i) : QString()) + (withAudio ? QString("[a%1]").arg(i) : QString());
        return {inputs + QString("concat=n=%1:v=%2:a=%3").arg(n).arg(withVideo ? 1 : 0).arg(withAudio ? 1 : 0) +
                (withVideo ? "[vcat]" : "") + (withAudio ? "[acat]" : "")};
    }
    const QVector<double> starts = project.clipStarts();
    QStringList out;
    QString video = "[v0]", audio = "[a0]";
    for (int i = 1; i < n; ++i) {
        const double overlap = project.overlap(i - 1);
        const bool last = i == n - 1;
        const QString nextVideo = last ? "[vcat]" : QString("[vj%1]").arg(i);
        const QString nextAudio = last ? "[acat]" : QString("[aj%1]").arg(i);
        if (overlap > 0.0) {
            const Transitions::Kind *kind = Transitions::find(project.clips[i - 1].transition);
            if (withVideo)
                out << QString("%1[v%2]xfade=transition=%3:duration=%4:offset=%5%6")
                           .arg(video).arg(i).arg(kind->xfade, num(overlap), num(starts[i]), nextVideo);
            if (withAudio)
                out << QString("%1[a%2]acrossfade=d=%3:c1=tri:c2=tri%4").arg(audio).arg(i).arg(num(overlap), nextAudio);
        } else {
            if (withVideo)
                out << QString("%1[v%2]concat=n=2:v=1:a=0%3").arg(video).arg(i).arg(nextVideo);
            if (withAudio)
                out << QString("%1[a%2]concat=n=2:v=0:a=1%3").arg(audio).arg(i).arg(nextAudio);
        }
        video = nextVideo;
        audio = nextAudio;
    }
    return out;
}

// The main track with the layers over it: [vlay] when there are layers,
// otherwise just [vcat].
QString baseVideo(const Project &project) {
    return project.layers.isEmpty() ? "[vcat]" : "[vlay]";
}

// Each layer's picture, framed in its box and placed on the frame over
// [vcat], later layers on top: [vlay] out. Its frames are shifted to start at
// the layer's start, and it shows only while it plays. layerInputs[k] is the
// input layer k reads.
QStringList layerChains(const Project &project, const QVector<int> &layerInputs) {
    QStringList out;
    QString below = "[vcat]";
    for (int k = 0; k < project.layers.size(); ++k) {
        const Layer &l = project.layers[k];
        const int w = qMax(2, even(Project::Width * l.scale));
        const int h = qMax(2, even(Project::Height * l.scale));
        const int x = qRound(l.x * Project::Width - w / 2.0);
        const int y = qRound(l.y * Project::Height - h / 2.0);
        const QString pad = QString("[ly%1]").arg(k);
        const QString next = k == project.layers.size() - 1 ? "[vlay]" : QString("[vl%1]").arg(k);
        QString picture = clipVideoChain(l.clip, QString("[%1:v]").arg(layerInputs[k]));
        if (w != Project::Width || h != Project::Height)
            picture += QString(",scale=%1:%2:flags=lanczos").arg(w).arg(h);
        out << picture + QString(",setpts=PTS+%1/TB").arg(num(l.start)) + pad;
        out << QString("%1%2overlay=x=%3:y=%4:eof_action=pass:enable='between(t,%5,%6)'%7")
                   .arg(below, pad).arg(x).arg(y).arg(num(l.start), num(l.end()), next);
        below = next;
    }
    return out;
}

// The layers' sound, each delayed to its start: [la<k>] for each layer that
// has sound. Returns the labels made.
QStringList layerAudioChains(const Project &project, const QVector<int> &layerInputs, QStringList *graph) {
    QStringList labels;
    for (int k = 0; k < project.layers.size(); ++k) {
        const Clip &c = project.layers[k].clip;
        if (!c.audio || c.still)
            continue;
        QString chain = QString("[%1:a]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS")
                            .arg(layerInputs[k]).arg(num(c.in), num(c.out));
        if (c.speed != 1.0)
            chain += "," + atempoChain(c.speed);
        const QString label = QString("[la%1]").arg(k);
        *graph << chain + "," + kAudioFormat + QString(",adelay=%1:all=1").arg(qRound(project.layers[k].start * 1000)) + label;
        labels << label;
    }
    return labels;
}

// The effects track, in order, each over the result of the one before:
// the base video in, [vfx] out. Nothing at all when there are no effects.
QStringList effectChains(const Project &project) {
    if (project.effects.isEmpty())
        return {};
    QStringList out;
    QString in = baseVideo(project);
    for (int i = 0; i < project.effects.size(); ++i) {
        const QString next = i == project.effects.size() - 1 ? "[vfx]" : QString("[vfx%1]").arg(i);
        out << Effects::exportChain(project.effects[i], in, next, i);
        in = next;
    }
    return out;
}

// Back down to the 4:2:0 that phones play, with Lanczos chroma, after text.
const char *const kTo420 = "scale=flags=lanczos+accurate_rnd+full_chroma_int,format=yuv420p";

// The look sliders, the filter, the user's LUT, then text, which must stay white.
// Fisheye text images (inputs textInputs[i] >= 0) go over everything, each
// switched on for its overlay's stretch of time.
QString videoFinish(const Project &project, const ExportOptions &options, const QVector<int> &textInputs) {
    QStringList video;
    const Look &look = project.look;
    if (!look.isNeutral()) {
        video << QString("eq=brightness=%1:contrast=%2:saturation=%3")
                     .arg(num(look.exposure * 0.25), num(1.0 + look.contrast * 0.5), num(1.0 + look.saturation));
        if (look.temperature != 0.0)
            video << QString("colortemperature=temperature=%1").arg(qRound(6500 - look.temperature * 2500));
    }
    if (!options.gradeFile.isEmpty())
        video << QString("lut3d=file=%1:interp=tetrahedral").arg(options.gradeFile);
    if (!options.filterFile.isEmpty())
        video << QString("lut3d=file=%1:interp=tetrahedral").arg(options.filterFile);
    if (!options.lutFile.isEmpty())
        video << QString("lut3d=file=%1").arg(options.lutFile);
    // Text is drawn onto full-color-resolution frames (4:4:4), so colored edges
    // keep every pixel, then the frame goes down to 4:2:0 with a Lanczos
    // filter, which keeps them sharper than drawing straight onto 4:2:0.
    const bool hasText = !options.assFile.isEmpty() || std::any_of(textInputs.begin(), textInputs.end(),
                                                                   [](int i) { return i >= 0; });
    if (hasText)
        video << "format=yuv444p";
    if (!options.assFile.isEmpty())
        video << QString("ass=filename=%1").arg(options.assFile);
    const QString finish = hasText ? QString(",") + kTo420 : QString();
    const QString in = project.effects.isEmpty() ? baseVideo(project) : "[vfx]";
    QVector<int> texts;
    for (int i = 0; i < textInputs.size(); ++i)
        if (textInputs[i] >= 0)
            texts << i;
    if (texts.isEmpty())
        return in + (video.isEmpty() ? QString("null") : video.join(',')) + finish + "[vout]";

    QStringList out{in + (video.isEmpty() ? QString("null") : video.join(',')) + "[vlook]"};
    QString below = "[vlook]";
    for (int k = 0; k < texts.size(); ++k) {
        const Overlay &o = project.overlays[texts[k]];
        const QString s = num(o.start), e = num(o.end);
        // Entrances, as near the .ass ones as an image allows: pop fades in.
        QStringList fades;
        if (o.animation == "fade" || o.animation == "pop")
            fades << QString("fade=t=in:st=%1:d=0.25:alpha=1").arg(s);
        if (o.animation == "slide")
            fades << QString("fade=t=in:st=%1:d=0.2:alpha=1").arg(s);
        if (o.animation == "fade")
            fades << QString("fade=t=out:st=%1:d=0.15:alpha=1").arg(num(qMax(o.start, o.end - 0.15)));
        const QString y = o.animation == "slide" ? QString("'160*max(0,1-(t-%1)/0.25)'").arg(s) : QString("0");
        const QString pad = QString("[ft%1]").arg(k);
        const QString next = k == texts.size() - 1 ? "[vout]" : QString("[vt%1]").arg(k);
        out << QString("[%1:v]format=rgba%2%3").arg(textInputs[texts[k]])
                   .arg(fades.isEmpty() ? QString() : "," + fades.join(','), pad);
        const bool last = k == texts.size() - 1;
        out << QString("%1%2overlay=x=0:y=%3:enable='between(t,%4,%5)':format=yuv444%6%7")
                   .arg(below, pad, y, s, e, last ? finish : QString(), next);
        below = next;
    }
    return out.join(';');
}

// The fisheye text images as extra inputs, numbered from `first`: each a
// still frame looped until its overlay ends. Which input each overlay is (or -1).
QVector<int> textInputs(const Project &project, const ExportOptions &options, int first, QStringList *args) {
    QVector<int> index(project.overlays.size(), -1);
    int next = first;
    for (int i = 0; i < project.overlays.size(); ++i) {
        const QString image = options.textImages.value(i);
        if (image.isEmpty())
            continue;
        *args << "-loop" << "1" << "-framerate" << QString::number(Project::Fps)
              << "-t" << num(project.overlays[i].end + 0.1) << "-i" << image;
        index[i] = next++;
    }
    return index;
}

// The mask videos as extra inputs, numbered from `first`; which input each
// clip's mask is (or -1).
QVector<int> maskInputs(const Project &project, const ExportOptions &options, int first, QStringList *args) {
    QVector<int> index(project.clips.size(), -1);
    int next = first;
    for (int i = 0; i < project.clips.size(); ++i) {
        const QString mask = options.masks.value(i);
        if (mask.isEmpty() || !Body::find(project.clips[i].body))
            continue;
        *args << "-i" << mask;
        index[i] = next++;
    }
    return index;
}

void addClipInput(const Clip &clip, QStringList *args) {
    // A photo becomes a stream of the same frame, as long as it shows.
    if (clip.still)
        *args << "-loop" << "1" << "-framerate" << QString::number(Project::Fps) << "-t" << num(clip.duration() + 0.1);
    *args << "-i" << clip.path;
}

QStringList clipInputs(const Project &project) {
    QStringList args;
    for (const Clip &clip : project.clips)
        addClipInput(clip, &args);
    return args;
}

// The layers' sources as extra inputs, numbered from `first`.
QVector<int> layerInputs(const Project &project, int first, QStringList *args) {
    QVector<int> index;
    for (const Layer &layer : project.layers) {
        addClipInput(layer.clip, args);
        index << first + index.size();
    }
    return index;
}
}  // namespace

QString num(double value, int decimals) {
    QString text = QString::number(value, 'f', decimals);
    if (text.contains('.')) {
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
    }
    return text == "-0" ? "0" : text;
}

QString clipVideoChain(const Clip &c, const QString &input) {
    if (c.still) {
        // A looped photo: framed once, then moved over its duration.
        const QString motion = motionFilter(c);
        return QString("%1trim=duration=%2,setpts=PTS-STARTPTS,%3%4,setsar=1,fps=%5,format=yuv420p")
            .arg(input, num(c.duration()), framingFilter(c), motion.isEmpty() ? QString() : "," + motion)
            .arg(Project::Fps);
    }
    return QString("%1trim=start=%2:end=%3,setpts=(PTS-STARTPTS)/%4,%5,setsar=1,fps=%6,format=yuv420p")
        .arg(input, num(c.in), num(c.out), num(c.speed, 4), framingFilter(c))
        .arg(Project::Fps);
}

QString framingFilter(const Clip &clip) {
    const double w = clip.width > 0 ? clip.width : Project::Width;
    const double h = clip.height > 0 ? clip.height : Project::Height;
    // Cover the frame, then zoom further in.
    const double scale = qMax(Project::Width / w, Project::Height / h) * qBound(1.0, clip.zoom, 3.0);
    const int sw = qMax(Project::Width, even(w * scale));
    const int sh = qMax(Project::Height, even(h * scale));
    const int x = qRound((sw - Project::Width) * (qBound(-1.0, clip.panX, 1.0) + 1.0) / 2.0);
    const int y = qRound((sh - Project::Height) * (qBound(-1.0, clip.panY, 1.0) + 1.0) / 2.0);
    return QString("scale=%1:%2:flags=lanczos,crop=%3:%4:%5:%6")
        .arg(sw).arg(sh).arg(Project::Width).arg(Project::Height).arg(x).arg(y);
}

QString motionFilter(const Clip &clip) {
    if (!clip.still || clip.motion == "none" || !Clip::motions().contains(clip.motion))
        return {};
    // zoompan works on whole pixels, so it runs on a frame twice the size to
    // keep slow moves smooth. Zoom goes 12% over the photo's duration.
    const int frames = qMax(1, qRound(clip.duration() * Project::Fps));
    const QString n = QString::number(frames);
    QString z, x, y = "ih/2-ih/zoom/2";
    if (clip.motion == "zoom-in") {
        z = "1+0.12*on/" + n;
        x = "iw/2-iw/zoom/2";
    } else if (clip.motion == "zoom-out") {
        z = "1.12-0.12*on/" + n;
        x = "iw/2-iw/zoom/2";
    } else if (clip.motion == "pan-left") {
        z = "1.12";
        x = "(iw-iw/zoom)*(1-on/" + n + ")";
    } else {
        z = "1.12";
        x = "(iw-iw/zoom)*on/" + n;
    }
    return QString("scale=%1:%2:flags=bicubic,zoompan=z='%3':x='%4':y='%5':d=1:s=%6x%7:fps=%8")
        .arg(Project::Width * 2).arg(Project::Height * 2).arg(z, x, y)
        .arg(Project::Width).arg(Project::Height).arg(Project::Fps);
}

QString atempoChain(double speed) {
    QStringList stages;
    double rest = qBound(0.25, speed, 4.0);
    while (rest < 0.5) {
        stages << "atempo=0.5";
        rest /= 0.5;
    }
    stages << QString("atempo=%1").arg(num(rest, 4));
    return stages.join(',');
}

QStringList exportArgs(const Project &project, const ExportOptions &options) {
    const int n = project.clips.size();
    const double total = project.duration();
    QStringList args{"-hide_banner", "-nostdin", "-y", "-progress", "pipe:1", "-nostats"};
    args << clipInputs(project);
    if (project.music.isSet())
        args << "-i" << project.music.path;
    const QVector<int> masks = maskInputs(project, options, n + (project.music.isSet() ? 1 : 0), &args);
    int inputs = n + (project.music.isSet() ? 1 : 0);
    for (int m : masks)
        inputs += m >= 0;
    const QVector<int> texts = textInputs(project, options, inputs, &args);
    for (int t : texts)
        inputs += t >= 0;
    const QVector<int> layers = layerInputs(project, inputs, &args);

    QStringList graph = clipChains(project, true, true, kAudioFormat, masks, options.clipGrades);
    graph << joinClips(project, true, true);
    graph << layerChains(project, layers);
    graph << effectChains(project);
    graph << videoFinish(project, options, texts);

    QString voice = "[acat]";
    const QStringList layerSound = layerAudioChains(project, layers, &graph);
    if (!layerSound.isEmpty()) {
        // The main track sets the length; layer sound past the end is cut.
        graph << voice + layerSound.join(QString()) +
                     QString("amix=inputs=%1:duration=first:normalize=0[alay]").arg(layerSound.size() + 1);
        voice = "[alay]";
    }
    if (project.music.isSet()) {
        const int m = n;
        graph << QString("[%1:a]atrim=start=%2:duration=%3,asetpts=PTS-STARTPTS,volume=%4,%5[mus]")
                     .arg(m).arg(num(project.music.offset), num(total), num(project.music.volume), kAudioFormat);
        QString music = "[mus]";
        if (project.music.duck) {
            // The clips' sound pushes the music down while it's there.
            graph << voice + "asplit=2[voice][key]";
            graph << "[mus][key]sidechaincompress=threshold=0.03:ratio=8:attack=20:release=400[ducked]";
            voice = "[voice]";
            music = "[ducked]";
        }
        graph << voice + music + "amix=inputs=2:duration=first:normalize=0[mix]";
        voice = "[mix]";
    }
    // loudnorm works at 192 kHz internally; bring it back down for AAC.
    // A reel with no sound at all is left alone: there's nothing to level,
    // and loudnorm turns crossfaded digital silence into NaN samples that
    // the AAC encoder rejects.
    bool hasSound = project.music.isSet();
    for (const Clip &c : project.clips)
        hasSound = hasSound || (c.audio && !c.still);
    for (const Layer &l : project.layers)
        hasSound = hasSound || (l.clip.audio && !l.clip.still);
    graph << voice + (hasSound ? "loudnorm=I=-14:TP=-1.5:LRA=11," : QString()) + "aresample=48000[aout]";

    args << "-filter_complex" << graph.join(';');
    args << "-map" << "[vout]" << "-map" << "[aout]";
    if (options.encoder == "h264_nvenc")
        args << "-c:v" << "h264_nvenc" << "-preset" << "p5" << "-rc" << "vbr" << "-cq" << "18"
             << "-b:v" << "0" << "-maxrate" << "20M" << "-bufsize" << "40M";
    else
        args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "17";
    args << "-profile:v" << "high" << "-pix_fmt" << "yuv420p" << "-r" << QString::number(Project::Fps)
         << "-g" << QString::number(Project::Fps * 2)
         << "-c:a" << "aac" << "-b:a" << "192k" << "-ar" << "48000" << "-ac" << "2"
         << "-movflags" << "+faststart" << "-t" << num(total) << options.output;
    return args;
}

QStringList timelineAudioArgs(const Project &project, const QString &wav) {
    const QString mono = "aformat=sample_fmts=s16:sample_rates=16000:channel_layouts=mono";
    QStringList args{"-hide_banner", "-nostdin", "-y", "-progress", "pipe:1", "-nostats"};
    args << clipInputs(project);
    QStringList graph = clipChains(project, false, true, mono);
    graph << joinClips(project, false, true);
    args << "-filter_complex" << graph.join(';') << "-map" << "[acat]" << "-c:a" << "pcm_s16le" << wav;
    return args;
}

QStringList frameArgs(const Project &project, const ExportOptions &options, double time, const QString &png, int width) {
    QStringList args{"-hide_banner", "-nostdin", "-y", "-loglevel", "error"};
    args << clipInputs(project);
    const QVector<int> masks = maskInputs(project, options, project.clips.size(), &args);
    int inputs = project.clips.size();
    for (int m : masks)
        inputs += m >= 0;
    const QVector<int> texts = textInputs(project, options, inputs, &args);
    for (int t : texts)
        inputs += t >= 0;
    const QVector<int> layers = layerInputs(project, inputs, &args);
    QStringList graph = clipChains(project, true, false, {}, masks, options.clipGrades);
    graph << joinClips(project, true, false);
    graph << layerChains(project, layers);
    graph << effectChains(project);
    graph << videoFinish(project, options, texts);
    graph.last().replace("[vout]", QString(",scale=%1:-2[vout]").arg(width));
    // Seeking the output keeps every filter's timing (captions, speed) exact.
    args << "-filter_complex" << graph.join(';') << "-map" << "[vout]"
         << "-ss" << num(qBound(0.0, time, qMax(0.0, project.duration() - 1.0 / Project::Fps)))
         << "-frames:v" << "1" << png;
    return args;
}
