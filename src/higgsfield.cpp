#include "higgsfield.h"

#include "publisher.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>

namespace {

const char *kScope = "openid email offline_access";

QString randomToken(int length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString out;
    for (int i = 0; i < length; ++i)
        out += QChar(alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))]);
    return out;
}

QByteArray page(const QString &message) {
    const QByteArray html = "<!doctype html><meta charset=utf-8><title>Reelcut</title>"
                            "<body style=\"font:16px sans-serif;margin:3em\"><h2>Reelcut</h2><p>"
                            + message.toHtmlEscaped().toUtf8() + "</p></body>";
    return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
           + QByteArray::number(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
}

QDateTime parseTime(const QString &text) {
    QDateTime t = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!t.isValid())
        t = QDateTime::fromString(text, Qt::ISODate);
    return t;
}

}  // namespace

HiggsfieldCredits::HiggsfieldCredits(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)), m_poll(new QTimer(this)) {
    // A stalled request (no bytes for 2 min) fails instead of hanging forever.
    m_net->setTransferTimeout(120000);
    connect(m_poll, &QTimer::timeout, this, &HiggsfieldCredits::refresh);
    QTimer::singleShot(0, this, [this] {
        load();
        if (connected()) {
            m_poll->start(pollSeconds * 1000);
            refresh();
        }
    });
}

HiggsfieldCredits::~HiggsfieldCredits() { stopListening(); }

QString HiggsfieldCredits::accountFile() const { return Net::stateFile(stateDir, "higgsfield.json"); }
QString HiggsfieldCredits::statusFile() const { return Net::stateFile(stateDir, "higgsfield-status.json"); }

void HiggsfieldCredits::load() {
    const QJsonObject json = Net::load(accountFile());
    m_clientId = json.value("clientId").toString();
    m_refreshToken = json.value("refreshToken").toString();
    m_accessToken = json.value("accessToken").toString();
    m_accessExpires = parseTime(json.value("accessExpires").toString());
    m_dollarsPerCredit = json.value("dollarsPerCredit").toDouble(0.05);
    if (!qIsFinite(m_dollarsPerCredit) || m_dollarsPerCredit <= 0)
        m_dollarsPerCredit = 0.05;
    m_budget = json.value("budget").toDouble(0);
    if (!qIsFinite(m_budget) || m_budget < 0)
        m_budget = 0;
    m_since = parseTime(json.value("since").toString());  // invalid: all time
    emit changed();
}

void HiggsfieldCredits::save(bool tokens) {
    // A settings change keeps whichever tokens are newest on disk; only a
    // sign-in, renewal or sign-out writes this window's own.
    if (!tokens)
        adoptSavedTokens();
    if (m_refreshToken.isEmpty() && m_clientId.isEmpty())
        return;
    Net::savePrivate(accountFile(), {{"clientId", m_clientId},
                                     {"refreshToken", m_refreshToken},
                                     {"accessToken", m_accessToken},
                                     {"accessExpires", m_accessExpires.toUTC().toString(Qt::ISODate)},
                                     {"dollarsPerCredit", m_dollarsPerCredit},
                                     {"budget", m_budget},
                                     {"since", m_since.isValid() ? m_since.toUTC().toString(Qt::ISODate) : QString()}});
}

void HiggsfieldCredits::writeStatus() {
    Net::savePrivate(statusFile(), {{"balanceCredits", m_balance},
                                    {"balanceDollars", m_balance * m_dollarsPerCredit},
                                    {"spentCredits", m_spentSince},
                                    {"spentDollars", m_spentSince * m_dollarsPerCredit},
                                    {"spentTodayDollars", m_spentToday * m_dollarsPerCredit},
                                    {"budgetDollars", m_budget},
                                    {"overBudget", overBudget()},
                                    {"spentAllTimeDollars", m_spentAll * m_dollarsPerCredit},
                                    {"since", m_since.isValid() ? m_since.toUTC().toString(Qt::ISODate) : QString()},
                                    {"updated", m_updated.toUTC().toString(Qt::ISODate)}});
}

