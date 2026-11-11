#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTimer;

// Watches how many Higgsfield credits are being spent, in dollars, so an agent
// generating through the Higgsfield MCP (from Claude, say) can be stopped
// before it runs past a budget.
//
// Reelcut reads the same account data the agent sees: it signs in to
// Higgsfield's MCP server (mcp.higgsfield.ai/mcp) as its own OAuth client and
// calls the server's `balance` and `transactions` tools.
//   1. Register a public client with Higgsfield's auth server (Clerk), with a
//      127.0.0.1 redirect: dynamic client registration, so nothing to paste.
//   2. Open the sign-in page with a PKCE (S256) challenge; the browser comes
//      back to 127.0.0.1 with a code.
//   3. Trade the code for access and refresh tokens. The refresh token is kept
//      in ~/.config/reelcut/higgsfield.json, readable only by you.
//   4. Every few seconds: MCP initialize (once per session), then tools/call
//      balance and transactions.
//
// Credits become dollars at dollarsPerCredit: $0.05 by default, what Plus
// auto-refill charges (20 credits per dollar). Spending is counted in all,
// from `since` (Reset sets it to now; unset, it's all time too), for today and
// for the last ten minutes. Past the budget, a desktop
// notification fires once, and ~/.config/reelcut/higgsfield-status.json says
// overBudget, for a hook that blocks further generations.
class HiggsfieldCredits : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString plan READ plan NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(double balance READ balance NOTIFY changed)
    Q_PROPERTY(double spentSince READ spentSince NOTIFY changed)
    Q_PROPERTY(double spentAll READ spentAll NOTIFY changed)
    Q_PROPERTY(double spentToday READ spentToday NOTIFY changed)
    Q_PROPERTY(double spentRecent READ spentRecent NOTIFY changed)
    Q_PROPERTY(QDateTime since READ since NOTIFY changed)
    Q_PROPERTY(QDateTime updated READ updated NOTIFY changed)
    Q_PROPERTY(double dollarsPerCredit READ dollarsPerCredit WRITE setDollarsPerCredit NOTIFY changed)
    Q_PROPERTY(double budget READ budget WRITE setBudget NOTIFY changed)
    Q_PROPERTY(bool overBudget READ overBudget NOTIFY changed)
    Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
public:
    explicit HiggsfieldCredits(QObject *parent = nullptr);
    ~HiggsfieldCredits() override;

    bool connected() const { return !m_refreshToken.isEmpty(); }
    bool signingIn() const { return m_listener != nullptr; }
    QString status() const { return m_status; }
    QString plan() const { return m_plan; }
    bool loaded() const { return m_loaded; }
    double balance() const { return m_balance; }
    double spentSince() const { return m_spentSince; }
    double spentAll() const { return m_spentAll; }
    double spentToday() const { return m_spentToday; }
    double spentRecent() const { return m_spentRecent; }
    QDateTime since() const { return m_since; }
    QDateTime updated() const { return m_updated; }
    double dollarsPerCredit() const { return m_dollarsPerCredit; }
    void setDollarsPerCredit(double value);
    double budget() const { return m_budget; }  // dollars since `since`; 0 for none
    void setBudget(double dollars);
    bool overBudget() const { return m_budget > 0 && m_spentSince * m_dollarsPerCredit >= m_budget; }
    QVariantList recent() const { return m_recent; }

    Q_INVOKABLE void signIn();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void resetSince();
    Q_INVOKABLE void clearSince();  // count from the very first charge again

    // Credits spent (refunds take it back) by transactions at or after `from`,
    // or ever when `from` is invalid.
    // Grants (plans, top-ups) and deducts (expired daily credits) aren't spending.
    static double spentFrom(const QJsonArray &transactions, const QDateTime &from);
    // The JSON-RPC message in an MCP reply, which comes as plain JSON or as
    // server-sent events.
    static QJsonObject rpcMessage(const QByteArray &body);
    // What a tools/call result carries: its structured content, or the JSON
    // in its first text block.
    static QJsonObject toolResult(const QJsonObject &message);
    // `newer` (a fresh first page or few) ahead of `history`, without repeats.
    static QJsonArray mergeNewest(const QJsonArray &newer, const QJsonArray &history);
    static QString transactionKey(const QJsonObject &transaction);

    QString accountFile() const;
    QString statusFile() const;

    std::function<void(const QUrl &)> openUrl;  // tests catch the browser
    std::function<void(const QString &)> notify;  // tests catch the alert
    QString authBase = "https://clerk.higgsfield.ai";
    QString mcpUrl = "https://mcp.higgsfield.ai/mcp";
    QString stateDir;  // empty: ~/.config/reelcut
    int pollSeconds = 20;

signals:
    void changed();
    void signInFinished(bool ok, const QString &message);

private:
    void load();
    void save(bool tokens = false);
    void writeStatus();
    void stopListening();
    void failSignIn(const QString &message);
    void exchange(const QString &code);
    void withAccess(const std::function<void(const QString &error)> &then, bool retried = false);
    bool adoptSavedTokens();
    void withSession(const std::function<void(const QString &error)> &then);
    // tools/call `name`; then(result, error). Starts a new MCP session once if
    // the server forgot ours.
    void callTool(const QString &name, const QJsonObject &args,
                  const std::function<void(const QJsonObject &, const QString &)> &then, bool retried = false);
    QNetworkReply *postRpc(const QJsonObject &message);
    void fetchTransactions(int cursor, QJsonArray collected, int pages);
    void setStatus(const QString &status);
    void finishPoll();

    QNetworkAccessManager *m_net;
    QTimer *m_poll;
    QString m_clientId, m_refreshToken, m_accessToken, m_session, m_status, m_plan;
    QDateTime m_accessExpires, m_since, m_updated;
    double m_balance = 0, m_spentAll = 0, m_spentSince = 0, m_spentToday = 0, m_spentRecent = 0;
    double m_dollarsPerCredit = 0.05, m_budget = 0;
    bool m_loaded = false, m_polling = false, m_alerted = false;
    QVariantList m_recent;
    QJsonArray m_history;  // every transaction, newest first
    bool m_haveHistory = false;

    // While signing in.
    QTcpServer *m_listener = nullptr;
    QString m_state, m_verifier, m_redirect, m_pendingClient;
};
