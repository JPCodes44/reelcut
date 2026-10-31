// Portal plumbing follows Omacut (MIT, David Heinemeier Hansson).
#include "portalfilepicker.h"

#include <QCoreApplication>

#include <QDBusConnection>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace {
struct PortalFilterRule {
    uint type;
    QString pattern;
};

using PortalFilterRules = QList<PortalFilterRule>;

struct PortalFileFilter {
    QString name;
    PortalFilterRules rules;
};

using PortalFileFilters = QList<PortalFileFilter>;

// The portal's combo-box "choices" option: (id, label, [(option-id, option-label)], initial).
struct PortalChoiceOption {
    QString id;
    QString label;
};

using PortalChoiceOptions = QList<PortalChoiceOption>;

struct PortalChoice {
    QString id;
    QString label;
    PortalChoiceOptions options;
    QString initial;
};

using PortalChoices = QList<PortalChoice>;

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule) {
    argument.beginStructure();
    argument << rule.type << rule.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule) {
    argument.beginStructure();
    argument >> rule.type >> rule.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFileFilter &filter) {
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFileFilter &filter) {
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoiceOption &option) {
    argument.beginStructure();
    argument << option.id << option.label;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoiceOption &option) {
    argument.beginStructure();
    argument >> option.id >> option.label;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoice &choice) {
    argument.beginStructure();
    argument << choice.id << choice.label << choice.options << choice.initial;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoice &choice) {
    argument.beginStructure();
    argument >> choice.id >> choice.label >> choice.options >> choice.initial;
    argument.endStructure();
    return argument;
}

void registerPortalFilterTypes() {
    static const bool registered = [] {
        qDBusRegisterMetaType<PortalFilterRule>();
        qDBusRegisterMetaType<PortalFilterRules>();
        qDBusRegisterMetaType<PortalFileFilter>();
        qDBusRegisterMetaType<PortalFileFilters>();
        qDBusRegisterMetaType<PortalChoiceOption>();
        qDBusRegisterMetaType<PortalChoiceOptions>();
        qDBusRegisterMetaType<PortalChoice>();
        qDBusRegisterMetaType<PortalChoices>();
        return true;
    }();
    Q_UNUSED(registered);
}

PortalFileFilter filter(const QString &name, const QStringList &mimes, const QStringList &globs) {
    PortalFileFilter result{name, {}};
    for (const QString &mime : mimes)
        result.rules.append({1, mime});
    for (const QString &glob : globs)
        result.rules.append({0, glob});
    return result;
}

PortalFileFilter filterFor(FilePicker::Kind kind) {
    switch (kind) {
    case FilePicker::Media:
        return filter(QCoreApplication::translate("PortalFilePicker", "Videos and photos"), {"video/*", "image/*"},
                      {"*.mp4", "*.mov", "*.m4v", "*.mkv", "*.webm", "*.avi", "*.jpg", "*.jpeg", "*.png",
                       "*.webp", "*.heic"});
    case FilePicker::Music:
        return filter(QCoreApplication::translate("PortalFilePicker", "Audio files"), {"audio/*"},
                      {"*.mp3", "*.m4a", "*.aac", "*.wav", "*.flac", "*.ogg", "*.opus"});
    case FilePicker::Lut:
        return filter(QCoreApplication::translate("PortalFilePicker", "3D LUTs"), {}, {"*.cube"});
    case FilePicker::OpenProject:
    case FilePicker::SaveProject:
        return filter(QCoreApplication::translate("PortalFilePicker", "Reelcut projects"), {}, {"*.reel"});
    case FilePicker::Export:
        return filter(QCoreApplication::translate("PortalFilePicker", "MP4 video"), {}, {"*.mp4"});
    }
    return {};
}

QString portalToken() {
    return QStringLiteral("reelcut_%1").arg(QRandomGenerator::global()->generate());
}

QByteArray portalPathBytes(const QString &path) {
    QByteArray bytes = path.toUtf8();
    bytes.append('\0');
    return bytes;
}

QString startFolder(FilePicker::Kind kind) {
    const auto location = kind == FilePicker::Music ? QStandardPaths::MusicLocation
                                                   : QStandardPaths::MoviesLocation;
    const QString folder = QStandardPaths::writableLocation(location);
    return QDir(folder).exists() ? folder : QDir::homePath();
}
}

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {
    registerPortalFilterTypes();
}