void HiggsfieldCredits::setDollarsPerCredit(double value) {
    if (!qIsFinite(value) || value <= 0 || qFuzzyCompare(value, m_dollarsPerCredit))
        return;
    m_dollarsPerCredit = value;
    m_alerted = overBudget();
    save();
    emit changed();
}

void HiggsfieldCredits::setBudget(double dollars) {
    dollars = qIsFinite(dollars) ? qMax(0.0, dollars) : 0.0;
    if (qFuzzyCompare(dollars + 1, m_budget + 1))
        return;
    m_budget = dollars;
    m_alerted = overBudget();  // a new limit alerts when it's crossed, not at once
    save();
    if (m_loaded)
        writeStatus();
    emit changed();
}

void HiggsfieldCredits::resetSince() {
    m_since = QDateTime::currentDateTimeUtc();
    m_alerted = false;
    save();
    finishPoll();
}

void HiggsfieldCredits::clearSince() {
    m_since = QDateTime();
    m_alerted = false;
    save();
    finishPoll();
}

void HiggsfieldCredits::setStatus(const QString &status) {
    if (status == m_status)
        return;
    m_status = status;
    emit changed();
}

// --- Signing in ---------------------------------------------------------

void HiggsfieldCredits::signIn() {
    stopListening();
    m_listener = new QTcpServer(this);
    if (!m_listener->listen(QHostAddress::LocalHost))
        return failSignIn(tr("Couldn't listen for Higgsfield's answer: %1").arg(m_listener->errorString()));
    m_redirect = QString("http://127.0.0.1:%1/callback").arg(m_listener->serverPort());
    m_state = randomToken(32);
    m_verifier = randomToken(64);
    emit changed();

    connect(m_listener, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_listener ? m_listener->nextPendingConnection() : nullptr) {
            socket->setParent(this);
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                // One request per connection: whatever follows its first
                // line (headers, a second request) is ignored.
                if (socket->property("answered").toBool()) {
                    socket->readAll();
                    return;
                }
                if (!socket->canReadLine()) {
                    if (socket->bytesAvailable() > 16 * 1024)  // no line ever coming
                        socket->abort();
                    return;
                }
                socket->setProperty("answered", true);
                const QList<QByteArray> request = socket->readLine().trimmed().split(' ');
                const QUrlQuery query(QUrl("http://x" + QString::fromUtf8(request.value(1))));
                const QString code = query.queryItemValue("code", QUrl::FullyDecoded);
                const QString error = query.queryItemValue("error", QUrl::FullyDecoded);
                if (code.isEmpty() && error.isEmpty()) {
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    return;
                }
                // A late answer (another tab, a reload) once this sign-in is
                // over: it can't start or end one.
                if (!m_listener) {
                    socket->write(page(tr("This sign-in is already over. Go back to Reelcut.")));
                    socket->disconnectFromHost();
                    return;
                }
                const bool ours = query.queryItemValue("state", QUrl::FullyDecoded) == m_state;
                socket->write(page(ours && error.isEmpty()
                                       ? tr("Signed in to Higgsfield. You can close this tab and go back to Reelcut.")
                                       : tr("Signing in didn't work. Go back to Reelcut to try again.")));
                socket->disconnectFromHost();
                if (!ours)
                    return failSignIn(tr("Higgsfield's answer didn't match this sign-in, so it was ignored."));
                if (!error.isEmpty())
                    return failSignIn(tr("Higgsfield said: %1")
                                          .arg(query.queryItemValue("error_description", QUrl::FullyDecoded).isEmpty()
                                                   ? error
                                                   : query.queryItemValue("error_description", QUrl::FullyDecoded)));
                stopListening();
                exchange(code);
            });
        }
    });
    QTimer::singleShot(10 * 60 * 1000, m_listener, [this] { failSignIn(tr("Signing in to Higgsfield timed out.")); });

    // A client of our own, registered for this redirect.
    QNetworkRequest request{QUrl(authBase + "/oauth/register")};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QJsonObject client{{"client_name", "Reelcut"},
                             {"redirect_uris", QJsonArray{m_redirect}},
                             {"grant_types", QJsonArray{"authorization_code", "refresh_token"}},
                             {"response_types", QJsonArray{"code"}},
                             {"token_endpoint_auth_method", "none"},
                             {"scope", kScope}};
    Net::whenDone(m_net->post(request, QJsonDocument(client).toJson(QJsonDocument::Compact)), this,
                  [this](const Net::Response &r) {
        if (!m_listener)
            return;
        m_pendingClient = r.json.value("client_id").toString();
        if (m_pendingClient.isEmpty())
            return failSignIn(tr("Higgsfield didn't register Reelcut: %1")
                                  .arg(r.error.isEmpty() ? QString::number(r.status) : r.error));
        const QByteArray challenge = QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256)
                                         .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        QUrl url(authBase + "/oauth/authorize");
        QUrlQuery q;
        auto add = [&q](const QString &key, const QString &value) {
            q.addQueryItem(key, QString::fromLatin1(QUrl::toPercentEncoding(value)));
        };
        add("client_id", m_pendingClient);
        add("redirect_uri", m_redirect);
        add("response_type", "code");
        add("scope", kScope);
        add("state", m_state);
        add("code_challenge", QString::fromLatin1(challenge));
        add("code_challenge_method", "S256");
        add("resource", mcpUrl);
        add("prompt", "consent");
        url.setQuery(q);
        if (openUrl)
            openUrl(url);
        else
            QDesktopServices::openUrl(url);
    });
}

