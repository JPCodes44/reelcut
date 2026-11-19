#include "appbridge.h"
#include "accounts.h"
#include "backend.h"
#include "downloads.h"
#include "facebookapi.h"
#include "higgsfield.h"
#include "instagramapi.h"
#include "postscheduler.h"
#include "stats.h"
#include "youtubeapi.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJSValue>
#include <QLocalSocket>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QPainter>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <memory>

namespace {
constexpr qsizetype requestLimit=1024*1024;
constexpr qsizetype responseLimit=32*1024*1024;
QQuickItem *findItem(QQuickItem *root,const QString &name) {
    if (!root) return nullptr;
    if (root->objectName()==name) return root;
    for (auto *child:root->childItems()) if (auto *found=findItem(child,name)) return found;
    return nullptr;
}
QJsonValue toJson(QVariant value) {
    if (value.metaType()==QMetaType::fromType<QJSValue>()) value=value.value<QJSValue>().toVariant();
    return QJsonValue::fromVariant(value);
}
bool credentialKey(const QString &key) {
    return key.contains("token",Qt::CaseInsensitive) || key.contains("secret",Qt::CaseInsensitive)
        || key.compare("key",Qt::CaseInsensitive)==0;
}
}
QString AppBridge::directory(const QString &stateDir) {
    return stateDir.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)+"/reelcut":stateDir;
}
AppBridge::AppBridge(const QString &stateDir,QObject *parent):QObject(parent),m_directory(directory(stateDir)) {
    m_instance=QString::number(QCoreApplication::applicationPid())+"-"+QUuid::createUuid().toString(QUuid::Id128).left(10);
    m_path=QDir(m_directory).filePath("mcp-app-"+m_instance+".sock");
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server,&QLocalServer::newConnection,this,[this] {
        while (auto *socket=m_server.nextPendingConnection()) {
            socket->setReadBufferSize(requestLimit+1);
            auto buffer=std::make_shared<QByteArray>();
            auto answered=std::make_shared<bool>(false);
            connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            connect(socket,&QLocalSocket::readyRead,this,[this,socket,buffer,answered] {
                if (*answered) {socket->readAll();return;}
                buffer->append(socket->readAll());
                auto send=[socket,answered](QJsonObject result) {
                    *answered=true;
                    auto bytes=QJsonDocument(result).toJson(QJsonDocument::Compact);
                    if (bytes.size()>responseLimit) bytes=QJsonDocument(AutomationRegistry::failure("Response exceeds the live API size limit; request a narrower component or history.")).toJson(QJsonDocument::Compact);
                    socket->write(bytes+'\n');socket->disconnectFromServer();
                };
                if (buffer->size()>requestLimit) {send(AutomationRegistry::failure("Request exceeds the live API size limit."));return;}
                const auto newline=buffer->indexOf('\n');if(newline<0)return;
                QJsonParseError error;const auto doc=QJsonDocument::fromJson(buffer->left(newline),&error);
                if (error.error!=QJsonParseError::NoError || !doc.isObject() || newline+1!=buffer->size()) {send(AutomationRegistry::failure("Expected one JSON object per connection."));return;}
                send(protect(handle(doc.object())).toObject());
            });
            QTimer::singleShot(15000,socket,[socket,answered] {if(!*answered)socket->abort();});
        }
    });
}
AppBridge::~AppBridge() {m_server.close();}
bool AppBridge::listen() {
    if (!QDir().mkpath(m_directory))return false;
    return m_server.listen(m_path);
}
void AppBridge::rememberSecrets(const QJsonObject &object) {
    for(auto it=object.begin();it!=object.end();++it) {
        if(credentialKey(it.key()) && it.value().isString() && !it.value().toString().isEmpty())m_secrets << it.value().toString() << it.value().toString().trimmed();
        if(it.value().isObject())rememberSecrets(it.value().toObject());
    }
    m_secrets.removeDuplicates();
}
QJsonValue AppBridge::protect(const QJsonValue &value) {
    if(m_backend) {
        m_secrets << m_backend->instagram()->token() << m_backend->facebook()->pageToken();
        rememberSecrets(Net::load(m_backend->youtube()->accountFile()));
        rememberSecrets(Net::load(m_backend->scheduler()->configFile()));
    }
    if(m_credits)rememberSecrets(Net::load(m_credits->accountFile()));
    m_secrets.removeAll(QString());m_secrets.removeDuplicates();
    std::function<QJsonValue(const QJsonValue &)> clean=[&](const QJsonValue &v)->QJsonValue {
        if(v.isString()) {
            QString s=v.toString();
            for(const auto &secret:m_secrets) {
                if(secret.size()<4) {if(s==secret)s="[hidden]";continue;}
                s.replace(secret,"[hidden]");s.replace(QString::fromLatin1(QUrl::toPercentEncoding(secret)),"[hidden]");
            }
            return s;
        }
        if(v.isArray()) {QJsonArray a;for(const auto &entry:v.toArray())a.append(clean(entry));return a;}
        if(v.isObject()) {
            QJsonObject o;const auto source=v.toObject();
            for(auto it=source.begin();it!=source.end();++it) {
                // Encoded image bytes are not text and must never be substring-filtered.
                if(it.key()=="data"&&source.value("mimeType")=="image/png")o[it.key()]=it.value();
                else o[it.key()]=clean(it.value());
            }
            return o;
        }
        return v;
    };
    return clean(value);
}
void AppBridge::appendEvent(const QString &type,const QJsonObject &data) {
    m_events.append(protect(QJsonObject{{"sequence",++m_sequence},{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"type",type},{"data",data}}));
    while(m_events.size()>128)m_events.removeFirst();
}
void AppBridge::attach(Backend *backend,Downloads *downloads,Stats *stats,HiggsfieldCredits *credits,QQuickWindow *window) {
    m_backend=backend;m_credits=credits;m_window=window;
    m_registry.add("backend",backend);m_registry.add("accounts",backend->accountSettings());
    m_registry.add("downloads",downloads);m_registry.add("stats",stats);m_registry.add("credits",credits);
    connect(backend,&Backend::jobChanged,this,[this,backend] {appendEvent("job",{{"busy",backend->busy()},{"text",backend->busyText()},{"progress",backend->progress()}});});
    connect(backend,&Backend::messageChanged,this,[this,backend] {appendEvent("message",{{"error",backend->messageIsError()},{"message",backend->message()}});});
    connect(backend,&Backend::postProgress,this,[this](const QString &message,double fraction) {appendEvent("post_progress",{{"message",message},{"fraction",fraction}});});
    connect(backend,&Backend::postPublished,this,[this](bool ok,const QString &message,const QUrl &url) {appendEvent("post_completed",{{"ok",ok},{"message",message},{"url",url.toString()}});});
    connect(backend,&Backend::postScheduled,this,[this](bool ok,const QString &message) {appendEvent("schedule_completed",{{"ok",ok},{"message",message}});});
    connect(backend,&Backend::platformPosted,this,[this](const QString &platform,bool ok,const QString &message,const QUrl &url) {appendEvent("platform_posted",{{"platform",platform},{"ok",ok},{"message",message},{"url",url.toString()}});});
    auto connected=[this](const QString &platform,bool ok,const QString &message){appendEvent("account_connection",{{"platform",platform},{"ok",ok},{"message",message}});};
    connect(backend,&Backend::instagramConnected,this,[connected](bool ok,const QString &message){connected("instagram",ok,message);});
    connect(backend,&Backend::facebookConnected,this,[connected](bool ok,const QString &message){connected("facebook",ok,message);});
    connect(backend,&Backend::youtubeConnected,this,[connected](bool ok,const QString &message){connected("youtube",ok,message);});
    connect(downloads,&Downloads::finished,this,[this](const QString &path,const QString &title){appendEvent("download_completed",{{"path",path},{"title",title}});});
    connect(downloads,&Downloads::failed,this,[this](const QString &message){appendEvent("download_failed",{{"message",message}});});
    connect(stats,&Stats::dailyLoaded,this,[this](const QString &platform,const QString &id,const QVariantList &points){appendEvent("stats_daily_loaded",{{"platform",platform},{"id",id},{"points",QJsonArray::fromVariantList(points)}});});
    connect(stats,&Stats::changed,this,[this,stats]{appendEvent("stats",{{"busy",stats->busy()},{"status",stats->status()},{"refreshed",stats->refreshed().toString(Qt::ISODate)}});});
    connect(credits,&HiggsfieldCredits::signInFinished,this,[this](bool ok,const QString &message){appendEvent("credits_sign_in",{{"ok",ok},{"message",message}});});
}
QJsonObject AppBridge::ui(const QString &method,const QJsonObject &arguments) {
    if(!m_window)return AutomationRegistry::failure("The GUI is unavailable.");
    QJsonObject schema;
    for(const auto &v:AutomationRegistry::uiTools())if(v.toObject().value("method")==method){schema=v.toObject().value("inputSchema").toObject();break;}
    if(schema.isEmpty())return AutomationRegistry::failure("Unknown UI action.");
    const auto properties=schema.value("properties").toObject();
    for(const auto &required:schema.value("required").toArray())if(!arguments.contains(required.toString()))return AutomationRegistry::failure("Missing UI argument: "+required.toString());
    for(auto it=arguments.begin();it!=arguments.end();++it) {
        if(it.key()=="instance" || !properties.contains(it.key()))return AutomationRegistry::failure("Unknown UI argument.");
        const auto type=properties.value(it.key()).toObject().value("type").toString();const auto v=it.value();
        if((type=="string"&&!v.isString()) || (type=="boolean"&&!v.isBool()) || (type=="object"&&!v.isObject())
           || ((type=="number"||type=="integer")&&(!v.isDouble()||!std::isfinite(v.toDouble())))
           || (type=="integer"&&std::floor(v.toDouble())!=v.toDouble()))return AutomationRegistry::failure("Invalid UI argument type: "+it.key());
    }
    auto *surface=m_window->findChild<QObject *>("automationSurface");
    if(!surface)return AutomationRegistry::failure("The UI automation surface is unavailable.");
    QVariant result;
    const bool ok=QMetaObject::invokeMethod(surface,"dispatch",Qt::DirectConnection,Q_RETURN_ARG(QVariant,result),
        Q_ARG(QVariant,QVariant(method)),Q_ARG(QVariant,arguments.toVariantMap()));
    if(!ok)return AutomationRegistry::failure("The UI action could not be invoked.");
    const auto value=toJson(result);
    if(value.isObject() && value.toObject().value("isError").toBool())return value.toObject();
    return {{"accepted",true},{"result",value}};
}
QJsonObject AppBridge::capture(bool color,const QJsonObject &arguments) {
    if(!m_window)return AutomationRegistry::failure("The GUI is unavailable.");
    QImage image;
    if(color) {
        if(!arguments.value("x").isDouble() || !arguments.value("y").isDouble())return AutomationRegistry::failure("x and y must be normalized numbers.");
        const double x=arguments.value("x").toDouble(),y=arguments.value("y").toDouble();
        if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1)return AutomationRegistry::failure("x and y must be between 0 and 1.");
        auto *picture=findItem(m_window->contentItem(),"previewPicture");
        if(!picture || !picture->isVisible())return AutomationRegistry::failure("Open the editor preview before sampling a color.");
        auto grab=picture->grabToImage();
        if(!grab)return AutomationRegistry::failure("The preview picture is unavailable.");
        QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);bool ready=false;
        connect(grab.data(),&QQuickItemGrabResult::ready,&loop,[&]{ready=true;loop.quit();});
        connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);timeout.start(3000);loop.exec();
        if(!ready || grab->image().isNull())return AutomationRegistry::failure("The preview picture could not be captured yet. Try again after rendering.");
        image=grab->image();
        const auto c=image.pixelColor(qRound(x*(image.width()-1)),qRound(y*(image.height()-1)));
        return {{"color",c.name(QColor::HexArgb)}};
    }
    image=m_window->grabWindow();if(image.isNull())return AutomationRegistry::failure("The window has not rendered a frame yet.");
    QJsonArray redactedFields;
    const QStringList privateFields{"settingsToken","settingsYouTubeClient","settingsYouTubeSecret","settingsSchedulerKey",
        "instagramToken","facebookToken","youtubeToken","youtubeClientId","youtubeClientSecret","postSchedulerKey"};
    QPainter painter(&image);
    const double sx=double(image.width())/qMax(1,m_window->width()),sy=double(image.height())/qMax(1,m_window->height());
    painter.scale(sx,sy);
    for(const auto &name:privateFields) {
        auto *field=findItem(m_window->contentItem(),name);
        if(!field || !field->isVisible() || field->width()<=0 || field->height()<=0)continue;
        const auto p=field->mapToScene(QPointF());const QRectF rect(p,QSizeF(field->width(),field->height()));
        painter.fillRect(rect,QColor("#202024"));painter.setPen(Qt::white);painter.drawText(rect,Qt::AlignCenter,"[credential field hidden]");
        redactedFields.append(name);
    }
    painter.end();
    if(arguments.contains("width")&&(!arguments.value("width").isDouble()||std::floor(arguments.value("width").toDouble())!=arguments.value("width").toDouble()||arguments.value("width").toDouble()<180||arguments.value("width").toDouble()>1920))return AutomationRegistry::failure("width must be an integer from 180 to 1920.");
    const int width=arguments.value("width").toInt(1280);
    if(image.width()>width)image=image.scaledToWidth(width,Qt::SmoothTransformation);
    QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");
    return {{"mimeType","image/png"},{"data",QString::fromLatin1(bytes.toBase64())},{"width",image.width()},{"height",image.height()},{"redactedFields",redactedFields}};
}
QJsonObject AppBridge::handle(const QJsonObject &request) {
    if(!request.value("action").isString())return AutomationRegistry::failure("A live protocol action is required.");
    const auto action=request.value("action").toString();
    for(const auto &field:{QString("component"),QString("method"),QString("property")})
        if(request.contains(field)&&!request.value(field).isString())return AutomationRegistry::failure("Invalid protocol field: "+field);
    if(request.contains("arguments")&&!request.value("arguments").isObject())return AutomationRegistry::failure("arguments must be an object.");
    if(action=="invoke"&&(!request.contains("component")||!request.contains("method")))return AutomationRegistry::failure("invoke needs a component and method.");
    if(action=="set_property"&&(!request.contains("component")||!request.contains("property")||!request.contains("value")))return AutomationRegistry::failure("set_property needs component, property and value.");
    if(action=="events"&&request.contains("after")&&(!request.value("after").isDouble()||request.value("after").toDouble()<0||std::floor(request.value("after").toDouble())!=request.value("after").toDouble()))return AutomationRegistry::failure("after must be a non-negative sequence number.");
    if(action=="hello")return {{"instance",m_instance},{"pid",QCoreApplication::applicationPid()},
        {"project",m_backend?m_backend->path():QString()},{"title",m_window?m_window->title():QString("Reelcut")}};
    if(action=="capabilities")return AutomationRegistry::inventory();
    if(action=="state") {
        const auto component=request.value("component").toString();
        if(component=="ui")return ui("state",{});
        auto snapshot=m_registry.state(component);
        if(!component.isEmpty()&&snapshot.isEmpty())return AutomationRegistry::failure("Application component is unavailable.");
        if(component.isEmpty()&&m_window)snapshot["ui"]=ui("state",{}).value("result");
        return {{"instance",m_instance},{"state",protect(snapshot)}};
    }
    if(action=="events") {
        QJsonArray result;const auto after=request.value("after").toDouble();
        for(const auto &entry:m_events)if(entry.toObject().value("sequence").toDouble()>after)result.append(entry);
        return {{"events",result},{"latest",m_sequence},{"oldest",m_events.isEmpty()?m_sequence:m_events.first().toObject().value("sequence").toInteger()}};
    }
    if(action=="screenshot")return capture(false,request.value("arguments").toObject());
    if(action=="sample_color")return capture(true,request.value("arguments").toObject());
    const auto component=request.value("component").toString();
    const auto arguments=request.value("arguments").toObject();
    if(action=="invoke") {
        const auto method=request.value("method").toString();
        auto result=component=="ui"?ui(method,arguments):m_registry.invoke(component,method,arguments);
        if(!result.value("isError").toBool()) {
            QJsonObject credentials;
            const bool connection=(component=="accounts"&&QStringList{"connectToken","connectYouTube"}.contains(method))
                || (component=="backend"&&QStringList{"connectInstagram","connectFacebook","connectYouTube","setupScheduler"}.contains(method));
            if(connection)for(auto it=arguments.begin();it!=arguments.end();++it)
                if(credentialKey(it.key()))credentials[it.key()]=it.value();
            rememberSecrets(credentials);
        }
        appendEvent("action_returned",{{"component",component},{"method",method},{"ok",!result.value("isError").toBool()}});
        return protect(result).toObject();
    }
    if(action=="set_property")return m_registry.setProperty(component,request.value("property").toString(),request.value("value"));
    return AutomationRegistry::failure("Unknown live app protocol action.");
}

