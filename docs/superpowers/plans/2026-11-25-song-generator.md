# Song Generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Songs page in Reelcut that generates songs with ACE-Step 1.5 on a Modal L4 GPU, plus a top-bar meter of Modal spend against the $30 free monthly credit.

**Architecture:** ACE-Step's own REST API server runs on Modal behind proxy auth. Reelcut talks to it over HTTPS from a new C++ `SongGenerator`, records estimated cost in a new `ModalUsage` ledger, and shows both through two new QML files. New code lives in new files with its own qrc and its own test executable, so the uncommitted work of other sessions in `resources.qrc`, `tests/reelcut_tests.cpp`, `automationregistry.cpp` and friends is not touched until the final merge.

**Tech Stack:** C++17, Qt 6 (Quick, Network, Multimedia, Test), qmake; Python + Modal for the cloud app; ACE-Step 1.5 at commit `ca1e85fe9430179831e6bc6be790c332190a3866`.

## Global Constraints

- Every request field is optional; an empty request still generates (random song).
- GPU `L4`, `max_containers=1`, `scaledown_window=120`, `timeout=600`, proxy auth required.
- Cost rate: GPU $0.000222/s + 4 cores × $0.0000131/s + 16 GiB × $0.00000222/s (≈ $0.000310/s). Idle tail 120 s.
- Meter limit default $30/month; orange at 80 %, red at 100 %; confirm before generating when over.
- Songs folder: `~/Videos/Reelcut songs/<yyyy-MM-dd-HHmm>-<slug>/` with `take-N.mp3` and `request.json`.
- Config: `~/.config/reelcut/modal.json` (mode 600), ledger `modal-usage.json`, status `modal-status.json`.
- Versions 1–4 (default 2); length 10–240 s or empty for auto.
- Work in worktree `~/reelcut-song-generator` on branch `song-generator`. Never stage files you did not create or deliberately edit.

## Deviations from the spec (decided while planning)