void HiggsfieldCredits::exchange(const QString &code) {
    QNetworkRequest request{QUrl(authBase + "/oauth/token")};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    const QString client = m_pendingClient;
    Net::whenDone(m_net->post(request, Net::formBody({{"grant_type", "authorization_code"},
                                                      {"code", code},
                                                      {"client_id", client},
                                                      {"redirect_uri", m_redirect},
                                                      {"code_verifier", m_verifier},
                                                      {"resource", mcpUrl}})), this,
                  [this, client](const Net::Response &r) {
        m_verifier.clear();
        const QString access = r.json.value("access_token").toString();
        const QString renewal = r.json.value("refresh_token").toString();
        if (access.isEmpty() || renewal.isEmpty())
            return failSignIn(tr("Couldn't sign in to Higgsfield: %1")
                                  .arg(r.error.isEmpty() ? tr("no refresh token came back") : r.error));
        m_clientId = client;
        m_refreshToken = renewal;
        m_accessToken = access;
        m_accessExpires = QDateTime::currentDateTimeUtc().addSecs(r.json.value("expires_in").toInt(3600) - 60);
        m_session.clear();
        save(true);
        emit changed();
        emit signInFinished(true, tr("Signed in to Higgsfield."));
        m_poll->start(pollSeconds * 1000);
        refresh();
    });
}

void HiggsfieldCredits::stopListening() {
    if (!m_listener)
        return;
    m_listener->close();
    m_listener->deleteLater();
    m_listener = nullptr;
    emit changed();
}

void HiggsfieldCredits::failSignIn(const QString &message) {
    stopListening();
    m_verifier.clear();
    setStatus(message);
    emit signInFinished(false, message);
}

void HiggsfieldCredits::signOut() {
    m_poll->stop();
    if (!m_refreshToken.isEmpty()) {
        QNetworkRequest request{QUrl(authBase + "/oauth/token/revoke")};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        Net::whenDone(m_net->post(request, Net::formBody({{"token", m_refreshToken}, {"client_id", m_clientId}})), this,
                      [](const Net::Response &) {});
    }
    m_refreshToken.clear();
    m_accessToken.clear();
    m_session.clear();
    m_loaded = false;
    m_haveHistory = false;
    m_history = {};
    m_recent.clear();
    m_status.clear();
    save(true);
    QFile::remove(statusFile());
    emit changed();
}

// --- Talking to the MCP server -------------------------------------------

