#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>

#include "backend.h"
#include "captions.h"
#include "exportgraph.h"
#include "filepicker.h"
#include "mcpserver.h"
#include "media.h"
#include "project.h"
#include "silence.h"
#include "theme.h"
#include "transcript.h"
#include "translator.h"

namespace {
Clip clip(double in, double out, double speed = 1.0) {
    Clip c;
    c.path = "/videos/a.mp4";
    c.in = in;
    c.out = out;
    c.speed = speed;
    c.width = 1920;
    c.height = 1080;
    c.sourceDuration = out;
    return c;
}

// Answers every dialog with a preset file, the way the portal would.
class FakePicker : public FilePicker {
public:
    QString answer;
    void open(Kind kind) override { emit selected(kind, {QUrl::fromLocalFile(answer)}); }
    void save(Kind kind, const QUrl &) override { emit selected(kind, {QUrl::fromLocalFile(answer)}); }
};

bool runFfmpeg(const QStringList &args) {
    QProcess proc;
    proc.start("ffmpeg", args);
    return proc.waitForFinished(120000) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}
}  // namespace

class ReelcutTests : public QObject {
    Q_OBJECT

private slots:
    // Theme and translator, from the Omarchy app template.

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    // Project

    void durationCountsSpeed()
    {
        Project p;
        p.clips = {clip(0, 4), clip(10, 14, 2.0)};
        QCOMPARE(p.duration(), 6.0);
        QCOMPARE(p.clipStart(1), 4.0);
    }

    void locateMapsTimelineToSource()
    {
        Project p;
        p.clips = {clip(0, 4), clip(10, 14, 2.0)};
        QCOMPARE(p.locate(1.0).index, 0);
        QCOMPARE(p.locate(1.0).source, 1.0);
        QCOMPARE(p.locate(5.0).index, 1);
        QCOMPARE(p.locate(5.0).source, 12.0);
        // The very end belongs to the last clip.
        QCOMPARE(p.locate(6.0).index, 1);
        QCOMPARE(p.locate(6.0).source, 14.0);
        QCOMPARE(Project().locate(0).index, -1);
    }

    void splitCutsTheClipUnderThePlayhead()
    {
        Project p;
        p.clips = {clip(0, 4), clip(10, 14, 2.0)};
        QVERIFY(p.split(5.0));
        QCOMPARE(p.clips.size(), 3);
        QCOMPARE(p.clips[1].out, 12.0);
        QCOMPARE(p.clips[2].in, 12.0);
        QCOMPARE(p.duration(), 6.0);
        // On an edge there is nothing to split.
        QVERIFY(!p.split(4.0));
        QVERIFY(!p.split(0.0));
    }

    void replaceKeepsFramingAndDropsSlivers()
    {
        Project p;
        p.clips = {clip(0, 10)};
        p.clips[0].zoom = 1.5;
        p.replaceClipWithRanges(0, {{0, 2}, {3, 3.01}, {5, 12}});
        QCOMPARE(p.clips.size(), 2);
        QCOMPARE(p.clips[1].in, 5.0);
        QCOMPARE(p.clips[1].out, 10.0);
        QCOMPARE(p.clips[1].zoom, 1.5);
    }

    void jsonRoundTripsWithRelativePaths()
    {
        Project p;
        p.clips = {clip(1, 3, 1.5)};
        p.clips[0].path = "/home/me/reels/clips/a.mp4";
        p.clips[0].panX = -0.5;
        p.music.path = "/home/me/reels/song.mp3";
        p.overlays = {{"Wait for it", 0, 2, 0.5, 0.2, 100, "slide"}};
        p.words = {{"hello", 0.1, 0.4}};
        p.look.saturation = 0.3;
        p.captionStyle = "pop";

        const QJsonObject json = p.toJson("/home/me/reels");
        QCOMPARE(json.value("clips").toArray().first().toObject().value("path").toString(), QString("clips/a.mp4"));

        const Project back = Project::fromJson(json, "/home/me/reels");
        QCOMPARE(back.clips[0].path, QString("/home/me/reels/clips/a.mp4"));
        QCOMPARE(back.clips[0].speed, 1.5);
        QCOMPARE(back.clips[0].panX, -0.5);
        QCOMPARE(back.music.path, QString("/home/me/reels/song.mp3"));
        QCOMPARE(back.overlays[0].animation, QString("slide"));
        QCOMPARE(back.words[0].text, QString("hello"));
        QCOMPARE(back.look.saturation, 0.3);
        QCOMPARE(back.captionStyle, QString("pop"));
    }