1. **Modal connection settings live in the meter's popup**, not a new Settings section. `SettingsPage.qml` has tabs tied to account automation; the meter popup already owns sign-in for Higgsfield, so Modal does the same.
2. **Empty lyrics without Instrumental uses ACE-Step `sample_mode`** with `sample_query` = prompt + style. That one call covers both "random song" (everything empty) and "write the words for me", replacing the separate `/create_random_sample` call.
3. **New QML goes in `src/songs.qrc`** and new tests in **`tests/songs_tests.{pro,cpp}`**, so `resources.qrc` and `tests/reelcut_tests.cpp` (both carrying other sessions' edits) stay untouched.
4. **`Main.qml` loads the new pieces through `Loader`s that are active only when the `songs` context property exists**, so the 20 existing QML tests that build Main without it keep passing.

## File structure

| File | Responsibility |
|---|---|
| `modal/acestep_app.py` | Modal app: image, checkpoint volume, model download, API web server |
| `src/songrequest.{h,cpp}` | Pure: page request map → ACE-Step form fields + attachment paths; folder slug |
| `src/modalusage.{h,cpp}` | Monthly spend ledger, idle-tail accounting, status file |
| `src/songgen.{h,cpp}` | Config, HTTP calls, polling, downloads, request.json, song list, images via `claude -p` |
| `src/ModalMeter.qml` | Top-bar meter + popup (spend, limit, connection settings) |
| `src/SongsPage.qml` | Form, attachments, song cards |
| `src/songs.qrc` | The two QML files |
| `tests/songs_tests.{pro,cpp}` | Unit tests for the three C++ units |
| `bin/test-songs` | Builds and runs the new tests |
| `src/main.cpp`, `reelcut.pro`, `src/Main.qml` | Wiring (additions only) |
| `src/automationregistry.cpp`, `src/appbridge.cpp` | Expose `songs` to MCP (Task 7) |
| `docs/songs.md` | Setup, usage, measured cost |

---

### Task 1: Worktree, spec update, Modal app

**Files:**
- Create: `modal/acestep_app.py`
- Modify: `docs/superpowers/specs/2026-11-25-song-generator-design.md` (record the deviations)

**Interfaces:**
- Produces: Modal app `reelcut-acestep`, web function `api` serving ACE-Step's REST API on port 8001; function `download` that fills volume `reelcut-acestep-checkpoints`.

- [ ] **Step 1: Create the worktree**

```bash
cd ~/reelcut
git worktree add -b song-generator ~/reelcut-song-generator main
cd ~/reelcut-song-generator && git status --short   # expect empty
```

- [ ] **Step 2: Record the deviations in the spec**

Append to the spec a `## Changes made while planning` section listing the four deviations above, word for word.

- [ ] **Step 3: Write `modal/acestep_app.py`**

```python
"""ACE-Step 1.5 on Modal for Reelcut's Songs page.

One-time:  modal run modal/acestep_app.py::download
Deploy:    modal deploy modal/acestep_app.py
"""
import subprocess

import modal

ACE_REPO = "https://github.com/ace-step/ACE-Step-1.5"
ACE_COMMIT = "ca1e85fe9430179831e6bc6be790c332190a3866"
CKPT = "/ckpt"

app = modal.App("reelcut-acestep")
checkpoints = modal.Volume.from_name("reelcut-acestep-checkpoints", create_if_missing=True)

image = (
    modal.Image.from_registry("nvidia/cuda:12.8.1-runtime-ubuntu22.04", add_python="3.11")
    .apt_install("git", "ffmpeg", "libsndfile1", "build-essential")
    .pip_install("uv")
    .run_commands(
        f"git clone {ACE_REPO} /opt/acestep && cd /opt/acestep && git checkout {ACE_COMMIT}",
        "cd /opt/acestep && uv sync --frozen",
    )
    .env({
        "ACESTEP_CHECKPOINTS_DIR": CKPT,
        "ACESTEP_API_HOST": "0.0.0.0",
        "ACESTEP_API_PORT": "8001",
        "ACESTEP_CONFIG_PATH": "acestep-v15-turbo",
        "ACESTEP_LM_MODEL_PATH": "acestep-5Hz-lm-1.7B",
        "ACESTEP_INIT_LLM": "true",
    })
)


@app.function(image=image, volumes={CKPT: checkpoints}, timeout=3600)
def download():
    bin_ = "/opt/acestep/.venv/bin/acestep-download"
    subprocess.run([bin_, "--dir", CKPT], check=True, cwd="/opt/acestep")
    subprocess.run([bin_, "--model", "acestep-5Hz-lm-1.7B", "--skip-main", "--dir", CKPT],
                   check=True, cwd="/opt/acestep")
    checkpoints.commit()


@app.function(image=image, gpu="L4", volumes={CKPT: checkpoints},
              max_containers=1, scaledown_window=120, timeout=600)
@modal.concurrent(max_inputs=20)
@modal.web_server(port=8001, startup_timeout=600, requires_proxy_auth=True)
def api():
    subprocess.Popen(["/opt/acestep/.venv/bin/acestep-api"], cwd="/opt/acestep")
```

- [ ] **Step 4: Syntax check**

Run: `python3 -m py_compile modal/acestep_app.py && echo ok`
Expected: `ok` (deploying happens in Task 8).

- [ ] **Step 5: Commit**

```bash
git add modal/acestep_app.py docs/superpowers/specs/2026-11-25-song-generator-design.md
git commit -m "Song generator: Modal app for ACE-Step and planning changes to the spec"
```

---

### Task 2: Test executable and `SongRequest`

**Files:**
- Create: `src/songrequest.h`, `src/songrequest.cpp`, `tests/songs_tests.pro`, `tests/songs_tests.cpp`, `bin/test-songs`

**Interfaces:**
- Produces:
  - `struct SongRequest::Form { QList<QPair<QString,QString>> fields; QString referencePath; QString sourcePath; QString caption; };`
  - `SongRequest::Form SongRequest::build(const QVariantMap &request);`
  - `QString SongRequest::slug(const QVariantMap &request);`
  - Request keys: `prompt`, `style`, `imageStyle`, `lyrics` (QString); `instrumental` (bool); `duration` (double, 0 = auto); `versions` (int); `reference`, `remix` (paths); `remixMode` (`"cover"|"extend"|"repaint"`); `repaintStart`, `repaintEnd` (double).

- [ ] **Step 1: Test project files**

`tests/songs_tests.pro`:

```
QT += core network testlib
CONFIG += c++17 testcase
TARGET = songs_tests
TEMPLATE = app
INCLUDEPATH += ../src
HEADERS += ../src/songrequest.h
SOURCES += songs_tests.cpp ../src/songrequest.cpp
```

`bin/test-songs` (chmod +x):

```sh
#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
mkdir -p "$ROOT/build/songs-tests"
cd "$ROOT/build/songs-tests"
qmake6 "$ROOT/tests/songs_tests.pro"
make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ./songs_tests "$@"
```

- [ ] **Step 2: Write failing tests** in `tests/songs_tests.cpp`

```cpp
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
```

- [ ] **Step 3: Run, expect a build failure** (`songrequest.h` missing)

Run: `bin/test-songs`
Expected: compile error, `songrequest.h: No such file`.

- [ ] **Step 4: Implement**

`src/songrequest.h`:

```cpp
#pragma once
#include <QList>
#include <QPair>
#include <QString>
#include <QVariantMap>

// The Songs page's request, every key optional, as ACE-Step /release_task
// form fields plus the attachment files to upload with them.
namespace SongRequest {
struct Form {
    QList<QPair<QString, QString>> fields;
    QString referencePath;  // uploaded as reference_audio
    QString sourcePath;     // uploaded as src_audio
    QString caption;        // prompt + style + image style, as sent
};
Form build(const QVariantMap &request);
// Up to four lowercase words from the prompt (or style) for the folder name.
QString slug(const QVariantMap &request);
}
```

`src/songrequest.cpp`:

```cpp
#include "songrequest.h"
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

namespace SongRequest {

static QString text(const QVariantMap &r, const char *key) { return r.value(key).toString().trimmed(); }
static QString number(double v) { return QString::number(v, 'g', 10); }

Form build(const QVariantMap &r) {
    Form f;
    QStringList parts;
    for (const char *key : {"prompt", "style", "imageStyle"})
        if (!text(r, key).isEmpty()) parts << text(r, key);
    f.caption = parts.join(". ");

    const int versions = r.contains("versions") ? qBound(1, r.value("versions").toInt(), 4) : 2;
    f.fields << qMakePair(QStringLiteral("audio_format"), QStringLiteral("mp3"))
             << qMakePair(QStringLiteral("batch_size"), QString::number(versions))
             << qMakePair(QStringLiteral("thinking"), QStringLiteral("true"));
    const double duration = r.value("duration").toDouble();
    if (duration > 0)
        f.fields << qMakePair(QStringLiteral("audio_duration"), number(qBound(10.0, duration, 240.0)));

    const bool instrumental = r.value("instrumental").toBool();
    const QString lyrics = r.value("lyrics").toString().trimmed();
    const QString remix = text(r, "remix");
    if (!remix.isEmpty()) {
        f.sourcePath = remix;
        const QString mode = text(r, "remixMode");
        const QString task = mode == "extend" ? "complete" : mode == "repaint" ? "repaint" : "cover";
        f.fields << qMakePair(QStringLiteral("task_type"), task);
        if (task == "repaint") {
            const double end = r.value("repaintEnd").toDouble();
            f.fields << qMakePair(QStringLiteral("repainting_start"), number(qMax(0.0, r.value("repaintStart").toDouble())))
                     << qMakePair(QStringLiteral("repainting_end"), end > 0 ? number(end) : QStringLiteral("-1"));
        }
    }
    if (instrumental) {
        f.fields << qMakePair(QStringLiteral("prompt"), f.caption)
                 << qMakePair(QStringLiteral("lyrics"), QStringLiteral("[Instrumental]"));
    } else if (!lyrics.isEmpty() || !remix.isEmpty()) {
        f.fields << qMakePair(QStringLiteral("prompt"), f.caption);
        if (!lyrics.isEmpty()) f.fields << qMakePair(QStringLiteral("lyrics"), lyrics);
    } else {
        f.fields << qMakePair(QStringLiteral("sample_mode"), QStringLiteral("true"))
                 << qMakePair(QStringLiteral("sample_query"), f.caption);
    }
    const QString reference = text(r, "reference");
    if (!reference.isEmpty()) f.referencePath = reference;
    return f;
}

QString slug(const QVariantMap &r) {
    QString source = text(r, "prompt");
    if (source.isEmpty()) source = text(r, "style");
    const QStringList words = source.toLower()
        .replace(QRegularExpression("[^a-z0-9- ]"), " ")
        .split(' ', Qt::SkipEmptyParts);
    return words.isEmpty() ? QStringLiteral("random") : words.mid(0, 4).join('-');
}

}
```

- [ ] **Step 5: Run, expect PASS**

Run: `bin/test-songs`
Expected: `Totals: 10 passed, 0 failed` (8 tests + initTestCase/cleanupTestCase).

- [ ] **Step 6: Commit**

```bash
git add src/songrequest.h src/songrequest.cpp tests/songs_tests.pro tests/songs_tests.cpp bin/test-songs
git commit -m "Song generator: request building for ACE-Step, with its own test executable"
```

---

### Task 3: `ModalUsage`

**Files:**
- Create: `src/modalusage.h`, `src/modalusage.cpp`
- Modify: `tests/songs_tests.pro` (add files + `../src/publisher.{h,cpp}` for `Net`), `tests/songs_tests.cpp`

**Interfaces:**
- Consumes: `Net::savePrivate`, `Net::load`, `Net::stateFile` from `src/publisher.h`.
- Produces: `class ModalUsage` with
  - `static constexpr double kRatePerSecond = 0.000222 + 4 * 0.0000131 + 16 * 0.00000222;`
  - `static constexpr int kIdleTailSeconds = 120;`
  - `ModalUsage(const QString &stateDir = {}, QObject *parent = nullptr)`
  - `void requestStarted()`, `void requestFinished(double billedSeconds, int songs)`, `void endTail()`, `void flushTail()`
  - `std::function<QDateTime()> now`
  - Q_PROPERTYs `spent`, `limit` (RW), `fraction`, `level` (`"ok"|"warn"|"over"`), `songCount`, `busySeconds`, `idleSeconds`; signal `changed()`.

- [ ] **Step 1: Add to `tests/songs_tests.pro`**

```
HEADERS += ../src/modalusage.h ../src/publisher.h
SOURCES += ../src/modalusage.cpp ../src/publisher.cpp
```

- [ ] **Step 2: Failing tests** (add `#include "modalusage.h"` and `#include <QTemporaryDir>` and these slots)

```cpp
    void usageCostAndLevels() {
        QTemporaryDir dir;
        ModalUsage u(dir.path());
        QCOMPARE(u.limit(), 30.0);
        QCOMPARE(u.level(), QString("ok"));
        u.requestStarted();
        u.requestFinished(1000, 2);
        QVERIFY(qAbs(u.spent() - 1000 * ModalUsage::kRatePerSecond) < 1e-9);
        QCOMPARE(u.songCount(), 2);
        u.setLimit(u.spent() / 0.85);  QCOMPARE(u.level(), QString("warn"));
        u.setLimit(u.spent() / 1.0);   QCOMPARE(u.level(), QString("over"));
        u.setLimit(u.spent() / 0.5);   QCOMPARE(u.level(), QString("ok"));
    }

```cpp
    void usageIdleTail() {
        QTemporaryDir dir;
        ModalUsage u(dir.path());
        QDateTime t = QDateTime::fromString("2026-11-25T10:00:00Z", Qt::ISODate);
        u.now = [&] { return t; };
        u.requestStarted();
        u.requestFinished(10, 1);           // tail opens at 10:00:00
        t = t.addSecs(30);
        u.requestStarted();                 // new request 30 s in: 30 s idle recorded
        QCOMPARE(u.idleSeconds(), 30.0);
        u.requestFinished(10, 1);
        u.endTail();                        // the timer ran out: full 120 s
        QCOMPARE(u.idleSeconds(), 150.0);
        u.requestStarted(); u.requestFinished(5, 0);
        t = t.addSecs(45);
        u.flushTail();                      // quitting 45 s into the tail
        QCOMPARE(u.idleSeconds(), 195.0);
        QCOMPARE(u.busySeconds(), 25.0);
    }
    void usageMonthRolloverAndPersistence() {
        QTemporaryDir dir;
        QDateTime t = QDateTime::fromString("2026-11-30T23:00:00Z", Qt::ISODate);
        {
            ModalUsage u(dir.path());
            u.now = [&] { return t; };
            u.setLimit(12);
            u.requestStarted(); u.requestFinished(100, 1); u.flushTail();
        }
        {
            ModalUsage u(dir.path());
            u.now = [&] { return t; };
            u.reload();
            QCOMPARE(u.songCount(), 1);
            QCOMPARE(u.limit(), 12.0);
            QVERIFY(QFile::exists(dir.filePath("modal-status.json")));
            t = QDateTime::fromString("2026-12-01T09:00:00Z", Qt::ISODate);
            u.requestStarted(); u.requestFinished(10, 1); u.flushTail();
            QCOMPARE(u.songCount(), 1);     // November's song is gone
            QCOMPARE(u.limit(), 12.0);      // the limit survives
        }
    }
```

- [ ] **Step 3: Run, expect a build failure** (`modalusage.h` missing). Run: `bin/test-songs`.

- [ ] **Step 4: Implement**

`src/modalusage.h`:

```cpp
#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <functional>

// This month's estimated Modal spend for the Songs page: billed request time
// plus the 120 s the GPU stays up afterwards, at Modal's list prices.
class ModalUsage : public QObject {
    Q_OBJECT
    Q_PROPERTY(double spent READ spent NOTIFY changed)
    Q_PROPERTY(double limit READ limit WRITE setLimit NOTIFY changed)
    Q_PROPERTY(double fraction READ fraction NOTIFY changed)
    Q_PROPERTY(QString level READ level NOTIFY changed)
    Q_PROPERTY(int songCount READ songCount NOTIFY changed)
    Q_PROPERTY(double busySeconds READ busySeconds NOTIFY changed)
    Q_PROPERTY(double idleSeconds READ idleSeconds NOTIFY changed)
public:
    static constexpr double kRatePerSecond = 0.000222 + 4 * 0.0000131 + 16 * 0.00000222;
    static constexpr int kIdleTailSeconds = 120;

    explicit ModalUsage(const QString &stateDir = {}, QObject *parent = nullptr);
    ~ModalUsage() override;

    double spent() const;
    double limit() const { return m_limit; }
    void setLimit(double dollars);
    double fraction() const { return m_limit > 0 ? spent() / m_limit : 0; }
    QString level() const;
    int songCount() const;
    double busySeconds() const { return total("busy"); }
    double idleSeconds() const { return total("idle"); }

    void requestStarted();                             // closes an open idle tail
    void requestFinished(double billedSeconds, int songs);
    void endTail();                                    // the full tail ran out
    void flushTail();                                  // record the tail so far (quit)
    Q_INVOKABLE void reload();

    std::function<QDateTime()> now = [] { return QDateTime::currentDateTimeUtc(); };

signals:
    void changed();

private:
    void record(const QString &kind, double seconds, int songs = 0);
    void rollover();
    void save();
    double total(const QString &kind) const;
    QString m_stateDir;
    QString m_month;
    double m_limit = 30;
    QJsonArray m_entries;
    QDateTime m_tailStart;
    QTimer m_tail;
};
```

`src/modalusage.cpp`:

```cpp
#include "modalusage.h"
#include "publisher.h"
#include <QJsonObject>

ModalUsage::ModalUsage(const QString &stateDir, QObject *parent) : QObject(parent), m_stateDir(stateDir) {
    m_tail.setSingleShot(true);
    m_tail.setInterval(kIdleTailSeconds * 1000);
    connect(&m_tail, &QTimer::timeout, this, &ModalUsage::endTail);
    reload();
}

ModalUsage::~ModalUsage() { flushTail(); }

void ModalUsage::reload() {
    const QJsonObject o = Net::load(Net::stateFile(m_stateDir, "modal-usage.json"));
    m_month = o.value("month").toString();
    m_limit = o.contains("limit") ? o.value("limit").toDouble() : 30;
    m_entries = o.value("entries").toArray();
    rollover();
    emit changed();
}

void ModalUsage::rollover() {
    const QString month = now().toUTC().toString("yyyy-MM");
    if (m_month != month) { m_month = month; m_entries = {}; }
}

double ModalUsage::total(const QString &kind) const {
    double s = 0;
    for (const auto &e : m_entries)
        if (e.toObject().value("kind").toString() == kind) s += e.toObject().value("seconds").toDouble();
    return s;
}

double ModalUsage::spent() const {
    double d = 0;
    for (const auto &e : m_entries) d += e.toObject().value("dollars").toDouble();
    return d;
}

int ModalUsage::songCount() const {
    int n = 0;
    for (const auto &e : m_entries) n += e.toObject().value("songs").toInt();
    return n;
}

QString ModalUsage::level() const {
    const double f = fraction();
    return f >= 1 ? "over" : f >= 0.8 ? "warn" : "ok";
}

void ModalUsage::setLimit(double dollars) {
    if (!(dollars >= 0) || dollars == m_limit) return;
    m_limit = dollars;
    save();
    emit changed();
}

void ModalUsage::record(const QString &kind, double seconds, int songs) {
    rollover();
    if (seconds <= 0 && songs == 0) return;
    m_entries.append(QJsonObject{{"at", now().toUTC().toString(Qt::ISODate)}, {"kind", kind},
                                 {"seconds", seconds}, {"songs", songs},
                                 {"dollars", seconds * kRatePerSecond}});
    save();
    emit changed();
}

void ModalUsage::requestStarted() { flushTail(); }

void ModalUsage::requestFinished(double billedSeconds, int songs) {
    record("busy", billedSeconds, songs);
    m_tailStart = now();
    m_tail.start();
}

void ModalUsage::endTail() {
    if (!m_tailStart.isValid()) return;
    m_tailStart = {};
    m_tail.stop();
    record("idle", kIdleTailSeconds);
}

void ModalUsage::flushTail() {
    if (!m_tailStart.isValid()) return;
    const double s = qBound<qint64>(0, m_tailStart.secsTo(now()), kIdleTailSeconds);
    m_tailStart = {};
    m_tail.stop();
    record("idle", s);
}

void ModalUsage::save() {
    Net::savePrivate(Net::stateFile(m_stateDir, "modal-usage.json"),
                     {{"month", m_month}, {"limit", m_limit}, {"entries", m_entries}});
    Net::savePrivate(Net::stateFile(m_stateDir, "modal-status.json"),
                     {{"spentDollars", spent()}, {"limitDollars", m_limit}, {"overBudget", level() == "over"},
                      {"songs", songCount()}, {"month", m_month}});
}
```

- [ ] **Step 5: Run, expect PASS.** Run: `bin/test-songs`. Expected: all pass.

Note: `Net::savePrivate` must create the parent directory; if a test fails on a missing directory, check `publisher.cpp`'s implementation and `QDir().mkpath` the state dir in `save()` instead.

- [ ] **Step 6: Commit**

```bash
git add src/modalusage.h src/modalusage.cpp tests/songs_tests.pro tests/songs_tests.cpp
git commit -m "Song generator: monthly Modal spend ledger with idle-tail accounting"
```

---

### Task 4: `SongGenerator`

**Files:**
- Create: `src/songgen.h`, `src/songgen.cpp`
- Modify: `tests/songs_tests.pro`, `tests/songs_tests.cpp`

**Interfaces:**
- Consumes: `SongRequest::build/slug` (Task 2), `ModalUsage::requestStarted/requestFinished` (Task 3), `Net::savePrivate/load/stateFile`.
- Produces: `class SongGenerator`:
  - `SongGenerator(ModalUsage *usage, const QString &stateDir = {}, const QString &songsDir = {}, QObject *parent = nullptr)` (empty `songsDir` = `~/Videos/Reelcut songs`)
  - Q_PROPERTYs: `configured`, `endpoint`, `tokenId`, `busy`, `stage`, `lastError`, `songs` (QVariantList of maps `{folder, name, files, created, seconds, cost, caption}`), `songsDir`
  - Q_INVOKABLE: `void configure(QString endpoint, QString tokenId, QString tokenSecret)`, `void testConnection()`, `bool generate(QVariantMap request)`, `void writeLyrics(QVariantMap request)`, `void describeImages(QStringList paths)`, `void reload()`, `QString readTextFile(QString path)`
  - Signals: `configChanged()`, `stateChanged()`, `songsChanged()`, `connectionTested(bool ok, QString message)`, `finished(QString folder)`, `failed(QString message)`, `lyricsWritten(QString lyrics, QString error)`, `imagesDescribed(QString text, QString error)`
  - Test hooks: `QString claudeCommand; int pollMs = 2000; int timeoutMs = 600000;`

- [ ] **Step 1: Add to `tests/songs_tests.pro`**

```
HEADERS += ../src/songgen.h
SOURCES += ../src/songgen.cpp
```

- [ ] **Step 2: Failing tests.** Add a minimal fake ACE-Step server and tests to `tests/songs_tests.cpp`:

```cpp
#include "songgen.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

// A tiny stand-in for the ACE-Step API: answers by path with canned bodies
// and remembers each request's path and headers.
class FakeAce : public QObject {
public:
    QTcpServer server;
    QStringList paths;
    QList<QByteArray> heads, bodies;
    int status = 200;          // for every reply
    int pollsBeforeDone = 1;   // /query_result answers "running" this many times
    int taskStatus = 1;        // then this
    FakeAce() {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto *buf = new QByteArray;
                connect(s, &QTcpSocket::disconnected, s, [s, buf] { delete buf; s->deleteLater(); });
                connect(s, &QTcpSocket::readyRead, s, [this, s, buf] {
                    buf->append(s->readAll());
                    const int end = buf->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray head = buf->left(end);
                    int length = 0;
                    for (const QByteArray &line : head.split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    if (buf->size() < end + 4 + length) return;
                    const QString path = QString::fromUtf8(head.split(' ').value(1));
                    paths << path; heads << head; bodies << buf->mid(end + 4, length);
                    buf->clear();
                    const QByteArray body = answer(path);
                    s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
            }
        });
    }
    QByteArray answer(const QString &path) {
        if (path.startsWith("/v1/audio")) return QByteArray("ID3fake-mp3-") + path.toUtf8();
        if (path == "/health") return R"({"data":{"status":"ok"},"code":200})";
        if (path == "/release_task") return R"({"data":{"task_id":"t1","status":"queued"},"code":200})";
        if (path == "/format_input") return R"({"data":{"caption":"c","lyrics":"[Verse]\nla la"},"code":200})";
        if (path == "/query_result") {
            if (pollsBeforeDone-- > 0) return R"({"data":[{"task_id":"t1","status":0,"result":"[]"}],"code":200})";
            const QString result = taskStatus == 1
                ? R"([{"file":"/v1/audio?path=%2Ftmp%2Fa.mp3","status":1},{"file":"/v1/audio?path=%2Ftmp%2Fb.mp3","status":1}])"
                : R"([{"status":2,"error":"CUDA out of memory"}])";
            return QJsonDocument(QJsonObject{{"code", 200}, {"data", QJsonArray{QJsonObject{
                {"task_id", "t1"}, {"status", taskStatus}, {"result", result}}}}}).toJson(QJsonDocument::Compact);
        }
        return "{}";
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(server.serverPort()); }
};
```

Slots:

```cpp
    void generatorNeedsConfig() {
        QTemporaryDir state, songs;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        QVERIFY(!g.configured());
        QVERIFY(!g.generate({}));
    }
    void generatorWritesTakesAndRequest() {
        QTemporaryDir state, songs;
        FakeAce ace;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.pollMs = 10;
        g.configure(ace.base(), "wk-id", "ws-secret");
        QVERIFY(g.configured());
        QVERIFY(QFile::exists(state.filePath("modal.json")));
        QSignalSpy done(&g, &SongGenerator::finished);
        QVERIFY(g.generate({{"prompt", "cat song"}, {"versions", 2}}));
        QVERIFY(g.busy());
        QVERIFY(done.wait(5000));
        const QString folder = done.first().first().toString();
        QVERIFY(folder.endsWith("-cat-song"));
        QVERIFY(QFile::exists(folder + "/take-1.mp3"));
        QVERIFY(QFile::exists(folder + "/take-2.mp3"));
        const QJsonObject req = QJsonDocument::fromJson([&] { QFile f(folder + "/request.json"); f.open(QIODevice::ReadOnly); return f.readAll(); }()).object();
        QCOMPARE(req.value("taskId").toString(), QString("t1"));
        QCOMPARE(req.value("request").toObject().value("prompt").toString(), QString("cat song"));
        QVERIFY(req.value("estimatedCost").toDouble() >= 0);
        QVERIFY(ace.heads.first().contains("Modal-Key: wk-id"));
        QVERIFY(ace.heads.first().contains("Modal-Secret: ws-secret"));
        QVERIFY(ace.bodies.first().contains("name=\"sample_query\""));
        QCOMPARE(g.songs().size(), 1);
        QCOMPARE(usage.songCount(), 2);
        QVERIFY(!g.busy());
    }
    void generatorUploadsAttachments() {
        QTemporaryDir state, songs, files;
        FakeAce ace;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.pollMs = 10;
        g.configure(ace.base(), "k", "s");
        QFile ref(files.filePath("ref.mp3")); ref.open(QIODevice::WriteOnly); ref.write("REFDATA"); ref.close();
        QSignalSpy done(&g, &SongGenerator::finished);
        QVERIFY(g.generate({{"reference", ref.fileName()}}));
        QVERIFY(done.wait(5000));
        QVERIFY(ace.bodies.first().contains("name=\"reference_audio\"; filename=\"ref.mp3\""));
        QVERIFY(ace.bodies.first().contains("REFDATA"));
    }
    void generatorReportsTaskFailure() {
        QTemporaryDir state, songs;
        FakeAce ace; ace.taskStatus = 2;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.pollMs = 10;
        g.configure(ace.base(), "k", "s");
        QSignalSpy failed(&g, &SongGenerator::failed);
        QVERIFY(g.generate({}));
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().first().toString().contains("CUDA out of memory"));
        QVERIFY(!g.busy());
        QCOMPARE(usage.songCount(), 0);
    }
    void generatorExplainsAuthAndDeploy() {
        QTemporaryDir state, songs;
        FakeAce ace;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.configure(ace.base(), "k", "s");
        QSignalSpy failed(&g, &SongGenerator::failed);
        ace.status = 401;
        QVERIFY(g.generate({}));
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.last().first().toString().contains("token"));
        ace.status = 404;
        QVERIFY(g.generate({}));
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.last().first().toString().contains("modal deploy"));
    }
    void generatorTestsConnectionAndWritesLyrics() {
        QTemporaryDir state, songs;
        FakeAce ace;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        g.configure(ace.base(), "k", "s");
        QSignalSpy tested(&g, &SongGenerator::connectionTested);
        g.testConnection();
        QVERIFY(tested.wait(5000));
        QCOMPARE(tested.first().first().toBool(), true);
        QSignalSpy lyrics(&g, &SongGenerator::lyricsWritten);
        g.writeLyrics({{"prompt", "cat"}});
        QVERIFY(lyrics.wait(5000));
        QCOMPARE(lyrics.first().first().toString(), QString("[Verse]\nla la"));
    }
    void generatorDescribesImagesWithClaude() {
        QTemporaryDir state, songs, bin;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        const QString fake = bin.filePath("claude");
        QFile f(fake); f.open(QIODevice::WriteOnly);
        f.write("#!/bin/sh\necho \"dreamy synthwave, 90 bpm\"\n"); f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        g.claudeCommand = fake;
        QSignalSpy described(&g, &SongGenerator::imagesDescribed);
        g.describeImages({"/tmp/a.png"});
        QVERIFY(described.wait(5000));
        QCOMPARE(described.first().first().toString(), QString("dreamy synthwave, 90 bpm"));
        g.claudeCommand = bin.filePath("missing");
        g.describeImages({"/tmp/a.png"});
        QVERIFY(described.wait(5000));
        QVERIFY(!described.last().at(1).toString().isEmpty());
    }
