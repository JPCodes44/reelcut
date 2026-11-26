// reelcut — make vertical reels: cut, caption, color, music, export.

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "backend.h"
#include "appbridge.h"
#include "downloads.h"
#include "higgsfield.h"
#include "modalusage.h"
#include "songgen.h"
#include "jobs.h"
#include "mcpserver.h"
#include "portalfilepicker.h"
#include "stats.h"
#include "theme.h"
#include "thumbprovider.h"
#include "waveformprovider.h"
#include "translator.h"

namespace {
const char *kUsage =
    "Usage:\n"
    "  reelcut [FILE...]                    open the editor, with a project or clips\n"
    "  reelcut export PROJECT.reel OUT.mp4  render a project without the window\n"
    "  reelcut mcp                          serve the Model Context Protocol on stdio\n"
    "  reelcut help                         show this help\n"
    "\n"
    "Add --json to export for a machine-readable result.\n";

// Renders a project from the command line, for scripts and agents: exits 0
// with the output written, or 1 with the reason on stderr.
int exportCommand(const QStringList &args) {
    QTextStream out(stdout), err(stderr);
    const bool json = args.contains("--json");
    QStringList positional;
    for (const QString &arg : args)
        if (!arg.startsWith("--"))
            positional.append(arg);

    auto fail = [&](const QString &message) {
        if (json)
            out << QJsonDocument(QJsonObject{{"ok", false}, {"error", message}}).toJson(QJsonDocument::Compact) << "\n";
        err << "reelcut: " << message << "\n";
        return 1;
    };
    if (positional.size() != 2)
        return fail("export needs a project and an output file");

    const QString projectPath = QFileInfo(positional[0]).absoluteFilePath();
    const QString output = QFileInfo(positional[1]).absoluteFilePath();
    QFile file(projectPath);
    const QJsonDocument doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()) : QJsonDocument();
    if (!doc.isObject())
        return fail(QString("%1 is not a Reelcut project").arg(positional[0]));
    const Project project = Project::fromJson(doc.object(), QFileInfo(projectPath).absolutePath());
    if (project.clips.isEmpty())
        return fail("the project has no clips");
    for (const Clip &clip : project.clips)
        if (!QFileInfo::exists(clip.path))
            return fail(QString("%1 is missing").arg(clip.path));

    QString encoder, error;
    if (!exportProject(project, Theme().accent(), output, &encoder, &error))
        return fail(error);
    if (json)
        out << QJsonDocument(QJsonObject{{"ok", true}, {"output", output}, {"encoder", encoder},
                                         {"duration", project.duration()}})
                   .toJson(QJsonDocument::Compact)
            << "\n";
    else
        out << "Exported " << output << "\n";
    return 0;
}
}  // namespace

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (command == "export" || command == "mcp" || command == "help" || command == "--help" || command == "-h") {
        // Fisheye text is drawn with QPainter, which needs fonts and so a
        // QGuiApplication; offscreen, since there's no window.
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication app(argc, argv);
        app.setApplicationName("reelcut");
        if (command == "export")
            return exportCommand(app.arguments().mid(2));
        if (command == "mcp")
            return McpServer().run();
        QTextStream(stdout) << kUsage;
        return 0;
    }

    // Qt's ffmpeg backend can fall through to Vulkan video and pick a
    // decoder that never falls back, leaving the preview black (see Omacut).
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "vaapi,cuda");

#ifdef Q_OS_LINUX
    // Without a display Qt aborts (and dumps core) creating the platform
    // integration; say what's wrong instead.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && qEnvironmentVariableIsEmpty("DISPLAY")) {
        QTextStream(stderr) << "reelcut: no display found (WAYLAND_DISPLAY and DISPLAY are unset).\n"
                               "Run it from a desktop session, or use `reelcut export` without a window.\n";
        return 1;
    }
