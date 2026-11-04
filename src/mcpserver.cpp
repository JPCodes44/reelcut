#include "mcpserver.h"

#include "bodyeffects.h"
#include "effects.h"
#include "filters.h"
#include "transitions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <iostream>
#include <string>

#include "captions.h"
#include "jobs.h"
#include "media.h"
#include "theme.h"

namespace {
constexpr int kUndoLimit = 100;
const char *kInstructions =
    "Reelcut edits vertical 1080x1920 reels for Instagram, Facebook, YouTube Shorts and TikTok. Work on one .reel project: "
    "create_project or open_project first (open_project with no path picks the project open in the Reelcut "
    "window). Every edit saves the file at once and the window reloads it live. Times are seconds on the "
    "timeline unless a tool says source seconds. Clip indexes start at 0. Use preview_frame to look at the "
    "result before export, and undo if an edit went wrong.";

QJsonObject prop(const QString &type, const QString &description) {
    return {{"type", type}, {"description", description}};
}

QJsonObject enumProp(const QStringList &values, const QString &description) {
    return {{"type", "string"}, {"enum", QJsonArray::fromStringList(values)}, {"description", description}};
}

QJsonObject text(const QString &message) {
    return {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", message}}}}};
}

QJsonObject json(const QJsonObject &object) {
    return text(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));
}

QJsonObject failure(const QString &message) {
    QJsonObject result = text(message);
    result.insert("isError", true);
    return result;
}

// Thrown by handlers for bad arguments; becomes an isError result.
struct ToolError {
    QString message;
};

// The color wheels argument shared by edit_clip and set_look.
QJsonObject wheelsProp() {
    return prop("object",
                "DaVinci-style color wheels, as {key: number}. Keys: lift.x, lift.y, lift.master, and the same "
                "for gamma, gain and offset (x, y: the puck in the unit disc, red at the top and hue clockwise "
                "to green and blue; master -1 to 1); contrast (0-2, default 1), pivot (0-1, default 0.435), "
                "saturation (0-2, default 1), hue (degrees), temperature and tint (-1 to 1). "
                "{\"reset\": 1} clears them, \"reset.lift\" one wheel.");
}

// Applies a color_wheels argument to a grade target (-1: the whole reel).
void applyWheels(Project &p, int index, const QJsonValue &value) {
    const QJsonObject wheels = value.toObject();
    // Resets first, so {"reset": 1, "gain.master": 0.2} starts clean.
    for (auto it = wheels.begin(); it != wheels.end(); ++it)
        if (it.key().startsWith("reset") && !p.setGrade(index, it.key(), 0))
            throw ToolError{QString("Unknown color wheel key %1.").arg(it.key())};
    for (auto it = wheels.begin(); it != wheels.end(); ++it)
        if (!it.key().startsWith("reset") && !p.setGrade(index, it.key(), it.value().toDouble()))
            throw ToolError{QString("Unknown color wheel key %1.").arg(it.key())};
}

}  // namespace

// ---- Reading arguments ----

namespace {
int requireIndex(const QJsonObject &args, const QString &key, int count, const QString &what) {
    if (!args.contains(key))
        throw ToolError{QString("'%1' is required.").arg(key)};
    const int index = args.value(key).toInt(-1);
    if (index < 0 || index >= count)
        throw ToolError{QString("There is no %1 %2; there are %3.").arg(what).arg(args.value(key).toInt()).arg(count)};
    return index;
}

QString absolute(const QString &path) {
    if (path.startsWith("~/"))
        return QDir::homePath() + path.mid(1);
    return QFileInfo(path).absoluteFilePath();
}

Clip clipFromFile(const QString &path, double photoSeconds = Clip::PhotoSeconds) {
    const media::VideoInfo info = media::probe(path);
    if (!info.ok)
        throw ToolError{QString("%1: %2").arg(path, info.error)};
    return media::clipFor(info, photoSeconds);
}

QString accent() {
    return Theme().accent();
}

// A Reelcut window, not this server or another one, is running.
bool windowRunning() {
    for (const QString &pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile cmdline("/proc/" + pid + "/cmdline");
        if (!cmdline.open(QIODevice::ReadOnly))
            continue;
        const QList<QByteArray> argv = cmdline.readAll().split('\0');
        if (!argv.isEmpty() && QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == "reelcut"
            && argv.value(1) != "mcp" && argv.value(1) != "export")
            return true;
    }
    return false;
}
}  // namespace

McpServer::McpServer(const QString &stateDir)
    : m_stateDir(stateDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/reelcut"
                                    : stateDir) {
    defineTools();
}

QString McpServer::openProjectFile(const QString &stateDir) {
    const QString dir = stateDir.isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/reelcut"
                            : stateDir;
    return dir + "/open-project";
}

// ---- Protocol ----

int McpServer::run() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const QByteArray bytes = QByteArray::fromStdString(line).trimmed();
        if (bytes.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes);
        const QJsonObject response = doc.isObject()
            ? handle(doc.object())
            : QJsonObject{{"jsonrpc", "2.0"}, {"id", QJsonValue::Null},
                          {"error", QJsonObject{{"code", -32700}, {"message", "Parse error"}}}};
        if (!response.isEmpty())
            std::cout << QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
    }
    return 0;
}

