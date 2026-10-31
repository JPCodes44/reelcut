#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include "captions.h"
#include "exportgraph.h"
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
        p.overlays = {{"Wait for it", 0, 2, 0.2, 100, "slide"}};
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
        p.overlays = {{"Watch {this}", 0, 2, 0.2, 96, "pop"}};
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

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(ReelcutTests)
#include "reelcut_tests.moc"