bool HiggsfieldCredits::adoptSavedTokens() {
    // Higgsfield rotates refresh tokens: each renewal retires the old one. So
    // another Reelcut window may have renewed since we last looked; take its
    // tokens rather than spend our stale one.
    const QJsonObject json = Net::load(accountFile());
    const QString refresh = json.value("refreshToken").toString();
    if (refresh.isEmpty() || refresh == m_refreshToken)
        return false;
    m_clientId = json.value("clientId").toString(m_clientId);
    m_refreshToken = refresh;
    m_accessToken = json.value("accessToken").toString();
    m_accessExpires = parseTime(json.value("accessExpires").toString());
    m_session.clear();
    return true;
}

void HiggsfieldCredits::withAccess(const std::function<void(const QString &)> &then, bool retried) {
    if (!m_accessToken.isEmpty() && QDateTime::currentDateTimeUtc() < m_accessExpires)
        return then({});
    if (adoptSavedTokens() && !m_accessToken.isEmpty() && QDateTime::currentDateTimeUtc() < m_accessExpires)
        return then({});
    QNetworkRequest request{QUrl(authBase + "/oauth/token")};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    Net::whenDone(m_net->post(request, Net::formBody({{"grant_type", "refresh_token"},
                                                      {"refresh_token", m_refreshToken},
                                                      {"client_id", m_clientId},
                                                      {"resource", mcpUrl}})), this,
                  [this, then, retried](const Net::Response &r) {
        if (r.json.value("error").toString() == "invalid_grant") {
            if (!retried && adoptSavedTokens())
                return withAccess(then, true);
            m_refreshToken.clear();
            m_accessToken.clear();
            m_poll->stop();
            save(true);
            emit changed();
            return then(tr("Higgsfield signed Reelcut out. Sign in again."));
        }
        const QString access = r.json.value("access_token").toString();
        if (access.isEmpty())
            return then(tr("Couldn't renew access to Higgsfield: %1")
                            .arg(r.error.isEmpty() ? QString::number(r.status) : r.error));
        m_accessToken = access;
        m_accessExpires = QDateTime::currentDateTimeUtc().addSecs(r.json.value("expires_in").toInt(3600) - 60);
        const QString rotated = r.json.value("refresh_token").toString();
        if (!rotated.isEmpty())
            m_refreshToken = rotated;
        save(true);  // shares the access token too, so other windows needn't renew
        then({});
    });
}