QJsonObject McpServer::handle(const QJsonObject &request) {
    const QString method = request.value("method").toString();
    const QJsonValue id = request.value("id");
    // Notifications get no answer.
    if (!request.contains("id"))
        return {};

    auto reply = [&](const QJsonObject &result) { return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}; };
    auto error = [&](int code, const QString &message) {
        return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
    };

    if (method == "initialize") {
        const QString version = request.value("params").toObject().value("protocolVersion").toString("2025-06-18");
        return reply({{"protocolVersion", version},
                      {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
                      {"serverInfo", QJsonObject{{"name", "reelcut"}, {"version", "0.4.1"}}},
                      {"instructions", kInstructions}});
    }
    if (method == "ping")
        return reply({});
    if (method == "tools/list") {
        QJsonArray tools;
        for (const QString &name : m_order)
            tools.append(m_tools.value(name).definition);
        return reply({{"tools", tools}});
    }
    if (method == "tools/call") {
        const QJsonObject params = request.value("params").toObject();
        const QString name = params.value("name").toString();
        if (!m_tools.contains(name))
            return error(-32602, QString("Unknown tool: %1").arg(name));
        return reply(callTool(name, params.value("arguments").toObject()));
    }
    return error(-32601, QString("Method not found: %1").arg(method));
}

// ---- Projects on disk ----

QString McpServer::resolvePath(const QJsonObject &args, QString *error) const {
    if (args.contains("project"))
        return absolute(args.value("project").toString());
    if (!m_current.isEmpty())
        return m_current;
    QFile state(openProjectFile(m_stateDir));
    if (state.open(QIODevice::ReadOnly)) {
        const QString path = QString::fromUtf8(state.readAll()).trimmed();
        if (QFileInfo::exists(path))
            return path;
    }
    *error = "No project yet. Call create_project, or open_project with a path.";
    return {};
}

bool McpServer::load(const QString &path, Project *project, QString *error) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QString("Could not open %1.").arg(path);
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        *error = QString("%1 is not a Reelcut project.").arg(path);
        return false;
    }
    *project = Project::fromJson(doc.object(), QFileInfo(path).absolutePath());
    return true;
}

bool McpServer::save(const QString &path, const Project &project, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const QByteArray data = QJsonDocument(project.toJson(QFileInfo(path).absolutePath())).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QString("Could not save %1.").arg(path);
        return false;
    }
    return true;
}

QJsonObject McpServer::summary(const Call &call, bool withWords) const {
    const Project &p = call.project;
    QJsonArray clips;
    const QVector<double> starts = p.clipStarts();
    for (int i = 0; i < p.clips.size(); ++i) {
        const Clip &c = p.clips[i];
        clips.append(QJsonObject{
            {"index", i}, {"file", c.path}, {"start", starts[i]}, {"duration", c.duration()},
            {"source_in", c.in}, {"source_out", c.out}, {"source_duration", c.sourceDuration},
            {"speed", c.speed}, {"zoom", c.zoom}, {"pan_x", c.panX}, {"pan_y", c.panY},
            {"size", QString("%1x%2").arg(c.width).arg(c.height)}, {"has_audio", c.audio},
            {"photo", c.still}, {"motion", c.motion},
            {"transition", c.transition}, {"transition_seconds", c.transitionSeconds},
            {"overlap", p.overlap(i)}, {"body", c.body}, {"body_amount", c.bodyAmount},
            {"color_wheels", c.grade.isNeutral() ? QJsonValue(QJsonValue::Null) : QJsonValue(c.grade.toJson())},
            {"missing", !QFileInfo::exists(c.path)},
        });
    }
    QJsonArray overlays;
    for (int i = 0; i < p.overlays.size(); ++i) {
        const Overlay &o = p.overlays[i];
        overlays.append(QJsonObject{{"index", i}, {"text", o.text}, {"start", o.start}, {"end", o.end},
                                    {"x", o.x}, {"position", o.y}, {"size", o.size}, {"animation", o.animation},
                                    {"style", o.style}, {"bulge", o.bulge}, {"fill", o.colorOf("fill")},
                                    {"outline", o.colorOf("outline")}, {"shadow", o.colorOf("shadow")},
                                    {"box", o.colorOf("box")}, {"font", o.fontOf()}, {"bold", o.boldOf()},
                                    {"italic", o.italic}, {"outline_width", o.outlineWidthOf()},
                                    {"shadow_distance", o.shadowDistanceOf()}, {"letter_spacing", o.letterSpacingOf()},
                                    {"no_shadow", o.noShadow}, {"thickness", o.thickness}});
    }
    QJsonArray effects;
    for (int i = 0; i < p.effects.size(); ++i) {
        const Effect &e = p.effects[i];
        effects.append(QJsonObject{{"index", i}, {"kind", e.kind}, {"start", e.start}, {"end", e.end},
                                   {"intensity", e.intensity}, {"speed", e.speed}});
    }
    QJsonArray lines;
    for (const CaptionLine &line : groupLines(p.words))
        lines.append(QJsonObject{{"start", line.start}, {"end", line.end}, {"text", line.text()}});
    QJsonObject captions{{"style", p.captionStyle}, {"x", p.captionX}, {"position", p.captionY}, {"follows_platform", p.captionAuto},
                         {"size", p.captionScale}, {"word_count", int(p.words.size())},
                         {"lines", lines}};
    if (withWords) {
        QJsonArray words;
        for (int i = 0; i < p.words.size(); ++i)
            words.append(QJsonObject{{"index", i}, {"text", p.words[i].text}, {"start", p.words[i].start},
                                     {"end", p.words[i].end}});
        captions.insert("words", words);
    }
    return {
        {"project", call.path},
        {"platform", p.platform},
        {"duration", p.duration()},
        {"clips", clips},
        {"captions", captions},
        {"text_overlays", overlays},
        {"effects", effects},
        {"look", QJsonObject{{"exposure", p.look.exposure}, {"contrast", p.look.contrast},
                             {"saturation", p.look.saturation}, {"temperature", p.look.temperature},
                             {"lut", p.look.lut}, {"filter", p.look.filter},
                             {"filter_amount", p.look.filterAmount},
                             {"color_wheels", p.look.grade.isNeutral() ? QJsonValue(QJsonValue::Null)
                                                                       : QJsonValue(p.look.grade.toJson())}}},
        {"music", p.music.isSet() ? QJsonValue(QJsonObject{{"file", p.music.path}, {"volume", p.music.volume},
                                                           {"start_in_song", p.music.offset}, {"duck", p.music.duck}})
                                  : QJsonValue(QJsonValue::Null)},
    };
}