```

- [ ] **Step 3: Run, expect a build failure** (`songgen.h` missing). Run: `bin/test-songs`.

- [ ] **Step 4: Implement `src/songgen.h`**

```cpp
#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

class ModalUsage;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

// Makes songs with ACE-Step on Modal: sends the Songs page's request, waits
// for the takes, saves them with the request in their own folder.
class SongGenerator : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY configChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY configChanged)
    Q_PROPERTY(QString tokenId READ tokenId NOTIFY configChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QVariantList songs READ songs NOTIFY songsChanged)
    Q_PROPERTY(QString songsDir READ songsDir CONSTANT)
public:
    SongGenerator(ModalUsage *usage, const QString &stateDir = {}, const QString &songsDir = {}, QObject *parent = nullptr);

    bool configured() const { return !m_endpoint.isEmpty() && !m_tokenId.isEmpty() && !m_tokenSecret.isEmpty(); }
    QString endpoint() const { return m_endpoint; }
    QString tokenId() const { return m_tokenId; }
    bool busy() const { return m_busy; }
    QString stage() const { return m_stage; }
    QString lastError() const { return m_lastError; }
    QVariantList songs() const { return m_songs; }
    QString songsDir() const { return m_songsDir; }

    Q_INVOKABLE void configure(const QString &endpoint, const QString &tokenId, const QString &tokenSecret);
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE bool generate(const QVariantMap &request);
    Q_INVOKABLE void writeLyrics(const QVariantMap &request);
    Q_INVOKABLE void describeImages(const QStringList &paths);
    Q_INVOKABLE void reload();
    Q_INVOKABLE QString readTextFile(const QString &path) const;   // a lyrics .txt for the page

    QString claudeCommand;      // empty: found on PATH or in the usual places
    int pollMs = 2000;
    int timeoutMs = 600000;