QJsonObject AppBridgeClient::request(const QString &path,const QJsonObject &object,int timeoutMs) {
    const auto payload=QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';
    if(payload.size()>requestLimit)return AutomationRegistry::failure("Request exceeds the live API size limit.");
    QLocalSocket socket;socket.setReadBufferSize(responseLimit+1);QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
    QByteArray bytes;bool complete=false;
    QObject::connect(&socket,&QLocalSocket::connected,&loop,[&]{socket.write(payload);});
    QObject::connect(&socket,&QLocalSocket::readyRead,&loop,[&]{bytes.append(socket.readAll());if(bytes.size()>responseLimit||bytes.contains('\n')){complete=bytes.contains('\n');loop.quit();}});
    QObject::connect(&socket,&QLocalSocket::disconnected,&loop,[&]{bytes.append(socket.readAll());complete=bytes.contains('\n');loop.quit();});
    QObject::connect(&socket,&QLocalSocket::errorOccurred,&loop,[&]{loop.quit();});
    QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    timeout.start(timeoutMs);socket.connectToServer(path);
    if(socket.state()!=QLocalSocket::UnconnectedState || socket.error()==QLocalSocket::UnknownSocketError)loop.exec();
    if(bytes.size()>responseLimit)return AutomationRegistry::failure("Response exceeds the live API size limit.");
    if(!complete)return AutomationRegistry::failure("The live Reelcut instance did not respond. Open/reconnect the app and try again; a timed-out action must not be blindly retried.");
    const auto document=QJsonDocument::fromJson(bytes.left(bytes.indexOf('\n')));
    return document.isObject()?document.object():AutomationRegistry::failure("Invalid response from the live app.");
}
QJsonArray AppBridgeClient::instances(const QString &stateDir) {
    QJsonArray result;QDir dir(AppBridge::directory(stateDir));
    const auto entries=dir.entryInfoList({"mcp-app-*.sock"},QDir::System|QDir::Files,QDir::Time);
    int examined=0;
    for(const auto &entry:entries) {
        if(++examined>64)break;
        auto hello=request(entry.absoluteFilePath(),{{"action","hello"}},300);
        if(!hello.value("instance").toString().isEmpty())result.append(hello);
    }
    return result;
}
QJsonObject AppBridgeClient::call(const QString &stateDir,const QJsonObject &requestObject,const QString &instance) {
    const auto available=instances(stateDir);
    if(available.isEmpty())return AutomationRegistry::failure("No live Reelcut window is available. Open Reelcut (or use launch_app) for live tools; saved-project tools still work without a window.");
    QString selected=instance;
    if(selected.isEmpty()) {
        if(available.size()!=1)return AutomationRegistry::failure("Multiple Reelcut windows are running. Use list_app_instances, then pass the desired instance ID.");
        selected=available.first().toObject().value("instance").toString();
    }
    bool found=false;for(const auto &entry:available)if(entry.toObject().value("instance")==selected)found=true;
    if(!found)return AutomationRegistry::failure("The selected live Reelcut instance is unavailable.");
    return request(QDir(AppBridge::directory(stateDir)).filePath("mcp-app-"+selected+".sock"),requestObject);
}