QJsonObject McpServer::callTool(const QString &name, const QJsonObject &args) {
    const Tool &tool = m_tools[name];
    Call call;
    call.args = args;
    try {
        if (tool.needsProject) {
            QString error;
            call.path = resolvePath(args, &error);
            if (call.path.isEmpty())
                return failure(error);
            if (!load(call.path, &call.project, &error))
                return failure(error);
        }
        const QJsonObject before = call.project.toJson();
        QJsonObject result = tool.handler(call);
        if (tool.edits && !result.value("isError").toBool()) {
            QString error;
            if (!save(call.path, call.project, &error))
                return failure(error);
            auto &undo = m_undo[call.path];
            undo.append(before);
            if (undo.size() > kUndoLimit)
                undo.removeFirst();
            m_redo[call.path].clear();
        }
        if (!call.path.isEmpty())
            m_current = call.path;
        return result;
    } catch (const ToolError &e) {
        return failure(e.message);
    }
}

void McpServer::addTool(const QString &name, const QString &description, const QJsonObject &properties,
                        const QStringList &required, bool edits, Handler handler, bool needsProject) {
    QJsonObject props = properties;
    if (needsProject && !props.contains("project"))
        props.insert("project", prop("string", "Path of the .reel file. Defaults to the project in use."));
    QJsonObject schema{{"type", "object"}, {"properties", props}};
    if (!required.isEmpty())
        schema.insert("required", QJsonArray::fromStringList(required));
    QJsonObject definition{{"name", name}, {"description", description}, {"inputSchema", schema}};
    if (!edits)
        definition.insert("annotations", QJsonObject{{"readOnlyHint", true}});
    m_tools.insert(name, {definition, std::move(handler), edits, needsProject});
    m_order.append(name);
}

// ---- Tools ----