signals:
    void configChanged();
    void stateChanged();
    void songsChanged();
    void connectionTested(bool ok, const QString &message);
    void finished(const QString &folder);
    void failed(const QString &message);
    void lyricsWritten(const QString &lyrics, const QString &error);
    void imagesDescribed(const QString &text, const QString &error);

private:
    QNetworkRequest request(const QString &path) const;
    static QString explain(QNetworkReply *reply);
    void setStage(const QString &stage);
    void poll();
    void download(const QStringList &files, int index);
    void finish();
    void fail(const QString &message);
    QString audioFor(const QString &path);   // ffmpeg video → mp3 in the song folder

    QPointer<ModalUsage> m_usage;
    QNetworkAccessManager *m_net;
    QString m_stateDir, m_songsDir;
    QString m_endpoint, m_tokenId, m_tokenSecret;
    bool m_busy = false;
    QString m_stage, m_lastError;
    QVariantList m_songs;
    // The request in flight.
    QVariantMap m_request;
    QJsonObject m_sent;
    QString m_folder, m_taskId;
    QStringList m_saved;
    QElapsedTimer m_clock;
    QTimer m_poll;
    int m_pollErrors = 0;
};
```

- [ ] **Step 5: Implement `src/songgen.cpp`**

```cpp
#include "songgen.h"
#include "modalusage.h"
#include "publisher.h"
#include "songrequest.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QUrlQuery>

