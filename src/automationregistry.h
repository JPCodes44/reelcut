#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>

// Only explicitly registered application surfaces are callable. QObject base
// methods, private slots, arbitrary descendants and credentials are not a surface.
class AutomationRegistry {
public:
    void add(const QString &component, QObject *object);
    QJsonObject invoke(const QString &component, const QString &method, const QJsonObject &arguments);
    QJsonObject state(const QString &component = {}) const;
    QJsonObject setProperty(const QString &component, const QString &property, const QJsonValue &value);
    static QJsonArray tools();
    static QJsonObject inventory();
    static QJsonArray uiTools();
    static QJsonObject failure(const QString &message);
    static QString toolName(const QString &component, const QString &method);
private:
    QHash<QString, QPointer<QObject>> m_objects;
};