QNetworkReply *HiggsfieldCredits::postRpc(const QJsonObject &message) {
    QNetworkRequest request{QUrl(mcpUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    request.setRawHeader("MCP-Protocol-Version", "2025-06-18");
    if (!m_session.isEmpty())
        request.setRawHeader("Mcp-Session-Id", m_session.toUtf8());
    return m_net->post(request, QJsonDocument(message).toJson(QJsonDocument::Compact));
}

QJsonObject HiggsfieldCredits::rpcMessage(const QByteArray &body) {
    const QJsonDocument plain = QJsonDocument::fromJson(body);
    if (plain.isObject())
        return plain.object();
    // Server-sent events: the reply is the last `data:` event with an id.
    QJsonObject last;
    QByteArray data;
    auto flush = [&] {
        const QJsonObject message = QJsonDocument::fromJson(data).object();
        if (message.contains("result") || message.contains("error"))
            last = message;
        data.clear();
    };
    for (const QByteArray &raw : body.split('\n')) {
        const QByteArray line = raw.endsWith('\r') ? raw.chopped(1) : raw;
        if (line.isEmpty())
            flush();
        else if (line.startsWith("data:"))
            data += line.mid(5).trimmed();
    }
    flush();
    return last;
}

QJsonObject HiggsfieldCredits::toolResult(const QJsonObject &message) {
    const QJsonObject result = message.value("result").toObject();
    const QJsonObject structured = result.value("structuredContent").toObject();
    if (!structured.isEmpty())
        return structured;
    for (const QJsonValue &block : result.value("content").toArray()) {
        const QJsonObject json = QJsonDocument::fromJson(block.toObject().value("text").toString().toUtf8()).object();
        if (!json.isEmpty())
            return json;
    }
    return {};
}

void HiggsfieldCredits::withSession(const std::function<void(const QString &)> &then) {
    withAccess([this, then](const QString &error) {
        if (!error.isEmpty() || !m_session.isEmpty())
            return then(error);
        const QJsonObject init{{"jsonrpc", "2.0"},
                               {"id", 1},
                               {"method", "initialize"},
                               {"params", QJsonObject{{"protocolVersion", "2025-06-18"},
                                                      {"capabilities", QJsonObject{}},
                                                      {"clientInfo", QJsonObject{{"name", "reelcut"},
                                                                                 {"version", "0.4.1"}}}}}};
        QNetworkReply *reply = postRpc(init);
        connect(reply, &QNetworkReply::finished, this, [this, reply, then] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject message = rpcMessage(reply->readAll());
            if (status == 401) {
                m_accessToken.clear();
                return then(tr("Higgsfield didn't accept Reelcut's sign-in."));
            }
            if (!message.contains("result"))
                return then(tr("Couldn't reach Higgsfield: %1")
                                .arg(reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                              : QString::number(status)));
            // Servers without sessions are fine too: no header, no session.
            m_session = QString::fromUtf8(reply->rawHeader("Mcp-Session-Id"));
            if (m_session.isEmpty())
                m_session = "-";
            Net::whenDone(postRpc({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}), this,
                          [](const Net::Response &) {});
            then({});
        });
    });
}

void HiggsfieldCredits::callTool(const QString &name, const QJsonObject &args,
                                 const std::function<void(const QJsonObject &, const QString &)> &then, bool retried) {
    withSession([=, this](const QString &error) {
        if (!error.isEmpty())
            return then({}, error);
        static int nextId = 2;
        QNetworkReply *reply = postRpc({{"jsonrpc", "2.0"},
                                        {"id", nextId++},
                                        {"method", "tools/call"},
                                        {"params", QJsonObject{{"name", name}, {"arguments", args}}}});
        connect(reply, &QNetworkReply::finished, this, [=, this] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject message = rpcMessage(reply->readAll());
            if ((status == 401 || status == 404 || status == 400) && !retried) {
                // An expired token or a forgotten session: start over once.
                if (status == 401)
                    m_accessToken.clear();
                m_session.clear();
                return callTool(name, args, then, true);
            }
            if (message.value("result").toObject().value("isError").toBool())
                return then({}, tr("Higgsfield's %1 failed").arg(name));
            const QJsonObject result = toolResult(message);
            if (result.isEmpty())
                return then({}, message.contains("error")
                                    ? message.value("error").toObject().value("message").toString()
                                    : tr("Couldn't reach Higgsfield (%1)").arg(status ? QString::number(status)
                                                                                      : reply->errorString()));
            then(result, {});
        });
    });
}

// --- Polling --------------------------------------------------------------

void HiggsfieldCredits::refresh() {
    if (!connected() || m_polling)
        return;
    m_polling = true;
    callTool("balance", {}, [this](const QJsonObject &balance, const QString &error) {
        if (!error.isEmpty()) {
            m_polling = false;
            return setStatus(error);
        }
        m_balance = balance.value("credits").toDouble();
        m_plan = balance.value("subscription_plan_type").toString();
        fetchTransactions(0, {}, 0);
    });
}

QString HiggsfieldCredits::transactionKey(const QJsonObject &t) {
    return t.value("created_at").toString() + '|' + t.value("action").toString() + '|'
           + QString::number(t.value("credits").toDouble(), 'g', 12) + '|' + t.value("display_name").toString();
}

QJsonArray HiggsfieldCredits::mergeNewest(const QJsonArray &newer, const QJsonArray &history) {
    QSet<QString> known;
    for (const QJsonValue &t : history)
        known.insert(transactionKey(t.toObject()));
    QJsonArray merged;
    for (const QJsonValue &t : newer)
        if (!known.contains(transactionKey(t.toObject())))
            merged.append(t);
    for (const QJsonValue &t : history)
        merged.append(t);
    return merged;
}