static QJsonObject parse(const QByteArray &body) { return QJsonDocument::fromJson(body).object(); }

SongGenerator::SongGenerator(ModalUsage *usage, const QString &stateDir, const QString &songsDir, QObject *parent)
    : QObject(parent), m_usage(usage), m_net(new QNetworkAccessManager(this)), m_stateDir(stateDir),
      m_songsDir(!songsDir.isEmpty() ? songsDir
                 : QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Reelcut songs") {
    const QJsonObject c = Net::load(Net::stateFile(m_stateDir, "modal.json"));
    m_endpoint = c.value("endpoint").toString();
    m_tokenId = c.value("tokenId").toString();
    m_tokenSecret = c.value("tokenSecret").toString();
    m_poll.setSingleShot(true);
    connect(&m_poll, &QTimer::timeout, this, &SongGenerator::poll);
    reload();
}

void SongGenerator::configure(const QString &endpoint, const QString &tokenId, const QString &tokenSecret) {
    QString e = endpoint.trimmed();
    while (e.endsWith('/')) e.chop(1);
    m_endpoint = e;
    m_tokenId = tokenId.trimmed();
    if (!tokenSecret.trimmed().isEmpty()) m_tokenSecret = tokenSecret.trimmed();  // blank keeps the old one
    Net::savePrivate(Net::stateFile(m_stateDir, "modal.json"),
                     {{"endpoint", m_endpoint}, {"tokenId", m_tokenId}, {"tokenSecret", m_tokenSecret}});
    emit configChanged();
}

QNetworkRequest SongGenerator::request(const QString &path) const {
    QNetworkRequest r(QUrl(m_endpoint + path));
    r.setRawHeader("Modal-Key", m_tokenId.toUtf8());
    r.setRawHeader("Modal-Secret", m_tokenSecret.toUtf8());
    r.setTransferTimeout(timeoutMs);
    return r;
}

QString SongGenerator::explain(QNetworkReply *reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401 || status == 403)
        return tr("Modal token rejected. Check the token ID and secret in the Modal meter.");
    if (status == 404)
        return tr("Modal app not deployed: run modal deploy modal/acestep_app.py");
    const QJsonObject o = parse(reply->peek(4096));
    const QString server = o.value("error").toString();
    if (!server.isEmpty()) return server;
    return reply->errorString();
}

void SongGenerator::setStage(const QString &stage) { m_stage = stage; emit stateChanged(); }

void SongGenerator::testConnection() {
    if (!configured()) { emit connectionTested(false, tr("Fill in the endpoint, token ID and secret.")); return; }
    QNetworkReply *reply = m_net->get(request("/health"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError;
        emit connectionTested(ok, ok ? tr("Connected. The GPU is up.") : explain(reply));
    });
}

QString SongGenerator::audioFor(const QString &path) {
    static const QStringList video{"mp4", "mov", "webm", "mkv", "m4v"};
    if (path.isEmpty() || !video.contains(QFileInfo(path).suffix().toLower())) return path;
    const QString out = m_folder + "/" + QFileInfo(path).completeBaseName() + ".mp3";
    QProcess ff;
    ff.start("ffmpeg", {"-y", "-hide_banner", "-loglevel", "error", "-i", path, "-vn", "-q:a", "2", out});
    if (!ff.waitForFinished(120000) || ff.exitCode() != 0) {
        m_lastError = tr("Couldn't take the audio out of %1: %2").arg(QFileInfo(path).fileName(),
                      QString::fromUtf8(ff.readAllStandardError()).trimmed());
        return {};
    }
    return out;
}

bool SongGenerator::generate(const QVariantMap &input) {
    if (m_busy || !configured()) return false;
    m_busy = true;
    m_lastError.clear();
    m_request = input;
    m_saved.clear();
    m_taskId.clear();
    m_pollErrors = 0;
    m_folder = m_songsDir + "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm") + "-" + SongRequest::slug(input);
    for (int n = 2; QFileInfo::exists(m_folder); ++n)
        m_folder = m_songsDir + "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm") + "-" + SongRequest::slug(input) + "-" + QString::number(n);
    QDir().mkpath(m_folder);
    setStage(tr("Preparing"));

    QVariantMap prepared = input;
    for (const char *key : {"reference", "remix"}) {
        const QString path = input.value(key).toString();
        if (path.isEmpty()) continue;
        const QString audio = audioFor(path);
        if (audio.isEmpty()) { fail(m_lastError); return true; }
        prepared[key] = audio;
    }
    const SongRequest::Form form = SongRequest::build(prepared);
    m_sent = {};
    for (const auto &f : form.fields) m_sent.insert(f.first, f.second);

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (const auto &f : form.fields) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"%1\"").arg(f.first));
        part.setBody(f.second.toUtf8());
        multi->append(part);
    }
    auto attach = [&](const QString &name, const QString &path) {
        if (path.isEmpty()) return true;
        auto *file = new QFile(path, multi);
        if (!file->open(QIODevice::ReadOnly)) return false;
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(path).name());
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QString("form-data; name=\"%1\"; filename=\"%2\"").arg(name, QFileInfo(path).fileName()));
        part.setBodyDevice(file);
        multi->append(part);
        return true;
    };
    if (!attach("reference_audio", form.referencePath) || !attach("src_audio", form.sourcePath)) {
        delete multi;
        fail(tr("Couldn't read an attached file."));
        return true;
    }

    if (m_usage) m_usage->requestStarted();
    m_clock.start();
    setStage(tr("Starting GPU (first time ~30–60 s)"));
    QNetworkReply *reply = m_net->post(request("/release_task"), multi);
    multi->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { fail(explain(reply)); return; }
        m_taskId = parse(reply->readAll()).value("data").toObject().value("task_id").toString();
        if (m_taskId.isEmpty()) { fail(tr("ACE-Step didn't return a task.")); return; }
        setStage(tr("Generating"));
        m_poll.start(pollMs);
    });
    return true;
}

void SongGenerator::poll() {
    if (m_clock.elapsed() > timeoutMs) { fail(tr("Timed out after %1 minutes.").arg(timeoutMs / 60000)); return; }
    QNetworkRequest r = request("/query_result");
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QByteArray body = QJsonDocument(QJsonObject{{"task_id_list", QJsonArray{m_taskId}}}).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_net->post(r, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 || status == 403 || status == 404 || ++m_pollErrors > 3) { fail(explain(reply)); return; }
            m_poll.start(pollMs);
            return;
        }
        m_pollErrors = 0;
        const QJsonObject task = parse(reply->readAll()).value("data").toArray().first().toObject();
        const int status = task.value("status").toInt();
        if (status == 0) { m_poll.start(pollMs); return; }
        const QJsonArray results = QJsonDocument::fromJson(task.value("result").toString().toUtf8()).array();
        if (status == 2) {
            QString why = results.first().toObject().value("error").toString();
            if (why.isEmpty()) why = task.value("error").toString();
            fail(why.isEmpty() ? tr("ACE-Step failed without saying why.") : why);
            return;
        }
        QStringList files;
        for (const auto &r : results) {
            const QString file = r.toObject().value("file").toString();
            if (!file.isEmpty()) files << file;
        }
        if (files.isEmpty()) { fail(tr("ACE-Step finished but returned no audio.")); return; }
        setStage(tr("Downloading"));
        download(files, 0);
    });
}

void SongGenerator::download(const QStringList &files, int index) {
    if (index >= files.size()) { finish(); return; }
    QNetworkReply *reply = m_net->get(request(files[index]));
    connect(reply, &QNetworkReply::finished, this, [this, reply, files, index] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { fail(explain(reply)); return; }
        const QString path = m_folder + QString("/take-%1.mp3").arg(index + 1);
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(reply->readAll()) < 0) { fail(tr("Couldn't save %1").arg(path)); return; }
        m_saved << path;
        download(files, index + 1);
    });
}

void SongGenerator::finish() {
    const double seconds = m_clock.elapsed() / 1000.0;
    if (m_usage) m_usage->requestFinished(seconds, m_saved.size());
    QJsonArray files;
    for (const QString &f : m_saved) files << QFileInfo(f).fileName();
    const QJsonObject record{{"request", QJsonObject::fromVariantMap(m_request)}, {"sent", m_sent},
                             {"taskId", m_taskId}, {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                             {"billedSeconds", seconds}, {"estimatedCost", seconds * ModalUsage::kRatePerSecond},
                             {"files", files}};
    QFile out(m_folder + "/request.json");
    if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(record).toJson());
    out.close();
    m_busy = false;
    setStage({});
    reload();
    emit finished(m_folder);
}

void SongGenerator::fail(const QString &message) {
    m_poll.stop();
    if (m_clock.isValid() && m_usage) m_usage->requestFinished(m_clock.elapsed() / 1000.0, 0);
    m_clock.invalidate();
    if (QDir(m_folder).isEmpty()) QDir().rmdir(m_folder);
    m_lastError = message;
    m_busy = false;
    setStage({});
    emit failed(message);
}

