#include "automationregistry.h"
#include "accounts.h"
#include "backend.h"
#include "downloads.h"
#include "higgsfield.h"
#include "stats.h"
#include "theme.h"
#include "translator.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QMap>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QUrl>
#include <QVariant>
#include <cmath>

namespace {
struct Surface { const char *name; const QMetaObject *meta; };
const QList<Surface> surfaces{
    {"backend", &Backend::staticMetaObject}, {"accounts", &Accounts::staticMetaObject},
    {"stats", &Stats::staticMetaObject}, {"downloads", &Downloads::staticMetaObject},
    {"credits", &HiggsfieldCredits::staticMetaObject}, {"theme", &Theme::staticMetaObject},
    {"language", &Translator::staticMetaObject}
};
QHash<QString, QString> equivalents() {
    return {{"backend.addMedia", "app_backend_select_files: purpose=media, paths=[local files]"},
        {"backend.open", "app_backend_open_file(path); open_project(path) for saved projects"},
        {"backend.saveAs", "app_backend_select_files: purpose=save_project, paths=[output.reel]"},
        {"backend.chooseMusic", "app_backend_select_files: purpose=music, paths=[audio file]"},
        {"backend.chooseLut", "app_backend_select_files: purpose=lut, paths=[cube file]"},
        {"backend.exportVideo", "app_backend_select_files: purpose=export, paths=[output.mp4]; export for saved projects"},
        {"backend.pixelColor", "sample_preview_color(x,y); preview_frame for rendered output"},
        {"downloads.startDrag", "get_app_state(downloads) returns local paths; app_downloads_copy_file provides the native file clipboard payload"}};
}
bool isReadOnly(const QString &component, const QString &method) {
    return (component == "backend" && QStringList{"checkPost","checkPosts","checkWhen","parseWhen","suggestWhen","locate","dropTarget","canConnectFacebookWithInstagram"}.contains(method))
        || (component == "stats" && method == "history");
}
bool isSensitive(const QString &component,const QString &method,const QString &name) {
    return name.contains("token",Qt::CaseInsensitive)||name.contains("secret",Qt::CaseInsensitive)||name.contains("password",Qt::CaseInsensitive)
        || (component=="backend"&&method=="setupScheduler"&&name=="key");
}
QJsonObject typeSchema(QMetaType type) {
    const int id=type.id();
    if (id==QMetaType::Bool) return {{"type","boolean"}};
    if (id==QMetaType::Int || id==QMetaType::UInt || id==QMetaType::LongLong || id==QMetaType::ULongLong) return {{"type","integer"}};
    if (id==QMetaType::Double || id==QMetaType::Float) return {{"type","number"}};
    if (id==QMetaType::QDateTime) return {{"type","string"},{"format","date-time"}};
    if (id==QMetaType::QString || id==QMetaType::QUrl) return {{"type","string"}};
    if (id==QMetaType::QStringList || type == QMetaType::fromType<QList<QUrl>>())
        return {{"type","array"},{"items",QJsonObject{{"type","string"}}}};
    if (id==QMetaType::QVariantList) return {{"type","array"}};
    if (id==QMetaType::QVariantMap) return {{"type","object"}};
    if (id==QMetaType::QVariant) return {}; // deliberately accepts JSON values only
    return {{"unsupported",QString::fromLatin1(type.name())}};
}
bool supportedReturn(QMetaType type) { return type.id()==QMetaType::Void || !typeSchema(type).contains("unsupported"); }
QList<QMetaMethod> methods(const QMetaObject *meta) {
    QMap<QByteArray,QMetaMethod> byName;
    for (int i=meta->methodOffset();i<meta->methodCount();++i) {
        auto method=meta->method(i);
        if (method.access()!=QMetaMethod::Public || method.methodType()!=QMetaMethod::Method) continue;
        if (!byName.contains(method.name()) || byName[method.name()].parameterCount()<method.parameterCount()) byName[method.name()]=method;
    }
    return byName.values();
}
const QMetaObject *metaFor(const QString &component) {
    for (const auto &surface:surfaces) if (component==surface.name) return surface.meta;
    return nullptr;
}
QJsonObject descriptor(const QString &component, const QMetaMethod &method) {
    const auto names=method.parameterNames();
    QJsonObject properties{{"instance",QJsonObject{{"type","string"},{"description","Live window instance ID; required when more than one Reelcut window is running."}}}};
    QJsonArray required;
    bool supported=supportedReturn(method.returnMetaType());
    for (int i=0;i<method.parameterCount();++i) {
        const auto name=QString::fromLatin1(names[i]);
        auto schema=typeSchema(method.parameterMetaType(i));
        supported &= !schema.contains("unsupported") && !name.isEmpty();
        schema["description"]=name+" ("+QString::fromLatin1(method.parameterTypeName(i))+")";
        if (isSensitive(component,QString::fromLatin1(method.name()),name)) { schema["writeOnly"]=true; schema["description"]=name+"; sensitive input, never returned in state or events."; }
        properties[name]=schema; required.append(name);
    }
    const QString name=QString::fromLatin1(method.name());
    if (component=="backend" && QStringList{"newProject","openFile","selectFiles"}.contains(name))
        properties["discard_unsaved"]=QJsonObject{{"type","boolean"},{"description","Explicitly discard unsaved edits when replacing the current project. Otherwise save it first."}};
    const bool readOnly=isReadOnly(component,name);
    const bool destructive=QStringList{"newProject","removeClip","removeLayer","removeOverlay","removeEffect","clearCaptions","removeMusic","disconnect","disconnectAccount","disconnectInstagram","disconnectFacebook","disconnectYouTube","remove","signOut","post","postTo","cancelScheduled","removeScheduled","retryScheduled","removePlaceholder"}.contains(name);
    const bool external=(component=="accounts" && name!="cancelSelection" && name!="disconnectAccount")
        || (component=="stats" && name!="history") || (component=="downloads" && name=="download")
        || (component=="credits" && QStringList{"signIn","refresh","signOut"}.contains(name))
        || (component=="backend" && (name.contains("Scheduled") || name.startsWith("connect") || QStringList{"post","postTo","setupScheduler","openLink"}.contains(name)));
    return {{"name",AutomationRegistry::toolName(component,name)}, {"component",component},{"method",name},{"supported",supported},
        {"description",QString("%1.%2 in the running Reelcut window. Uses the same application action as the UI. Async work returns acceptance; inspect get_app_state and get_app_events for progress/completion.").arg(component,name)},
        {"inputSchema",QJsonObject{{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}}},
        {"annotations",QJsonObject{{"readOnlyHint",readOnly},{"destructiveHint",destructive},{"openWorldHint",external}}}};
}
bool convert(const QJsonValue &value, QMetaType type, QVariant *out) {
    const auto schema=typeSchema(type); if (schema.contains("unsupported") || value.isUndefined()) return false;
    const auto kind=schema.value("type").toString();
    if (kind=="boolean" && !value.isBool()) return false;
    if ((kind=="integer" || kind=="number") && (!value.isDouble() || !std::isfinite(value.toDouble()))) return false;
    if (kind=="integer" && std::floor(value.toDouble())!=value.toDouble()) return false;
    if (kind=="integer" && type.id()==QMetaType::Int && (value.toDouble()<-2147483648.0 || value.toDouble()>2147483647.0)) return false;
    if (kind=="string" && !value.isString()) return false;
    if (kind=="object" && !value.isObject()) return false;
    if (kind=="array" && !value.isArray()) return false;
    if (type==QMetaType::fromType<QList<QUrl>>()) {
        QList<QUrl> urls;
        for (const auto &entry:value.toArray()) { if (!entry.isString()) return false; urls << QUrl::fromUserInput(entry.toString(),{},QUrl::AssumeLocalFile); }
        *out=QVariant::fromValue(urls); return true;
    }
    if (type.id()==QMetaType::QStringList) {
        QStringList strings; for (const auto &entry:value.toArray()) { if (!entry.isString()) return false; strings << entry.toString(); }
        *out=strings; return true;
    }
    *out=value.toVariant();
    return type.id()==QMetaType::QVariant || out->convert(type);
}
}

