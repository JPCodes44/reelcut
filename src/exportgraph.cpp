#include "exportgraph.h"

#include <cmath>

namespace {
const QString kAudioFormat = "aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo";

int even(double value) {
    const int rounded = int(std::ceil(value));
    return rounded + (rounded % 2);
}

// One clip's video and audio chains, labelled [v<i>] and [a<i>].
QStringList clipChains(const Project &project, bool withVideo, bool withAudio, const QString &audioFormat) {
    QStringList chains;
    for (int i = 0; i < project.clips.size(); ++i) {
        const Clip &c = project.clips[i];
        if (withVideo && c.still) {
            // A looped photo: framed once, then moved over its duration.
            QString motion = motionFilter(c);
            chains << QString("[%1:v]trim=duration=%2,setpts=PTS-STARTPTS,%3%4,setsar=1,fps=%5,format=yuv420p[v%1]")
                          .arg(i).arg(num(c.duration()), framingFilter(c), motion.isEmpty() ? QString() : "," + motion)
                          .arg(Project::Fps);
        } else if (withVideo) {
            chains << QString("[%1:v]trim=start=%2:end=%3,setpts=(PTS-STARTPTS)/%4,%5,setsar=1,fps=%6,"
                              "format=yuv420p[v%1]")
                          .arg(i).arg(num(c.in), num(c.out), num(c.speed, 4), framingFilter(c))
                          .arg(Project::Fps);
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

QString concatInputs(int count, bool withVideo, bool withAudio) {
    QString inputs;
    for (int i = 0; i < count; ++i)
        inputs += (withVideo ? QString("[v%1]").arg(i) : QString()) + (withAudio ? QString("[a%1]").arg(i) : QString());
    return inputs;
}

// The look, then the LUT on top of it, then text, which must stay white.
QString videoFinish(const Project &project, const ExportOptions &options) {
    QStringList video;
    const Look &look = project.look;
    if (!look.isNeutral()) {
        video << QString("eq=brightness=%1:contrast=%2:saturation=%3")
                     .arg(num(look.exposure * 0.25), num(1.0 + look.contrast * 0.5), num(1.0 + look.saturation));
        if (look.temperature != 0.0)
            video << QString("colortemperature=temperature=%1").arg(qRound(6500 - look.temperature * 2500));
    }
    if (!options.lutFile.isEmpty())
        video << QString("lut3d=file=%1").arg(options.lutFile);
    if (!options.assFile.isEmpty())
        video << QString("ass=filename=%1").arg(options.assFile);
    return "[vcat]" + (video.isEmpty() ? QString("null") : video.join(',')) + "[vout]";
}

QStringList clipInputs(const Project &project) {
    QStringList args;
    for (const Clip &clip : project.clips) {
        // A photo becomes a stream of the same frame, as long as it shows.
        if (clip.still)
            args << "-loop" << "1" << "-framerate" << QString::number(Project::Fps) << "-t" << num(clip.duration() + 0.1);
        args << "-i" << clip.path;
    }
    return args;
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

    QStringList graph = clipChains(project, true, true, kAudioFormat);
    graph << concatInputs(n, true, true) + QString("concat=n=%1:v=1:a=1[vcat][acat]").arg(n);
    graph << videoFinish(project, options);

    QString voice = "[acat]";
    if (project.music.isSet()) {
        const int m = n;
        graph << QString("[%1:a]atrim=start=%2:duration=%3,asetpts=PTS-STARTPTS,volume=%4,%5[mus]")
                     .arg(m).arg(num(project.music.offset), num(total), num(project.music.volume), kAudioFormat);
        QString music = "[mus]";
        if (project.music.duck) {
            // The clips' sound pushes the music down while it's there.
            graph << "[acat]asplit=2[voice][key]";
            graph << "[mus][key]sidechaincompress=threshold=0.03:ratio=8:attack=20:release=400[ducked]";
            voice = "[voice]";
            music = "[ducked]";
        }
        graph << voice + music + "amix=inputs=2:duration=first:normalize=0[mix]";
        voice = "[mix]";
    }
    // loudnorm works at 192 kHz internally; bring it back down for AAC.
    graph << voice + "loudnorm=I=-14:TP=-1.5:LRA=11,aresample=48000[aout]";

    args << "-filter_complex" << graph.join(';');
    args << "-map" << "[vout]" << "-map" << "[aout]";
    if (options.encoder == "h264_nvenc")
        args << "-c:v" << "h264_nvenc" << "-preset" << "p5" << "-rc" << "vbr" << "-cq" << "21"
             << "-b:v" << "0" << "-maxrate" << "20M" << "-bufsize" << "40M";
    else
        args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "20";
    args << "-profile:v" << "high" << "-pix_fmt" << "yuv420p" << "-r" << QString::number(Project::Fps)
         << "-g" << QString::number(Project::Fps * 2)
         << "-c:a" << "aac" << "-b:a" << "192k" << "-ar" << "48000" << "-ac" << "2"
         << "-movflags" << "+faststart" << "-t" << num(total) << options.output;
    return args;
}

QStringList timelineAudioArgs(const Project &project, const QString &wav) {
    const QString mono = "aformat=sample_fmts=s16:sample_rates=16000:channel_layouts=mono";
    const int n = project.clips.size();
    QStringList args{"-hide_banner", "-nostdin", "-y", "-progress", "pipe:1", "-nostats"};
    args << clipInputs(project);
    QStringList graph = clipChains(project, false, true, mono);
    graph << concatInputs(n, false, true) + QString("concat=n=%1:v=0:a=1[aout]").arg(n);
    args << "-filter_complex" << graph.join(';') << "-map" << "[aout]" << "-c:a" << "pcm_s16le" << wav;
    return args;
}

QStringList frameArgs(const Project &project, const ExportOptions &options, double time, const QString &png, int width) {
    const int n = project.clips.size();
    QStringList args{"-hide_banner", "-nostdin", "-y", "-loglevel", "error"};
    args << clipInputs(project);
    QStringList graph = clipChains(project, true, false, {});
    graph << concatInputs(n, true, false) + QString("concat=n=%1:v=1:a=0[vcat]").arg(n);
    graph << videoFinish(project, options);
    graph.last().replace("[vout]", QString(",scale=%1:-2[vout]").arg(width));
    // Seeking the output keeps every filter's timing (captions, speed) exact.
    args << "-filter_complex" << graph.join(';') << "-map" << "[vout]"
         << "-ss" << num(qBound(0.0, time, qMax(0.0, project.duration() - 1.0 / Project::Fps)))
         << "-frames:v" << "1" << png;
    return args;
}