void SongGenerator::reload() {
    QVariantList list;
    const QFileInfoList dirs = QDir(m_songsDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo &d : dirs) {
        QFile f(d.filePath() + "/request.json");
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject o = parse(f.readAll());
        QStringList files;
        for (const auto &name : o.value("files").toArray()) files << d.filePath() + "/" + name.toString();
        const QJsonObject req = o.value("request").toObject();
        QStringList caption;
        for (const char *k : {"prompt", "style"}) if (!req.value(k).toString().isEmpty()) caption << req.value(k).toString();
        list << QVariantMap{{"folder", d.filePath()}, {"name", d.fileName()}, {"files", files},
                            {"created", o.value("createdAt").toString()}, {"seconds", o.value("billedSeconds").toDouble()},
                            {"cost", o.value("estimatedCost").toDouble()},
                            {"caption", caption.isEmpty() ? tr("Random song") : caption.join(" · ")}};
    }
    m_songs = list;
    emit songsChanged();
}

QString SongGenerator::readTextFile(const QString &path) const {
    QFile f(path);
    return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.read(64 * 1024)) : QString();
}

void SongGenerator::writeLyrics(const QVariantMap &input) {
    if (!configured()) { emit lyricsWritten({}, tr("Connect Modal first (click the Modal meter).")); return; }
    const SongRequest::Form form = SongRequest::build(input);
    QNetworkRequest r = request("/format_input");
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    if (m_usage) m_usage->requestStarted();
    auto *clock = new QElapsedTimer; clock->start();
    QNetworkReply *reply = m_net->post(r, Net::formBody({{"prompt", form.caption}, {"lyrics", input.value("lyrics").toString()}}));
    connect(reply, &QNetworkReply::finished, this, [this, reply, clock] {
        reply->deleteLater();
        if (m_usage) m_usage->requestFinished(clock->elapsed() / 1000.0, 0);
        delete clock;
        if (reply->error() != QNetworkReply::NoError) { emit lyricsWritten({}, explain(reply)); return; }
        const QString lyrics = parse(reply->readAll()).value("data").toObject().value("lyrics").toString().trimmed();
        emit lyricsWritten(lyrics, lyrics.isEmpty() ? tr("ACE-Step returned no lyrics.") : QString());
    });
}

void SongGenerator::describeImages(const QStringList &paths) {
    QString program = claudeCommand;
    if (program.isEmpty()) program = QStandardPaths::findExecutable("claude");
    for (const QString &c : {QDir::homePath() + "/.local/share/mise/shims/claude", QDir::homePath() + "/.local/bin/claude", QString("/usr/bin/claude")})
        if (program.isEmpty() && QFileInfo(c).isExecutable()) program = c;
    if (program.isEmpty() || !QFileInfo(program).isExecutable()) { emit imagesDescribed({}, tr("Claude Code isn't installed, so images were skipped.")); return; }
    const QString prompt = tr("Look at these images and describe the song they suggest in one short line for a music "
                              "generator: genre, mood, tempo in bpm, and main instruments. Reply with that line only.\n")
                           + paths.join('\n');
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus) {
        p->deleteLater();
        const QString text = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        if (code != 0 || text.isEmpty())
            emit imagesDescribed({}, tr("Claude Code couldn't describe the images: %1").arg(QString::fromUtf8(p->readAllStandardError()).trimmed()));
        else
            emit imagesDescribed(text, {});
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        p->deleteLater();
        emit imagesDescribed({}, tr("Couldn't start Claude Code."));
    });
    p->start(program, {"-p", prompt, "--allowedTools", "Read", "--output-format", "text"});
}
```

- [ ] **Step 6: Run, expect PASS.** Run: `bin/test-songs`. Fix until all pass; do not weaken assertions.

- [ ] **Step 7: Commit**

```bash
git add src/songgen.h src/songgen.cpp tests/songs_tests.pro tests/songs_tests.cpp
git commit -m "Song generator: ACE-Step client that saves takes, explains failures and records cost"
```

---

### Task 5: `ModalMeter.qml`

**Files:**
- Create: `src/ModalMeter.qml`, `src/songs.qrc`

**Interfaces:**
- Consumes: context properties `modalUsage` (Task 3) and `songs` (Task 4).

- [ ] **Step 1: `src/songs.qrc`**

```xml
<RCC>
    <qresource prefix="/">
        <file alias="ModalMeter.qml">ModalMeter.qml</file>
        <file alias="SongsPage.qml">SongsPage.qml</file>
    </qresource>
</RCC>
```

- [ ] **Step 2: `src/ModalMeter.qml`**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Modal spending for the Songs page in the top bar: this month's estimate
// against the free credit. Click to connect Modal and set the limit.
Button {
    id: meter
    flat: true
    Layout.minimumWidth: implicitWidth
    function money(d) { return "$" + d.toLocaleString(Qt.locale(), 'f', 2) }
    readonly property color tint: modalUsage.level === "over" ? "#ff6b6b" : modalUsage.level === "warn" ? "#ffb74d" : "#7fd99a"

    text: !songs.configured ? qsTr("Modal") : qsTr("Modal ") + money(modalUsage.spent) + " / $" + modalUsage.limit.toLocaleString(Qt.locale(), 'f', 0)
    Material.foreground: modalUsage.level === "ok" ? Material.foreground : tint
    font.bold: modalUsage.level === "over"
    background: Rectangle {
        implicitHeight: 34
        radius: 17
        color: meter.hovered ? "#24242a" : "transparent"
        border.width: 1
        border.color: modalUsage.level === "ok" ? "#3a3a42" : meter.tint
        // The usage bar along the bottom edge.
        Rectangle {
            anchors { left: parent.left; bottom: parent.bottom; margins: 6 }
            height: 3; radius: 1.5
            width: (parent.width - 12) * Math.min(1, modalUsage.fraction)
            color: meter.tint
            visible: songs.configured
        }
    }
    ToolTip.visible: hovered && !panel.visible
    ToolTip.delay: 400
    ToolTip.text: qsTr("%1 songs this month · %2 s generating · %3 s idle GPU\nEstimates at Modal list prices; check modal.com/usage for the bill.")
                  .arg(modalUsage.songCount).arg(Math.round(modalUsage.busySeconds)).arg(Math.round(modalUsage.idleSeconds))
    onClicked: panel.open()

    Popup {
        id: panel
        y: meter.height + 4
        x: meter.width - width
        width: 400
        padding: 16
        background: Rectangle { color: "#1d1d21"; border.color: "#2a2a2f"; radius: 8 }
        onOpened: { endpoint.text = songs.endpoint; tokenId.text = songs.tokenId; tokenSecret.text = ""; result.text = "" }

        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: qsTr("Modal songs"); font.bold: true; font.pixelSize: 15 }
            Label {
                Layout.fillWidth: true
                text: meter.money(modalUsage.spent) + qsTr(" of $") + modalUsage.limit.toLocaleString(Qt.locale(), 'f', 0) + qsTr(" this month")
                color: meter.tint; font.pixelSize: 20; font.bold: true
            }
            ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: Math.min(1, modalUsage.fraction) }
            RowLayout {
                Label { text: qsTr("Monthly limit $"); color: "#9a9aa2" }
                SpinBox {
                    objectName: "modalLimit"
                    from: 1; to: 1000; editable: true
                    value: modalUsage.limit
                    onValueModified: modalUsage.limit = value
                }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#9a9aa2"; font.pixelSize: 12
                    text: qsTr("Modal's Starter plan includes $30 of free compute a month. Costs are estimated from GPU time.") }
            MenuSeparator { Layout.fillWidth: true }
            Label { text: qsTr("Connection"); font.bold: true }
            TextField { id: endpoint; objectName: "modalEndpoint"; Layout.fillWidth: true; placeholderText: qsTr("https://…--reelcut-acestep-api.modal.run") }
            TextField { id: tokenId; objectName: "modalTokenId"; Layout.fillWidth: true; placeholderText: qsTr("Proxy auth token ID (wk-…)") }
            TextField { id: tokenSecret; objectName: "modalTokenSecret"; Layout.fillWidth: true; echoMode: TextInput.Password
                        placeholderText: songs.configured ? qsTr("Secret saved; type to replace") : qsTr("Proxy auth token secret (ws-…)") }
            RowLayout {
                Button { text: qsTr("Save"); highlighted: true; onClicked: { songs.configure(endpoint.text, tokenId.text, tokenSecret.text); tokenSecret.text = ""; result.text = qsTr("Saved.") } }
                Button { text: qsTr("Test"); enabled: songs.configured; onClicked: { result.text = qsTr("Waking the GPU…"); songs.testConnection() } }
            }
            Label { id: result; Layout.fillWidth: true; wrapMode: Text.WordWrap; visible: text !== "" }
            Connections { target: songs; function onConnectionTested(ok, message) { result.text = message; result.color = ok ? "#7fd99a" : "#ff6b6b" } }
        }
    }
}
```

