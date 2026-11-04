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
    // True while captions sit on the platform's home spot and follow it.
    Q_PROPERTY(bool captionAuto READ captionAuto NOTIFY projectChanged)
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
    // Every platform (src/posting.h) as maps: id, label, limits, safe zone.
    Q_PROPERTY(QVariantList platforms READ platforms CONSTANT)
    // The project's platform, as one of those maps.
    Q_PROPERTY(QVariantMap platformInfo READ platformInfo NOTIFY projectChanged)
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
    Q_PROPERTY(QString postCaption READ postCaption NOTIFY projectChanged)
    Q_PROPERTY(QString postHashtags READ postHashtags NOTIFY projectChanged)
    // The Instagram account Reelcut can publish to through Instagram's API, or empty.
    Q_PROPERTY(QString instagramUser READ instagramUser NOTIFY instagramChanged)
    // Scheduled posts (src/postscheduler.h): whether the backend is set up, and what's on it.
    Q_PROPERTY(bool schedulerReady READ schedulerReady NOTIFY schedulerChanged)
    Q_PROPERTY(QVariantList scheduledPosts READ scheduledPosts NOTIFY schedulerChanged)
    Q_PROPERTY(QString schedulerError READ schedulerError NOTIFY schedulerChanged)
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
    bool captionAuto() const { return m_project.captionAuto; }
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
    QVariantList platforms() const;
    QVariantMap platformInfo() const;
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
    QString postCaption() const { return m_project.postCaption; }
    QString postHashtags() const { return m_project.postHashtags; }
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
    // Puts the captions back on the platform's home spot.
    Q_INVOKABLE void resetCaptionPosition();

    // Files and jobs
    Q_INVOKABLE void newProject();
    Q_INVOKABLE void open();
    Q_INVOKABLE bool openFile(const QString &path);
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveAs();
    Q_INVOKABLE void exportVideo();
    // Posting (src/posting.h): what the caption will be and whether it fits,
    // then export, open the site's upload page, show the file and copy the text.
    Q_INVOKABLE QVariantMap checkPost(const QString &platform, const QString &caption, const QString &hashtags) const;
    // Successive keystrokes share one undo step when newUndoStep is false.
    Q_INVOKABLE void setPostText(const QString &caption, const QString &hashtags, bool newUndoStep = true);
    // publish: Instagram only, through its API (src/instagramapi.h), when an
    // account is connected. Otherwise the site opens for you to post by hand.
    // publishAtMs, when set (ms since the epoch), schedules the Instagram post
    // for then instead of publishing now.
    Q_INVOKABLE void post(const QString &platform, const QString &caption, const QString &hashtags,
                          bool publish = false, double publishAtMs = 0);
    // A typed time to post at: "2026-11-05 18:30", "tomorrow 9am", "today 6:30pm", "fri 9:00".
    // Gives ok, ms, label ("Thursday, Nov 5 at 6:30 PM · in 13 hours") and problem.
    Q_INVOKABLE QVariantMap parseWhen(const QString &text) const;
    // The next full hour, at least an hour away, as parseWhen reads it; or text moved by minutes.
    Q_INVOKABLE QString suggestWhen(const QString &text = {}, int minutes = 0) const;
    Q_INVOKABLE void setupScheduler(const QString &url, const QString &key);
    Q_INVOKABLE void refreshScheduled();
    Q_INVOKABLE void cancelScheduled(const QString &id);
    Q_INVOKABLE void rescheduleScheduled(const QString &id, double publishAtMs);
    Q_INVOKABLE void retryScheduled(const QString &id);
    Q_INVOKABLE void removeScheduled(const QString &id);
    bool schedulerReady() const;
    QVariantList scheduledPosts() const;
    QString schedulerError() const;
    class PostScheduler *scheduler() const { return m_scheduler; }
    Q_INVOKABLE void connectInstagram(const QString &token);
    Q_INVOKABLE void disconnectInstagram();
    Q_INVOKABLE void openLink(const QUrl &url);
    QString instagramUser() const;
    void openForHand(const QString &platform);
    class InstagramApi *instagram() const { return m_instagram; }
    Q_INVOKABLE void copyPostText();
    Q_INVOKABLE void showPostVideo();
    // What posting does outside the app: open a web page, show a file. Tests
    // replace both so nothing opens.
    std::function<void(const QUrl &)> openUrl;
    std::function<void(const QString &)> revealFile;
    QString postFolder;  // where posted videos are exported; empty for ~/Videos
    // A photo set for TikTok's photo mode, one JPG per clip, in ~/Videos.
    Q_INVOKABLE void exportPhotos();
    Q_INVOKABLE void cancelJob();
    Q_INVOKABLE void dismissMessage();
    Q_INVOKABLE void showExport();

signals:
    // The upload page is open, the file shown and the text copied.
    void postOpened(const QString &platform, const QString &file, const QString &steps);
    void instagramChanged();
    void instagramConnected(bool ok, const QString &message);
    // Publishing through the API: what's happening (fraction -1 when unknown), then the result.
    void postProgress(const QString &text, double fraction);
    void postPublished(bool ok, const QString &message, const QUrl &link);
    void postScheduled(bool ok, const QString &message);
    void schedulerChanged();
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
    std::function<void(const QString &)> m_afterExport;  // runs once an export succeeds
    QString m_postFile;
    class InstagramApi *m_instagram = nullptr;
    class PostScheduler *m_scheduler = nullptr;

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
