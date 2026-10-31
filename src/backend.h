#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVector>

#include <functional>
#include <atomic>
#include <memory>

#include "jobs.h"
#include "project.h"

class FilePicker;
class QNetworkAccessManager;
class QNetworkReply;
class Theme;
class ThumbProvider;

// The app's state and work, handed to QML as `backend`. Project edits go
// through here so undo, the dirty flag and the views stay in step.
//
// Discrete actions (split, delete, cut silences) record an undo step on
// their own. Continuous edits (sliders, typing) don't, so a drag isn't a
// hundred steps: QML calls checkpoint() when the edit begins.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList clips READ clips NOTIFY projectChanged)
    Q_PROPERTY(double duration READ duration NOTIFY projectChanged)
    Q_PROPERTY(QVariantList captionLines READ captionLines NOTIFY projectChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY projectChanged)
    Q_PROPERTY(QString captionStyle READ captionStyle NOTIFY projectChanged)
    Q_PROPERTY(double captionX READ captionX NOTIFY projectChanged)
    Q_PROPERTY(double captionY READ captionY NOTIFY projectChanged)
    Q_PROPERTY(double captionSize READ captionSize NOTIFY projectChanged)
    Q_PROPERTY(QVariantList overlays READ overlays NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap music READ music NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap look READ look NOTIFY projectChanged)
    Q_PROPERTY(QVariantList filters READ filters CONSTANT)
    Q_PROPERTY(QVariantList transitions READ transitions CONSTANT)
    Q_PROPERTY(QVariantList effectKinds READ effectKinds CONSTANT)
    Q_PROPERTY(QVariantList effects READ effects NOTIFY projectChanged)
    Q_PROPERTY(QVariantList bodyKinds READ bodyKinds CONSTANT)
    Q_PROPERTY(QString platform READ platform NOTIFY projectChanged)
    Q_PROPERTY(QString captionFont READ captionFont CONSTANT)
    Q_PROPERTY(double captionScale READ captionScale CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(QString title READ title NOTIFY projectChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY jobChanged)
    Q_PROPERTY(double progress READ progress NOTIFY jobChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
    Q_PROPERTY(QString lastExport READ lastExport NOTIFY messageChanged)
    Q_PROPERTY(QString whisperState READ whisperState NOTIFY jobChanged)

public:
    // stateDir is where the open project's path is recorded for `reelcut mcp`;
    // tests pass a temp dir.
    Backend(FilePicker *picker, ThumbProvider *thumbs, Theme *theme, QObject *parent = nullptr,
            const QString &stateDir = {});
    ~Backend() override;

    QVariantList clips() const;
    double duration() const { return m_project.duration(); }
    QVariantList captionLines() const;
    int wordCount() const { return m_project.words.size(); }
    QString captionStyle() const { return m_project.captionStyle; }
    double captionX() const { return m_project.captionX; }
    double captionY() const { return m_project.captionY; }
    double captionSize() const { return m_project.captionScale; }
    QVariantList overlays() const;
    QVariantMap music() const;
    QVariantMap look() const;
    // The filter library: id, label and three swatch colors per look.
    QVariantList filters() const;
    // The transition library: id and label.
    QVariantList transitions() const;
    // The effects library (id, label, mode, usesSpeed) and the effects track.
    QVariantList effectKinds() const;
    QVariantList effects() const;
    QVariantList bodyKinds() const;
    QString platform() const { return m_project.platform; }
    QString captionFont() const;
    // Qt pixel size per ASS font size unit; libass sizes a font by its line
    // height (ascent + descent), Qt by its em, so the preview scales by this.
    double captionScale() const;
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool dirty() const { return m_dirty; }
    QString path() const { return m_path; }  // the open .reel, empty if never saved
    QString title() const;
    bool busy() const { return !m_busyText.isEmpty(); }
    QString busyText() const { return m_busyText; }
    double progress() const { return m_progress; }
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }
    QString lastExport() const { return m_lastExport; }
    // ready, no-whisper (the tool isn't installed), or no-model (downloads on first use)
    QString whisperState() const;

    const Project &project() const { return m_project; }

    // Undo
    Q_INVOKABLE void checkpoint();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Clips
    Q_INVOKABLE void addMedia();
    Q_INVOKABLE void addUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void removeClip(int index);
    Q_INVOKABLE void moveClip(int from, int to);
    Q_INVOKABLE bool split(double time);
    // in, out, speed, zoom, panX, panY; values are clamped to what makes sense
    Q_INVOKABLE void setClipValue(int index, const QString &key, double value);
    Q_INVOKABLE void setClipMotion(int index, const QString &motion);
    // The transition from clip `index` into the next; empty for a cut.
    Q_INVOKABLE void setClipTransition(int index, const QString &id);
    // A body effect on a clip; empty removes it. Finds the person in the
    // clip in the background if that hasn't been done for these frames yet.
    Q_INVOKABLE void setClipBody(int index, const QString &id);
    // Color wheels (src/colorgrade.h): index -1 is the whole reel.
    Q_INVOKABLE void setGrade(int index, const QString &key, double value);
    // The eyedropper: the color at (x, y), 0..1 across a grabbed image.
    Q_INVOKABLE QString pixelColor(const QVariant &image, double x, double y) const;
    // Makes any person masks the project's body effects still need, then
    // runs `then` (on success). Shows progress; Cancel stops it.
    void findPeople(std::function<void()> then = {});
    Q_INVOKABLE QVariantMap locate(double time) const;
    Q_INVOKABLE void cutSilences(int index);

    // Captions
    Q_INVOKABLE void transcribe();
    Q_INVOKABLE void setWordText(int index, const QString &text);
    Q_INVOKABLE void clearCaptions();
    Q_INVOKABLE void setCaptionStyle(const QString &style);
    Q_INVOKABLE void setCaptionY(double y);
    // x, y or scale; no undo step of its own (call checkpoint() first).
    Q_INVOKABLE void setCaptionValue(const QString &key, double value);

    // Text overlays
    Q_INVOKABLE int addOverlay(double time);
    // The "Fisheye Text" preset: a fisheye-style overlay at `time`.
    Q_INVOKABLE int addFisheyeText(double time);
    Q_INVOKABLE void setOverlayValue(int index, const QString &key, const QVariant &value);
    Q_INVOKABLE void removeOverlay(int index);

    // Video effects
    // An effect at `time`; returns its index, or -1 for an unknown kind.
    Q_INVOKABLE int addEffect(const QString &kind, double time);
    // start, end, intensity, speed; no undo step of its own (call checkpoint() first).
    Q_INVOKABLE void setEffectValue(int index, const QString &key, double value);
    Q_INVOKABLE void removeEffect(int index);

    // Music and look
    Q_INVOKABLE void chooseMusic();
    Q_INVOKABLE void removeMusic();
    Q_INVOKABLE void setMusicValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void setLookValue(const QString &key, double value);
    Q_INVOKABLE void applyPreset(const QString &name);
    // A look from the filter library; empty removes it. Records an undo step.
    Q_INVOKABLE void setFilter(const QString &id);
    Q_INVOKABLE void chooseLut();
    Q_INVOKABLE void clearLut();
    Q_INVOKABLE void setPlatform(const QString &platform);

    // Files and jobs
    Q_INVOKABLE void newProject();
    Q_INVOKABLE void open();
    Q_INVOKABLE bool openFile(const QString &path);
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveAs();
    Q_INVOKABLE void exportVideo();
    // A photo set for TikTok's photo mode, one JPG per clip, in ~/Videos.
    Q_INVOKABLE void exportPhotos();
    Q_INVOKABLE void cancelJob();
    Q_INVOKABLE void dismissMessage();
    Q_INVOKABLE void showExport();

signals:
    void projectChanged();
    void jobChanged();
    void messageChanged();

private:
    void picked(int kind, const QList<QUrl> &urls);
    void changed();
    void restore(const QJsonObject &json);
    bool writeProject(const QString &path);
    void setBusy(const QString &text, double progress = -1.0);
    void clearBusy();
    void setMessage(const QString &text, bool error);
    int thumbKey(const QString &path);
    void runExport(const QString &output, const QString &encoder);
    void downloadModel();
    // Follows the project file, so edits made elsewhere (the MCP server) show up.
    void watchProject();
    void reloadIfChangedOutside();
    void recordOpenProject();
    void startTranscription();

    FilePicker *m_picker;
    ThumbProvider *m_thumbs;
    Theme *m_theme;
    Project m_project;
    QVector<QJsonObject> m_undo;
    QVector<QJsonObject> m_redo;
    bool m_dirty = false;
    QString m_path;

    Job m_job;
    // What to do when the running job ends; jobs chain (audio, then whisper).
    std::function<void(bool, const QByteArray &, const QByteArray &)> m_then;
    std::unique_ptr<QTemporaryDir> m_work;
    QString m_busyText;
    double m_progress = -1.0;
    int m_probing = 0;
    QString m_message;
    bool m_messageIsError = false;
    QString m_lastExport;

    QHash<QString, int> m_thumbKeys;
    QString m_stateDir;
    QFileSystemWatcher m_watcher;
    QTimer *m_reloadDebounce = nullptr;
    // The file as last read or written here; anything else on disk came from outside.
    QByteArray m_onDisk;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_download = nullptr;
    std::shared_ptr<std::atomic_bool> m_stopMasks;
};