void PortalFilePicker::open(Kind kind) {
    const PortalFileFilter primary = filterFor(kind);
    QVariantMap options;
    options.insert(QStringLiteral("accept_label"), tr("Open"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("multiple"), kind == Media);
    options.insert(QStringLiteral("current_folder"), portalPathBytes(startFolder(kind)));
    options.insert(QStringLiteral("filters"),
                   QVariant::fromValue(PortalFileFilters{primary, {tr("All files"), {{0, QStringLiteral("*")}}}}));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(primary));

    const QString title = kind == Media ? tr("Add Videos or Photos")
                        : kind == Music ? tr("Choose Music")
                        : kind == Lut ? tr("Choose LUT")
                        : tr("Open Project");
    requestFile(QStringLiteral("OpenFile"), title, options, kind);
}

void PortalFilePicker::save(Kind kind, const QUrl &suggestedUrl) {
    const QFileInfo target(suggestedUrl.toLocalFile());
    const PortalFileFilter primary = filterFor(kind);

    QVariantMap options;
    options.insert(QStringLiteral("accept_label"), kind == Export ? tr("Export") : tr("Save"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("current_folder"), portalPathBytes(target.absolutePath()));
    options.insert(QStringLiteral("current_name"), target.fileName());
    options.insert(QStringLiteral("filters"), QVariant::fromValue(PortalFileFilters{primary}));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(primary));

    requestFile(QStringLiteral("SaveFile"), kind == Export ? tr("Export Reel") : tr("Save Project"),
                options, kind);
}

bool PortalFilePicker::connectToRequestPath(const QString &path) {
    m_pendingPath = path;
    return QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
        QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
        this, SLOT(handleResponse(uint,QVariantMap)));
}

void PortalFilePicker::closePendingRequest() {
    if (m_pendingPath.isEmpty())
        return;
    QDBusInterface request(QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                           QStringLiteral("org.freedesktop.portal.Request"),
                           QDBusConnection::sessionBus());
    request.asyncCall(QStringLiteral("Close"));
}

void PortalFilePicker::requestFile(const QString &method, const QString &title,
                                   QVariantMap options, Kind kind) {
    // A request whose Response never arrives (the portal crashed or was
    // killed with the dialog up) must not wedge every later dialog behind
    // it: close the stuck one best-effort and replace it.
    if (m_pendingKind != -1) {
        closePendingRequest();
        clearPending();
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileChooser"),
                          bus);
    if (!portal.isValid()) {
        emit failed(tr("The XDG desktop portal file chooser is not available."));
        return;
    }

    // Subscribe to the Response signal at the request path the portal will
    // derive from our handle_token *before* making the call, so a response
    // can't slip past while our match rule is still being installed.
    const QString token = portalToken();
    options.insert(QStringLiteral("handle_token"), token);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString predictedPath =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    m_pendingKind = kind;
    if (!connectToRequestPath(predictedPath)) {
        clearPending();
        emit failed(tr("Could not listen for the portal file picker response."));
        return;
    }

    const int generation = ++m_pendingGeneration;
    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCall(method, QString(), title, options), this);
    m_pendingWatcher = watcher;

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (m_pendingWatcher == watcher)
            m_pendingWatcher = nullptr;

        // Superseded by a newer request, or a response may already have
        // been handled while the reply was in flight.
        if (generation != m_pendingGeneration || m_pendingKind == -1)
            return;

        if (reply.isError()) {
            clearPending();
            emit failed(tr("The portal file picker failed: %1")
                            .arg(reply.error().message()));
            return;
        }

        // Old portal versions can hand back a different request path than the
        // handle_token predicts; move our subscription over if so.
        const QString actualPath = reply.value().path();
        if (actualPath != m_pendingPath) {
            QDBusConnection::sessionBus().disconnect(
                QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(handleResponse(uint,QVariantMap)));
            if (!connectToRequestPath(actualPath)) {
                clearPending();
                emit failed(tr("Could not listen for the portal file picker response."));
            }
        }
    });
}

void PortalFilePicker::handleResponse(uint response, const QVariantMap &results) {
    const int kind = m_pendingKind;
    clearPending();
    if (response != 0)
        return;

    QList<QUrl> urls;
    for (const QString &uri : results.value(QStringLiteral("uris")).toStringList())
        urls.append(QUrl(uri));
    if (!urls.isEmpty())
        emit selected(kind, urls);
}

void PortalFilePicker::clearPending() {
    // A caller that never gets its reply must not outlive the request it
    // belonged to (repeated stuck requests would otherwise pile them up).
    if (m_pendingWatcher) {
        m_pendingWatcher->deleteLater();
        m_pendingWatcher = nullptr;
    }
    if (!m_pendingPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
            this, SLOT(handleResponse(uint,QVariantMap)));
    }

    m_pendingPath.clear();
    m_pendingKind = -1;
}