- [ ] **Step 3: Commit** (exercised by the QML load test in Task 6)

```bash
git add src/ModalMeter.qml src/songs.qrc
git commit -m "Song generator: Modal spend meter for the top bar"
```

---

### Task 6: `SongsPage.qml` and wiring

**Files:**
- Create: `src/SongsPage.qml`
- Modify: `src/main.cpp`, `reelcut.pro`, `src/Main.qml`, `tests/songs_tests.pro`, `tests/songs_tests.cpp`

**Interfaces:**
- Consumes: `songs` (`generate`, `writeLyrics`, `describeImages`, `songs`, `busy`, `stage`, `lastError`, signals), `modalUsage` (`level`), `backend.selectFiles("music", [path])` (existing).
- Produces: `win.songsVisible` in `Main.qml`; objectNames `songsToggle`, `songsPage`, `songGenerate`.

- [ ] **Step 1: Failing QML load test** — in `tests/songs_tests.pro` add `QT += qml quick quickcontrols2 multimedia` and `RESOURCES += ../src/songs.qrc`; add to `tests/songs_tests.cpp` (`#include <QQmlApplicationEngine>`, `#include <QQmlContext>`, `#include <QQuickItem>`):

```cpp
    void songsQmlLoads() {
        QTemporaryDir state, songs;
        ModalUsage usage(state.path());
        SongGenerator g(&usage, state.path(), songs.path());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("modalUsage", &usage);
        engine.rootContext()->setContextProperty("songs", &g);
        engine.rootContext()->setContextProperty("backend", nullptr);
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &w) {
            for (const auto &e : w) warnings << e.toString();
        });
        engine.loadData(R"(import QtQuick
import QtQuick.Controls
ApplicationWindow { width: 1200; height: 800; visible: true
  header: ToolBar { ModalMeter {} }
  SongsPage { anchors.fill: parent } })", QUrl("qrc:/SongsTestWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        auto *generate = engine.rootObjects().first()->findChild<QObject *>("songGenerate");
        QVERIFY(generate);
        QCOMPARE(generate->property("enabled").toBool(), false);   // not configured yet
    }
```

- [ ] **Step 2: Run, expect FAIL** (`SongsPage.qml` missing from qrc). Run: `bin/test-songs`.

- [ ] **Step 3: Write `src/SongsPage.qml`**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia

// Make a song with ACE-Step on Modal. Every box is optional: leave them all
// empty for a random song.
Rectangle {
    id: page
    objectName: "songsPage"
    color: "#141418"
    property string referencePath: ""
    property string remixPath: ""
    property var imagePaths: []
    property bool describing: false
    property bool writing: false
    property string note: ""
    readonly property var styleChips: ["pop", "hip-hop", "lo-fi", "EDM", "rock", "acoustic", "cinematic", "comedic"]

    function fileName(p) { return p.substring(p.lastIndexOf("/") + 1) }
    function localPath(url) { return decodeURIComponent(url.toString().replace(/^file:\/\//, "")) }
    function request() {
        const r = { prompt: prompt.text, style: style.text, imageStyle: imageStyle.text, lyrics: lyrics.text,
                    instrumental: instrumental.checked, versions: versions.value }
        if (length.text !== "") r.duration = Number(length.text)
        if (referencePath !== "") r.reference = referencePath
        if (remixPath !== "") {
            r.remix = remixPath
            r.remixMode = ["cover", "extend", "repaint"][remixMode.currentIndex]
            r.repaintStart = Number(repaintStart.text || 0)
            r.repaintEnd = Number(repaintEnd.text || 0)
        }
        return r
    }
    function startGenerate() {
        note = ""
        if (!songs.generate(request())) note = songs.configured ? qsTr("Already making a song.") : qsTr("Connect Modal first: click the Modal meter.")
    }

    Connections {
        target: songs
        function onFailed(message) { page.note = message }
        function onLyricsWritten(text, error) { page.writing = false; if (text !== "") lyrics.text = text; page.note = error }
        function onImagesDescribed(text, error) { page.describing = false; if (text !== "") imageStyle.text = text; page.note = error }
    }

    FileDialog { id: referenceDialog; title: qsTr("Reference song"); nameFilters: [qsTr("Audio or video (*.mp3 *.wav *.flac *.m4a *.ogg *.mp4 *.mov *.webm)")]
                 onAccepted: page.referencePath = page.localPath(selectedFile) }
    FileDialog { id: remixDialog; title: qsTr("Audio to remix"); nameFilters: referenceDialog.nameFilters
                 onAccepted: page.remixPath = page.localPath(selectedFile) }
    FileDialog { id: lyricsDialog; title: qsTr("Lyrics file"); nameFilters: [qsTr("Text (*.txt *.md)")]
                 onAccepted: lyrics.text = songs.readTextFile(page.localPath(selectedFile)) }
    FileDialog { id: imagesDialog; title: qsTr("Images"); fileMode: FileDialog.OpenFiles; nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
                 onAccepted: { page.imagePaths = selectedFiles.slice(0, 6).map(page.localPath); page.describing = true; songs.describeImages(page.imagePaths) } }
    MediaPlayer { id: player; audioOutput: AudioOutput {} }
    Dialog {
        id: overLimit
        title: qsTr("Over your Modal limit")
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: qsTr("This month's estimated spend has reached your limit. Make the song anyway?") }
        onAccepted: page.startGenerate()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 24

        // The request.
        ScrollView {
            Layout.preferredWidth: 460
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 10
                Label { text: qsTr("Make a song"); font.pixelSize: 22; font.bold: true }
                Label { text: qsTr("Every box is optional. Leave them all empty for a random song."); color: "#9a9aa2" }

                TextField { id: prompt; objectName: "songPrompt"; Layout.fillWidth: true; placeholderText: qsTr("What it's about") }
                TextField { id: style; objectName: "songStyle"; Layout.fillWidth: true; placeholderText: qsTr("Style: genre, mood, instruments") }
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    Repeater {
                        model: page.styleChips
                        delegate: Button { text: modelData; flat: true; onClicked: style.text = style.text === "" ? modelData : style.text + ", " + modelData }
                    }
                }
                TextArea { id: imageStyle; Layout.fillWidth: true; visible: text !== "" || page.describing
                           placeholderText: page.describing ? qsTr("Claude is looking at the images…") : ""; wrapMode: TextEdit.Wrap }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Lyrics"); font.bold: true; Layout.fillWidth: true }
                    CheckBox { id: instrumental; text: qsTr("Instrumental") }
                    Button { text: qsTr("Load .txt"); flat: true; onClicked: lyricsDialog.open() }
                    Button { text: page.writing ? qsTr("Writing…") : qsTr("Write lyrics"); flat: true
                             enabled: !page.writing && songs.configured
                             onClicked: { page.writing = true; songs.writeLyrics(page.request()) } }
                }
                TextArea { id: lyrics; objectName: "songLyrics"; Layout.fillWidth: true; Layout.preferredHeight: 160
                           enabled: !instrumental.checked; wrapMode: TextEdit.Wrap
                           placeholderText: qsTr("Empty: ACE-Step writes the words") }

                RowLayout {
                    Label { text: qsTr("Length (s)") }
                    TextField { id: length; Layout.preferredWidth: 80; placeholderText: qsTr("auto"); validator: IntValidator { bottom: 10; top: 240 } }
                    Label { text: qsTr("Versions") }
                    SpinBox { id: versions; from: 1; to: 4; value: 2 }
                }

                Label { text: qsTr("Attachments"); font.bold: true }
                // Inline components can't see page's id, so the name is worked out here.
                component Attachment: RowLayout {
                    id: attachment
                    property string label
                    property string path
                    signal pick()
                    signal clear()
                    Layout.fillWidth: true
                    Button { text: attachment.label; onClicked: attachment.pick() }
                    Label { text: attachment.path === "" ? qsTr("none") : attachment.path.substring(attachment.path.lastIndexOf("/") + 1)
                            color: "#9a9aa2"; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    ToolButton { text: "✕"; visible: attachment.path !== ""; onClicked: attachment.clear() }
                }
                Attachment { label: qsTr("Reference song"); path: page.referencePath; onPick: referenceDialog.open(); onClear: page.referencePath = "" }
                Attachment { label: qsTr("Audio to remix"); path: page.remixPath; onPick: remixDialog.open(); onClear: page.remixPath = "" }
                RowLayout {
                    visible: page.remixPath !== ""
                    ComboBox { id: remixMode; model: [qsTr("Cover in new style"), qsTr("Extend"), qsTr("Repaint a section")] }
                    TextField { id: repaintStart; visible: remixMode.currentIndex === 2; Layout.preferredWidth: 70; placeholderText: qsTr("from s") }
                    TextField { id: repaintEnd; visible: remixMode.currentIndex === 2; Layout.preferredWidth: 70; placeholderText: qsTr("to s") }
                }
                Attachment { label: qsTr("Images"); path: page.imagePaths.length ? qsTr("%n image(s)", "", page.imagePaths.length) : ""
                             onPick: imagesDialog.open(); onClear: { page.imagePaths = []; imageStyle.text = "" } }

                Button {
                    id: generateButton
                    objectName: "songGenerate"
                    Layout.fillWidth: true
                    highlighted: true
                    enabled: songs.configured && !songs.busy && !page.describing
                    text: songs.busy ? songs.stage : qsTr("Generate")
                    onClicked: modalUsage.level === "over" ? overLimit.open() : page.startGenerate()
                }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#ff8a65"; text: page.note; visible: text !== "" }
            }
        }

        // The songs.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label { text: qsTr("Your songs"); font.pixelSize: 18; font.bold: true }
            Label { text: songs.songsDir; color: "#6a6a72"; font.pixelSize: 11 }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 10
                model: songs.songs
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: column.implicitHeight + 20
                    radius: 8
                    color: "#1d1d21"
                    ColumnLayout {
                        id: column
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        Label { text: modelData.caption; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { color: "#9a9aa2"; font.pixelSize: 12
                                text: modelData.name + " · " + Math.round(modelData.seconds) + " s · ~$" + modelData.cost.toFixed(3) }
                        Repeater {
                            model: modelData.files
                            delegate: RowLayout {
                                required property string modelData
                                Layout.fillWidth: true
                                ToolButton {
                                    text: player.source.toString().endsWith(encodeURI(modelData)) && player.playbackState === MediaPlayer.PlayingState ? "❚❚" : "▶"
                                    onClicked: {
                                        if (player.playbackState === MediaPlayer.PlayingState && player.source.toString().endsWith(encodeURI(modelData))) { player.pause(); return }
                                        player.source = "file://" + modelData
                                        player.play()
                                    }
                                }
                                Label { text: page.fileName(modelData); Layout.fillWidth: true }
                                Button { text: qsTr("Use in reel"); flat: true; enabled: backend !== null; onClicked: backend.selectFiles("music", [modelData]) }
                                Button { text: qsTr("Remix this"); flat: true; onClicked: page.remixPath = modelData }
                            }
                        }
                        Button { text: qsTr("Show folder"); flat: true; onClicked: Qt.openUrlExternally("file://" + modelData.folder) }
                    }
                }
            }
        }
    }
}
```

- [ ] **Step 4: Run the QML test, expect PASS.** Run: `bin/test-songs`. Any QML warning fails the test; fix the QML rather than filtering warnings.

- [ ] **Step 5: Wire into the app**

`reelcut.pro` — append:

```
HEADERS += src/songrequest.h src/modalusage.h src/songgen.h
SOURCES += src/songrequest.cpp src/modalusage.cpp src/songgen.cpp
RESOURCES += src/songs.qrc
```

`src/main.cpp` — add `#include "modalusage.h"` and `#include "songgen.h"` next to `#include "higgsfield.h"`; next to `HiggsfieldCredits higgsfield;` add

