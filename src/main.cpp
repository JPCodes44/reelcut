// reelcut — make vertical reels: cut, caption, color, music, export.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "backend.h"
#include "jobs.h"
#include "mcpserver.h"
#include "portalfilepicker.h"
#include "theme.h"
#include "thumbprovider.h"
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
        QCoreApplication app(argc, argv);
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

    QQmlApplicationEngine engine;
    // The engine takes ownership of the image provider.
    engine.addImageProvider("thumbs", thumbs);
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

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

    // For screenshots of the real window (README, checking a change offscreen):
    // REELCUT_GRAB=out.png [REELCUT_GRAB_SECONDS=3] [REELCUT_GRAB_AT=seconds on the timeline].
    const QString grab = qEnvironmentVariable("REELCUT_GRAB");
    if (!grab.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const int delay = qEnvironmentVariableIntValue("REELCUT_GRAB_SECONDS") * 1000;
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