// The whole history is read once (100 a page); after that only the pages
// newer than what's already known.
void HiggsfieldCredits::fetchTransactions(int cursor, QJsonArray collected, int pages) {
    QJsonObject args{{"size", 100}};
    if (cursor > 0)
        args.insert("cursor", cursor);
    callTool("transactions", args, [=, this](const QJsonObject &page, const QString &error) mutable {
        if (!error.isEmpty()) {
            m_polling = false;
            return setStatus(error);
        }
        const QJsonArray items = page.value("items").toArray();
        bool reachedKnown = false;
        QSet<QString> known;
        if (m_haveHistory)
            for (int i = 0; i < qMin(int(m_history.size()), 200); ++i)
                known.insert(transactionKey(m_history.at(i).toObject()));
        for (const QJsonValue &item : items) {
            if (known.contains(transactionKey(item.toObject())))
                reachedKnown = true;
            collected.append(item);
        }
        const QJsonValue next = page.value("next_cursor");
        const bool more = !next.isNull() && next.toInt() > 0 && !items.isEmpty();
        if (more && !reachedKnown && pages < 500)
            return fetchTransactions(next.toInt(), collected, pages + 1);
        m_history = m_haveHistory ? mergeNewest(collected, m_history) : collected;
        m_haveHistory = true;
        m_polling = false;
        finishPoll();
    });
}

double HiggsfieldCredits::spentFrom(const QJsonArray &transactions, const QDateTime &from) {
    double net = 0;
    for (const QJsonValue &value : transactions) {
        const QJsonObject t = value.toObject();
        // Only charges and their refunds: grants are plans and top-ups, and
        // deducts are unused daily credits expiring.
        const QString action = t.value("action").toString();
        if (action != "spend" && action != "refund")
            continue;
        const QDateTime at = parseTime(t.value("created_at").toString());
        if (!from.isValid() || (at.isValid() && at >= from))
            net += t.value("credits").toDouble();  // spends are negative
    }
    return qMax(0.0, -net);
}

void HiggsfieldCredits::finishPoll() {
    if (!m_haveHistory) {
        emit changed();
        return;
    }
    const QJsonArray &transactions = m_history;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    m_spentSince = spentFrom(transactions, m_since);
    m_spentAll = spentFrom(transactions, QDateTime());
    m_spentToday = spentFrom(transactions, QDateTime(QDate::currentDate(), QTime(0, 0)).toUTC());
    m_spentRecent = spentFrom(transactions, now.addSecs(-10 * 60));
    m_recent.clear();
    for (const QJsonValue &value : transactions) {
        if (m_recent.size() >= 40)
            break;
        const QJsonObject t = value.toObject();
        const double credits = t.value("credits").toDouble();
        m_recent.append(QVariantMap{{"name", t.value("display_name").toString()},
                                    {"action", t.value("action").toString()},
                                    {"credits", credits},
                                    {"dollars", credits * m_dollarsPerCredit},
                                    {"time", parseTime(t.value("created_at").toString()).toLocalTime()}});
    }
    m_updated = now;
    m_loaded = true;
    m_status.clear();
    if (overBudget() && !m_alerted) {
        m_alerted = true;
        const QString when = m_since.isValid() ? tr("since %1").arg(QLocale().toString(m_since.toLocalTime(), QLocale::ShortFormat))
                                               : tr("in all");
        const QString text = tr("Spent $%1 of your $%2 budget %3. Stop the agent if it's still generating.")
                                 .arg(QLocale().toString(m_spentSince * m_dollarsPerCredit, 'f', 2),
                                      QLocale().toString(m_budget, 'f', 2), when);
        if (notify)
            notify(text);
        else
            QProcess::startDetached("notify-send", {"-u", "critical", "-a", "Reelcut", "-i", "reelcut",
                                                    tr("Higgsfield budget reached"), text});
    } else if (!overBudget()) {
        m_alerted = false;
    }
    writeStatus();
    emit changed();
}