    // Silence

    void parsesSilencedetectOutput()
    {
        const QByteArray log =
            "[silencedetect @ 0x5581] silence_start: 1.2\n"
            "[silencedetect @ 0x5581] silence_end: 2.5 | silence_duration: 1.3\n"
            "size=N/A time=00:00:05.00\n"
            "[silencedetect @ 0x5581] silence_start: 4.1\n";
        const auto silences = parseSilences(log);
        QCOMPARE(silences.size(), 2);
        QCOMPARE(silences[0], Range(1.2, 2.5));
        QCOMPARE(silences[1].first, 4.1);
        QVERIFY(std::isinf(silences[1].second));
    }

    void keepRangesPadsAndDropsShortBits()
    {
        const auto kept = keepRanges(0, 6, {{1.0, 2.0}, {2.1, 2.2}, {4.0, INFINITY}});
        // The 0.1 s silence vanishes into its padding; the trailing one cuts the end.
        QCOMPARE(kept.size(), 2);
        QCOMPARE(kept[0], Range(0.0, 1.1));
        QCOMPARE(kept[1], Range(1.9, 4.1));
    }

    // Captions

    void groupsWordsIntoShortLines()
    {
        const QVector<Word> words = {{"this", 0, .2}, {"is", .2, .3}, {"really", .3, .6},
                                     {"cool", .6, .9}, {"after", 2.0, 2.3}, {"pause", 2.3, 2.6}};
        const auto lines = groupLines(words);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].text(), QString("this is really"));
        QCOMPARE(lines[1].text(), QString("cool"));
        QCOMPARE(lines[2].text(), QString("after pause"));
        // A line holds until a pause, then ends with its last word.
        QCOMPARE(lines[1].end, 0.9);

        const auto byChars = groupLines({{"extraordinarily", 0, 1}, {"long", 1, 2}});
        QCOMPARE(byChars.size(), 2);
    }

    void formatsAssTime()
    {
        QCOMPARE(assTime(0), QString("0:00:00.00"));
        QCOMPARE(assTime(61.237), QString("0:01:01.24"));
        QCOMPARE(assTime(3725.5), QString("1:02:05.50"));
    }

    void highlightStyleLightsEachWord()
    {
        Project p;
        p.words = {{"hey", 0, .3}, {"you", .3, .6}};
        p.overlays = {{"Watch {this}", 0, 2, 0.5, 0.2, 96, "pop"}};
        const QString ass = assDocument(p, "#FF0000");
        QCOMPARE(ass.count("Style: Caption"), 1);
        QCOMPARE(ass.count(",Caption,"), 2);
        QVERIFY(ass.contains("{\\c&H0000FF&}HEY{\\c&HFFFFFF&} YOU"));
        QVERIFY(ass.contains("HEY {\\c&H0000FF&}YOU{\\c&HFFFFFF&}"));
        // User text can't open override tags.
        QVERIFY(ass.contains("}Watch (this)"));
        QVERIFY(ass.contains("PlayResY: 1920"));
    }

    // Transcript

    void parsesWhisperWords()
    {
        const QByteArray json = R"({"transcription":[
            {"offsets":{"from":0,"to":320},"text":" Hello"},
            {"offsets":{"from":320,"to":330},"text":","},
            {"offsets":{"from":400,"to":900},"text":" world"},
            {"offsets":{"from":900,"to":2000},"text":" [BLANK_AUDIO]"}]})";
        const auto words = parseWhisperJson(json);
        QCOMPARE(words.size(), 2);
        QCOMPARE(words[0].text, QString("Hello,"));
        QCOMPARE(words[0].end, 0.33);
        QCOMPARE(words[1].start, 0.4);
    }

    // Export graph

    void framingCoversTheFrame()
    {
        Clip landscape = clip(0, 1);
        QCOMPARE(framingFilter(landscape), QString("scale=3414:1920:flags=lanczos,crop=1080:1920:1167:0"));
        landscape.panX = -1.0;
        QVERIFY(framingFilter(landscape).endsWith("crop=1080:1920:0:0"));
        landscape.panX = 1.0;
        QVERIFY(framingFilter(landscape).endsWith("crop=1080:1920:2334:0"));

        Clip portrait = clip(0, 1);
        portrait.width = 1080;
        portrait.height = 1920;
        QCOMPARE(framingFilter(portrait), QString("scale=1080:1920:flags=lanczos,crop=1080:1920:0:0"));
        portrait.zoom = 2.0;
        QCOMPARE(framingFilter(portrait), QString("scale=2160:3840:flags=lanczos,crop=1080:1920:540:960"));
    }

    void atempoStaysInRange()
    {
        QCOMPARE(atempoChain(2.5), QString("atempo=2.5"));
        QCOMPARE(atempoChain(0.25), QString("atempo=0.5,atempo=0.5"));
        QCOMPARE(atempoChain(0.3), QString("atempo=0.5,atempo=0.6"));
    }

    void graphCoversMusicLookAndSilentClips()
    {
        Project p;
        p.clips = {clip(0, 2), clip(5, 7, 2.0)};
        p.clips[1].audio = false;
        p.music.path = "/music/song.mp3";
        p.look.lut = "/luts/film.cube";
        ExportOptions options{"/out/reel.mp4", "captions.ass", "look.cube", "libx264"};

        const QStringList args = exportArgs(p, options);
        const QString graph = args.at(args.indexOf("-filter_complex") + 1);
        QVERIFY(graph.contains("anullsrc=r=48000:cl=stereo,atrim=duration=1,"));
        QVERIFY(graph.contains("setpts=(PTS-STARTPTS)/2,"));
        QVERIFY(graph.contains("[2:a]atrim=start=0:duration=3,"));
        QVERIFY(graph.contains("sidechaincompress"));
        QVERIFY(graph.contains("[vcat]lut3d=file=look.cube,ass=filename=captions.ass[vout]"));
        QVERIFY(!graph.contains("eq="));
        QVERIFY(args.contains("libx264"));
        QCOMPARE(args.last(), QString("/out/reel.mp4"));

        p.music.duck = false;
        p.look.saturation = 0.5;
        const QStringList plain = exportArgs(p, {"/out/reel.mp4", {}, {}, "h264_nvenc"});
        const QString plainGraph = plain.at(plain.indexOf("-filter_complex") + 1);
        QVERIFY(!plainGraph.contains("sidechaincompress"));
        QVERIFY(plainGraph.contains("[acat][mus]amix"));
        QVERIFY(plainGraph.contains("eq=brightness=0:contrast=1:saturation=1.5"));
    }

    // End to end: ffmpeg renders a real reel from a generated landscape clip.
    void exportsAPlayableReel()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("source.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=1280x720:rate=30:duration=4", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=4", "-shortest", "-c:v", "libx264", "-c:a", "aac",
                           source}));
        const media::VideoInfo info = media::probe(source);
        QVERIFY2(info.ok, qPrintable(info.error));

        Project p;
        Clip c;
        c.path = source;
        c.out = info.duration;
        c.width = info.width;
        c.height = info.height;
        c.audio = info.audio;
        p.clips = {c};
        QVERIFY(p.split(2.0));
        p.clips[1].speed = 2.0;
        p.words = {{"it", 0.2, 0.5}, {"works", 0.5, 1.0}};
        p.look.contrast = 0.2;
        write(dir.filePath("captions.ass"), assDocument(p, "#FFD60A").toUtf8());

        QProcess proc;
        proc.setWorkingDirectory(dir.path());
        proc.start("ffmpeg", exportArgs(p, {dir.filePath("reel.mp4"), "captions.ass", {}, "libx264"}));
        QVERIFY(proc.waitForFinished(180000));
        QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().right(2000).constData());

        const media::VideoInfo out = media::probe(dir.filePath("reel.mp4"));
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.width, 1080);
        QCOMPARE(out.height, 1920);
        QVERIFY(out.audio);
        QVERIFY(qAbs(out.duration - 3.0) < 0.15);
    }

    // Transcription input: the clips' sound at timeline speed, as Whisper wants it.
    void timelineAudioIsWhisperReady()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("a.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=320x240:rate=30:duration=4", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=4", "-shortest", "-c:v", "libx264", "-c:a", "aac", source}));
        Project p;
        Clip a = clip(0, 2), b = clip(2, 4, 2.0);
        a.path = b.path = source;
        p.clips = {a, b};
        QVERIFY(runFfmpeg(timelineAudioArgs(p, dir.filePath("audio.wav"))));
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-show_entries", "stream=sample_rate,channels:format=duration",
                                "-of", "csv=p=0", dir.filePath("audio.wav")});
        QVERIFY(probe.waitForFinished(10000));
        const QString out = QString::fromUtf8(probe.readAllStandardOutput());
        QVERIFY2(out.contains("16000,1"), qPrintable(out));
        QVERIFY2(qAbs(out.split('\n').value(1).toDouble() - 3.0) < 0.1, qPrintable(out));
    }

    // Backend

    void undoAndRedoRestoreEdits()
    {
        QTemporaryDir dir;
        Project p;
        p.clips = {clip(0, 4), clip(10, 14)};
        write(dir.filePath("a.reel"), QJsonDocument(p.toJson()).toJson());

        QTemporaryDir state;
        Backend backend(nullptr, nullptr, nullptr, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("a.reel")));
        QVERIFY(!backend.dirty());
        QVERIFY(backend.split(2.0));
        QCOMPARE(backend.project().clips.size(), 3);
        QVERIFY(backend.dirty());

        // A slider drag is one step however many values it sets.
        backend.checkpoint();
        backend.setClipValue(0, "speed", 1.5);
        backend.setClipValue(0, "speed", 9.0);
        QCOMPARE(backend.project().clips[0].speed, 4.0);

        backend.undo();
        QCOMPARE(backend.project().clips[0].speed, 1.0);
        QCOMPARE(backend.project().clips.size(), 3);
        backend.undo();
        QCOMPARE(backend.project().clips.size(), 2);
        QVERIFY(!backend.canUndo());
        backend.redo();
        QCOMPARE(backend.project().clips.size(), 3);
    }

    void cutsSilencesFromAClip()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("talk.mp4");
        // A second of tone, a second and a half of silence, a second of tone.
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "color=c=gray:size=320x240:rate=30:duration=3.5", "-f", "lavfi", "-i",
                           "aevalsrc='if(between(t,1,2.5),0,0.5*sin(440*2*PI*t))':d=3.5", "-shortest",
                           "-c:v", "libx264", "-c:a", "aac", source}));
        Project p;
        Clip c = clip(0, 3.5);
        c.path = source;
        p.clips = {c};
        write(dir.filePath("talk.reel"), QJsonDocument(p.toJson()).toJson());

        QTemporaryDir state;
        Backend backend(nullptr, nullptr, nullptr, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("talk.reel")));
        backend.cutSilences(0);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 30000);
        QCOMPARE(backend.project().clips.size(), 2);
        // 1.5 s of silence goes, minus 0.1 s of padding kept on each side.
        QVERIFY2(qAbs(backend.project().duration() - 2.2) < 0.15,
                 qPrintable(QString::number(backend.project().duration())));
        backend.undo();
        QCOMPARE(backend.project().clips.size(), 1);
    }

    void savesAndExportsThroughTheDialogs()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("clip.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=640x360:rate=30:duration=2", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=2", "-shortest", "-c:v", "libx264", "-c:a", "aac",
                           source}));
        FakePicker picker;
        QTemporaryDir state;
        Backend backend(&picker, nullptr, nullptr, nullptr, state.path());
        backend.addUrls({QUrl::fromLocalFile(source)});
        QTRY_COMPARE_WITH_TIMEOUT(backend.project().clips.size(), 1, 15000);
        backend.addOverlay(0.0);

        picker.answer = dir.filePath("mine");  // the extension is added
        backend.saveAs();
        QVERIFY(QFileInfo::exists(dir.filePath("mine.reel")));
        QVERIFY(!backend.dirty());
        QCOMPARE(backend.title(), QString("mine"));

        picker.answer = dir.filePath("out.mp4");
        backend.exportVideo();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 120000);
        QVERIFY2(!backend.messageIsError(), qPrintable(backend.message()));
        QCOMPARE(backend.lastExport(), dir.filePath("out.mp4"));
        const media::VideoInfo out = media::probe(dir.filePath("out.mp4"));
        QVERIFY(out.ok);
        QCOMPARE(out.height, 1920);
    }

    // Photos

    void photosBecomeTimedSlides()
    {
        QTemporaryDir dir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("a.jpg")}));
        const media::VideoInfo info = media::probe(dir.filePath("a.jpg"));
        QVERIFY2(info.ok, qPrintable(info.error));
        QVERIFY(info.still);
        QVERIFY(!info.audio);
        // Timeline thumbnails of photos come from frame 0.
        QVERIFY(!media::thumbnail(dir.filePath("a.jpg"), 0.0, 90).isNull());
        const Clip clip = media::clipFor(info, 3.0);
        QVERIFY(clip.still);
        QCOMPARE(clip.duration(), 3.0);
        QCOMPARE(clip.motion, QString("zoom-in"));

        Project p;
        p.clips = {clip};
        QVERIFY(p.setClipValue(0, "duration", 4.5));
        QCOMPARE(p.clips[0].duration(), 4.5);
        QVERIFY(!p.setClipValue(0, "speed", 2.0));
        QVERIFY(p.setClipMotion(0, "pan-left"));
        QVERIFY(!p.setClipMotion(0, "spin"));
        const Project back = Project::fromJson(p.toJson());
        QVERIFY(back.clips[0].still);
        QCOMPARE(back.clips[0].motion, QString("pan-left"));
        QCOMPARE(back.clips[0].duration(), 4.5);
    }

    void motionFilterMovesPhotosOnly()
    {
        Clip photo = clip(0, 2);
        photo.still = true;
        photo.motion = "zoom-in";
        QVERIFY(motionFilter(photo).contains("zoompan=z='1+0.12*on/60'"));
        QVERIFY(motionFilter(photo).contains("s=1080x1920"));
        photo.motion = "pan-right";
        QVERIFY(motionFilter(photo).contains("x='(iw-iw/zoom)*on/60'"));
        photo.motion = "none";
        QVERIFY(motionFilter(photo).isEmpty());
        QVERIFY(motionFilter(clip(0, 2)).isEmpty());
    }

    void exportsASlideshowAndAPhotoSet()
    {
        QTemporaryDir dir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("wide.jpg")}));
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "mandelbrot=size=1080x1350",
                           "-frames:v", "1", dir.filePath("tall.png")}));
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=640x360:rate=30:duration=2", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=2", "-shortest", "-c:v", "libx264", "-c:a", "aac",
                           dir.filePath("video.mp4")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("wide.jpg")), 2.0),
                   media::clipFor(media::probe(dir.filePath("tall.png")), 1.5),
                   media::clipFor(media::probe(dir.filePath("video.mp4")))};
        p.clips[1].motion = "pan-right";
        p.overlays = {{"Slide one", 0, 2, 0.5, 0.2, 96, "pop"}};

        QString encoder, error;
        QVERIFY2(exportProject(p, "#FFD60A", dir.filePath("show.mp4"), &encoder, &error), qPrintable(error));
        const media::VideoInfo out = media::probe(dir.filePath("show.mp4"));
        QCOMPARE(out.width, 1080);
        QCOMPARE(out.height, 1920);
        QVERIFY(out.audio);
        QVERIFY2(qAbs(out.duration - 5.5) < 0.15, qPrintable(QString::number(out.duration)));

        const QStringList files = exportPhotoSet(p, "#FFD60A", dir.filePath("set"), &error);
        QVERIFY2(files.size() == 3, qPrintable(error));
        QCOMPARE(QFileInfo(files[0]).fileName(), QString("01.jpg"));
        QImage first(files[0]);
        QCOMPARE(first.size(), QSize(1080, 1920));
    }

    // MCP server

    void mcpSpeaksTheProtocol()
    {
        QTemporaryDir state;
        McpServer server(state.path());
        const QJsonObject init = server.handle({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
                                                {"params", QJsonObject{{"protocolVersion", "2025-06-18"}}}});
        QCOMPARE(init.value("result").toObject().value("serverInfo").toObject().value("name").toString(), QString("reelcut"));
        QVERIFY(server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}).isEmpty());

        const QJsonArray tools = server.handle({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}})
                                     .value("result").toObject().value("tools").toArray();
        QStringList names;
        for (const QJsonValue &tool : tools)
            names.append(tool.toObject().value("name").toString());
        for (const char *name : {"create_project", "add_clips", "split", "edit_clip", "cut_silences", "transcribe",
                                 "set_captions", "edit_words", "add_text", "set_look", "set_music", "undo",
                                 "preview_frame", "export", "show_in_app"})
            QVERIFY2(names.contains(name), name);

        const QJsonObject unknown = server.handle({{"jsonrpc", "2.0"}, {"id", 3}, {"method", "nope"}});
        QCOMPARE(unknown.value("error").toObject().value("code").toInt(), -32601);
        // Without a project, tools say what to do instead of failing obscurely.
        const QJsonObject noProject = call(server, "get_project", {});
        QVERIFY(noProject.value("isError").toBool());
    }

    void mcpEditsPreviewsAndExportsAReel()
    {
        QTemporaryDir dir, state;
        const QString source = dir.filePath("clip.mp4");
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                           "testsrc2=size=1280x720:rate=30:duration=4", "-f", "lavfi", "-i",
                           "sine=frequency=440:duration=4", "-shortest", "-c:v", "libx264", "-c:a", "aac", source}));
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=220:duration=10",
                           dir.filePath("song.mp3")}));
        McpServer server(state.path());

        QJsonObject r = call(server, "create_project", {{"path", dir.filePath("demo")}, {"clips", QJsonArray{source}},
                                                         {"platform", "tiktok"}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        QVERIFY(QFileInfo::exists(dir.filePath("demo.reel")));

        // The rest use the project in use without naming it.
        QVERIFY(!call(server, "split", {{"time", 2.0}}).value("isError").toBool());
        QVERIFY(!call(server, "edit_clip", {{"index", 1}, {"speed", 2.0}, {"zoom", 9.0}}).value("isError").toBool());
        QVERIFY(call(server, "edit_clip", {{"index", 7}}).value("isError").toBool());
        QVERIFY(!call(server, "set_captions", {{"style", "pop"}, {"words", QJsonArray{
            QJsonObject{{"text", "made"}, {"start", 0.2}, {"end", 0.6}},
            QJsonObject{{"text", "by"}, {"start", 0.6}, {"end", 0.9}},
            QJsonObject{{"text", "agnet"}, {"start", 0.9}, {"end", 1.4}}}}}).value("isError").toBool());
        QVERIFY(!call(server, "edit_words", {{"edits", QJsonArray{QJsonObject{{"index", 2}, {"text", "agent"}}}}})
                     .value("isError").toBool());
        QVERIFY(!call(server, "add_text", {{"text", "Watch this"}, {"end", 1.5}, {"animation", "slide"}}).value("isError").toBool());
        QVERIFY(!call(server, "set_look", {{"preset", "warm"}, {"saturation", 0.5}}).value("isError").toBool());
        QVERIFY(!call(server, "set_music", {{"file", dir.filePath("song.mp3")}, {"volume", 0.2}}).value("isError").toBool());

        Project saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("demo.reel"))).object(), dir.path());
        QCOMPARE(saved.platform, QString("tiktok"));
        QCOMPARE(saved.clips.size(), 2);
        QCOMPARE(saved.clips[1].speed, 2.0);
        QCOMPARE(saved.clips[1].zoom, 3.0);
        QCOMPARE(saved.words[2].text, QString("agent"));
        QCOMPARE(saved.overlays[0].animation, QString("slide"));
        QCOMPARE(saved.look.temperature, 0.4);
        QCOMPARE(saved.look.saturation, 0.5);
        QCOMPARE(saved.music.volume, 0.2);

        // Undo walks back one tool call at a time.
        QVERIFY(!call(server, "undo", {}).value("isError").toBool());
        saved = Project::fromJson(QJsonDocument::fromJson(readAll(dir.filePath("demo.reel"))).object(), dir.path());
        QVERIFY(!saved.music.isSet());
        QVERIFY(!call(server, "redo", {}).value("isError").toBool());

        const QJsonObject frame = call(server, "preview_frame", {{"time", 1.0}, {"width", 270}});
        QVERIFY2(!frame.value("isError").toBool(), qPrintable(textOf(frame)));
        const QJsonObject image = frame.value("content").toArray().first().toObject();
        QCOMPARE(image.value("type").toString(), QString("image"));
        QImage png;
        QVERIFY(png.loadFromData(QByteArray::fromBase64(image.value("data").toString().toLatin1()), "PNG"));
        QCOMPARE(png.size(), QSize(270, 480));

        const QJsonObject exported = call(server, "export", {});
        QVERIFY2(!exported.value("isError").toBool(), qPrintable(textOf(exported)));
        const QJsonObject info = QJsonDocument::fromJson(textOf(exported).toUtf8()).object();
        QCOMPARE(info.value("output").toString(), dir.filePath("demo-tiktok.mp4"));
        QCOMPARE(info.value("size").toString(), QString("1080x1920"));
        QVERIFY(qAbs(info.value("duration").toDouble() - 3.0) < 0.15);
    }

    void mcpBuildsAPhotoSlideshow()
    {
        QTemporaryDir dir, state;
        QStringList photos;
        for (int i = 0; i < 3; ++i) {
            photos << dir.filePath(QString("p%1.jpg").arg(i));
            QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                               QString("color=c=0x%1:size=1200x1600").arg(i == 0 ? "aa3333" : i == 1 ? "33aa33" : "3333aa"),
                               "-frames:v", "1", photos.last()}));
        }
        McpServer server(state.path());
        QVERIFY(!call(server, "create_project", {{"path", dir.filePath("slides")}}).value("isError").toBool());
        QJsonObject r = call(server, "add_clips", {{"files", QJsonArray::fromStringList(photos)}, {"photo_seconds", 2.0},
                                                   {"motion", "none"}});
        QVERIFY2(!r.value("isError").toBool(), qPrintable(textOf(r)));
        QVERIFY(!call(server, "edit_clip", {{"index", 1}, {"duration", 3.0}, {"motion", "zoom-out"}}).value("isError").toBool());
        QVERIFY(!call(server, "add_text", {{"text", "my handwriting\nthen vs now"}, {"end", 2.0}}).value("isError").toBool());

        const QJsonObject summary = QJsonDocument::fromJson(textOf(call(server, "get_project", {})).toUtf8()).object();
        QCOMPARE(summary.value("duration").toDouble(), 7.0);
        QVERIFY(summary.value("clips").toArray().at(0).toObject().value("photo").toBool());
        QCOMPARE(summary.value("clips").toArray().at(1).toObject().value("motion").toString(), QString("zoom-out"));

        const QJsonObject set = call(server, "export_photos", {});
        QVERIFY2(!set.value("isError").toBool(), qPrintable(textOf(set)));
        const QJsonObject result = QJsonDocument::fromJson(textOf(set).toUtf8()).object();
        QCOMPARE(result.value("files").toArray().size(), 3);
        QVERIFY(QFileInfo::exists(dir.filePath("slides-photos/03.jpg")));
    }

    void windowFollowsEditsMadeElsewhere()
    {
        QTemporaryDir dir, state;
        Project p;
        p.clips = {clip(0, 4)};
        write(dir.filePath("live.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        Backend backend(nullptr, nullptr, nullptr, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("live.reel")));
        // The window records what it has open, so the MCP server finds it.
        QCOMPARE(QString::fromUtf8(readAll(McpServer::openProjectFile(state.path()))), dir.filePath("live.reel"));

        McpServer server(state.path());
        QVERIFY(!call(server, "open_project", {}).value("isError").toBool());
        QVERIFY(!call(server, "split", {{"time", 1.0}}).value("isError").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(backend.project().clips.size(), 2, 5000);
        QVERIFY(!backend.dirty());
        // The outside edit is one undo away in the window.
        backend.undo();
        QCOMPARE(backend.project().clips.size(), 1);
    }

    // The real window, offscreen: everything on the picture moves and resizes
    // with the mouse, one undo step per gesture, Ctrl+Z / Ctrl+Y.
    void previewItemsDragAndResize()
    {
        QTemporaryDir dir, state, themeDir;
        QVERIFY(runFfmpeg({"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=1600x1200",
                           "-frames:v", "1", dir.filePath("photo.jpg")}));
        Project p;
        p.clips = {media::clipFor(media::probe(dir.filePath("photo.jpg")), 4.0)};
        p.clips[0].motion = "none";
        p.clips[0].zoom = 1.5;
        p.clips.append(media::clipFor(media::probe(dir.filePath("photo.jpg")), 2.0));
        p.overlays = {{"drag me", 0, 4, 0.5, 0.3, 96, "none"}};
        p.overlays[0].x = 0.5;
        p.words = {{"hello", 0.0, 2.0}, {"there", 2.0, 4.0}};
        write(dir.filePath("ui.reel"), QJsonDocument(p.toJson(dir.path())).toJson());

        QQuickStyle::setStyle("Material");
        Theme theme(themeDir.path());
        Backend backend(nullptr, nullptr, &theme, nullptr, state.path());
        QVERIFY(backend.openFile(dir.filePath("ui.reel")));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1360, 860);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, 1.0));
        QTest::qWait(300);

        // Repeater delegates aren't QObject children, so walk the visual tree.
        std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (QQuickItem *child : item->childItems())
                if (QQuickItem *found = find(child, name))
                    return found;
            return nullptr;
        };
        auto center = [&](const char *name) {
            auto *item = find(window->contentItem(), name);
            if (!item || !item->isVisible())
                return QPoint(-1, -1);
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        };
        auto drag = [&](QPoint from, QPoint to) {
            QTest::mousePress(window, Qt::LeftButton, {}, from);
            for (int i = 1; i <= 8; ++i) {
                QTest::mouseMove(window, from + (to - from) * i / 8);
                QTest::qWait(15);
            }
            QTest::mouseRelease(window, Qt::LeftButton, {}, to);
            QTest::qWait(30);
        };

        // Drag the text overlay right and down.
        const QPoint text = center("overlayBox0");
        QVERIFY(text.x() > 0);
        drag(text, text + QPoint(60, 40));
        QTRY_VERIFY(backend.project().overlays[0].x > 0.6);
        QVERIFY(backend.project().overlays[0].y > 0.33);

        // Pull its bottom-right corner out to make it bigger.
        auto *box = find(window->contentItem(), "overlayBox0");
        const QPoint corner = box->mapToScene(QPointF(box->width(), box->height())).toPoint();
        drag(corner, corner + QPoint(50, 30));
        QTRY_VERIFY(backend.project().overlays[0].size > 110);

        // Ctrl+Z undoes the resize, then the move; Ctrl+Y brings the move back.
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(backend.project().overlays[0].size, 96);
        QVERIFY(backend.project().overlays[0].x > 0.6);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(backend.project().overlays[0].x, 0.5);
        QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier);
        QTRY_VERIFY(backend.project().overlays[0].x > 0.6);

        // Captions move too.
        const QPoint caption = center("captionBox");
        QVERIFY(caption.x() > 0);
        drag(caption, caption + QPoint(0, -80));
        QTRY_VERIFY(backend.project().captionY < 0.6);

        // Dragging the picture pans it; the wheel zooms it.
        const QPoint picture = center("clipBox") + QPoint(0, 150);
        drag(picture, picture + QPoint(-40, 0));
        QTRY_VERIFY(backend.project().clips[0].panX > 0.05);
        QTest::wheelEvent(window, picture, QPoint(0, 240), QPoint(0, 0));
        QTRY_VERIFY(backend.project().clips[0].zoom > 1.5);

        // In the timeline, dragging a text bar moves it later without changing its length.
        auto *bar = find(window->contentItem(), "textBar0");
        QVERIFY(bar);
        const QPoint barCenter = bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2)).toPoint();
        drag(barCenter, barCenter + QPoint(-200, 0));  // can't go before 0
        QTRY_COMPARE(backend.project().overlays[0].start, 0.0);
        const Overlay moved = backend.project().overlays[0];
        QCOMPARE(moved.end - moved.start, 4.0);

        // Dragging the first clip past the second swaps them.
        auto *first = find(window->contentItem(), "clip0");
        QVERIFY(first);
        const QPoint from = first->mapToScene(QPointF(first->width() / 2, first->height() / 2)).toPoint();
        drag(from, from + QPoint(int(first->width() * 0.9), 0));
        QTRY_COMPARE(backend.project().clips[0].duration(), 2.0);
        QCOMPARE(backend.project().clips[1].duration(), 4.0);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(backend.project().clips[0].duration(), 4.0);
    }

private:
    static QJsonObject call(McpServer &server, const QString &tool, const QJsonObject &args)
    {
        return server.handle({{"jsonrpc", "2.0"}, {"id", 9}, {"method", "tools/call"},
                              {"params", QJsonObject{{"name", tool}, {"arguments", args}}}})
            .value("result").toObject();
    }

    static QString textOf(const QJsonObject &result)
    {
        for (const QJsonValue &item : result.value("content").toArray())
            if (item.toObject().value("type") == "text")
                return item.toObject().value("text").toString();
        return {};
    }

    static QByteArray readAll(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(ReelcutTests)
#include "reelcut_tests.moc"