```cpp
    ModalUsage modalUsage;
    SongGenerator songs(&modalUsage);
```

and next to `setContextProperty("higgsfield", &higgsfield);` add

```cpp
    engine.rootContext()->setContextProperty("modalUsage", &modalUsage);
    engine.rootContext()->setContextProperty("songs", &songs);
```

`src/Main.qml`:
1. Next to `property bool workflowVisible`, add `property bool songsVisible: false`.
2. In `typing:` add `|| songsVisible` after `workflowVisible`.
3. After `CreditsMeter {}` add
   ```qml
   Loader { active: typeof songs !== "undefined"; sourceComponent: Component { ModalMeter {} } }
   ```
4. After the Workflow `ToolButton`, add
   ```qml
   ToolButton {
       objectName: "songsToggle"
       visible: typeof songs !== "undefined"
       Layout.minimumWidth: implicitWidth
       text: qsTr("Songs")
       checkable: true
       checked: win.songsVisible
       onToggled: win.songsVisible = checked
       ToolTip.visible: hovered
       ToolTip.delay: 500
       ToolTip.text: qsTr("Make a song with ACE-Step on Modal")
   }
   ```
5. Next to the `WorkflowPage { … }` block, add a page shown the same way the workflow page is (copy its `visible`/anchors pattern, reading `win.songsVisible`):
   ```qml
   Loader {
       active: typeof songs !== "undefined" && win.songsVisible
       visible: win.songsVisible
       anchors.fill: workflowPage
       z: workflowPage.z
       sourceComponent: Component { SongsPage {} }
   }
   ```
   If `WorkflowPage` is not anchored in a way `anchors.fill: workflowPage` can follow, read how it is placed and mirror it exactly.
6. Where Escape closes the schedule/workflow pages (around line 195), add `else if (win.songsVisible) win.songsVisible = false` in the same chain.
7. Make showing one page hide the others: wherever `workflowVisible` is set true alongside clearing `scheduleVisible`/`statsVisible`, clear `songsVisible` too, and in `onSongsVisibleChanged` clear the others when it becomes true.

- [ ] **Step 6: Build and run all tests**

```bash
bin/build && bin/test-songs && bin/test
```

Expected: both suites pass. Then a screenshot check:

```bash
REELCUT_GRAB=/tmp/claude-1000/songs.png REELCUT_GRAB_AT=3 build/reelcut
```

Open the screenshot; then start `build/reelcut`, click **Songs**, and confirm the page, the chips, the attachments and the meter appear.

- [ ] **Step 7: Commit**

```bash
git add src/SongsPage.qml src/main.cpp reelcut.pro src/Main.qml tests/songs_tests.pro tests/songs_tests.cpp
git commit -m "Song generator: Songs page and top-bar wiring"
```

---

### Task 7: MCP / automation

**Files:**
- Modify: `src/automationregistry.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: `AutomationRegistry::add(name, QObject*)` (existing), `AppBridge::registry()` (existing).
- Produces: MCP tools `app_songs_generate`, `app_songs_reload`, `app_songs_write_lyrics`, … generated from `SongGenerator`'s invokables.

- [ ] **Step 1:** In `src/automationregistry.cpp` add `#include "songgen.h"` and the surface `{"songs", &SongGenerator::staticMetaObject}` to `surfaces`. In `isSensitive`, `configure`'s `tokenSecret` is already covered by the `secret` substring rule; confirm by reading it.
- [ ] **Step 2:** In `src/main.cpp`, after `bridge.attach(...)`, add `bridge.registry().add("songs", &songs);`.
- [ ] **Step 3:** Add `../src/songgen.h ../src/songrequest.h ../src/modalusage.h` and their `.cpp` files to `tests/reelcut_tests.pro`'s HEADERS/SOURCES (it links `automationregistry.cpp`). Run `bin/build && bin/test`. If an MCP coverage test lists every tool name, add the new `songs` tools to its expected list.
- [ ] **Step 4:** Verify live: start `build/reelcut`, then from Claude Code call `mcp__reelcut__get_app_capabilities` and confirm `songs.generate` is listed.
- [ ] **Step 5: Commit**

```bash
git add src/automationregistry.cpp src/main.cpp tests/reelcut_tests.pro
git commit -m "Song generator: expose the Songs page to the Reelcut MCP"
```

---

### Task 8: Deploy, real song, docs

**Files:**
- Create: `docs/songs.md`

- [ ] **Step 1: Reinstall Modal and log in** (user runs the login in their terminal)

```bash
uv tool install modal
# user: ! modal setup
```

- [ ] **Step 2: Download the models into the volume, then deploy**

```bash
modal run modal/acestep_app.py::download      # ~10 GB, once
modal deploy modal/acestep_app.py              # prints the api URL
```

If the image build fails on `uv sync --frozen`, read the error, adjust the image (for example `uv sync --frozen --no-dev`), and redeploy. Record any change in the spec.

- [ ] **Step 3: Create a proxy auth token** — user creates one at modal.com → Settings → Proxy Auth Tokens and pastes the ID and secret into the Modal meter popup with the printed URL. Click **Test**; expect "Connected".

- [ ] **Step 4: Generate one real song** — prompt "upbeat pop song about editing reels", length 60, 1 version. Record: wall time from Generate to finished, `billedSeconds` and `estimatedCost` from `request.json`, cold-start time. Play the take. Also try **Write lyrics** once. If `/format_input` returns empty lyrics, remove the Write lyrics button from `SongsPage.qml` (empty lyrics already make ACE-Step write the words during generation through `sample_mode`), note it in the spec, and tell the user.

- [ ] **Step 5: Write `docs/songs.md`** — setup (Steps 1–3), usage, the measured numbers from Step 4, the cost formula, and "estimates, check modal.com/usage". Commit:

```bash
git add docs/songs.md
git commit -m "Song generator: setup and measured cost"
```

- [ ] **Step 6: Finish** — use superpowers:finishing-a-development-branch. Note for the merge: `~/reelcut` has other sessions' uncommitted edits in `src/Main.qml` and `src/automationregistry.cpp`, which this branch also changes, so a plain `git merge` will refuse. Show the user `git status` and ask before stashing anything.
