#include "mcpserver.h"

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
    "Reelcut edits vertical 1080x1920 reels for Instagram and TikTok. Work on one .reel project: "
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

Clip clipFromFile(const QString &path) {
    const media::VideoInfo info = media::probe(path);
    if (!info.ok)
        throw ToolError{QString("%1: %2").arg(path, info.error)};
    Clip clip;
    clip.path = path;
    clip.out = info.duration;
    clip.sourceDuration = info.duration;
    clip.width = info.width;
    clip.height = info.height;
    clip.audio = info.audio;
    return clip;
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
                      {"serverInfo", QJsonObject{{"name", "reelcut"}, {"version", "0.2.0"}}},
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
    double start = 0.0;
    for (int i = 0; i < p.clips.size(); ++i) {
        const Clip &c = p.clips[i];
        clips.append(QJsonObject{
            {"index", i}, {"file", c.path}, {"start", start}, {"duration", c.duration()},
            {"source_in", c.in}, {"source_out", c.out}, {"source_duration", c.sourceDuration},
            {"speed", c.speed}, {"zoom", c.zoom}, {"pan_x", c.panX}, {"pan_y", c.panY},
            {"size", QString("%1x%2").arg(c.width).arg(c.height)}, {"has_audio", c.audio},
            {"missing", !QFileInfo::exists(c.path)},
        });
        start += c.duration();
    }
    QJsonArray overlays;
    for (int i = 0; i < p.overlays.size(); ++i) {
        const Overlay &o = p.overlays[i];
        overlays.append(QJsonObject{{"index", i}, {"text", o.text}, {"start", o.start}, {"end", o.end},
                                    {"position", o.y}, {"size", o.size}, {"animation", o.animation}});
    }
    QJsonArray lines;
    for (const CaptionLine &line : groupLines(p.words))
        lines.append(QJsonObject{{"start", line.start}, {"end", line.end}, {"text", line.text()}});
    QJsonObject captions{{"style", p.captionStyle}, {"position", p.captionY}, {"word_count", int(p.words.size())},
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
        {"look", QJsonObject{{"exposure", p.look.exposure}, {"contrast", p.look.contrast},
                             {"saturation", p.look.saturation}, {"temperature", p.look.temperature},
                             {"lut", p.look.lut}}},
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
             {"platform", enumProp({"instagram", "tiktok"}, "Which app's safe zones to design for.")}},
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
                    call.project.platform = call.args.value("platform").toString();
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
                return json({{"file", info.path}, {"duration", info.duration}, {"width", info.width},
                             {"height", info.height}, {"has_audio", info.audio}});
            }, false);

    addTool("set_platform", "Design for Instagram Reels or TikTok (changes the safe zones shown and the file name).",
            {{"platform", enumProp({"instagram", "tiktok"}, "The platform.")}}, {"platform"}, true, [this](Call &call) {
                call.project.platform = call.args.value("platform").toString() == "tiktok" ? "tiktok" : "instagram";
                return json(summary(call));
            });

    // Clips

    addTool("add_clips", "Add video files to the timeline, at the end or at a position.",
            {{"files", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
                                   {"description", "Video files, in order."}}},
             {"at", prop("integer", "Clip index to insert before. Default: the end.")}},
            {"files"}, true, [this](Call &call) {
                int at = call.args.contains("at") ? qBound(0, call.args.value("at").toInt(), int(call.project.clips.size()))
                                                  : int(call.project.clips.size());
                for (const QJsonValue &file : call.args.value("files").toArray())
                    call.project.clips.insert(at++, clipFromFile(absolute(file.toString())));
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
            "Change a clip: trim (source seconds), speed, and framing in the 9:16 frame. Values are clamped.",
            {{"index", prop("integer", "Clip index.")},
             {"source_in", prop("number", "Where the clip starts in its file, in seconds.")},
             {"source_out", prop("number", "Where the clip ends in its file, in seconds.")},
             {"speed", prop("number", "0.25 to 4.")},
             {"zoom", prop("number", "1 (fills the frame) to 3.")},
             {"pan_x", prop("number", "-1 (left edge) to 1 (right edge); 0 is centered.")},
             {"pan_y", prop("number", "-1 (top) to 1 (bottom); 0 is centered.")}},
            {"index"}, true, [this](Call &call) {
                const int index = requireIndex(call.args, "index", call.project.clips.size(), "clip");
                const QList<QPair<QString, QString>> keys{{"source_in", "in"}, {"source_out", "out"}, {"speed", "speed"},
                                                          {"zoom", "zoom"}, {"pan_x", "panX"}, {"pan_y", "panY"}};
                // Out before in, so widening a clip both ways in one call works.
                if (call.args.contains("source_out") && call.args.value("source_out").toDouble() > call.project.clips[index].out)
                    call.project.setClipValue(index, "out", call.args.value("source_out").toDouble());
                for (const auto &key : keys)
                    if (call.args.contains(key.first))
                        call.project.setClipValue(index, key.second, call.args.value(key.first).toDouble());
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
             {"position", prop("number", "Vertical center, 0.1 (top) to 0.9 (bottom). Default 0.68.")},
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
                    p.captionY = qBound(0.1, call.args.value("position").toDouble(), 0.9);
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
            {"size", prop("integer", "Font size at 1080x1920, 40 to 200. Default 96.")},
            {"animation", enumProp({"pop", "fade", "slide", "none"}, "How it enters. Default pop.")}};
    };
    auto applyOverlay = [](Project &p, int index, const QJsonObject &args) {
        if (args.contains("end"))
            p.setOverlayValue(index, "end", args.value("end").toDouble());
        const QList<QPair<QString, QString>> keys{{"text", "text"}, {"start", "start"}, {"end", "end"},
                                                  {"position", "y"}, {"size", "size"}, {"animation", "animation"}};
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

    // Look

    addTool("set_look",
            "Color the whole reel: a preset, sliders from -1 to 1 (0 is untouched), and/or a .cube LUT. "
            "A preset is applied first, then any sliders given.",
            {{"preset", enumProp(Project::presets(), "A starting look.")},
             {"exposure", prop("number", "-1 to 1.")},
             {"contrast", prop("number", "-1 to 1.")},
             {"saturation", prop("number", "-1 (black and white) to 1.")},
             {"temperature", prop("number", "-1 (cool) to 1 (warm).")},
             {"lut", prop("string", "A .cube file, applied over the sliders. Empty string removes it.")}},
            {}, true, [this](Call &call) {
                Project &p = call.project;
                if (call.args.contains("preset") && !p.applyPreset(call.args.value("preset").toString()))
                    throw ToolError{"Unknown preset. Use one of: " + Project::presets().join(", ")};
                for (const QString &key : {"exposure", "contrast", "saturation", "temperature"})
                    if (call.args.contains(key))
                        p.setLookValue(key, call.args.value(key).toDouble());
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