void AutomationRegistry::add(const QString &component, QObject *object) { if (metaFor(component) && object && object->metaObject()->inherits(metaFor(component))) m_objects[component]=object; }
QJsonObject AutomationRegistry::failure(const QString &message) { return {{"isError",true},{"error",message}}; }
QString AutomationRegistry::toolName(const QString &component, const QString &method) {
    QString snake=method; snake.replace(QRegularExpression("([a-z0-9])([A-Z])"),"\\1_\\2");
    return "app_"+component+"_"+snake.toLower();
}
QJsonArray AutomationRegistry::tools() {
    QJsonArray result;
    const auto mappings=equivalents();
    for (const auto &surface:surfaces) for (const auto &method:methods(surface.meta)) {
        if (mappings.contains(QString(surface.name)+"."+QString::fromLatin1(method.name()))) continue;
        auto tool=descriptor(surface.name,method);
        if (tool.value("supported").toBool()) result.append(tool);
    }
    return result;
}
QJsonObject AutomationRegistry::inventory() {
    QJsonArray exposed,mapped,unsupported,properties;
    const auto mappings=equivalents();
    int total=0,totalProperties=0;
    for (const auto &surface:surfaces) {
        for (const auto &method:methods(surface.meta)) {
            ++total;
            const QString key=QString(surface.name)+"."+QString::fromLatin1(method.name());
            if (mappings.contains(key)) mapped.append(QJsonObject{{"action",key},{"equivalent",mappings[key]}});
            else if (descriptor(surface.name,method).value("supported").toBool()) exposed.append(key);
            else unsupported.append(key);
        }
        for (int i=surface.meta->propertyOffset();i<surface.meta->propertyCount();++i) {
            ++totalProperties;
            const auto p=surface.meta->property(i);
            const bool reference=p.metaType().flags().testFlag(QMetaType::PointerToQObject);
            if (!reference && !typeSchema(p.metaType()).contains("unsupported"))
                properties.append(QJsonObject{{"component",surface.name},{"property",p.name()},{"writable",p.isWritable()}});
            else if (reference && QString(p.name())=="accountSettings") properties.append(QJsonObject{{"component",surface.name},{"property",p.name()},{"equivalent","accounts component"}});
            else unsupported.append(QString(surface.name)+".property:"+p.name());
        }
    }
    return {{"definition","Functional application API coverage; native chooser/drag gestures are mapped to path/value equivalents. This is not line coverage."},
        {"exposedMethods",exposed.size()},{"mappedMethods",mapped.size()},{"totalMethods",total},
        {"totalProperties",totalProperties},{"coveragePercent",(exposed.size()+mapped.size()+properties.size()+uiTools().size())*100/qMax(1,total+totalProperties+int(uiTools().size()))},
        {"exposed",exposed},{"mapped",mapped},{"unsupported",unsupported},{"properties",properties},{"uiActions",uiTools()},
        {"humanSteps",QJsonArray{"OAuth consent and password/CAPTCHA entry remain in the provider's browser flow.","Native drag gestures use equivalent local file paths or clipboard payloads; no arbitrary browser or desktop control is exposed."}}};
}
QJsonObject AutomationRegistry::invoke(const QString &component, const QString &name, const QJsonObject &input) {
    const auto meta=metaFor(component);
    auto *object=m_objects.value(component).data();
    if (!meta || !object) return failure("Application component is unavailable: "+component);
    if (equivalents().contains(component+"."+name)) return failure("Use the documented functional equivalent: "+equivalents().value(component+"."+name));
    QMetaMethod method;
    for (const auto &candidate:methods(meta)) if (candidate.name()==name.toLatin1()) {method=candidate;break;}
    if (!method.isValid() || !descriptor(component,method).value("supported").toBool()) return failure("Unknown or unsupported application action.");
    auto args=input; bool discard=false;
    if (component=="backend" && QStringList{"newProject","openFile","selectFiles"}.contains(name) && args.contains("discard_unsaved")) {
        if (!args.value("discard_unsaved").isBool()) return failure("discard_unsaved must be a boolean.");
        discard=args.take("discard_unsaved").toBool();
    }
    if (component=="backend"&&name=="openLink") {
        const QUrl url(args.value("url").toString());
        if(url.scheme()!="https"&&url.scheme()!="http")return failure("Automation opens only http/https links; use file-path operations for local files.");
    }
    if (auto *backend=qobject_cast<Backend *>(object)) {
        const bool replacing=name=="newProject" || name=="openFile" || (name=="selectFiles" && args.value("purpose")=="open_project");
        if (replacing && backend->dirty() && !discard) return failure("Save the current project first, or explicitly set discard_unsaved=true.");
        if (name=="save" && backend->path().isEmpty()) return failure("The project has no path. Use app_backend_select_files with purpose=save_project.");
    }
    const auto paramNames=method.parameterNames(); const auto typeNames=method.parameterTypes();
    if (args.size()!=method.parameterCount()) return failure("Arguments must match the action's schema exactly.");
    QVector<QVariant> values(method.parameterCount()); QGenericArgument a[10];
    if (values.size()>10) return failure("Action has too many parameters.");
    for (int i=0;i<values.size();++i) {
        const auto key=QString::fromLatin1(paramNames[i]);
        if (!convert(args.value(key),method.parameterMetaType(i),&values[i])) return failure("Invalid or missing argument: "+key);
        a[i]=QGenericArgument(typeNames[i].constData(),method.parameterMetaType(i).id()==QMetaType::QVariant?static_cast<const void *>(&values[i]):values[i].constData());
    }
    if (auto *backend=qobject_cast<Backend *>(object)) {
        if (QStringList{"setClipValue","setLayerValue","setCaptionValue","setOverlayValue","setEffectValue","setMusicValue","setLookValue","setGrade"}.contains(name) && !(name=="setGrade" && args.value("key").toString().startsWith("reset"))) backend->checkpoint();
    }
    QVariant result;
    QGenericReturnArgument ret;
    const auto returnType=method.returnMetaType();
    if (returnType.id()==QMetaType::QVariant) ret=QGenericReturnArgument("QVariant",&result);
    else if (returnType.id()!=QMetaType::Void) { result=QVariant(returnType);ret=QGenericReturnArgument(method.typeName(),result.data()); }
    const bool ok=method.invoke(object,Qt::DirectConnection,ret,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9]);
    if (!ok) return failure("The application action could not be invoked.");
    return {{"accepted",true},{"component",component},{"method",name},{"result",QJsonValue::fromVariant(result)}};
}
QJsonObject AutomationRegistry::state(const QString &component) const {
    QJsonObject result;
    for (const auto &surface:surfaces) {
        if (!component.isEmpty() && component!=surface.name) continue;
        auto *object=m_objects.value(surface.name).data(); if (!object) continue;
        QJsonObject values;
        for (int i=surface.meta->propertyOffset();i<surface.meta->propertyCount();++i) {
            auto p=surface.meta->property(i);
            if (!p.isReadable() || p.metaType().flags().testFlag(QMetaType::PointerToQObject) || typeSchema(p.metaType()).contains("unsupported")) continue;
            values[p.name()]=QJsonValue::fromVariant(p.read(object));
        }
        if (auto *backend=qobject_cast<Backend *>(object)) {
            values["project"]=backend->project().toJson();values["path"]=backend->path();
        }
        if (!component.isEmpty()) return values;
        result[surface.name]=values;
    }
    return result;
}
QJsonObject AutomationRegistry::setProperty(const QString &component, const QString &name, const QJsonValue &value) {
    const auto meta=metaFor(component);auto *object=m_objects.value(component).data();
    if (!meta || !object) return failure("Application component is unavailable.");
    const int at=meta->indexOfProperty(name.toLatin1());
    if (at<meta->propertyOffset()) return failure("Unknown application property.");
    const auto property=meta->property(at);QVariant converted;
    if (!property.isWritable() || !convert(value,property.metaType(),&converted)) return failure("Property is read-only or has an invalid value.");
    if (!property.write(object,converted)) return failure("Property could not be updated.");
    return {{"accepted",true},{"component",component},{"property",name}};
}

