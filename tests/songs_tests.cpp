#include "songrequest.h"
#include <QtTest>

static QString field(const SongRequest::Form &f, const QString &key) {
    for (const auto &p : f.fields) if (p.first == key) return p.second;
    return QStringLiteral("<missing>");
}

class SongsTests : public QObject {
    Q_OBJECT
private slots:
    void emptyRequestIsRandomSample() {
        const auto f = SongRequest::build({});
        QCOMPARE(field(f, "sample_mode"), QString("true"));
        QCOMPARE(field(f, "sample_query"), QString(""));
        QCOMPARE(field(f, "batch_size"), QString("2"));
        QCOMPARE(field(f, "audio_format"), QString("mp3"));
        QCOMPARE(field(f, "thinking"), QString("true"));
        QCOMPARE(field(f, "audio_duration"), QString("<missing>"));
        QCOMPARE(field(f, "prompt"), QString("<missing>"));
        QVERIFY(f.referencePath.isEmpty() && f.sourcePath.isEmpty());
    }
    void promptAndStyleWithoutLyricsWritesWords() {
        const auto f = SongRequest::build({{"prompt", "a song about a cat"}, {"style", "pop"}, {"imageStyle", "neon, 120 bpm"}});
        QCOMPARE(field(f, "sample_mode"), QString("true"));
        QCOMPARE(field(f, "sample_query"), QString("a song about a cat. pop. neon, 120 bpm"));
        QCOMPARE(f.caption, QString("a song about a cat. pop. neon, 120 bpm"));
    }
    void lyricsAreSung() {
        const auto f = SongRequest::build({{"style", "rock"}, {"lyrics", "[Verse]\nhello"}});
        QCOMPARE(field(f, "sample_mode"), QString("<missing>"));
        QCOMPARE(field(f, "prompt"), QString("rock"));
        QCOMPARE(field(f, "lyrics"), QString("[Verse]\nhello"));
    }
    void instrumentalDropsLyrics() {
        const auto f = SongRequest::build({{"lyrics", "words"}, {"instrumental", true}});
        QCOMPARE(field(f, "lyrics"), QString("[Instrumental]"));
        QCOMPARE(field(f, "sample_mode"), QString("<missing>"));
    }
    void lengthAndVersionsAreClamped() {
        auto f = SongRequest::build({{"duration", 5.0}, {"versions", 9}});
        QCOMPARE(field(f, "audio_duration"), QString("10"));
        QCOMPARE(field(f, "batch_size"), QString("4"));
        f = SongRequest::build({{"duration", 999.0}, {"versions", 0}});
        QCOMPARE(field(f, "audio_duration"), QString("240"));
        QCOMPARE(field(f, "batch_size"), QString("1"));
    }
    void referenceIsAFile() {
        const auto f = SongRequest::build({{"reference", "/tmp/ref.mp3"}});
        QCOMPARE(f.referencePath, QString("/tmp/ref.mp3"));
    }
    void remixModes() {
        auto f = SongRequest::build({{"remix", "/tmp/a.mp3"}, {"remixMode", "cover"}, {"style", "jazz"}});
        QCOMPARE(f.sourcePath, QString("/tmp/a.mp3"));
        QCOMPARE(field(f, "task_type"), QString("cover"));
        QCOMPARE(field(f, "prompt"), QString("jazz"));
        QCOMPARE(field(f, "sample_mode"), QString("<missing>"));
        f = SongRequest::build({{"remix", "/tmp/a.mp3"}, {"remixMode", "extend"}});
        QCOMPARE(field(f, "task_type"), QString("complete"));
        f = SongRequest::build({{"remix", "/tmp/a.mp3"}, {"remixMode", "repaint"}, {"repaintStart", 4.0}, {"repaintEnd", 0.0}});
        QCOMPARE(field(f, "task_type"), QString("repaint"));
        QCOMPARE(field(f, "repainting_start"), QString("4"));
        QCOMPARE(field(f, "repainting_end"), QString("-1"));
    }
    void slugs() {
        QCOMPARE(SongRequest::slug({{"prompt", "A Song about my CAT, Milo!!"}}), QString("a-song-about-my"));
        QCOMPARE(SongRequest::slug({{"style", "lo-fi"}}), QString("lo-fi"));
        QCOMPARE(SongRequest::slug({}), QString("random"));
    }
};

QTEST_MAIN(SongsTests)
#include "songs_tests.moc"