void McpServer::defineTools() {
    // Projects

    addTool("create_project",
            "Start a new reel project file, optionally with clips in order. It becomes the project in use.",
            {{"path", prop("string", "Where to write the .reel file (the extension is added if missing).")},
             {"clips", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
                                   {"description", "Video files to add, in timeline order."}}},
             {"platform", enumProp({"instagram", "facebook", "youtube", "tiktok"}, "Which app's safe zones to design for.")}},
            {"path"}, false, [this](Call &call) {
                QString path = absolute(call.args.value("path").toString());
                if (!path.endsWith(".reel"))
                    path += ".reel";
                if (QFileInfo::exists(path))
                    throw ToolError{QString("%1 already exists. Use open_project to edit it.").arg(path)};
                call.path = path;
                for (const QJsonValue &file : call.args.value("clips").toArray())
                    call.project.clips.append(clipFromFile(absolute(file.toString())));
                if (call.args.contains("platform"))
                    call.project.setPlatform(call.args.value("platform").toString());
                QString error;
                if (!save(path, call.project, &error))
                    throw ToolError{error};
                return json(summary(call));
            }, false);

    addTool("open_project",
            "Use an existing .reel project. Without a path, picks the project open in the Reelcut window.",
            {{"path", prop("string", "The .reel file.")}}, {}, false, [this](Call &call) {
                QString error;
                QJsonObject args;
                if (call.args.contains("path"))
                    args.insert("project", call.args.value("path"));
                const QString keep = m_current;
                if (!call.args.contains("path"))
                    m_current.clear();
                call.path = resolvePath(args, &error);
                if (call.path.isEmpty()) {
                    m_current = keep;
                    throw ToolError{call.args.contains("path") ? error
                        : QString("No project is open in the Reelcut window (an unsaved one can't be reached; "
                                  "save it first).")};
                }
                if (!load(call.path, &call.project, &error))
                    throw ToolError{error};
                return json(summary(call));
            }, false);

    addTool("get_project", "Everything in the project: clips with timeline positions, captions, text, look, music.",
            {{"include_words", prop("boolean", "Also list every caption word with its index and timing.")}}, {},
            false, [this](Call &call) { return json(summary(call, call.args.value("include_words").toBool())); });

    addTool("probe_media", "Read a video or audio file's duration, size and whether it has sound.",
            {{"file", prop("string", "The media file.")}}, {"file"}, false, [](Call &call) {
                const media::VideoInfo info = media::probe(absolute(call.args.value("file").toString()));
                if (!info.ok)
                    throw ToolError{info.error};
                return json({{"file", info.path}, {"photo", info.still}, {"duration", info.duration},
                             {"width", info.width}, {"height", info.height}, {"has_audio", info.audio}});
            }, false);

    addTool("set_platform", "Design for Instagram Reels, Facebook Reels, YouTube Shorts or TikTok (changes the safe zones shown and the file name).",
            {{"platform", enumProp({"instagram", "facebook", "youtube", "tiktok"}, "The platform.")}}, {"platform"}, true, [this](Call &call) {
                call.project.setPlatform(call.args.value("platform").toString());
                return json(summary(call));
            });

    // Clips

    addTool("add_clips",
            "Add videos and/or photos to the timeline, at the end or at a position. Photos (jpg, png, webp, heic) "
            "become slides that show for photo_seconds with a slow zoom; a slideshow is just a timeline of photos.",
            {{"files", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
                                   {"description", "Video or photo files, in order."}}},
             {"at", prop("integer", "Clip index to insert before. Default: the end.")},
             {"photo_seconds", prop("number", "How long each photo shows. Default 2.5.")},
             {"motion", enumProp(Clip::motions(), "Movement for the photos added. Default zoom-in.")}},
            {"files"}, true, [this](Call &call) {
                int at = call.args.contains("at") ? qBound(0, call.args.value("at").toInt(), int(call.project.clips.size()))
                                                  : int(call.project.clips.size());
                const double seconds = call.args.value("photo_seconds").toDouble(Clip::PhotoSeconds);
                for (const QJsonValue &file : call.args.value("files").toArray()) {
                    Clip clip = clipFromFile(absolute(file.toString()), seconds);
                    if (clip.still && call.args.contains("motion")) {
                        if (!Clip::motions().contains(call.args.value("motion").toString()))
                            throw ToolError{"Unknown motion. Use one of: " + Clip::motions().join(", ")};
                        clip.motion = call.args.value("motion").toString();
                    }
                    call.project.clips.insert(at++, clip);
                }
                return json(summary(call));
            });

    addTool("delete_clip", "Remove a clip from the timeline.", {{"index", prop("integer", "Clip index.")}}, {"index"},
            true, [this](Call &call) {
                call.project.clips.remove(requireIndex(call.args, "index", call.project.clips.size(), "clip"));
                return json(summary(call));
            });

    addTool("move_clip", "Move a clip to another position in the timeline.",
            {{"from", prop("integer", "Clip index to move.")}, {"to", prop("integer", "Its new index.")}},
            {"from", "to"}, true, [this](Call &call) {
                const int n = call.project.clips.size();
                call.project.clips.move(requireIndex(call.args, "from", n, "clip"), requireIndex(call.args, "to", n, "clip"));
                return json(summary(call));
            });

    addTool("split", "Split the clip under a timeline time into two clips.",
            {{"time", prop("number", "Timeline seconds.")}}, {"time"}, true, [this](Call &call) {
                if (!call.project.split(call.args.value("time").toDouble()))
                    throw ToolError{"Nothing to split there (no clip, or right on a clip edge)."};
                return json(summary(call));
            });

    addTool("edit_clip",
            "Change a clip: trim (source seconds), speed, and framing in the 9:16 frame; for photos, how long they "
            "show and how they move. Values are clamped.",
            {{"index", prop("integer", "Clip index.")},
             {"duration", prop("number", "Seconds on the timeline (photos: how long the slide shows).")},
             {"motion", enumProp(Clip::motions(), "Photos only: slow zoom or pan over the slide.")},
             {"source_in", prop("number", "Where the clip starts in its file, in seconds.")},
             {"source_out", prop("number", "Where the clip ends in its file, in seconds.")},
             {"speed", prop("number", "0.25 to 4.")},
             {"zoom", prop("number", "1 (fills the frame) to 3.")},
             {"pan_x", prop("number", "-1 (left edge) to 1 (right edge); 0 is centered.")},
             {"pan_y", prop("number", "-1 (top) to 1 (bottom); 0 is centered.")},
             {"transition", enumProp(QStringList{""} + Transitions::ids(),
                                     "Transition from this clip into the next. Empty string makes it a cut. "
                                     "A transition overlaps the two clips, so the reel gets shorter.")},
             {"transition_seconds", prop("number", "0.1 to 2. Default 0.5. Limited to half of either clip.")},
             {"body", enumProp(QStringList{""} + Body::ids(),
                               "A body effect: treats the person and the background differently. Empty removes it. "
                               "The person is found with a segmentation model before export or preview_frame "
                               "(downloaded once, ~6 MB).")},
             {"body_amount", prop("number", "0 to 1, how strong the body effect is. Default 0.8.")},
             {"color_wheels", wheelsProp()}},
            {"index"}, true, [this](Call &call) {
                const int index = requireIndex(call.args, "index", call.project.clips.size(), "clip");
                if (call.args.contains("transition") &&
                    !call.project.setClipTransition(index, call.args.value("transition").toString()))
                    throw ToolError{"Unknown transition. Use one of: " + Transitions::ids().join(", ")};
                if (call.args.contains("body") && !call.project.setClipBody(index, call.args.value("body").toString()))
                    throw ToolError{"Unknown body effect. Use one of: " + Body::ids().join(", ")};
                if (call.args.contains("body_amount"))
                    call.project.setClipValue(index, "bodyAmount", call.args.value("body_amount").toDouble());
                if (call.args.contains("color_wheels"))
                    applyWheels(call.project, index, call.args.value("color_wheels"));
                if (call.args.contains("transition_seconds"))
                    call.project.setClipValue(index, "transitionSeconds", call.args.value("transition_seconds").toDouble());
                const QList<QPair<QString, QString>> keys{{"source_in", "in"}, {"source_out", "out"}, {"speed", "speed"},
                                                          {"zoom", "zoom"}, {"pan_x", "panX"}, {"pan_y", "panY"}};
                // Out before in, so widening a clip both ways in one call works.
                if (call.args.contains("source_out") && call.args.value("source_out").toDouble() > call.project.clips[index].out)
                    call.project.setClipValue(index, "out", call.args.value("source_out").toDouble());
                for (const auto &key : keys)
                    if (call.args.contains(key.first))
                        call.project.setClipValue(index, key.second, call.args.value(key.first).toDouble());
                if (call.args.contains("duration"))
                    call.project.setClipValue(index, "duration", call.args.value("duration").toDouble());
                if (call.args.contains("motion")) {
                    if (!call.project.clips[index].still)
                        throw ToolError{"Only photos have motion."};
                    if (!call.project.setClipMotion(index, call.args.value("motion").toString()))
                        throw ToolError{"Unknown motion. Use one of: " + Clip::motions().join(", ")};
                }
                return json(summary(call));
            });

    addTool("cut_silences", "Remove pauses from clips with sound, keeping 0.1 s around speech. Default: every clip.",
            {{"index", prop("integer", "Only this clip.")},
             {"threshold_db", prop("number", "Quieter than this counts as silence. Default -35.")},
             {"min_silence", prop("number", "Shortest pause to cut, in seconds. Default 0.4.")}},
            {}, true, [this](Call &call) {
                const double threshold = call.args.value("threshold_db").toDouble(-35.0);
                const double minimum = call.args.value("min_silence").toDouble(0.4);
                QVector<int> indexes;
                if (call.args.contains("index"))
                    indexes = {requireIndex(call.args, "index", call.project.clips.size(), "clip")};
                else
                    for (int i = 0; i < call.project.clips.size(); ++i)
                        indexes.append(i);
                // From the end, so cutting a clip into pieces doesn't shift the ones still to do.
                double total = 0.0;
                for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) {
                    double removed = 0.0;
                    QString error;
                    if (!::cutSilences(call.project, *it, threshold, minimum, &removed, &error))
                        throw ToolError{error};
                    total += removed;
                }
                QJsonObject result = summary(call);
                result.insert("removed_seconds", total);
                return json(result);
            });

    // Captions

    addTool("transcribe",
            "Caption the reel: transcribe the clips' speech on this computer with Whisper, word by word. "
            "Replaces existing captions. The first run downloads a 148 MB model.",
            {}, {}, true, [this](Call &call) {
                if (call.project.clips.isEmpty())
                    throw ToolError{"Add clips first."};
                QString error;
                const QVector<Word> words = transcribeProject(call.project, &error);
                if (words.isEmpty())
                    throw ToolError{error};
                call.project.words = words;
                return json(summary(call, true));
            });

    addTool("set_captions", "Caption style and position, or set the caption words yourself.",
            {{"style", enumProp({"highlight", "pop", "bold", "box"},
                                "highlight: line with the spoken word in the accent color; pop: one word at a time; "
                                "bold: plain white line; box: white text on a dark box.")},
             {"position", prop("number", "Vertical center, 0.05 (top) to 0.95 (bottom). Default: just above the platform's own caption area (0.68 Instagram, 0.55 Facebook, 0.63 YouTube, 0.65 TikTok), following the platform until set.")},
             {"x", prop("number", "Horizontal center, 0 (left) to 1 (right). Default: the middle of the platform's safe zone.")},
             {"size", prop("number", "Size, 0.5 to 2 times normal. Default 1.")},
             {"words", QJsonObject{{"type", "array"},
                                   {"description", "Replace all caption words, each {text, start, end} in timeline seconds."},
                                   {"items", QJsonObject{{"type", "object"},
                                                         {"properties", QJsonObject{{"text", prop("string", "")},
                                                                                    {"start", prop("number", "")},
                                                                                    {"end", prop("number", "")}}},
                                                         {"required", QJsonArray{"text", "start", "end"}}}}}},
             {"clear", prop("boolean", "Remove all captions.")}},
            {}, true, [this](Call &call) {
                Project &p = call.project;
                if (call.args.contains("style")) {
                    const QString style = call.args.value("style").toString();
                    if (!QStringList{"highlight", "pop", "bold", "box"}.contains(style))
                        throw ToolError{"Unknown caption style."};
                    p.captionStyle = style;
                }
                if (call.args.contains("position"))
                    p.setCaptionValue("y", call.args.value("position").toDouble());
                if (call.args.contains("x"))
                    p.setCaptionValue("x", call.args.value("x").toDouble());
                if (call.args.contains("size"))
                    p.setCaptionValue("scale", call.args.value("size").toDouble());
                if (call.args.value("clear").toBool())
                    p.words.clear();
                if (call.args.contains("words")) {
                    p.words.clear();
                    for (const QJsonValue &value : call.args.value("words").toArray()) {
                        const QJsonObject w = value.toObject();
                        p.words.append({w.value("text").toString(), w.value("start").toDouble(), w.value("end").toDouble()});
                    }
                    std::sort(p.words.begin(), p.words.end(), [](const Word &a, const Word &b) { return a.start < b.start; });
                }
                return json(summary(call, true));
            });

    addTool("edit_words", "Fix caption words by index (see get_project include_words). Empty text deletes the word.",
            {{"edits", QJsonObject{{"type", "array"},
                                   {"items", QJsonObject{{"type", "object"},
                                                         {"properties", QJsonObject{{"index", prop("integer", "")},
                                                                                    {"text", prop("string", "")}}},
                                                         {"required", QJsonArray{"index", "text"}}}}}}},
            {"edits"}, true, [this](Call &call) {
                Project &p = call.project;
                QVector<QPair<int, QString>> edits;
                for (const QJsonValue &value : call.args.value("edits").toArray()) {
                    const QJsonObject e = value.toObject();
                    edits.append({requireIndex(e, "index", p.words.size(), "word"), e.value("text").toString().simplified()});
                }
                // Highest index first, so deletions don't shift the rest.
                std::sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
                for (const auto &edit : edits) {
                    if (edit.second.isEmpty())
                        p.words.remove(edit.first);
                    else
                        p.words[edit.first].text = edit.second;
                }
                return json(summary(call, true));
            });

    // Text overlays

    auto overlayProps = [] {
        return QJsonObject{
            {"text", prop("string", "The text. Use \\n for a line break.")},
            {"start", prop("number", "Timeline seconds it appears.")},
            {"end", prop("number", "Timeline seconds it disappears.")},
            {"position", prop("number", "Vertical center, 0.05 (top) to 0.95. Default 0.2.")},
            {"x", prop("number", "Horizontal center, 0 (left) to 1 (right). Default 0.5.")},
            {"size", prop("integer", "Font size at 1080x1920, 40 to 200. Default 96.")},
            {"animation", enumProp({"pop", "fade", "slide", "none"}, "How it enters. Default pop.")},
            {"style", enumProp({"plain", "fisheye"},
                               "plain: white with an outline. fisheye: yellow serif bulged through a lens, "
                               "biggest in the middle. Default plain.")},
            {"bulge", prop("number", "Fisheye strength, 0 to 1. Default 0.6.")},
            {"fill", prop("string", "Text color: #rrggbb, #aarrggbb (aa = opacity), a color name, "
                                    "\"transparent\", or \"default\" for the style's own.")},
            {"outline", prop("string", "Outline color, same forms as fill.")},
            {"shadow", prop("string", "Shadow color, same forms as fill. Plain text has none by default.")},
            {"box", prop("string", "Color of a box behind the text, same forms as fill. None by default; "
                                   "on plain text a box replaces the outline.")},
            {"font", prop("string", "A font family installed on this computer, e.g. \"Liberation Serif\". "
                                    "\"default\" for the style's own (plain: Noto Sans, fisheye: Liberation Serif).")},
            {"bold", prop("boolean", "Bold. Plain text is bold by default, fisheye is not.")},
            {"italic", prop("boolean", "Italic. Default false.")},
            {"outline_width", prop("number", "Outline thickness in pixels at 1080x1920, 0 to 30; -1 for the "
                                             "style's default (plain 8, fisheye 0).")},
            {"shadow_distance", prop("number", "How far the shadow falls, in pixels, 0 to 30; -1 for the default.")},
            {"letter_spacing", prop("number", "Extra pixels between letters, -20 to 60, or \"default\" for the "
                                              "font's own spacing (the default).")},
            {"no_shadow", prop("boolean", "True turns the shadow off entirely. Default false.")},
            {"thickness", prop("number", "Makes the letters themselves heavier, in their fill color: pixels at "
                                         "1080x1920, 0 to 20. Default 0. Any outline goes outside it.")}};
    };
    auto applyOverlay = [](Project &p, int index, const QJsonObject &args) {
        if (args.contains("end"))
            p.setOverlayValue(index, "end", args.value("end").toDouble());
        const QList<QPair<QString, QString>> keys{{"text", "text"}, {"start", "start"}, {"end", "end"},
                                                  {"position", "y"}, {"x", "x"}, {"size", "size"}, {"animation", "animation"},
                                                  {"style", "style"}, {"bulge", "bulge"}, {"fill", "fill"},
                                                  {"outline", "outline"}, {"shadow", "shadow"}, {"box", "box"},
                                                  {"font", "font"}, {"bold", "bold"}, {"italic", "italic"},
                                                  {"outline_width", "outlineWidth"}, {"shadow_distance", "shadowDistance"},
                                                  {"letter_spacing", "letterSpacing"}, {"no_shadow", "noShadow"},
                                                  {"thickness", "thickness"}};
        for (const auto &key : keys)
            if (args.contains(key.first) && !p.setOverlayValue(index, key.second, args.value(key.first).toVariant()))
                throw ToolError{QString("Invalid %1.").arg(key.first)};
    };

    addTool("add_text", "Put text over the video for a stretch of time, like a hook title.", overlayProps(), {"text"},
            true, [this, applyOverlay](Call &call) {
                const double start = call.args.value("start").toDouble(0.0);
                const int index = call.project.addOverlay(call.args.value("text").toString(), start,
                                                          call.args.value("end").toDouble(-1.0));
                applyOverlay(call.project, index, call.args);
                return json(summary(call));
            });

    QJsonObject editProps = overlayProps();
    editProps.insert("index", prop("integer", "Text overlay index."));
    addTool("edit_text", "Change a text overlay.", editProps, {"index"}, true, [this, applyOverlay](Call &call) {
        applyOverlay(call.project, requireIndex(call.args, "index", call.project.overlays.size(), "text overlay"), call.args);
        return json(summary(call));
    });

    addTool("remove_text", "Delete a text overlay.", {{"index", prop("integer", "Text overlay index.")}}, {"index"}, true,
            [this](Call &call) {
                call.project.overlays.remove(requireIndex(call.args, "index", call.project.overlays.size(), "text overlay"));
                return json(summary(call));
            });

    // Video effects

    auto effectProps = [&]() {
        return QJsonObject{
            {"start", prop("number", "Timeline seconds where it begins.")},
            {"end", prop("number", "Timeline seconds where it ends. Default 1.5 s after start.")},
            {"intensity", prop("number", "0 to 1. Default 0.6.")},
            {"speed", prop("number", "0 to 1, for effects that move (shake, zoom-pulse, strobe, glitch, "
                                     "mono-flicker). Default 0.5.")}};
    };
    auto applyEffect = [](Project &p, int index, const QJsonObject &args) {
        // End before start when moving later, so neither clamps against the old value.
        if (args.contains("end") && args.value("end").toDouble() > p.effects[index].end)
            p.setEffectValue(index, "end", args.value("end").toDouble());
        for (const QString &key : QStringList{"start", "end", "intensity", "speed"})
            if (args.contains(key))
                p.setEffectValue(index, key, args.value(key).toDouble());
    };
    QJsonObject addEffectProps = effectProps();
    addEffectProps.insert("kind", enumProp(Effects::ids(), "Which effect."));
    addTool("add_effect",
            "Add a video effect for a stretch of the timeline, on the effects track. Effects stack in order.",
            addEffectProps, {"kind"}, true, [this, applyEffect](Call &call) {
                const double start = call.args.value("start").toDouble(0.0);
                const int index = call.project.addEffect(call.args.value("kind").toString(), start,
                                                         call.args.value("end").toDouble(-1.0));
                if (index < 0)
                    throw ToolError{"Unknown effect. Use one of: " + Effects::ids().join(", ")};
                applyEffect(call.project, index, call.args);
                return json(summary(call));
            });
    QJsonObject editEffectProps = effectProps();
    editEffectProps.insert("index", prop("integer", "Effect index."));
    addTool("edit_effect", "Change a video effect's timing, intensity or speed.", editEffectProps, {"index"}, true,
            [this, applyEffect](Call &call) {
                applyEffect(call.project, requireIndex(call.args, "index", call.project.effects.size(), "effect"), call.args);
                return json(summary(call));
            });
    addTool("remove_effect", "Delete a video effect.", {{"index", prop("integer", "Effect index.")}}, {"index"}, true,
            [this](Call &call) {
                call.project.effects.remove(requireIndex(call.args, "index", call.project.effects.size(), "effect"));
                return json(summary(call));
            });

    // Look

    addTool("set_look",
            "Color the whole reel: a preset, sliders from -1 to 1 (0 is untouched), a filter from the "
            "built-in library, and/or a .cube LUT. A preset is applied first, then any sliders given.",
            {{"preset", enumProp(Project::presets(), "A starting look.")},
             {"exposure", prop("number", "-1 to 1.")},
             {"contrast", prop("number", "-1 to 1.")},
             {"saturation", prop("number", "-1 (black and white) to 1.")},
             {"temperature", prop("number", "-1 (cool) to 1 (warm).")},
             {"filter", enumProp(QStringList{""} + Filters::ids(), "A look from the filter library. Empty string removes it.")},
             {"filter_amount", prop("number", "0 to 1, how strong the filter is. Default 1.")},
             {"lut", prop("string", "A .cube file, applied over the sliders. Empty string removes it.")},
             {"color_wheels", wheelsProp()}},
            {}, true, [this](Call &call) {
                Project &p = call.project;
                if (call.args.contains("preset") && !p.applyPreset(call.args.value("preset").toString()))
                    throw ToolError{"Unknown preset. Use one of: " + Project::presets().join(", ")};
                for (const QString &key : {"exposure", "contrast", "saturation", "temperature"})
                    if (call.args.contains(key))
                        p.setLookValue(key, call.args.value(key).toDouble());
                if (call.args.contains("filter") && !p.setFilter(call.args.value("filter").toString()))
                    throw ToolError{"Unknown filter. Use one of: " + Filters::ids().join(", ")};
                if (call.args.contains("filter_amount"))
                    p.setLookValue("filterAmount", call.args.value("filter_amount").toDouble());
                if (call.args.contains("color_wheels"))
                    applyWheels(p, -1, call.args.value("color_wheels"));
                if (call.args.contains("lut")) {
                    const QString lut = call.args.value("lut").toString();
                    if (!lut.isEmpty() && !QFileInfo::exists(absolute(lut)))
                        throw ToolError{QString("%1 doesn't exist.").arg(lut)};
                    p.look.lut = lut.isEmpty() ? QString() : absolute(lut);
                }
                return json(summary(call));
            });

    // Music

    addTool("set_music", "Add, adjust or remove the music track. Music is lowered under speech when duck is on.",
            {{"file", prop("string", "An audio file. Empty string removes the music.")},
             {"volume", prop("number", "0 to 1.5. Default 0.35.")},
             {"start_in_song", prop("number", "Seconds into the song where the reel starts.")},
             {"duck", prop("boolean", "Lower the music while clips have sound. Default true.")}},
            {}, true, [this](Call &call) {
                Music &m = call.project.music;
                if (call.args.contains("file")) {
                    const QString file = call.args.value("file").toString();
                    if (file.isEmpty()) {
                        m = {};
                    } else {
                        if (!QFileInfo::exists(absolute(file)))
                            throw ToolError{QString("%1 doesn't exist.").arg(file)};
                        m.path = absolute(file);
                    }
                }
                if (!m.isSet() && (call.args.contains("volume") || call.args.contains("duck") || call.args.contains("start_in_song")))
                    throw ToolError{"There's no music yet; pass a file."};
                if (call.args.contains("volume"))
                    call.project.setMusicValue("volume", call.args.value("volume").toDouble());
                if (call.args.contains("start_in_song"))
                    call.project.setMusicValue("offset", call.args.value("start_in_song").toDouble());
                if (call.args.contains("duck"))
                    call.project.setMusicValue("duck", call.args.value("duck").toBool());
                return json(summary(call));
            });

    // History

    addTool("undo", "Undo the last edit made through this server.", {}, {}, false, [this](Call &call) {
        auto &undo = m_undo[call.path];
        if (undo.isEmpty())
            throw ToolError{"Nothing to undo."};
        m_redo[call.path].append(call.project.toJson());
        call.project = Project::fromJson(undo.takeLast());
        QString error;
        if (!save(call.path, call.project, &error))
            throw ToolError{error};
        return json(summary(call));
    });

    addTool("redo", "Redo an edit undone with undo.", {}, {}, false, [this](Call &call) {
        auto &redo = m_redo[call.path];
        if (redo.isEmpty())
            throw ToolError{"Nothing to redo."};
        m_undo[call.path].append(call.project.toJson());
        call.project = Project::fromJson(redo.takeLast());
        QString error;
        if (!save(call.path, call.project, &error))
            throw ToolError{error};
        return json(summary(call));
    });

    // Looking and output

    addTool("preview_frame",
            "See the finished reel at a moment: a PNG of exactly what the export draws there "
            "(framing, look, LUT, captions, text).",
            {{"time", prop("number", "Timeline seconds. Default 0.")},
             {"width", prop("integer", "Image width in pixels, 180 to 1080. Default 540.")}},
            {}, false, [](Call &call) {
                QString error;
                const int width = qBound(180, call.args.value("width").toInt(540), 1080) / 2 * 2;
                const QByteArray png = renderFrame(call.project, accent(), call.args.value("time").toDouble(), width, &error);
                if (png.isEmpty())
                    throw ToolError{error};
                return QJsonObject{{"content", QJsonArray{
                    QJsonObject{{"type", "image"}, {"mimeType", "image/png"}, {"data", QString::fromLatin1(png.toBase64())}},
                    QJsonObject{{"type", "text"}, {"text", QString("Frame at %1 s of %2 s.")
                                                               .arg(call.args.value("time").toDouble())
                                                               .arg(call.project.duration())}}}}};
            });

    addTool("export",
            "Render the reel to an MP4 ready to post: 1080x1920, 30 fps, H.264 (GPU when available), AAC, "
            "loudness -14 LUFS. Takes about as long as the reel on the CPU, much less on the GPU.",
            {{"output", prop("string", "The .mp4 to write. Default: next to the project, named after it.")}}, {}, false,
            [](Call &call) {
                QString output = call.args.value("output").toString();
                if (output.isEmpty()) {
                    const QFileInfo project(call.path);
                    output = project.absolutePath() + "/" + project.completeBaseName()
                             + (call.project.platform == "tiktok" ? "-tiktok" : "-reel") + ".mp4";
                }
                output = absolute(output);
                if (!output.endsWith(".mp4", Qt::CaseInsensitive))
                    output += ".mp4";
                QString encoder, error;
                if (!exportProject(call.project, accent(), output, &encoder, &error))
                    throw ToolError{error};
                const media::VideoInfo info = media::probe(output);
                return json({{"output", output}, {"encoder", encoder}, {"duration", info.duration},
                             {"size", QString("%1x%2").arg(info.width).arg(info.height)},
                             {"bytes", QFileInfo(output).size()}});
            });

    addTool("export_photos",
            "Render a photo set for TikTok photo mode (swipeable photo posts): one 1080x1920 JPG per clip, "
            "numbered in order, with the look, captions and text drawn in. Post them in the TikTok app and pick "
            "the sound there.",
            {{"folder", prop("string", "Where to write the JPGs. Default: <project name>-photos next to the project.")}},
            {}, false, [](Call &call) {
                QString folder = call.args.value("folder").toString();
                if (folder.isEmpty()) {
                    const QFileInfo project(call.path);
                    folder = project.absolutePath() + "/" + project.completeBaseName() + "-photos";
                }
                QString error;
                const QStringList files = exportPhotoSet(call.project, accent(), absolute(folder), &error);
                if (files.isEmpty())
                    throw ToolError{error};
                return json({{"folder", absolute(folder)}, {"files", QJsonArray::fromStringList(files)},
                             {"size", "1080x1920"}});
            });

    addTool("show_in_app", "Open the project in the Reelcut window so the user can watch and edit it.", {}, {}, false,
            [this](Call &call) {
                QFile state(openProjectFile(m_stateDir));
                const bool open = state.open(QIODevice::ReadOnly) && QString::fromUtf8(state.readAll()).trimmed() == call.path;
                if (open && windowRunning())
                    return text("It's already open in Reelcut, which shows every edit as it's saved.");
                if (!QProcess::startDetached("reelcut", {call.path}))
                    throw ToolError{"Could not start Reelcut."};
                return text("Opened in Reelcut. The window reloads the project after every edit.");
            });
}