QJsonArray AutomationRegistry::uiTools() {
    const QJsonObject string{{"type","string"}}, number{{"type","number"}}, integer{{"type","integer"}}, boolean{{"type","boolean"}}, object{{"type","object"}};
    QJsonArray tools;
    auto add=[&](const QString &name,const QString &description,QJsonObject properties,const QStringList &required,bool readOnly=false) {
        properties["instance"]=QJsonObject{{"type","string"},{"description","Live app instance ID when multiple windows are running."}};
        const bool destructive=QStringList{"postSubmit","deleteSelection","quit","resolveConfirmation"}.contains(name);
        tools.append(QJsonObject{{"name",toolName("ui",name)},{"component","ui"},{"method",name},{"description",description},
            {"inputSchema",QJsonObject{{"type","object"},{"properties",properties},{"required",QJsonArray::fromStringList(required)},{"additionalProperties",false}}},
            {"annotations",QJsonObject{{"readOnlyHint",readOnly},{"destructiveHint",destructive},{"openWorldHint",name=="postSubmit" || name=="statsSelect" || name=="scheduleChoose"}}}});
    };
    add("state","Read non-secret live UI state: navigation, playback, selection, stats/calendar detail and the current post draft.",{}, {},true);
    add("navigate","Open editor, stats, schedule, settings, downloads or help. Settings may select a platform.",{{"page",string},{"platform",string}}, {"page","platform"});
    add("seek","Seek the live editor, synchronizing all players and the timeline.",{{"seconds",number}}, {"seconds"});
    add("playback","Play, pause, toggle, refresh, step_forward or step_backward (one frame).",{{"action",string}}, {"action"});
    add("select","Select a clip, layer, text overlay, effect, captions, music or none, and show its inspector.",{{"kind",string},{"index",integer}}, {"kind","index"});
    add("deleteSelection","Delete the currently selected editor item using the UI's selection semantics.",{},{});
    add("setOptions","Set editor view options: safeZones, showAppUi, downloadsVisible, helpVisible, inspectorTab, clipWheelsOpen, lookWheelsOpen, timelineZoom (1..40), timelineScroll, musicHeight (52..320), scheduleTimingOpen.",{{"options",object}}, {"options"});
    add("settingsTab","Select the Settings accounts or scheduling tab.",{{"tab",string}}, {"tab"});
    add("scheduleTiming","Edit Calendar Timing draft fields by platform (start HH:mm, spacingMinutes 60/90/120), optionally Apply the same validated UI action.",{{"fields",object},{"apply",boolean}}, {"fields","apply"});
    add("statsFilter","Set Stats platform, periodDays, sortKey, sortDesc or topMetric. Unknown keys and invalid values are rejected.",{{"options",object}}, {"options"});
    add("statsSelect","Open a reel's Stats detail and load its daily growth.",{{"platform",string},{"id",string}}, {"platform","id"});
    add("statsCloseDetail","Close Stats detail, preserving filters.",{},{});
    add("scheduleDay","Show a local calendar date (YYYY-MM-DD); handles local midnight/DST rather than fixed 24-hour shifts.",{{"date",string}}, {"date"});
    add("scheduleSlots","Read planned slots and cards for a platform on the selected calendar day.",{{"platform",string}}, {"platform"},true);
    add("scheduleChoose","Choose a future calendar slot and open Post with the platform/time prefilled; does not publish.",{{"platform",string},{"publishAtMs",number}}, {"platform","publishAtMs"});
    add("scheduleDetail","Open a scheduled/published calendar card by platform/id.",{{"platform",string},{"id",string}}, {"platform","id"});
    add("scheduleCloseDetail","Close calendar post detail.",{},{});
    add("postOpen","Open Post for a platform without publishing.",{{"platform",string}}, {"platform"});
    add("postDraft","Set the live Post dialog's draft: title, caption, hashtags, platforms, useApi, scheduleLater, when. Credentials are not draft fields.",{{"fields",object}}, {"fields"});
    add("postSubmit","Explicitly press the Post dialog's enabled Publish/Schedule/Next action using its current validated draft. May publish externally.",{},{});
    add("postBack","Return from Post connection/scheduler setup to the draft.",{},{});
    add("closeDialogs","Close idle Post/Settings dialogs; active publishing must be cancelled explicitly first.",{},{});
    add("previewPostEditor","Open or close the preview's caption editor.",{{"open",boolean}}, {"open"});
    add("escape","Apply Escape semantics: close details/pages, hide help, clear selection.",{},{});
    add("resolveConfirmation","Accept or reject an existing native Reelcut confirmation; no callback or script can be supplied.",{{"accept",boolean}}, {"accept"});
    add("quit","Request the GUI to quit. Refuses dirty projects unless discard_unsaved is explicitly true; save via app_backend_select_files first otherwise.",{{"discard_unsaved",boolean}}, {"discard_unsaved"});
    return tools;
}