#endif

    QGuiApplication app(argc, argv);
    app.setApplicationName("reelcut");
    app.setApplicationVersion("0.4.1");

    // Matches the window to reelcut.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("reelcut");
    app.setWindowIcon(QIcon::fromTheme("reelcut"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    PortalFilePicker picker;
    auto *thumbs = new ThumbProvider();
    Backend backend(&picker, thumbs, &theme);
    Downloads downloads;
    HiggsfieldCredits higgsfield;
    ModalUsage modalUsage;
    SongGenerator songs(&modalUsage);
    // Reads the accounts of Backend's publishers: made after the Backend, so
    // it's gone before them, and before the engine that shows it.
    Stats stats(backend.instagram(), backend.facebook(), backend.youtube());

    QQmlApplicationEngine engine;
    // The engine takes ownership of the image provider.
    engine.addImageProvider("thumbs", thumbs);
    engine.addImageProvider("waveforms", new WaveformProvider());
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("downloads", &downloads);
    engine.rootContext()->setContextProperty("higgsfield", &higgsfield);
    engine.rootContext()->setContextProperty("modalUsage", &modalUsage);
    engine.rootContext()->setContextProperty("songs", &songs);
    engine.rootContext()->setContextProperty("stats", &stats);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    AppBridge bridge;
    bridge.attach(&backend,&downloads,&stats,&higgsfield,qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    bridge.registry().add("songs", &songs);
    bridge.registry().add("theme",&theme);
    bridge.registry().add("language",&translator);
    if (!bridge.listen()) qWarning("reelcut: the live MCP interface could not be started");

    // A project opens as is; videos are added as clips.
    QList<QUrl> clips;
    for (const QString &arg : app.arguments().mid(1)) {
        const QString path = QFileInfo(arg).absoluteFilePath();
        if (path.endsWith(".reel"))
            backend.openFile(path);
        else if (QFileInfo::exists(path))
            clips.append(QUrl::fromLocalFile(path));
    }
    if (!clips.isEmpty())
        backend.addUrls(clips);

    // Live reload while developing: with REELCUT_DEV_RELOAD set, a rebuilt
    // binary replaces the running app. The project is saved, the new build
    // starts on it at the same playhead, and this one exits.
    auto *rootWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (qEnvironmentVariableIsSet("REELCUT_DEV_RELOAD") && rootWindow) {
        QPointer<QQuickWindow> window = rootWindow;
        const QString exe = QCoreApplication::applicationFilePath();
        const QDateTime built = QFileInfo(exe).lastModified();
        auto *watcher = new QFileSystemWatcher({QFileInfo(exe).absolutePath()}, &app);
        auto *settle = new QTimer(&app);  // let the linker finish writing
        settle->setSingleShot(true);
        settle->setInterval(1500);
        QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, settle, qOverload<>(&QTimer::start));
        QObject::connect(settle, &QTimer::timeout, &app, [&backend, window, exe, built] {
            const QFileInfo binary(exe);
            if (!binary.exists() || !binary.isExecutable() || binary.lastModified() <= built)
                return;
            if (backend.dirty()) {
                if (backend.path().isEmpty()) {
                    qWarning("reelcut: new build ready; save the project to switch to it");
                    return;
                }
                backend.save();
            }
            if (!window)
                return;
            qputenv("REELCUT_RESUME_AT", QByteArray::number(window->property("playhead").toDouble()));
            if (QProcess::startDetached(exe, backend.path().isEmpty() ? QStringList() : QStringList{backend.path()}))
                QCoreApplication::exit(0);
        });
    }
    if (qEnvironmentVariableIsSet("REELCUT_RESUME_AT") && rootWindow) {
        auto *window = rootWindow;
        QTimer::singleShot(500, window, [window] {
            QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, qEnvironmentVariable("REELCUT_RESUME_AT").toDouble()));
        });
        qunsetenv("REELCUT_RESUME_AT");
    }

    // For screenshots of the real window (README, checking a change offscreen):
    // REELCUT_GRAB=out.png [REELCUT_GRAB_SECONDS=3] [REELCUT_GRAB_AT=seconds on the timeline]
    // [REELCUT_GRAB_SELECT=kind:index] [REELCUT_GRAB_TAB=inspector tab] [REELCUT_GRAB_SIZE=WxH]
    // [REELCUT_GRAB_SCROLL=pixels].
    const QString grab = qEnvironmentVariable("REELCUT_GRAB");
    if (!grab.isEmpty() && rootWindow) {
        auto *window = rootWindow;
        const int delay = qEnvironmentVariableIntValue("REELCUT_GRAB_SECONDS") * 1000;
        // REELCUT_GRAB_SELECT=clip:0 (or overlay:1, effect:0) selects something first,
        // REELCUT_GRAB_TAB=3 then opens that inspector tab.
        // REELCUT_GRAB_SIZE=1200x2400 sizes the window, to see a long panel whole.
        const QStringList size = qEnvironmentVariable("REELCUT_GRAB_SIZE").split('x');
        if (size.size() == 2)
            window->resize(size[0].toInt(), size[1].toInt());
        const QStringList select = qEnvironmentVariable("REELCUT_GRAB_SELECT").split(':');
        if (select.size() == 2)
            QTimer::singleShot(qMax(400, delay / 3), window, [window, select] {
                QMetaObject::invokeMethod(window, "select", Q_ARG(QVariant, select[0]), Q_ARG(QVariant, select[1].toInt()));
            });
        if (qEnvironmentVariableIsSet("REELCUT_GRAB_TAB"))
            QTimer::singleShot(qMax(450, delay / 3 + 50), window, [window] {
                if (QObject *inspector = window->findChild<QObject *>("inspector"))
                    inspector->setProperty("currentIndex", qEnvironmentVariableIntValue("REELCUT_GRAB_TAB"));
            });
        // REELCUT_GRAB_SCROLL=600 scrolls the inspector's visible panel down that far.
        if (qEnvironmentVariableIsSet("REELCUT_GRAB_SCROLL"))
            QTimer::singleShot(qMax(800, delay / 2), window, [window] {
                QObject *inspector = window->findChild<QObject *>("inspector");
                for (QQuickItem *item : inspector ? inspector->findChildren<QQuickItem *>() : QList<QQuickItem *>())
                    if (item->inherits("QQuickFlickable") && item->isVisible() && item->width() > 0)
                        item->setProperty("contentY", qEnvironmentVariableIntValue("REELCUT_GRAB_SCROLL"));
            });
        if (qEnvironmentVariableIsSet("REELCUT_GRAB_AT"))
            QTimer::singleShot(qMax(500, delay / 2), window, [window] {
                QMetaObject::invokeMethod(window, "seek", Q_ARG(QVariant, qEnvironmentVariable("REELCUT_GRAB_AT").toDouble()));
            });
        QTimer::singleShot(delay > 0 ? delay : 3000, window, [window, grab] {
            window->grabWindow().save(grab);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
