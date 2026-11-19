#pragma once
#include "automationregistry.h"
#include <QJsonArray>
#include <QLocalServer>
#include <QPointer>
#include <QStringList>

class QQuickWindow;
class Backend;
class Downloads;
class Stats;
class HiggsfieldCredits;

class AppBridge : public QObject {
    Q_OBJECT
public:
    explicit AppBridge(const QString &stateDir = {}, QObject *parent = nullptr);
    ~AppBridge() override;
    bool listen();
    QString instanceId() const { return m_instance; }
    QString socketPath() const { return m_path; }
    AutomationRegistry &registry() { return m_registry; }
    void attach(Backend *, Downloads *, Stats *, HiggsfieldCredits *, QQuickWindow *);
    static QString directory(const QString &stateDir);
    QJsonObject handle(const QJsonObject &request);
private:
    void appendEvent(const QString &type, const QJsonObject &data = {});
    QJsonValue protect(const QJsonValue &value);
    void rememberSecrets(const QJsonObject &arguments);
    QJsonObject ui(const QString &method,const QJsonObject &arguments);
    QJsonObject capture(bool color,const QJsonObject &arguments);
    AutomationRegistry m_registry;
    QLocalServer m_server;
    QString m_directory,m_instance,m_path;
    QPointer<QQuickWindow> m_window;
    QPointer<Backend> m_backend;
    QPointer<HiggsfieldCredits> m_credits;
    QJsonArray m_events;
    qint64 m_sequence = 0;
    QStringList m_secrets;
};

class AppBridgeClient {
public:
    static QJsonArray instances(const QString &stateDir = {});
    static QJsonObject call(const QString &stateDir,const QJsonObject &request,const QString &instance = {});
private:
    static QJsonObject request(const QString &socket,const QJsonObject &request,int timeoutMs=10000);
};
