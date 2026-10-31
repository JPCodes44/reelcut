#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>

#include "project.h"

// A Model Context Protocol server on stdin/stdout (`reelcut mcp`), so an
// agent can do everything the window does: build and edit projects, caption,
// color, add music, look at frames, and export. Every edit is saved to the
// .reel file right away, and an open Reelcut window showing that project
// reloads it, so the user watches the edit happen.
class McpServer {
public:
    // stateDir holds the path of the project open in the window; tests pass a temp dir.
    explicit McpServer(const QString &stateDir = {});

    // Serves newline-delimited JSON-RPC on stdin/stdout until stdin closes.
    int run();
    // One request in, its response out (null for notifications).
    QJsonObject handle(const QJsonObject &request);

    static QString openProjectFile(const QString &stateDir = {});

private:
    struct Call {
        QJsonObject args;
        QString path;
        Project project;
    };
    using Handler = std::function<QJsonObject(Call &)>;
    struct Tool {
        QJsonObject definition;
        Handler handler;
        bool edits = false;  // saves the project afterwards and records undo
        bool needsProject = true;
    };

    void addTool(const QString &name, const QString &description, const QJsonObject &properties,
                 const QStringList &required, bool edits, Handler handler, bool needsProject = true);
    void defineTools();
    QJsonObject callTool(const QString &name, const QJsonObject &args);
    QString resolvePath(const QJsonObject &args, QString *error) const;
    bool load(const QString &path, Project *project, QString *error) const;
    bool save(const QString &path, const Project &project, QString *error);
    QJsonObject summary(const Call &call, bool withWords = false) const;

    QString m_stateDir;
    QString m_current;
    QHash<QString, Tool> m_tools;
    QStringList m_order;
    QHash<QString, QVector<QJsonObject>> m_undo;
    QHash<QString, QVector<QJsonObject>> m_redo;
};
